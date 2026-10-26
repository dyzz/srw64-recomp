#!/usr/bin/env python3
"""Write the Nintendo Switch counterparts of the runtime sources (docs/design/switch-port.md, S0).

The pinned upstream checkouts stay unchanged, as with prepare_runtime_lifecycle.py: each
generated file is the pinned text with checked, whole-string replacements, recorded in a
manifest with before/after hashes. build/recomp/switch-runtime/ then holds:

  recomp.cpp                  the lifecycle recomp.cpp (prepare_runtime_lifecycle.py) with
                              RDRAM from calloc (16 MiB: libnx has no mmap, so no 4 GiB
                              reservation and no guard pages), without the mod system, the
                              live recompiler and the recompiled heap (dlopen and executable
                              memory, neither of which a homebrew NRO has), and the ROM read
                              once into memory: the launcher has checked it (rom_order.hpp,
                              rom_import.cpp), and upstream wrote a 32 MiB copy into every
                              session on the SD card, then read and hashed it twice
  librecomp/CMakeLists.txt    librecomp without mods.cpp, mod_*.cpp, config*.cpp, heap.cpp,
                              o1heap and the N64Recomp/LiveRecomp targets; its sources named
                              from the upstream checkout, recomp.cpp and files.cpp from here
  include/ultramodern/renderer_context.hpp
                              the window handle for the Switch (an SDL_Window*, as on Linux)
  include/lightweightsemaphore.h
                              moodycamel's semaphores, the lightweight one too, as a count
                              under a mutex with a condition variable
  include/blockingconcurrentqueue.h
                              moodycamel's ConcurrentQueue and BlockingConcurrentQueue as a
                              deque under a mutex with a condition variable, with the calls the
                              runtime makes. The lock-free queue lost what the VI thread put in:
                              at the world map's hang (2026-10-24) 7356 VI messages had gone
                              into the external queue, the VI's message queue was empty and its
                              thread waited on it, and the idle thread waited on an empty
                              external queue. (The pinned header is generated over, not used.)
  events.cpp                  the lifecycle events.cpp (the 120 Hz latch, which now swaps the
                              VI states under the setters' lock on every platform: first
                              found here and on the PS Vita (prepare_vita_runtime.py): unlocked, a VI event
                              the game set between the swap and the copy was overwritten
                              (mq 0), no VI message came again, and every game thread waited
                              in osRecvMesg (the world map hang, 2026-10-24, creport)
  error_handling.cpp          std::_Exit: libstdc++ there has no std::quick_exit
  files.cpp                   the save file's backup and publication copied through streams:
                              libstdc++'s copy_file sets the copy's mode, which the SD card
                              refuses

The include directory goes before the upstream ones (src/host/CMakeLists.txt does this when
NINTENDO_SWITCH). tools/release/switch/build.sh runs this; prepare_runtime_lifecycle.py must
have run.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
UPSTREAM = ROOT / "build/recomp/upstream"
OUTPUT = ROOT / "build/recomp/switch-runtime"
# The game's 8 MiB (osMemSize), overlay segment 14 at 0x80400000 and the host's scratch
# area at 0x807F0000 all lie within it.
RDRAM_BYTES = 16 * 1024 * 1024


def sha256(text: str) -> str:
    return hashlib.sha256(text.encode()).hexdigest()


def replace_once(source: str, old: str, new: str) -> str:
    if source.count(old) != 1:
        raise RuntimeError(f"Pinned runtime source differs: {old[:80]!r}")
    return source.replace(old, new)


def remove_between(source: str, start: str, end: str, digest: str) -> str:
    """Delete from the unique `start` up to (not including) the unique `end`; the removed
    text must hash to `digest`, so only the pinned block ever goes."""
    if source.count(start) != 1 or source.count(end) != 1:
        raise RuntimeError(f"Pinned runtime region anchors differ: {start[:60]!r}")
    begin, stop = source.index(start), source.index(end)
    if stop <= begin:
        raise RuntimeError(f"Pinned runtime region order differs: {start[:60]!r}")
    removed = source[begin:stop]
    if sha256(removed) != digest:
        raise RuntimeError(f"Pinned runtime region content differs: {start[:60]!r} ({sha256(removed)})")
    return source[:begin] + source[stop:]


def pinned(checkout: Path, relative: str) -> str:
    marker = checkout / ".srw64-revision"
    if not (checkout / ".git").exists() and marker.is_file():
        return (checkout / relative).read_text()
    text = subprocess.check_output(["git", "show", f"HEAD:{relative}"], cwd=checkout, text=True)
    if (checkout / relative).read_text() != text:
        raise RuntimeError(f"{relative} has local changes")
    return text


def adapt_recomp_cpp(source: str) -> str:
    source = replace_once(source, '#include "recompiler/live_recompiler.h"\n', '')
    source = replace_once(source, '''#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <Windows.h>
#else
#    include <sys/mman.h>
#endif
''', '')
    source = replace_once(source, '''// The global mod context.
std::unique_ptr<recomp::mods::ModContext> mod_context = std::make_unique<recomp::mods::ModContext>();
''', '')
    source = replace_once(source, '''    if (!entry.mod_game_id.empty()) {
        std::lock_guard<std::mutex> lock(mod_context_mutex);
        mod_context->register_game(entry.mod_game_id);
    }
    return true;
}
''', '''    return true;
}
''')
    # The mod API wrappers, from initialize_mods() to the last one before the ROM helpers.
    source = remove_between(source, 'void recomp::mods::initialize_mods() {\n',
                            'bool check_hash(const std::vector<uint8_t>& rom_data, uint64_t expected_hash) {',
                            '21d8a716e30f4829856ea812222fd3a467a145c0c07ec9abcccaac9098df5d0a')
    source = replace_once(source, '''    game_status.notify_all();
    mods::set_latest_game_mode_id(game_mode_id);
}
''', '''    game_status.notify_all();
}
''')
    source = remove_between(source, 'void recomp::mods::enable_mod(const std::string& mod_id, bool enabled) {\n',
                            'bool wait_for_game_started(uint8_t* rdram, recomp_context* context) {',
                            '2291a41d6f2b246effe7b241e123396c963cd4777164c1c185d982545fd5fcea')
    source = remove_between(source, '                uint32_t mod_ram_used = 0;\n',
                            '                save_type = game_entry.save_type;',
                            '3a978b80918deb1f1e395814a0ee7229d5f921fbeef710fa7e74cba788232e52')
    source = replace_once(source, '''void print_cli_game_mode_options(const std::vector<recomp::mods::ModDetails> &mods) {
    for (const auto &mod : mods) {
        if (mod.custom_gamemode) {
            bool mod_enabled = recomp::mods::is_mod_enabled(mod.mod_id) || recomp::mods::is_mod_auto_enabled(mod.mod_id);
            fprintf(stderr, "\\"%s\\" (%s)%s\\n", mod.mod_id.c_str(), mod.display_name.c_str(), mod_enabled ? "" : " (Disabled)");
        }
    }
}

''', '')
    source = remove_between(source, '''            if (!cli_game_mode_id.empty() || game_mode_missing_argument) {
                std::vector<recomp::mods::ModDetails> mods = recomp::mods::get_all_mod_details(cli_game_id);
''', '''            if (!game_mode_error) {
                recomp::start_game(game_id, game_mode_id);
''', '9f8749b19981c102d2b45428c46fb411417de489591da0b31a52890a04169ab0')
    source = replace_once(source, '''            if (!game_mode_error) {
                recomp::start_game(game_id, game_mode_id);
''', '''            if (!cli_game_mode_id.empty() || game_mode_missing_argument) {
                fprintf(stderr, "Game modes need the mod system, which this build leaves out.\\n");
                game_mode_error = true;
            }
            if (!game_mode_error) {
                recomp::start_game(game_id, game_mode_id);
''')
    source = replace_once(source, '''    recomp::mods::initialize_mods();
    recomp::mods::scan_mods();

    // Allocate rdram without comitting it. Use a platform-specific virtual allocation function
    // that initializes to zero. Protect the region above the memory size to catch accesses to invalid addresses.
    uint8_t* rdram;
    bool alloc_failed;
#ifdef _WIN32
    rdram = reinterpret_cast<uint8_t*>(VirtualAlloc(nullptr, allocation_size, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS));
    DWORD old_protect = 0;
    alloc_failed = (rdram == nullptr);
    if (!alloc_failed) {
        // VirtualProtect returns 0 on failure.
        alloc_failed = (VirtualProtect(rdram, mem_size, PAGE_READWRITE, &old_protect) == 0);
        if (alloc_failed) {
            VirtualFree(rdram, 0, MEM_RELEASE);
        }
    }
#else
    rdram = (uint8_t*)mmap(NULL, allocation_size, PROT_NONE, MAP_ANON | MAP_PRIVATE, -1, 0);
    alloc_failed = rdram == reinterpret_cast<uint8_t*>(MAP_FAILED);
    if (!alloc_failed) {
        // mprotect returns -1 on failure.
        alloc_failed = (mprotect(rdram, mem_size, PROT_READ | PROT_WRITE) == -1);
        if (alloc_failed) {
            munmap(rdram, allocation_size);
        }
    }
#endif
''', '''    // RDRAM as a plain zeroed allocation (docs/design/switch-port.md): libnx has no mmap,
    // so no 4 GiB reservation and no guard pages; the game's 8 MiB and the host's areas
    // below 16 MiB are all it uses.
    uint8_t* rdram = (uint8_t*)calloc(srw64_rdram_bytes, 1);
    bool alloc_failed = rdram == nullptr;
''')
    source = replace_once(source, '''    recomp::register_heap_exports();
    recomp::mods::register_config_exports();
    recomp::mods::register_hook_exports();
''', '')
    source = replace_once(source, '''    bool free_failed;
#ifdef _WIN32
    // VirtualFree returns zero on failure.
    free_failed = (VirtualFree(rdram, 0, MEM_RELEASE) == 0);
#else
    // munmap returns -1 on failure.
    free_failed = (munmap(rdram, allocation_size) == -1);
#endif
''', '''    std::free(rdram);
    bool free_failed = false;
''')
    source = adapt_rom_loading(source)
    return replace_once(source, 'enum GameStatus {', f'''// docs/design/switch-port.md: RDRAM as one allocation.
constexpr size_t srw64_rdram_bytes = {RDRAM_BYTES};

enum GameStatus {{''')


def adapt_rom_loading(source: str) -> str:
    """The ROM read once, as the PS Vita's build does (prepare_vita_runtime.py)."""
    source = remove_between(source, """    if (!check_hash(rom_data, game_entry.rom_hash)) {
        const std::string_view name{""", """    write_file(config_path / game_entry.stored_filename(), rom_data);""",
                            '3915038df5eb55df74751167cb8e5130ca6c71cb6657c484a4c206f859df7c50')
    source = replace_once(source, """    write_file(config_path / game_entry.stored_filename(), rom_data);
    
    return recomp::RomValidationError::Good;""", """    // Checked by the launcher (rom_order.hpp, rom_import.cpp) before the game starts.
    valid_game_roms.insert(game_id);
    recomp::set_rom_contents(std::move(rom_data));
    return recomp::RomValidationError::Good;""")
    source = replace_once(source, """void recomp::check_all_stored_roms() {
    for (const auto& cur_rom_entry: game_roms) {
        if (check_stored_rom(cur_rom_entry.second)) {
            valid_game_roms.insert(cur_rom_entry.first);
        }
    }
}""", """void recomp::check_all_stored_roms() {
    // select_rom keeps the ROM in memory and marks it valid; nothing is stored.
}""")
    return replace_once(source, """    std::vector<uint8_t> stored_rom_data = read_file(config_path / find_it->second.stored_filename());

    if (!check_hash(stored_rom_data, find_it->second.rom_hash)) {
        // The ROM no longer has the right hash, delete it.
        std::filesystem::remove(config_path / find_it->second.stored_filename());
        return false;
    }

    recomp::set_rom_contents(std::move(stored_rom_data));
    return true;""", """    return recomp::is_rom_loaded();""")


