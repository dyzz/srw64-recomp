// The Switch's platform layer (docs/design/switch-port.md, S0): what the runtime and the
// host ask of the system that libnx answers differently from the desktop.
#include <switch.h>

#include <cstdio>
#include <malloc.h>
#include <cstring>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <thread>
#include <string>
#include <sys/iosupport.h>
#include <sys/socket.h>
#include <cerrno>
#include <fcntl.h>
#include <iterator>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "ultramodern/ultramodern.hpp"
#include "stb/stb_image.h"

// Horizon has no call to name a thread; the name places it instead. libnx starts every
// thread on core 0, where SDL's audio thread then got about half of real time (the
// DK64Recomp-NX port, which runs the same runtime): the graphics thread goes to core 2,
// the game's threads (one runs at a time), the VI, timer and RSP task threads to core 1,
// and core 0 stays with the main thread and the audio.
// Every thread the program makes, for the performance lines' CPU time per thread
// (srw64_switch_perf_system): its handle, the name ultramodern gives it, else where it was
// made (return addresses into the NRO, for addr2line on the matching ELF).
namespace {
struct Watched { Handle handle; char name[24]; uintptr_t made[4]; u64 last; };
std::mutex watched_mutex;
std::vector<Watched> watched;
uintptr_t module_base() {
    static const uintptr_t base = [] {
        MemoryInfo info{};
        u32 page = 0;
        svcQueryMemory(&info, &page, reinterpret_cast<u64>(&module_base));
        return uintptr_t(info.addr);
    }();
    return base;
}
void watch_thread(Handle handle, const char* name, const uintptr_t* made) {
    std::lock_guard lock(watched_mutex);
    for (auto& entry : watched)
        if (entry.handle == handle) {
            if (name) std::snprintf(entry.name, sizeof entry.name, "%s", name);
            return;
        }
    Watched entry{handle, {}, {}, 0};
    if (name) std::snprintf(entry.name, sizeof entry.name, "%s", name);
    if (made) std::memcpy(entry.made, made, sizeof entry.made);
    watched.push_back(entry);
}
}

namespace ultramodern {
void set_native_thread_name(const std::string& name) {
    watch_thread(threadGetCurHandle(), name.c_str(), nullptr);
    int core = -1;
    if (name == "Gfx Thread") core = 2;
    else for (const char* game_side : {"VI Thread", "Timer Thread", "SP Task Thread", "Game"})
        if (name.rfind(game_side, 0) == 0) core = 1;
    if (core >= 0) svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, 1u << core);
    // One above the helpers that share its core (srw64_switch_background_thread): time-sliced
    // with them, a frame's work waited behind a glyph or a decode (an 80 ms "upload" of an
    // 82 x 100 glyph). It blocks between frames (about a third of the core), so they still run.
    if (name == "Gfx Thread") svcSetThreadPriority(CUR_THREAD_HANDLE, 0x3A);
}
// Priorities stay libnx's default for new threads, 0x3B: the level Horizon time-slices (the
// Gfx thread alone goes one above, over its core's helpers).
// Ranked as the desktop does (0x2C-0x30 for VI, timer and game), a thread that waited by
// yielding never let a less urgent one on its core run again, and the game stopped at its
// first dialogue on the world map while the VI went on.
void set_native_thread_priority(ThreadPriority) {}
}

// A helper thread that must never hold up the ones it serves (the UI's picture reader, the
// screenshot writer, the log writer, the HD decoders): on the Gfx thread's core 2, away from
// the window thread on core 0, at the priority Horizon time-slices (0x3B) so it shares the core
// rather than waits for it. On core 0 the UI's update waited 30-40 ms for the picture reader's
// decodes; at the lowest priority (0x3F), on either core, a busy thread starved it: pages never
// got their HD pictures, and the UI waited 35 ms a frame on the lock it held when preempted.
extern "C" void srw64_switch_background_thread() {
    svcSetThreadCoreMask(CUR_THREAD_HANDLE, 2, 1u << 2);
    svcSetThreadPriority(CUR_THREAD_HANDLE, 0x3B);
}

