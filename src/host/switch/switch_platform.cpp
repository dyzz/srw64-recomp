// The Switch's platform layer (docs/design/switch-port.md, S0): what the runtime and the
// host ask of the system that libnx answers differently from the desktop.
#include <switch.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <cerrno>
#include <fcntl.h>
#include <iterator>
#include <sys/stat.h>
#include <unistd.h>

#include "ultramodern/ultramodern.hpp"

// Horizon has no call to name a thread; the name places it instead. libnx starts every
// thread on core 0, where SDL's audio thread then got about half of real time (the
// DK64Recomp-NX port, which runs the same runtime): the graphics thread goes to core 2,
// the game's threads (one runs at a time), the VI, timer and RSP task threads to core 1,
// and core 0 stays with the main thread and the audio.
namespace ultramodern {
void set_native_thread_name(const std::string& name) {
    int core = -1;
    if (name == "Gfx Thread") core = 2;
    else for (const char* game_side : {"VI Thread", "Timer Thread", "SP Task Thread", "Game"})
        if (name.rfind(game_side, 0) == 0) core = 1;
    if (core >= 0) svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, 1u << core);
}
// Priorities stay libnx's default for new threads, 0x3B: the level Horizon time-slices.
// Ranked as the desktop does (0x2C-0x30 for VI, timer and game), a thread that waited by
// yielding never let a less urgent one on its core run again, and the game stopped at its
// first dialogue on the world map while the VI went on.
void set_native_thread_priority(ThreadPriority) {}
}

// Whether stdout and stderr go to nxlink (console_log.cpp keeps them there).
static int nxlink_socket = -1;
bool srw64_switch_nxlink() { return nxlink_socket >= 0; }

extern "C" {
// libnx's own: the arguments hbmenu passed, argv[0] the NRO's path ("sdmc:/switch/...").
extern int __system_argc;
extern char** __system_argv;
}

// The NRO's path without its device: "/switch/Marchwind64/Marchwind64.nro". sdmc: is the
// default device, and a path without one is absolute to std::filesystem (a "sdmc:/" path
// is relative there, and fs::absolute would put the working folder in front of it).
std::string srw64_switch_program_path() {
    if (__system_argc < 1 || !__system_argv || !__system_argv[0]) return {};
    std::string path = __system_argv[0];
    if (path.rfind("sdmc:", 0) == 0) path.erase(0, 5);
    return path;
}

// The console's language as a BCP 47 code (zh-Hans, zh-Hant, ja, en-US...), for the first
// launch (runtime.cpp, preferred_languages).
std::string srw64_switch_language() {
    std::string language;
    if (R_SUCCEEDED(setInitialize())) {
        u64 code = 0;
        if (R_SUCCEEDED(setGetSystemLanguage(&code))) language.assign(reinterpret_cast<const char*>(&code), strnlen(reinterpret_cast<const char*>(&code), sizeof code));
        setExit();
    }
    return language;
}

// The launch on screen until the game's window takes it: libnx's text console, one line a
// step with the seconds since start (the same steps go to nxlink as SRW64_SWITCH_TIME).
// The console takes stdout; srw64_switch_boot_end gives it back to nxlink.
namespace {
enum class Boot { Off, Shown, Over };
Boot boot = Boot::Off;
}
void srw64_switch_mark(const char* label) {
    const unsigned long long ms = armTicksToNs(armGetSystemTick()) / 1000000ull;
    std::fprintf(stderr, "SRW64_SWITCH_TIME %llu ms %s\n", ms, label);
    if (boot == Boot::Over) return;
    if (boot == Boot::Off) {
        consoleInit(nullptr);
        std::printf("\n  Marchwind64\n\n");
        boot = Boot::Shown;
    }
    std::printf("  %5.1f s  %s\n", ms / 1000.0, label);
    consoleUpdate(nullptr);
}
void srw64_switch_boot_end() {
    if (boot == Boot::Shown) {
        consoleExit(nullptr);
        if (nxlink_socket >= 0) dup2(nxlink_socket, STDOUT_FILENO);
    }
    boot = Boot::Over;
}

// Started as an application (a game held R on, or the title override) and not from the
// album, where a library applet gets about 370 MiB in all (switch-port.md, the smoke run).
bool srw64_switch_application() {
    const AppletType type = appletGetAppletType();
    return type == AppletType_Application || type == AppletType_SystemApplication;
}