def adapt_renderer_context(source: str) -> str:
    # Its neighbours by their include-path names: this copy lives in another directory.
    source = replace_once(source, '#include "ultra64.h"\n#include "config.hpp"\n',
                          '#include "ultramodern/ultra64.h"\n#include "ultramodern/config.hpp"\n')
    return replace_once(source, '''#elif defined(__linux__) || defined(__ANDROID__)
        using WindowHandle = SDL_Window*;''', '''#elif defined(__linux__) || defined(__ANDROID__) || defined(__SWITCH__)
        using WindowHandle = SDL_Window*;''')


SWITCH_SEMAPHORE = """#elif defined(__SWITCH__)
//---------------------------------------------------------
// Semaphore (Switch): a count under a mutex with a condition variable, waits measured on the
// steady clock. moodycamel's POSIX path (newlib's sem_* over libnx) lost wake-ups: the game's
// idle thread slept on in pause_self while the VI's messages came in, and every game thread
// waited in osRecvMesg (creport, 2026-10-24) -- as the PS Vita's did on its own semaphores.
//---------------------------------------------------------
class Semaphore
{
private:
	std::mutex m_mutex;
	std::condition_variable m_condition;
	int m_count;

	Semaphore(const Semaphore& other) = delete;
	Semaphore& operator=(const Semaphore& other) = delete;

public:
	Semaphore(int initialCount = 0) : m_count(initialCount)
	{
		assert(initialCount >= 0);
	}

	bool wait()
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		m_condition.wait(lock, [this] { return m_count > 0; });
		--m_count;
		return true;
	}

	bool try_wait()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_count == 0) return false;
		--m_count;
		return true;
	}

	bool timed_wait(std::uint64_t usecs)
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		if (!m_condition.wait_for(lock, std::chrono::microseconds(usecs), [this] { return m_count > 0; })) return false;
		--m_count;
		return true;
	}

	void signal()
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			++m_count;
		}
		m_condition.notify_one();
	}

	void signal(int count)
	{
		if (count <= 0) return;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_count += count;
		}
		if (count == 1) m_condition.notify_one();
		else m_condition.notify_all();
	}
};
"""


