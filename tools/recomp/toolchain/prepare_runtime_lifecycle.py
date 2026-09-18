#!/usr/bin/env python3
"""Build checked local runtime sources with cooperative guest/timer shutdown.

Keep the pinned upstream checkout unchanged. Compile generated counterparts of
runtime files, recording their original and adapted identities beside outputs.
Keep RDRAM alive until every registered game thread has actually been joined.
"""
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]

SHUTDOWN_TRACE_PATH = ROOT / "src/host/runtime-support/shutdown_trace.hpp"
GUEST_SHUTDOWN_PATH = ROOT / "src/host/runtime-support/guest_shutdown.hpp"


def replace_once(source: str, old: str, new: str) -> str:
    if source.count(old) != 1:
        raise RuntimeError(f"Pinned guest lifecycle source differs: {old[:80]}")
    return source.replace(old, new)


def adapt_threads(source: str) -> str:
    source = '#include "guest_shutdown.hpp"\n' + source
    source = replace_once(source, '    thread_context->running.wait();',
                          '    srw64_guest_checkpoint();\n    thread_context->running.wait();\n    srw64_guest_checkpoint();')
    source = replace_once(source, 'void run_next_thread(RDRAM_ARG1) {',
                          'void run_next_thread(RDRAM_ARG1) {\n    srw64_guest_checkpoint();')
    source = replace_once(source, '    if (self->context == thread_context) {\n        debug_printf("[Thread] Thread started:',
                          '    if (!exited.load() && self->context == thread_context) {\n        debug_printf("[Thread] Thread started:')
    source = replace_once(source, '''    if (self->context == thread_context) {
        self->context = nullptr;
        run_next_thread(PASS_RDRAM1);
    }''', '''    try {
        if (!exited.load() && self->context == thread_context) {
            self->context = nullptr;
            run_next_thread(PASS_RDRAM1);
        }
    } catch (ultramodern::thread_terminated&) {
        // Shutdown must not schedule another guest or touch its queues.
    }''')
    source = replace_once(source,
        '    context->host_thread = std::thread{_thread_func, PASS_RDRAM t_, entrypoint, arg, t->context};',
        '''    try {
        srw64_guest_threads.start(context, [&] {
            return std::thread{_thread_func, PASS_RDRAM t_, entrypoint, arg, context};
        });
    } catch (...) {
        t->context = nullptr;
        delete context;
        throw;
    }''')
    source = replace_once(source, '    context->initialized.wait();',
                          '    // Initialized semaphore consumed while registration pins the context.')
    source = replace_once(source, '''    while (!exited) {
        UltraThreadContext* to_delete;
        if (deleted_threads.wait_dequeue_timed(to_delete, 10ms)) {
            debug_printf("[Cleanup] Deleting thread context %p\\n", to_delete);

            to_delete->host_thread.join();
            delete to_delete;
        }
    }''', '''    while (true) {
        UltraThreadContext* to_delete;
        deleted_threads.wait_dequeue(to_delete);
        if (to_delete == nullptr) return;
        srw64_guest_threads.reclaim(to_delete);
    }''')
    source = replace_once(source, '''void ultramodern::join_thread_cleaner_thread() {
    thread_cleaner_thread.join();
}''', '''void srw64_request_guest_shutdown() {
    assert(exited.load());
    const size_t count = srw64_guest_threads.wake_all();
    for (size_t i = 0; i <= count; ++i) srw64_wake_external_message();
}

void ultramodern::join_thread_cleaner_thread() {
    // Entry thread is already joined and shutdown rejects new children.
    srw64_guest_threads.wait_empty();
    if (thread_cleaner_thread.joinable()) {
        deleted_threads.enqueue(nullptr);
        thread_cleaner_thread.join();
    }
}''')
    return source


def adapt_messages(source: str) -> str:
    source = '#include "guest_shutdown.hpp"\n' + source
    source = replace_once(source, 'std::bitset<32> requeue_enabled;', '''std::bitset<32> requeue_enabled;

void srw64_wake_external_message() {
    // Host-only wake token. The exit check consumes it before any guest access.
    external_messages.enqueue({NULLPTR, 0, false, false});
}''')
    for signature in ('void dequeue_external_messages(RDRAM_ARG1) {',
                      'void ultramodern::wait_for_external_message(RDRAM_ARG1) {',
                      'void ultramodern::wait_for_external_message_timed(RDRAM_ARG u32 millis) {'):
        source = replace_once(source, signature, signature + '\n    srw64_guest_checkpoint();')
    source = replace_once(source, '    while (external_messages.try_dequeue(to_send)) {',
                          '    while (external_messages.try_dequeue(to_send)) {\n        srw64_guest_checkpoint();')
    source = replace_once(source, '    external_messages.wait_dequeue(to_send);',
                          '    external_messages.wait_dequeue(to_send);\n    srw64_guest_checkpoint();')
    source = replace_once(source, '    if (external_messages.wait_dequeue_timed(to_send, std::chrono::milliseconds{millis})) {',
                          '    if (external_messages.wait_dequeue_timed(to_send, std::chrono::milliseconds{millis})) {\n        srw64_guest_checkpoint();')
    return source