// stdout and stderr (console_log.hpp): one device for both that keeps the last 512 KiB in
// memory (the debug interface serves it as console.log: Horizon will not open or even stat a
// file another handle is writing), appends to the run's console.log once it is known, and
// sends to nxlink when the NRO was pushed with it. The file is written by a thread of its own
// a quarter second at a time: a write to the SD card took up to most of a second now and
// then, and the thread that printed (the render thread's SRW64_PERF, the UI's) waited it out.
static int nxlink_socket = -1;
bool srw64_switch_nxlink() { return nxlink_socket >= 0; }
namespace {
std::mutex log_mutex;
std::condition_variable log_wake;
std::string log_tail, log_pending;
int log_file = -1;
bool log_writer = false;
constexpr size_t log_keep = 512 * 1024;
ssize_t log_write(struct _reent*, void*, const char* data, size_t size) {
    std::lock_guard lock(log_mutex);
    log_tail.append(data, size);
    if (log_tail.size() > log_keep + log_keep / 4) log_tail.erase(0, log_tail.size() - log_keep);
    if (log_file >= 0 && log_pending.size() < log_keep) log_pending.append(data, size);
    if (nxlink_socket >= 0) ::send(nxlink_socket, data, size, MSG_DONTWAIT);
    return ssize_t(size);
}
extern "C" void srw64_switch_background_thread();
void write_log_file() {
    srw64_switch_background_thread();
    std::unique_lock lock(log_mutex);
    for (;;) {
        log_wake.wait_for(lock, std::chrono::milliseconds(250));
        if (log_pending.empty() || log_file < 0) continue;
        std::string chunk;
        chunk.swap(log_pending);
        const int file = log_file;
        lock.unlock();
        (void)!::write(file, chunk.data(), chunk.size());
        lock.lock();
    }
}
const devoptab_t log_device = {.name = "log", .write_r = log_write};
void install_log() {
    devoptab_list[STD_OUT] = &log_device;
    devoptab_list[STD_ERR] = &log_device;
}
}
void srw64_switch_log_to(const char* path) {
    std::lock_guard lock(log_mutex);
    if (log_file >= 0) ::close(log_file);
    log_file = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (log_file >= 0) (void)!::write(log_file, log_tail.data(), log_tail.size());
    if (!log_writer) {
        log_writer = true;
        std::thread(write_log_file).detach();   // lives as long as the program
    }
}
std::string srw64_switch_log_tail() {
    std::lock_guard lock(log_mutex);
    return log_tail;
}

extern "C" {
// libnx's own: the arguments hbmenu passed, argv[0] the NRO's path ("sdmc:/switch/...").
extern int __system_argc;
extern char** __system_argv;
}

// The NRO's path without its device: "/switch/Marchwind64/Marchwind64.nro". sdmc: is the
// default device, and a path without one is absolute to std::filesystem (a "sdmc:/" path
// is relative there, and fs::absolute would put the working folder in front of it).
// Where the HD pack prepared for the GPU is: the NRO's romfs (a release), else beside the NRO
// in hd/ (development: package.py --hd sends it once, the NRO stays small); "" for none.
std::string srw64_switch_program_path();
std::string srw64_switch_hd_art() {
    std::error_code missing;
    if (std::filesystem::exists("romfs:/hd/art/srw64-portraits-hd.json", missing)) return "romfs:/hd/art";
    const auto beside = std::filesystem::path(srw64_switch_program_path()).parent_path() / "hd" / "art";
    if (std::filesystem::exists(beside / "srw64-portraits-hd.json", missing)) return beside.string();
    return {};
}
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