SWITCH_LIGHTWEIGHT = """#if defined(__SWITCH__)
// The Switch: every operation is the counted semaphore's own (above). The atomic count and
// the spinning in front of it (the class below, elsewhere) is what lost wake-ups.
class LightweightSemaphore
{
public:
	typedef std::make_signed<std::size_t>::type ssize_t;

private:
	details::Semaphore m_sema;

public:
	LightweightSemaphore(ssize_t initialCount = 0, int = 10000) : m_sema(int(initialCount)) {}
	LightweightSemaphore(const LightweightSemaphore&) = delete;
	LightweightSemaphore& operator=(const LightweightSemaphore&) = delete;

	bool tryWait() { return m_sema.try_wait(); }
	bool wait() { return m_sema.wait(); }
	bool wait(std::int64_t timeout_usecs)
	{
		if (timeout_usecs < 0) return wait();
		return m_sema.timed_wait(std::uint64_t(timeout_usecs));
	}
	ssize_t tryWaitMany(ssize_t max)
	{
		ssize_t taken = 0;
		while (taken < max && tryWait()) ++taken;
		return taken;
	}
	ssize_t waitMany(ssize_t max, std::int64_t timeout_usecs)
	{
		if (max <= 0 || !wait(timeout_usecs)) return 0;
		return 1 + tryWaitMany(max - 1);
	}
	ssize_t waitMany(ssize_t max) { return waitMany(max, -1); }
	void signal(ssize_t count = 1) { m_sema.signal(int(count)); }
	std::size_t availableApprox() const { return 0; }
};
"""