// A message on libnx's text console until + is pressed: for the album, where the error
// applet cannot be started (it ended in Atmosphère's fatal screen, 2128-0500, on the
// first try) and there is no window yet.
void srw64_switch_message(const std::string& message) {
    std::fprintf(stderr, "SRW64_SWITCH_MESSAGE %s\n", message.c_str());
    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    std::printf("\n  Marchwind64\n\n  %s\n\n  Press + to leave.\n", message.c_str());
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr);
}

// The system's error box: the message in a dialog, the details behind its button. Only an
// application can start it (srw64_switch_message otherwise).
void srw64_switch_error(const std::string& message, const std::string& details) {
    std::fprintf(stderr, "SRW64_SWITCH_ERROR %s\n%s\n", message.c_str(), details.c_str());
    if (!srw64_switch_application()) {
        srw64_switch_message(details.empty() ? message : message + "\n\n  " + details);
        return;
    }
    ErrorApplicationConfig config;
    if (R_FAILED(errorApplicationCreate(&config, message.substr(0, 2047).c_str(),
                                        details.empty() ? nullptr : details.substr(0, 2047).c_str()))) return;
    errorApplicationShow(&config);
}

extern "C" {

// GPU memory the driver may map: 8 MiB by default, where GL uploads failed with
// GL_OUT_OF_MEMORY once enough textures lived (DK64Recomp-NX).
u32 __nx_nv_transfermem_size = 32 * 1024 * 1024;

// Threads: libnx gives a thread made without a stack 128 KiB, too little for the
// recompiled code's frames; 1 MiB, as on the desktop's smallest. And it cannot detach a
// thread (std::thread::detach threw ENOSYS): a detached thread here lives until the
// program ends anyway, so detaching succeeds without freeing anything.
int __real___syscall_thread_create(void* thread, void* (*function)(void*), void* argument, void* stack, size_t size);
int __wrap___syscall_thread_create(void* thread, void* (*function)(void*), void* argument, void* stack, size_t size) {
    if (!stack && !size) size = 1024 * 1024;
    return __real___syscall_thread_create(thread, function, argument, stack, size);
}
int __syscall_thread_detach(void*) { return 0; }

// The save files and the run's state are written to a temporary name and renamed over
// the old file; FAT on the SD card refuses to rename onto an existing file
// (std::filesystem::rename threw in on_vi's live-state.json), so the old one goes first.
int __real_rename(const char* from, const char* to);
int __wrap_rename(const char* from, const char* to) {
    if (__real_rename(from, to) == 0) return 0;
    const int failure = errno;
    struct stat source{}, target{};
    if (stat(from, &source) != 0 || stat(to, &target) != 0 || S_ISDIR(target.st_mode) || unlink(to) != 0) {
        errno = failure;
        return -1;
    }
    return __real_rename(from, to);
}

// std::filesystem::copy_file ends by setting the copy's mode with fchmod, which libnx does
// not implement (ENOSYS, "cannot copy file: Function not implemented" for the save card).
// FAT has no modes to set: succeed.
int __wrap_fchmod(int, mode_t) { return 0; }
int __wrap_chmod(const char*, mode_t) { return 0; }

// A crash is left to Atmosphère's creport (sdmc:/atmosphere/crash_reports, switch-port.md
// "Crashes"): it walks the stack with the modules' addresses. abort() (fail(), a failed
// assert, std::terminate) would only end the process, so it breaks into the kernel
// instead, which creport reports the same way (--wrap=abort, src/host/CMakeLists.txt).
void __wrap_abort() {
    std::fputs("SRW64_ABORT\n", stderr);
    std::fflush(stderr);
    svcBreak(BreakReason_Panic, 0, 0);
    __builtin_unreachable();
}

// Before main and after it (libnx's weak hooks): the sockets, and stdout and stderr to
// nxlink on the Mac that pushed the NRO (tools/release/switch/README.txt).
void userAppInit() {
    if (R_SUCCEEDED(socketInitializeDefault())) nxlink_socket = nxlinkStdio();
    // Never wait on the Mac: a blocked write (the console's Wi-Fi asleep) stopped the thread
    // that logged (MegaMan64Recomp-NX). A line may be lost instead.
    if (nxlink_socket >= 0) fcntl(nxlink_socket, F_SETFL, fcntl(nxlink_socket, F_GETFL, 0) | O_NONBLOCK);
}

void userAppExit() {
    if (nxlink_socket >= 0) close(nxlink_socket);
    socketExit();
}
}
