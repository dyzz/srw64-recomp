#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <set>
#include <vector>
#include "ultramodern/ultramodern.hpp"

extern std::atomic_bool exited;

// Only call on guest threads at scheduling/message boundaries. Unwind to the
// runtime's thread_terminated catch without resuming guest code or queues.
inline void srw64_guest_checkpoint() {
    if (exited.load()) throw ultramodern::thread_terminated{};
}

// Own contexts until the cleaner has joined them. Registration and host_thread
// publication share the lock with wakeup and deletion, including never-started
// threads and a child which terminates immediately during osCreateThread.
struct Srw64GuestThreads {
    std::mutex mutex;
    std::condition_variable changed;
    std::set<UltraThreadContext*> contexts;
    std::vector<UltraThreadContext*> retired;
    size_t created{}, joined{};

    template<class Start> void start(UltraThreadContext* context, Start start) {
        std::lock_guard lock(mutex);
        srw64_guest_checkpoint();
        contexts.insert(context);
        try { context->host_thread = start(); }
        catch (...) { contexts.erase(context); throw; }
        ++created;
        // A shutdown child may finish before osCreateThread returns. Keep the
        // cleaner from deleting its initialized semaphore before we consume it.
        context->initialized.wait();
    }

    size_t wake_all() {
        std::lock_guard lock(mutex);
        for (auto* context : contexts) context->running.signal();
        return contexts.size();
    }

    void reclaim(UltraThreadContext* context) {
        std::lock_guard lock(mutex);
        // A completed thread only enqueues itself; it never takes this lock.
        context->host_thread.join();
        const auto removed = contexts.erase(context);
        assert(removed == 1);
        // Guest OSThread.context may still be held by another thread that was
        // already inside a scheduling operation when quit arrived. Join now,
        // but keep its semaphore alive until ALL guest code has stopped.
        if (exited.load()) retired.push_back(context);
        else delete context;
        ++joined;
        changed.notify_all();
    }

    void wait_empty() {
        std::unique_lock lock(mutex);
        changed.wait(lock, [&] { return contexts.empty(); });
        assert(created == joined);
        for (auto* context : retired) delete context;
        retired.clear();
        std::fprintf(stderr, "SRW64_GUEST_THREADS_JOINED created=%zu joined=%zu remaining=0\n", created, joined);
    }
};

inline Srw64GuestThreads srw64_guest_threads;
void srw64_wake_external_message();
void srw64_request_guest_shutdown();