def adapt_semaphore(source: str) -> str:
    if source.count("#elif defined(__unix__)") != 2:
        raise RuntimeError("Pinned lightweightsemaphore.h differs")
    source = replace_once(source, "#elif defined(__unix__)\n#include <semaphore.h>",
                          "#elif defined(__SWITCH__)\n#include <cassert>\n#include <chrono>\n#include <condition_variable>\n#include <mutex>\n"
                          "#elif defined(__unix__)\n#include <semaphore.h>")
    source = replace_once(source, "#elif defined(__unix__)\n//----", SWITCH_SEMAPHORE + "#elif defined(__unix__)\n//----")
    source = replace_once(source, "class LightweightSemaphore\n{", SWITCH_LIGHTWEIGHT + "#else\nclass LightweightSemaphore\n{")
    return replace_once(source, "\t}\n};\n\n}   // end namespace moodycamel", "\t}\n};\n#endif\n\n}   // end namespace moodycamel")


def adapt_error_handling(source: str) -> str:
    return replace_once(source, '''#ifdef __APPLE__
    std::_Exit(exit_status);''', '''#if defined(__APPLE__) || defined(__SWITCH__)
    std::_Exit(exit_status);''')


def adapt_events(source: str) -> str:
    # A VI state whose retrace count is 0 (one the game had not set yet, swapped in) left the
    # countdown at 0; the next VI took it to -1, it never met 0 again, and no VI message was
    # sent for the rest of the run while the VI went on: the hang that stayed after the lock
    # (on the Switch, and on the PS Vita, psv-port.md "the second kind").
    source = replace_once(source, """            if (remaining_retraces == 0) {""", """            if (remaining_retraces <= 0) {""")
    source = replace_once(source, """                remaining_retraces = cur_state->retrace_count;""",
                          """                remaining_retraces = cur_state->retrace_count > 0 ? cur_state->retrace_count : 1;""")
    # The VI thread's state for the stall report (stall_watchdog.cpp): VIs looped and sent,
    # and the countdown with the current and next states' queue and retrace count.
    source = replace_once(source, "    int remaining_retraces = 1;\n", "    remaining_retraces = 1;\n")
    source = replace_once(source, "        ultramodern::sleep_until(next);\n", "        ultramodern::sleep_until(next);\n        ++srw64_vi_loops;\n")
    source = replace_once(source, """                    ultramodern::enqueue_external_message_src(cur_state->mq, cur_state->msg, false, ultramodern::EventMessageSource::Vi);
""", """                    ultramodern::enqueue_external_message_src(cur_state->mq, cur_state->msg, false, ultramodern::EventMessageSource::Vi);
                    ++srw64_vi_sent;
""")
    source = replace_once(source, "void vi_thread_func() {\n", """static int remaining_retraces = 1;
static std::atomic<uint64_t> srw64_vi_loops{0}, srw64_vi_sent{0};
extern "C" void srw64_vi_report(char* out, size_t size) {
    std::lock_guard lock{ events_context.message_mutex };
    snprintf(out, size, "loops=%llu sent=%llu remaining=%d cur_mq=%08X cur_retraces=%d next_mq=%08X next_retraces=%d started=%d",
             (unsigned long long)srw64_vi_loops.load(), (unsigned long long)srw64_vi_sent.load(), remaining_retraces,
             uint32_t(events_context.vi.get_cur_state()->mq), events_context.vi.get_cur_state()->retrace_count,
             uint32_t(events_context.vi.get_next_state()->mq), events_context.vi.get_next_state()->retrace_count,
             int(ultramodern::is_game_started()));
}

void vi_thread_func() {
""")
    # The graphics thread presents with the swap that waits for the display: presenting every
    # screen update drains its queue at the VI rate, and a backlog built behind one slow display
    # list never shrank while the game's lists waited in it (DK64Recomp-NX and
    # MegaMan64Recomp-NX do the same). An update with a newer one queued behind it is skipped.
    source = replace_once(source, "        events_context.action_queue.enqueue(ScreenUpdateAction{ events_context.vi.regs });\n",
                          "        srw64_pending_screen_updates.fetch_add(1);\n"
                          "        events_context.action_queue.enqueue(ScreenUpdateAction{ events_context.vi.regs });\n")
    source = replace_once(source, """            else if (const auto* screen_update_action = std::get_if<ScreenUpdateAction>(&action)) {
""", """            else if (const auto* screen_update_action = std::get_if<ScreenUpdateAction>(&action)) {
                if (srw64_pending_screen_updates.fetch_sub(1) > 1) {
                    ++srw64_screen_updates_skipped;
                    continue;
                }
""")
    source = replace_once(source, "static int remaining_retraces = 1;\n", """static int remaining_retraces = 1;
static std::atomic<int> srw64_pending_screen_updates{0};
static std::atomic<uint64_t> srw64_screen_updates_skipped{0};
extern "C" uint64_t srw64_screen_updates_skipped_count() { return srw64_screen_updates_skipped.load(); }
""")
    # The lifecycle's 120 Hz latch (prepare_runtime_lifecycle.py) takes the message lock itself.
    return source


