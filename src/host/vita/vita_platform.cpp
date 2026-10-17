// What the PS Vita's newlib and the runtime leave out (docs/design/psv-port.md, V2).
#include <cstdio>
#include <exception>
#include <typeinfo>
#include <malloc.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <string>

#include "ultramodern/ultramodern.hpp"

// The runtime names and ranks its threads on Windows, Linux and Apple only; the Vita's
// threads keep their defaults (as on the other platforms, the ranks change nothing).
namespace ultramodern {
void set_native_thread_name(const std::string&) {}
void set_native_thread_priority(ThreadPriority) {}
}

extern "C" {
// Memory: the user partition's 365 MiB with ATTRIBUTE2=12 in the VPK's param.sfo, most of it
// for newlib's heap (the ROM, RDRAM, fonts, RmlUi: some 200 MiB at the first story scene,
// and RmlUi's 20 MiB font did not fit in 200), the rest vitaGL's 24 MiB pool and the
// system's. The main thread and every pthread (the runtime's N64 threads run deep
// recompiled calls) get stacks of a size that holds them.
int _newlib_heap_size_user = 320 * 1024 * 1024;
unsigned int sceUserMainThreadStackSize = 4 * 1024 * 1024;
unsigned int _pthread_stack_default_user = 1 * 1024 * 1024;

// newlib on the Vita has neither: one process, and stdio locks of its own.
void flockfile(FILE*) {}
void funlockfile(FILE*) {}
int flock(int, int) { return 0; }
}

// The network for the debug interface (ux0:data/Marchwind64/debug): the module loaded and
// started, as every Vita program that opens a socket must first.
bool srw64_vita_network() {
    static bool started = [] {
        if (sceSysmoduleLoadModule(SCE_SYSMODULE_NET) < 0) return false;
        static char memory[1024 * 1024];
        SceNetInitParam parameters{memory, int(sizeof memory), 0};
        const int result = sceNetInit(&parameters);
        if (result < 0 && result != int(SCE_NET_ERROR_EBUSY)) return false;
        sceNetCtlInit();
        return true;
    }();
    return started;
}

namespace {
// The heap in use and the memory the system has left (docs/design/psv-port.md's budget).
void report_memory() {
    const struct mallinfo heap = mallinfo();
    SceKernelFreeMemorySizeInfo free{};
    free.size = sizeof free;
    sceKernelGetFreeMemorySize(&free);
    std::fprintf(stderr, "SRW64_VITA_MEMORY heap=%d KiB free user=%d KiB cdram=%d KiB phycont=%d KiB\n",
                 heap.uordblks / 1024, free.size_user / 1024, free.size_cdram / 1024, free.size_phycont / 1024);
    std::fflush(stderr);
}
}

// An exception that leaves a thread ends the program with abort(), which Vita3K and the
// console report only as an undefined instruction: name it in console.log first.
void srw64_vita_terminate() {
    if (const auto current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception& failure) {
            std::fprintf(stderr, "SRW64_TERMINATE %s (%s)\n", failure.what(), typeid(failure).name());
        } catch (...) {
            std::fprintf(stderr, "SRW64_TERMINATE unknown exception\n");
        }
    } else {
        std::fprintf(stderr, "SRW64_TERMINATE no exception\n");
    }
    report_memory();
    std::abort();
}