// The launch on screen until the game's window takes it: the loading screen staged beside the
// NRO (tools/release/switch/splash.py: splash/base-<locale>.png, the language's logo on the
// Steam art's navy, and step-<locale>-<n>.png, what is loading, in the game's language), blitted into libnx's framebuffer with a
// bar under it, a step at a time (app::boot_step, which also logs SRW64_BOOT). Without the
// pictures, the screen stays as it is. srw64_switch_boot_end gives the screen to the window.
namespace {
enum class Boot { Off, Shown, Over };
Boot boot = Boot::Off;
Framebuffer boot_frame;
struct Picture { std::vector<uint32_t> pixels; int width = 0, height = 0; };
Picture boot_base;
std::string boot_locale;
constexpr const char* kBootSteps[] = {"host start", "dialogue load", "host setup", "text pages", "game start"};
constexpr int kBootStepCount = int(sizeof kBootSteps / sizeof kBootSteps[0]);
Picture load_picture(const std::string& path) {
    Picture picture;
    int channels = 0;
    if (unsigned char* data = stbi_load(path.c_str(), &picture.width, &picture.height, &channels, 4)) {
        picture.pixels.assign(reinterpret_cast<const uint32_t*>(data), reinterpret_cast<const uint32_t*>(data) + size_t(picture.width) * picture.height);
        stbi_image_free(data);
    }
    return picture;
}
// The game's language: the settings run_prepared reads (it sets SRW64_LOCALE before the host
// starts, after this screen is up), else the console's.
std::string boot_language() {
    std::string locale;
    if (const char* chosen = std::getenv("SRW64_LOCALE"); chosen && *chosen) locale = chosen;
    else {
        const auto settings = std::filesystem::path(srw64_switch_program_path()).parent_path() / "presentation.json";
        if (FILE* file = std::fopen(settings.string().c_str(), "rb")) {
            char text[4096] = {};
            const size_t got = std::fread(text, 1, sizeof text - 1, file);
            std::fclose(file);
            const std::string json(text, got);
            if (const auto at = json.find("\"locale\""); at != std::string::npos)
                if (const auto open = json.find('"', json.find(':', at)); open != std::string::npos)
                    locale = json.substr(open + 1, json.find('"', open + 1) - open - 1);
        }
        if (locale.empty()) locale = srw64_switch_language();
    }
    if (locale.rfind("zh", 0) == 0) return "zh-Hans";
    if (locale.rfind("ja", 0) == 0) return "ja";
    return "en";
}
// One premultiplied-alpha "over" of a straight RGBA pixel (bytes R, G, B, A) onto an opaque one.
inline uint32_t over(uint32_t below, uint32_t above) {
    const uint32_t a = above >> 24;
    if (a == 255) return above | 0xFF000000u;
    if (a == 0) return below;
    uint32_t out = 0xFF000000u;
    for (int shift = 0; shift < 24; shift += 8) {
        const uint32_t s = (above >> shift) & 0xFF, d = (below >> shift) & 0xFF;
        out |= ((s * a + d * (255 - a) + 127) / 255) << shift;
    }
    return out;
}
std::vector<uint32_t> boot_last;   // the last step's screen, for the window until the game draws
// One step's screen into `frame` (1280 x 720, `row` pixels a line).
void compose_boot(int step, uint32_t* frame, size_t row) {
    const std::string folder = std::filesystem::path(srw64_switch_program_path()).parent_path().string() + "/splash/";
    const Picture text = load_picture(folder + "step-" + boot_locale + "-" + std::to_string(step) + ".png");
    for (int y = 0; y < 720; ++y) std::memcpy(frame + size_t(y) * row, boot_base.pixels.data() + size_t(y) * 1280, 1280 * 4);
    if (text.width == 1280)
        for (int y = 0; y < text.height && 560 + y < 720; ++y)
            for (int x = 0; x < 1280; ++x) {
                uint32_t& at = frame[size_t(560 + y) * row + x];
                at = over(at, text.pixels[size_t(y) * 1280 + x]);
            }
    // The bar: a faint track, filled to this step.
    const int left = 440, width = 400, filled = width * (step + 1) / kBootStepCount;
    for (int y = 640; y < 644; ++y)
        for (int x = left; x < left + width; ++x) {
            uint32_t& at = frame[size_t(y) * row + x];
            at = x < left + filled ? 0xFF28C8FFu : over(at, 0x28FFFFFFu);   // amber (R 255, G 200, B 40); white at 16 %
        }
}
void draw_boot(int step) {
    if (boot_base.width != 1280 || boot_base.height != 720) return;
    u32 stride = 0;
    auto* frame = static_cast<uint32_t*>(framebufferBegin(&boot_frame, &stride));
    compose_boot(step, frame, stride / 4);
    framebufferEnd(&boot_frame);
    if (step == kBootStepCount - 1) {
        boot_last.resize(1280 * 720);
        compose_boot(step, boot_last.data(), 1280);
    }
}
}
// app::boot_step's lines ("dialogue load 0.9 s"), which also go to stderr as SRW64_BOOT.
void srw64_switch_boot_line(const char* line) {
    if (boot == Boot::Over) return;
    if (boot == Boot::Off) {
        const std::string folder = std::filesystem::path(srw64_switch_program_path()).parent_path().string() + "/splash/";
        boot_locale = boot_language();
        boot_base = load_picture(folder + "base-" + boot_locale + ".png");
        if (boot_base.width != 1280 || boot_base.height != 720) { boot = Boot::Over; return; }
        if (R_FAILED(framebufferCreate(&boot_frame, nwindowGetDefault(), 1280, 720, PIXEL_FORMAT_RGBA_8888, 2))) { boot = Boot::Over; return; }
        framebufferMakeLinear(&boot_frame);
        boot = Boot::Shown;
    }
    const std::string text = line;
    for (int n = 0; n < kBootStepCount; ++n)
        if (text.rfind(kBootSteps[n], 0) == 0) draw_boot(n);
}
void srw64_switch_boot_end() {
    if (boot == Boot::Shown) framebufferClose(&boot_frame);
    boot_base = {};
    boot = Boot::Over;
}
// The loading screen's last step (1280 x 720, bytes R G B A), for the game's window to show
// until the game shows a picture (graphics.cpp); empty when there was none, or once dropped.
const std::vector<uint32_t>& srw64_switch_splash() { return boot_last; }
void srw64_switch_drop_splash() { boot_last = {}; boot_last.shrink_to_fit(); }