SWITCH_QUEUE = """// Generated by tools/recomp/toolchain/prepare_switch_runtime.py: moodycamel's queues for the
// Switch as a deque under a mutex with a condition variable (the lock-free ones lost items
// there), with the calls the runtime makes.
#pragma once
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <utility>
#include "lightweightsemaphore.h"

namespace moodycamel {

template<typename T>
class ConcurrentQueue {
public:
	ConcurrentQueue() = default;
	ConcurrentQueue(const ConcurrentQueue&) = delete;
	ConcurrentQueue& operator=(const ConcurrentQueue&) = delete;

	bool enqueue(const T& item) { return push(T(item)); }
	bool enqueue(T&& item) { return push(std::move(item)); }
	template<typename U>
	bool try_dequeue(U& item)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (items_.empty()) return false;
		item = std::move(items_.front());
		items_.pop_front();
		return true;
	}
	std::size_t size_approx() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return items_.size();
	}

protected:
	bool push(T&& item)
	{
		{
			std::lock_guard<std::mutex> lock(mutex_);
			items_.push_back(std::move(item));
		}
		ready_.notify_one();
		return true;
	}
	mutable std::mutex mutex_;
	std::condition_variable ready_;
	std::deque<T> items_;
};

template<typename T>
class BlockingConcurrentQueue : public ConcurrentQueue<T> {
public:
	template<typename U>
	void wait_dequeue(U& item)
	{
		std::unique_lock<std::mutex> lock(this->mutex_);
		this->ready_.wait(lock, [this] { return !this->items_.empty(); });
		item = std::move(this->items_.front());
		this->items_.pop_front();
	}
	template<typename U, typename Rep, typename Period>
	bool wait_dequeue_timed(U& item, const std::chrono::duration<Rep, Period>& timeout)
	{
		std::unique_lock<std::mutex> lock(this->mutex_);
		if (!this->ready_.wait_for(lock, timeout, [this] { return !this->items_.empty(); })) return false;
		item = std::move(this->items_.front());
		this->items_.pop_front();
		return true;
	}
	template<typename U>
	bool wait_dequeue_timed(U& item, std::int64_t timeout_usecs)
	{
		if (timeout_usecs < 0) { wait_dequeue(item); return true; }
		return wait_dequeue_timed(item, std::chrono::microseconds(timeout_usecs));
	}
};

}  // namespace moodycamel
"""


