#include "stall_watchdog.hpp"

#if defined(__ANDROID__) && defined(__aarch64__)
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <string>
#include <sys/syscall.h>
#include <thread>
#include <ucontext.h>
#include <unistd.h>

uint64_t srw64_current_vi();

namespace srw64::stall {
namespace {
std::atomic<uint64_t> frames{0};
std::atomic<bool> started{false};
bool test_stall = false;    // SRW64_STALL_TEST=1 (development): the render thread stops at frame 120

// One thread at a time: the watchdog names it, its handler fills the frames.
constexpr int kMaxFrames = 48;
std::atomic<int> target{0};
uintptr_t pcs[kMaxFrames];
std::atomic<int> depth{-1};

// Return addresses carry a pointer authentication code in their high bits on newer CPUs
// (Android signs them); XPACLRI strips it, and is a no-op where there is none.
uintptr_t strip(uintptr_t address) {
    register uintptr_t lr asm("x30") = address;
    asm("hint #7" : "+r"(lr));
    return lr;
}

// pc, lr, then the frame-pointer chain (x29 -> {previous x29, return address}), kept to
// the thread's own stack: above sp, rising, 16-byte aligned, within 8 MB.
void sample(int, siginfo_t*, void* context) {
    if (int(syscall(SYS_gettid)) != target.load()) return;
    const auto& m = static_cast<ucontext_t*>(context)->uc_mcontext;
    int n = 0;
    pcs[n++] = uintptr_t(m.pc);
    pcs[n++] = strip(uintptr_t(m.regs[30]));
    uintptr_t fp = uintptr_t(m.regs[29]);
    const uintptr_t sp = uintptr_t(m.sp);
    while (n < kMaxFrames && fp > sp && fp - sp < (8u << 20) && (fp & 15) == 0) {
        const auto* frame = reinterpret_cast<const uintptr_t*>(fp);
        const uintptr_t next = frame[0], ret = strip(frame[1]);
        if (!ret) break;
        pcs[n++] = ret;
        if (next <= fp) break;
        fp = next;
    }
    depth.store(n);
}

std::string name_of(const char* tid) {
    char path[64];
    std::snprintf(path, sizeof path, "/proc/self/task/%s/comm", tid);
    char name[32] = {};
    if (FILE* f = std::fopen(path, "r")) {
        if (!std::fgets(name, sizeof name, f)) name[0] = 0;
        std::fclose(f);
    }
    if (char* end = std::strchr(name, '\n')) *end = 0;
    return name;
}

void dump(double seconds) {
    std::fprintf(stderr, "SRW64_STALL vi=%llu frames=%llu stalled=%.1fs: thread stacks follow\n",
                 (unsigned long long)srw64_current_vi(), (unsigned long long)frames.load(), seconds);
    const int self = int(syscall(SYS_gettid));
    DIR* dir = opendir("/proc/self/task");
    if (!dir) return;
    int threads = 0;
    while (dirent* entry = readdir(dir)) {
        const int tid = std::atoi(entry->d_name);
        if (tid <= 0 || tid == self || ++threads > 80) continue;
        depth.store(-1);
        target.store(tid);
        if (syscall(SYS_tgkill, getpid(), tid, SIGUSR2) != 0) continue;
        for (int i = 0; i < 50 && depth.load() < 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(4));
        const int n = depth.load();
        target.store(0);
        if (n < 0) continue;
        std::fprintf(stderr, "SRW64_STALL thread %d \"%s\"\n", tid, name_of(entry->d_name).c_str());
        for (int i = 0; i < n; ++i) {
            Dl_info info{};
            if (dladdr(reinterpret_cast<void*>(pcs[i]), &info) && info.dli_fname) {
                const char* file = std::strrchr(info.dli_fname, '/');
                std::fprintf(stderr, "  #%02d pc %08lx %s", i, (unsigned long)(pcs[i] - uintptr_t(info.dli_fbase)),
                             file ? file + 1 : info.dli_fname);
                if (info.dli_sname)
                    std::fprintf(stderr, " (%s+%lu)", info.dli_sname, (unsigned long)(pcs[i] - uintptr_t(info.dli_saddr)));
                std::fputc('\n', stderr);
            } else {
                std::fprintf(stderr, "  #%02d pc %016lx\n", i, (unsigned long)pcs[i]);
            }
        }
    }
    closedir(dir);
    std::fflush(stderr);
}

void watch() {
    uint64_t last_frames = frames.load(), last_vi = srw64_current_vi();
    auto since = std::chrono::steady_clock::now();
    int dumps = 0;
    bool dumped = false;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        const uint64_t f = frames.load(), vi = srw64_current_vi();
        const auto now = std::chrono::steady_clock::now();
        if (f != last_frames) {                 // drawing again: re-arm
            last_frames = f; last_vi = vi; since = now; dumped = false;
            continue;
        }
        const double seconds = std::chrono::duration<double>(now - since).count();
        // The game runs on (a paused app stops the VI clock too) but nothing is drawn.
        if (!dumped && dumps < 3 && f > 0 && seconds >= 5 && vi >= last_vi + 120) {
            dump(seconds);
            dumped = true;
            ++dumps;
        }
    }
}
}

void start() {
    if (started.exchange(true)) return;
    const char* test = std::getenv("SRW64_STALL_TEST");
    test_stall = test && std::strcmp(test, "1") == 0;
    struct sigaction action{};
    action.sa_sigaction = sample;
    action.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&action.sa_mask);
    sigaction(SIGUSR2, &action, nullptr);
    std::thread(watch).detach();
}

void frame_done() {
    if (frames.fetch_add(1, std::memory_order_relaxed) + 1 == 120 && test_stall) {
        std::fprintf(stderr, "SRW64_STALL_TEST: the render thread stops here\n");
        for (;;) std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
}
#else
namespace srw64::stall {
void start() {}
void frame_done() {}
}
#endif