// The system's side of a performance line (graphics.cpp, SwitchPerf): the process's memory and
// each application core's load since the last line (from the kernel's idle tick counts).
std::string srw64_switch_perf_system() {
    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    const u64 tick = armGetSystemTick();
    char text[160];
    // libnx takes the whole heap at start (used is nearly total). malloc's own count
    // (mallinfo) would say what the program holds, but it locks the allocator and walks the
    // whole heap: ~30 ms in which every thread that allocates stalls, once a line. (A core's
    // idle count is only given for the asking thread's own core, so the load is per thread below.)
    std::snprintf(text, sizeof text, "memory=%lluMiB threads", (unsigned long long)(total >> 20));
    (void)used;
    // The threads that took more than 3% of a core since the last line.
    std::string line = text;
    std::lock_guard lock(watched_mutex);
    static u64 last_line_tick = 0;
    const u64 span = last_line_tick && tick > last_line_tick ? tick - last_line_tick : 0;
    last_line_tick = tick;
    for (auto& entry : watched) {
        u64 ticks = 0;
        if (R_FAILED(svcGetInfo(&ticks, InfoType_ThreadTickCount, entry.handle, TickCountInfo_Total))) continue;
        const double share = span ? 100.0 * double(ticks - entry.last) / double(span) : 0.0;
        entry.last = ticks;
        if (share < 3) continue;
        char part[96];
        if (entry.name[0]) std::snprintf(part, sizeof part, " [%s %.0f%%]", entry.name, share);
        else std::snprintf(part, sizeof part, " [%lx/%lx/%lx %.0f%%]", (unsigned long)entry.made[0], (unsigned long)entry.made[1],
                           (unsigned long)entry.made[2], share);
        line += part;
    }
    return line;
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
int __real___syscall_thread_create(struct __pthread_t** thread, void* (*function)(void*), void* argument, void* stack, size_t size);
int __wrap___syscall_thread_create(struct __pthread_t** thread, void* (*function)(void*), void* argument, void* stack, size_t size) {
    if (!stack && !size) size = 1024 * 1024;
    const int result = __real___syscall_thread_create(thread, function, argument, stack, size);
    if (result == 0 && thread && *thread) {
        // Where it was made: the frame records' return addresses above this call, as offsets.
        uintptr_t made[4] = {};
        const uintptr_t base = module_base();
        auto* frame = static_cast<uintptr_t*>(__builtin_frame_address(0));
        for (int depth = 0, kept = 0; frame && depth < 12 && kept < 4; ++depth) {
            const uintptr_t at = frame[1];
            if (at > base && at - base < (64u << 20) && depth >= 2) made[kept++] = at - base;
            auto* next = reinterpret_cast<uintptr_t*>(frame[0]);
            if (next <= frame) break;
            frame = next;
        }
        // libnx's struct __pthread_t starts with its Thread, whose first member is the handle.
        watch_thread(*reinterpret_cast<const Handle*>(*thread), nullptr, made);
    }
    return result;
}
int __syscall_thread_detach(struct __pthread_t*) { return 0; }

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
namespace {
// The GPU at its docked clock in handheld too, as DK64Recomp-NX's "docked GPU" profile does:
// apm's configuration for the normal (handheld) mode, 768 MHz, else its fallback (460.8 MHz,
// RAM 1600 MHz). Docked runs the boost mode's configuration and is left alone. The console's
// own configuration comes back while the game is out of focus (the HOME menu) and at exit.
constexpr u32 kApmDockedGpu = 0x00010001, kApmDockedGpuFallback = 0x92220007;
bool apm_open = false, apm_changed = false;
u32 apm_original = 0;
AppletHookCookie focus_cookie;

void raise_gpu() {
    if (!apm_open || apm_changed || R_FAILED(apmGetPerformanceConfiguration(ApmPerformanceMode_Normal, &apm_original))) return;
    Result rc = apmSetPerformanceConfiguration(ApmPerformanceMode_Normal, kApmDockedGpu);
    if (R_FAILED(rc)) rc = apmSetPerformanceConfiguration(ApmPerformanceMode_Normal, kApmDockedGpuFallback);
    apm_changed = R_SUCCEEDED(rc);
    std::printf("SRW64_SWITCH apm handheld GPU raised: 0x%x (was 0x%08x)\n", rc, apm_original);
}

void restore_gpu() {
    if (!apm_changed) return;
    apmSetPerformanceConfiguration(ApmPerformanceMode_Normal, apm_original);
    apm_changed = false;
}

void on_focus(AppletHookType type, void*) {
    if (type != AppletHookType_OnFocusState) return;
    if (appletGetFocusState() == AppletFocusState_InFocus) raise_gpu();
    else restore_gpu();
}
}  // namespace

void userAppInit() {
    watch_thread(threadGetCurHandle(), "main", nullptr);
    // The socket only: the log device (above) sends to it, never waiting on the Mac (a
    // blocked write, the console's Wi-Fi asleep, stopped the thread that logged in
    // MegaMan64Recomp-NX). A line may be lost instead.
    if (R_SUCCEEDED(socketInitializeDefault())) nxlink_socket = nxlinkConnectToHost(false, false);
    install_log();
    romfsInit();   // the HD pack the NRO carries (hd_texture.hpp), when it does
    apm_open = R_SUCCEEDED(apmInitialize());
    raise_gpu();
    appletHook(&focus_cookie, on_focus, nullptr);
}

void userAppExit() {
    appletUnhook(&focus_cookie);
    romfsExit();
    restore_gpu();
    if (apm_open) apmExit();
    if (nxlink_socket >= 0) close(nxlink_socket);
    socketExit();
}
}