FILES_HELPER = '''
namespace {
// std::filesystem::copy_file through streams: libstdc++'s sets the copy's mode, which the
// Switch's SD card refuses.
void copy_through_streams(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& ec) {
    ec.clear();
    std::ifstream input{from, std::ios::binary};
    std::ofstream output{to, std::ios::binary | std::ios::trunc};
    if (!input || !output || !(output << input.rdbuf()) || !output.flush()) {
        ec = std::make_error_code(std::errc::io_error);
    }
}
}
'''


def adapt_files(source: str) -> str:
    source = replace_once(source, '#include "files.hpp"\n', '#include "files.hpp"\n' + FILES_HELPER)
    source = replace_once(source, "std::filesystem::copy_file(filepath, backup_path, std::filesystem::copy_options::overwrite_existing, ec);",
                          "copy_through_streams(filepath, backup_path, ec);")
    return replace_once(source, "std::filesystem::copy_file(temp_path, filepath, std::filesystem::copy_options::overwrite_existing, ec);",
                        "copy_through_streams(temp_path, filepath, ec);")


def adapt_librecomp_cmake(source: str) -> str:
    for removed in ("mods.cpp", "mod_events.cpp", "mod_hooks.cpp", "mod_manifest.cpp", "mod_config_api.cpp",
                    "config_option.cpp", "config.cpp", "heap.cpp"):
        source = replace_once(source, f'    "${{CMAKE_CURRENT_SOURCE_DIR}}/src/{removed}"\n', '')
    source = replace_once(source, '    "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/o1heap/o1heap/o1heap.c"\n', '')
    source = replace_once(source, '    "${CMAKE_CURRENT_SOURCE_DIR}/src/recomp.cpp"\n',
                          '    "${SRW64_SWITCH_RUNTIME}/recomp.cpp"\n')
    source = replace_once(source, '    "${CMAKE_CURRENT_SOURCE_DIR}/src/files.cpp"\n',
                          '    "${SRW64_SWITCH_RUNTIME}/files.cpp"\n')
    source = source.replace('${CMAKE_CURRENT_SOURCE_DIR}', '${SRW64_LIBRECOMP_SOURCE}')
    source = source.replace('${PROJECT_SOURCE_DIR}/..', '${SRW64_LIBRECOMP_SOURCE}/..')
    source = replace_once(source, '''target_include_directories(librecomp PUBLIC
    "${SRW64_LIBRECOMP_SOURCE}/include"
''', '''target_include_directories(librecomp PUBLIC
    "${SRW64_SWITCH_RUNTIME}/include"
    "${SRW64_LIBRECOMP_SOURCE}/include"
    "${SRW64_LIBRECOMP_SOURCE}/../N64Recomp/include"
''')
    source = replace_once(source, '''add_subdirectory(${SRW64_LIBRECOMP_SOURCE}/../thirdparty/miniz ${CMAKE_CURRENT_BINARY_DIR}/miniz)
add_subdirectory(${SRW64_LIBRECOMP_SOURCE}/../N64Recomp ${CMAKE_CURRENT_BINARY_DIR}/N64Recomp EXCLUDE_FROM_ALL)

target_link_libraries(librecomp PRIVATE ultramodern N64Recomp LiveRecomp)
''', '''add_subdirectory(${SRW64_LIBRECOMP_SOURCE}/../thirdparty/miniz ${CMAKE_CURRENT_BINARY_DIR}/miniz)

target_link_libraries(librecomp PRIVATE ultramodern)
''')
    return ('# Generated by tools/recomp/toolchain/prepare_switch_runtime.py from librecomp/CMakeLists.txt;\n'
            '# SRW64_LIBRECOMP_SOURCE and SRW64_SWITCH_RUNTIME come from the including project.\n' + source)