def adapt_timer(source: str) -> str:
    replacements = [
        ("using Action = std::variant<AddTimerAction, RemoveTimerAction>;",
         "struct StopTimerAction {};\nusing Action = std::variant<AddTimerAction, RemoveTimerAction, StopTimerAction>;", 1),
        ("auto process_timer_action = [&](const Action& action) {",
         "auto process_timer_action = [&](const Action& action) -> bool {\n"
         "        if (std::holds_alternative<StopTimerAction>(action)) return false;", 1),
        ("            active_timers.erase(remove_action->timer);\n        }\n    };",
         "            active_timers.erase(remove_action->timer);\n        }\n        return true;\n    };", 1),
        ("process_timer_action(cur_action);", "if (!process_timer_action(cur_action)) return;", 3),
        ("    timer_context.thread.detach();", "", 1),
        ("uint32_t ultramodern::get_speed_multiplier() {",
         "void srw64_shutdown_timers() {\n"
         "    if (timer_context.thread.joinable()) {\n"
         "        timer_context.action_queue.enqueue(StopTimerAction{});\n"
         "        timer_context.thread.join();\n"
         "    }\n}\n\nuint32_t ultramodern::get_speed_multiplier() {", 1),
    ]
    for old, new, count in replacements:
        if source.count(old) != count:
            raise RuntimeError("Pinned timer lifecycle source differs")
        source = source.replace(old, new)
    return source


def main() -> None:
    checkout = ROOT / "build/recomp/upstream/N64ModernRuntime"
    lock = json.loads((ROOT / "config/recomp/toolchain.json").read_text())["sources"]["N64ModernRuntime"]["commit"]
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=checkout, text=True).strip()
    if revision != lock:
        raise RuntimeError("Runtime lifecycle source revision differs")
    output = ROOT / "build/recomp/runtime-lifecycle"
    output.mkdir(exist_ok=True)
    support = output / GUEST_SHUTDOWN_PATH.name
    if not support.exists() or support.read_bytes() != GUEST_SHUTDOWN_PATH.read_bytes():
        support.write_bytes(GUEST_SHUTDOWN_PATH.read_bytes())
    records = []
    for relative in ("ultramodern/src/timer.cpp", "ultramodern/src/threads.cpp",
                     "ultramodern/src/mesgqueue.cpp", "ultramodern/src/scheduling.cpp",
                     "librecomp/src/recomp.cpp"):
        source = subprocess.check_output(["git", "show", f"HEAD:{relative}"], cwd=checkout, text=True)
        if (checkout / relative).read_text() != source:
            raise RuntimeError("Runtime lifecycle input has local changes")
        if relative.endswith("timer.cpp"):
            adapted = adapt_timer(source)
        elif relative.endswith("threads.cpp"):
            adapted = adapt_threads(source)
        elif relative.endswith("mesgqueue.cpp"):
            adapted = adapt_messages(source)
        elif relative.endswith("scheduling.cpp"):
            adapted = '#include "guest_shutdown.hpp"\n' + replace_once(source,
                'void ultramodern::check_running_queue(RDRAM_ARG1) {',
                'void ultramodern::check_running_queue(RDRAM_ARG1) {\n    srw64_guest_checkpoint();')
        else:
            old = "    // Free rdram."
            if source.count(old) != 1:
                raise RuntimeError("Pinned RDRAM release location differs")
            source = replace_once(source, '    game_thread.join();',
                '    srw64_request_guest_shutdown();\n    game_thread.join();\n    ::srw64_shutdown_timers();')
            adapted = SHUTDOWN_TRACE_PATH.read_text() + "\nvoid srw64_shutdown_timers();\nvoid srw64_request_guest_shutdown();\n\n" + source.replace(old,
                '    srw64_trace_shutdown_threads("before_rdram_free", rdram);\n\n' + old)
            after_free = "    if (free_failed) {"
            if adapted.count(after_free) != 1:
                raise RuntimeError("Pinned RDRAM release result differs")
            adapted = adapted.replace(after_free,
                '    srw64_trace_shutdown_threads("after_rdram_free", rdram);\n\n' + after_free)
        destination = output / Path(relative).name
        if not destination.exists() or destination.read_text() != adapted:
            destination.write_text(adapted)
        records.append({"source": relative, "generated": str(destination.relative_to(ROOT)),
                        "before_sha256": hashlib.sha256(source.encode()).hexdigest(),
                        "after_sha256": hashlib.sha256(adapted.encode()).hexdigest()})
    (output / "manifest.json").write_text(json.dumps({"schema": "srw64.runtime-lifecycle.v1",
        "revision": revision, "changes": records,
        "support_files": {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                          for path in (SHUTDOWN_TRACE_PATH, GUEST_SHUTDOWN_PATH)}}, indent=2) + "\n")


if __name__ == "__main__":
    main()
