// Exercise adapted upstream scheduling, waits and the real context cleaner.
// No ROM is needed; the guest entrypoints below create deterministic wait states.
#include "threads.cpp"
#include "mesgqueue.cpp"
#include "scheduling.cpp"
#include <iostream>
#include <vector>

std::atomic_bool exited{false};
std::atomic<unsigned> entered{}, writes{};
std::atomic<bool> release_creator{};
std::atomic<UltraThreadContext*> retained_peer{};
constexpr int32_t idle = int32_t(0x80001000);
constexpr int32_t worker = int32_t(0x80001200);
constexpr int32_t dormant = int32_t(0x80001400);
constexpr int32_t queue = int32_t(0x80001800);

void run_thread_function(uint8_t* rdram, uint64_t entry, uint64_t, uint64_t) {
    ++entered;
    if (entry == 1) {
        // Idle guest blocked on the host queue: shutdown must supply a wakeup.
        for (;;) ultramodern::wait_for_external_message(rdram);
    } else if (entry == 2) {
        // Empty guest message queue yields to idle; shutdown must wake this
        // context without resuming osRecvMesg or dequeuing a nonexistent task.
        osRecvMesg(rdram, queue, NULLPTR, OS_MESG_BLOCK);
        ++writes;
    } else if (entry == 3) {
        // A running guest which polls messages must stop even if never blocked.
        for (;;) { osRecvMesg(rdram, queue, NULLPTR, OS_MESG_NOBLOCK); ++writes; }
    } else if (entry == 4) {
        // Shutdown racing a guest which creates a child: registration rejected.
        while (!release_creator.load()) std::this_thread::yield();
        osCreateThread(rdram, dormant, 3, 99, NULLPTR, int32_t(0x80007000), 1);
        ++writes;
    } else if (entry == 5) {
        osDestroyThread(rdram, NULLPTR);
    } else if (entry == 6) {
        // Model an in-flight scheduler operation holding a peer context at
        // quit. The peer joins first, but its semaphore must remain alive.
        UltraThreadContext* peer = retained_peer.load();
        while (!peer) {
            std::this_thread::yield();
            peer = retained_peer.load();
        }
        while (!exited.load()) std::this_thread::yield();
        for (;;) {
            bool peer_joined;
            {
                std::lock_guard lock(srw64_guest_threads.mutex);
                peer_joined = !srw64_guest_threads.contexts.contains(peer);
            }
            if (peer_joined) break;
            std::this_thread::yield();
        }
        peer->running.signal();
        srw64_guest_checkpoint();
    } else {
        assert(false && "never-started or post-shutdown child executed");
    }
}

template<class Predicate> void until(Predicate predicate) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!predicate()) {
        assert(std::chrono::steady_clock::now() < end);
        std::this_thread::yield();
    }
}

int main() {
    for (unsigned mode = 0; mode < 6; ++mode) {
        exited = false; entered = 0; writes = 0; release_creator = false;
        retained_peer = nullptr;
        thread_self = NULLPTR;
        ultramodern::set_entrypoint_thread();
        std::vector<uint8_t> ram(32768);
        auto* rdram = ram.data();
        ultramodern::init_thread_cleanup();
        osCreateMesgQueue(rdram, queue, int32_t(0x80001900), 1);
        if (mode == 0 || mode == 4) {
            osCreateThread(rdram, idle, 1, 1, NULLPTR, int32_t(0x80006000), 1);
            osCreateThread(rdram, worker, 2, mode == 0 ? 2 : 5, NULLPTR, int32_t(0x80006800), 10);
            osCreateThread(rdram, dormant, 3, 99, NULLPTR, int32_t(0x80007000), 1);
            if (mode == 4) {
                // Normal destruction/reuse of a stopped OSThread must retain
                // both host contexts until their independent joins complete.
                osDestroyThread(rdram, dormant);
                osCreateThread(rdram, dormant, 3, 99, NULLPTR, int32_t(0x80007000), 1);
            }
            ultramodern::schedule_running_thread(rdram, idle);
            osStartThread(rdram, worker);
            until([] { return entered == 2; });
        } else if (mode == 1 || mode == 2) {
            osCreateThread(rdram, worker, 2, mode == 1 ? 3 : 4, NULLPTR, int32_t(0x80006800), 10);
            osStartThread(rdram, worker);
            until([] { return entered == 1; });
        } else if (mode == 5) {
            osCreateThread(rdram, dormant, 3, 99, NULLPTR, int32_t(0x80007000), 1);
            retained_peer = TO_PTR(OSThread, dormant)->context;
            osCreateThread(rdram, worker, 2, 6, NULLPTR, int32_t(0x80006800), 10);
            osStartThread(rdram, worker);
            until([] { return entered == 1; });
        }
        exited = true;
        srw64_request_guest_shutdown();
        release_creator = true;
        ultramodern::join_thread_cleaner_thread();
        ultramodern::join_thread_cleaner_thread();
        assert(srw64_guest_threads.contexts.empty());
        if (mode != 1) assert(writes == 0);
        const auto final_writes = writes.load();
        ram.clear(); ram.shrink_to_fit();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        assert(writes == final_writes);
        // Reset host-only fixture queues between independent runtime instances.
        QueuedMessage unused;
        while (external_messages.try_dequeue(unused)) {}
    }
    for (unsigned iteration = 0; iteration < 40; ++iteration) {
        exited = false;
        std::vector<uint8_t> ram(32768);
        ultramodern::init_thread_cleanup();
        std::atomic<bool> ready{};
        std::thread quitter([&] {
            until([&] { return ready.load(); });
            exited = true;
            srw64_request_guest_shutdown();
        });
        ready = true;
        try {
            osCreateThread(ram.data(), dormant, 3, 99, NULLPTR, int32_t(0x80007000), 1);
        } catch (ultramodern::thread_terminated&) {}
        quitter.join();
        ultramodern::join_thread_cleaner_thread();
        QueuedMessage unused;
        while (external_messages.try_dequeue(unused)) {}
    }
    std::cout << "Guest shutdown: blocked receive, idle wait, never-started, running poll, destruction/reuse, retained scheduler context, creation races, no guests and repeated join passed\n";
}