def main() -> None:
    lock = json.loads((ROOT / "config/recomp/toolchain.json").read_text())["sources"]
    runtime = UPSTREAM / "N64ModernRuntime"
    lifecycle = json.loads((ROOT / "build/recomp/runtime-lifecycle/manifest.json").read_text())
    if lifecycle["revision"] != lock["N64ModernRuntime"]["commit"]:
        raise RuntimeError("Runtime lifecycle sources were generated from another revision")
    # recomp.cpp and events.cpp: the lifecycle's (prepare_runtime_lifecycle.py), with the
    # Switch's changes over them.
    lifecycle_sources = {}
    for relative in ("librecomp/src/recomp.cpp", "ultramodern/src/events.cpp"):
        change = next(c for c in lifecycle["changes"] if c["source"] == relative)
        text = (ROOT / change["generated"]).read_text()
        if sha256(text) != change["after_sha256"]:
            raise RuntimeError(f"Runtime lifecycle {relative} differs from its manifest")
        lifecycle_sources[change["generated"]] = text
    lifecycle_recomp, lifecycle_events = lifecycle_sources
    inputs = [
        ("recomp.cpp", None, lifecycle_recomp, adapt_recomp_cpp),
        ("librecomp/CMakeLists.txt", runtime, "librecomp/CMakeLists.txt", adapt_librecomp_cmake),
        ("include/ultramodern/renderer_context.hpp", runtime, "ultramodern/include/ultramodern/renderer_context.hpp", adapt_renderer_context),
        ("include/lightweightsemaphore.h", runtime, "thirdparty/concurrentqueue/lightweightsemaphore.h", adapt_semaphore),
        ("include/blockingconcurrentqueue.h", runtime, "thirdparty/concurrentqueue/blockingconcurrentqueue.h", lambda text: SWITCH_QUEUE),
        ("error_handling.cpp", runtime, "ultramodern/src/error_handling.cpp", adapt_error_handling),
        ("events.cpp", None, lifecycle_events, adapt_events),
        ("files.cpp", runtime, "librecomp/src/files.cpp", adapt_files),
    ]
    records = []
    for name, checkout, relative, adapt in inputs:
        source = lifecycle_sources[relative] if checkout is None else pinned(checkout, relative)
        adapted = adapt(source)
        destination = OUTPUT / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not destination.exists() or destination.read_text() != adapted:
            destination.write_text(adapted)
        records.append({"source": relative if checkout is None else f"{checkout.name}/{relative}",
                        "generated": str(destination.relative_to(ROOT)),
                        "before_sha256": sha256(source), "after_sha256": sha256(adapted)})
    (OUTPUT / "manifest.json").write_text(json.dumps({
        "schema": "srw64.switch-runtime.v1", "rdram_bytes": RDRAM_BYTES,
        "revisions": {"N64Recomp": lock["N64Recomp"]["commit"], "N64ModernRuntime": lock["N64ModernRuntime"]["commit"]},
        "changes": records}, indent=2) + "\n")


if __name__ == "__main__":
    main()
