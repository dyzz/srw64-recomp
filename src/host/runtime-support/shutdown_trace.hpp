// Opt-in diagnostics only: inspect our own host threads at the RDRAM boundary.
#if defined(__APPLE__)
#include <libproc.h>
#include <sys/proc_info.h>
#include <unistd.h>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
static void srw64_trace_shutdown_threads(const char* phase, const void* ram) {
    if (!std::getenv("SRW64_SHUTDOWN_TRACE")) return;
    uint64_t ids[1024]{};
    const int bytes = proc_pidinfo(getpid(), PROC_PIDLISTTHREADS, 0, ids, sizeof(ids));
    unsigned guest_threads = 0;
    bool observed = bytes > 0 && bytes < int(sizeof(ids));
    for (int i = 0; i < bytes / int(sizeof(ids[0])); ++i) {
        proc_threadinfo info{};
        if (proc_pidinfo(getpid(), PROC_PIDTHREADINFO, ids[i], &info, sizeof(info)) != sizeof(info)) {
            observed = false;
            continue;
        }
        if (std::strncmp(info.pth_name, "Game Thread ", 12) != 0) continue;
        ++guest_threads;
        std::fprintf(stderr, "SRW64_SHUTDOWN_THREAD phase=%s id=%llu name=%s state=%d rdram=%p\n",
            phase, (unsigned long long)ids[i], info.pth_name, info.pth_run_state, ram);
    }
    if (observed) std::fprintf(stderr, "SRW64_SHUTDOWN_BOUNDARY phase=%s guest_threads=%u rdram=%p\n", phase, guest_threads, ram);
    else std::fprintf(stderr, "SRW64_SHUTDOWN_UNOBSERVED phase=%s rdram=%p\n", phase, ram);
    std::fflush(stderr);
}
#else
static void srw64_trace_shutdown_threads(const char*, const void*) {}
#endif
