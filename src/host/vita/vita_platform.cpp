// What the PS Vita's newlib and the runtime leave out (docs/design/psv-port.md, V2).
#include "app/runtime.hpp"
#include <cstdio>
#include <exception>
#include <typeinfo>
#include <malloc.h>
#include <psp2/display.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <algorithm>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <psp2/kernel/threadmgr.h>
#include <mutex>
#include <vector>
#include <cstring>
#include <cctype>
#include <filesystem>
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
// for newlib's heap (the ROM, RDRAM, fonts, RmlUi: some 270 MiB at the title, and RmlUi's
// 20 MiB font did not fit in 200), the rest vitaGL's 12 MiB pool, the threads' stacks and
// the system's: a console leaves 20-odd MiB less than Vita3K. The main thread and every pthread (the runtime's N64 threads run deep
// recompiled calls) get stacks of a size that holds them.
int _newlib_heap_size_user = 300 * 1024 * 1024;
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
    // newlib starts the network (its module, sceNetInit, sceNetCtlInit) with the first
    // socket; starting it here as well had the system refuse the second start.
    static bool started = [] {
        const int probe = socket(AF_INET, SOCK_STREAM, 0);
        if (probe < 0) return false;
        close(probe);
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

// ux0:data/Marchwind64/like-console (an empty file, for Vita3K): the memory left as a console
// leaves it. Vita3K leaves the program 34 MiB more user memory than a PS Vita does once the
// heap is taken (65 MiB free at start against 31, a PCH-1000 on 3.65, 2026-10-17); this
// takes the difference, so that running short shows in Vita3K as it does on the console.
// The video memory left is the same on both (110 MiB).
void srw64_vita_like_console() {
    constexpr int console_free = 31 * 1024 * 1024;
    SceKernelFreeMemorySizeInfo before{};
    before.size = sizeof before;
    sceKernelGetFreeMemorySize(&before);
    const int extra = (before.size_user - console_free) & ~(64 * 1024 - 1);
    SceUID block = -1;
    if (extra > 0) block = sceKernelAllocMemBlock("srw64_like_console", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, extra, nullptr);
    SceKernelFreeMemorySizeInfo after{};
    after.size = sizeof after;
    sceKernelGetFreeMemorySize(&after);
    std::fprintf(stderr, "SRW64_VITA_LIKE_CONSOLE took=%d KiB (%s) free user=%d KiB then %d KiB cdram=%d KiB phycont=%d KiB\n",
                 extra > 0 ? extra / 1024 : 0, block >= 0 ? "ok" : "none", before.size_user / 1024, after.size_user / 1024,
                 after.size_cdram / 1024, after.size_phycont / 1024);
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


// ux0:data/Marchwind64/perf (an empty file): every 300 screens, one SRW64_VITA_PERF line in
// console.log with the rates the game reached, the graphics thread's time per display list
// and per screen (drawing, then the swap's wait), and the memory (docs/design/psv-port.md, V2).
namespace {
struct PerfWindow {
    SceUInt64 start = 0;
    uint64_t lists = 0, list_us = 0, list_max = 0;
    uint64_t screens = 0, draw_us = 0, draw_max = 0, swap_us = 0;
    uint64_t skipped = 0;  // screens kept as they were (graphics.cpp)
};
PerfWindow perf;
// The threads whose CPU time each line reports, with their run time at the window's start.
struct Watched {
    SceUID id;
    std::string label;
    SceUInt64 run = 0;
};
std::mutex watched_mutex;
std::vector<Watched> watched;
SceUInt64 run_time(SceUID id) {
    SceKernelThreadInfo info{};
    info.size = sizeof info;
    return sceKernelGetThreadInfo(id, &info) < 0 ? 0 : info.runClocks;
}
}

bool srw64_vita_perf_enabled();
// The calling thread's CPU time goes in the lines, under `label`, from its first call.
void srw64_vita_perf_thread(const char* label) {
    if (!srw64_vita_perf_enabled()) return;
    const SceUID id = sceKernelGetThreadId();
    std::lock_guard lock(watched_mutex);
    for (const auto& thread : watched)
        if (thread.id == id) return;
    watched.push_back({id, label, run_time(id)});
}

bool srw64_vita_perf_enabled() {
    static const bool enabled = [] {
        return srw64::app::list_names("ux0:data/Marchwind64").contains("perf");
    }();
    return enabled;
}

uint64_t srw64_vita_perf_now() { return sceKernelGetProcessTimeWide(); }

void srw64_vita_perf_list(uint64_t microseconds) {
    ++perf.lists;
    perf.list_us += microseconds;
    perf.list_max = std::max(perf.list_max, microseconds);
}

void srw64_vita_perf_skipped() { ++perf.skipped; }

void srw64_vita_perf_screen(uint64_t draw_microseconds, uint64_t swap_microseconds) {
    srw64_vita_perf_thread("gfx");
    const SceUInt64 now = sceKernelGetProcessTimeWide();
    if (!perf.start) perf.start = now;
    ++perf.screens;
    perf.draw_us += draw_microseconds;
    perf.draw_max = std::max(perf.draw_max, draw_microseconds);
    perf.swap_us += swap_microseconds;
    if (perf.screens + perf.skipped < 300) return;
    const double seconds = double(now - perf.start) / 1e6;
    const struct mallinfo heap = mallinfo();
    SceKernelFreeMemorySizeInfo free{};
    free.size = sizeof free;
    sceKernelGetFreeMemorySize(&free);
    std::fprintf(stderr,
                 "SRW64_VITA_PERF seconds=%.2f screens_per_s=%.1f kept_per_s=%.1f lists_per_s=%.1f list_ms=%.2f/%.2f screen_ms=%.2f/%.2f swap_ms=%.2f "
                 "heap=%d KiB free_user=%d KiB free_cdram=%d KiB free_phycont=%d KiB\n",
                 seconds, perf.screens / seconds, perf.skipped / seconds, perf.lists / seconds,
                 perf.lists ? perf.list_us / 1000.0 / perf.lists : 0.0, perf.list_max / 1000.0,
                 perf.draw_us / 1000.0 / perf.screens, perf.draw_max / 1000.0, perf.swap_us / 1000.0 / perf.screens,
                 heap.uordblks / 1024, free.size_user / 1024, free.size_cdram / 1024, free.size_phycont / 1024);
    // Each watched thread's share of the window, and the core it last ran on.
    {
        std::string line = "SRW64_VITA_CPU";
        std::lock_guard lock(watched_mutex);
        for (auto& thread : watched) {
            SceKernelThreadInfo info{};
            info.size = sizeof info;
            if (sceKernelGetThreadInfo(thread.id, &info) < 0) continue;
            char part[64];
            std::snprintf(part, sizeof part, " %s=%.0f%%@%d", thread.label.c_str(),
                          double(info.runClocks - thread.run) / 1e4 / seconds, int(info.lastExecutedCpuId));
            line += part;
            thread.run = info.runClocks;
        }
        std::fprintf(stderr, "%s\n", line.c_str());
    }
    std::fflush(stderr);
    perf = PerfWindow{now};
}

// The launch before the graphics start, on a frame buffer of its own that the GL renderer
// lets go of once vitaGL shows its first frame (srw64_vita_progress_end): each step as it
// begins, with its time and memory (app::boot_step).
namespace {
SceUID progress_block = -1;
uint32_t* progress_pixels = nullptr;
bool boot_screen_over = false;  // vitaGL has the display
// vitaGL starts on a thread of its own (graphics.cpp) while the host's steps still come in.
std::mutex boot_screen_mutex;
std::vector<std::string> boot_lines;
constexpr int screen_width = 960, screen_height = 544;
// 5 x 7 capitals, digits and a few signs, a row of bits each; a letter shows as its capital.
constexpr char font_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.:=-/%";
constexpr uint8_t font_rows[][7] = {
    {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30}, {14, 17, 16, 16, 16, 17, 14}, {30, 17, 17, 17, 17, 17, 30},
    {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16}, {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17},
    {14, 4, 4, 4, 4, 4, 14},      {7, 2, 2, 2, 2, 18, 12},       {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
    {17, 27, 21, 21, 17, 17, 17}, {17, 17, 25, 21, 19, 17, 17}, {14, 17, 17, 17, 17, 17, 14}, {30, 17, 17, 30, 16, 16, 16},
    {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17}, {15, 16, 16, 14, 1, 1, 30},   {31, 4, 4, 4, 4, 4, 4},
    {17, 17, 17, 17, 17, 17, 14}, {17, 17, 17, 17, 17, 10, 4},  {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17},
    {17, 17, 17, 10, 4, 4, 4},    {31, 1, 2, 4, 8, 16, 31},     {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14},
    {14, 17, 1, 2, 4, 8, 31},     {31, 2, 4, 2, 1, 17, 14},     {2, 6, 10, 18, 31, 2, 2},     {31, 16, 30, 1, 1, 17, 14},
    {6, 8, 16, 30, 17, 17, 14},   {31, 1, 2, 4, 8, 8, 8},       {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 2, 12},
    {0, 0, 0, 0, 0, 12, 12},      {0, 12, 12, 0, 12, 12, 0},    {0, 0, 31, 0, 31, 0, 0},      {0, 0, 0, 31, 0, 0, 0},
    {0, 1, 2, 4, 8, 16, 0},       {24, 25, 2, 4, 8, 19, 3}};
void fill(int x, int y, int w, int h, uint32_t colour) {
    for (int row = std::max(y, 0); row < std::min(y + h, screen_height); ++row)
        for (int column = std::max(x, 0); column < std::min(x + w, screen_width); ++column)
            progress_pixels[row * screen_width + column] = colour;
}
// Text from x, y, each character 6 x 8 cells of scale pixels.
void text(const std::string& line, int x, int y, int scale, uint32_t colour) {
    for (const char c : line) {
        const char* found = std::strchr(font_chars, std::toupper(static_cast<unsigned char>(c)));
        if (c != ' ' && found && *found) {
            const auto& rows = font_rows[found - font_chars];
            for (int row = 0; row < 7; ++row)
                for (int column = 0; column < 5; ++column)
                    if (rows[row] & (16 >> column)) fill(x + column * scale, y + row * scale, scale, scale, colour);
        }
        x += 6 * scale;
    }
}
bool boot_screen() {
    if (boot_screen_over) return false;
    if (progress_block >= 0) return progress_pixels != nullptr;
    progress_block = sceKernelAllocMemBlock("srw64_progress", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 2 * 1024 * 1024, nullptr);
    if (progress_block < 0 || sceKernelGetMemBlockBase(progress_block, reinterpret_cast<void**>(&progress_pixels)) < 0) {
        progress_pixels = nullptr;
        return false;
    }
    fill(0, 0, screen_width, screen_height, 0xFF000000);
    SceDisplayFrameBuf frame{};
    frame.size = sizeof frame;
    frame.base = progress_pixels;
    frame.pitch = screen_width;
    frame.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
    frame.width = screen_width;
    frame.height = screen_height;
    sceDisplaySetFrameBuf(&frame, SCE_DISPLAY_SETBUF_NEXTFRAME);
    return true;
}
constexpr int lines_x = 180, lines_y = 140, line_height = 22, lines_shown = 12;
}

void srw64_vita_boot_line(const char* line) {
    std::lock_guard lock(boot_screen_mutex);
    if (!boot_screen()) return;
    boot_lines.emplace_back(line);
    if (boot_lines.size() > size_t(lines_shown)) boot_lines.erase(boot_lines.begin());
    fill(0, lines_y, screen_width, lines_shown * line_height, 0xFF000000);
    for (size_t i = 0; i < boot_lines.size(); ++i)
        text(boot_lines[i], lines_x, lines_y + int(i) * line_height, 2,
             i + 1 == boot_lines.size() ? 0xFFFFFFFF : 0xFF909090);  // the step under way is the bright one
}

void srw64_vita_progress_end() {
    std::lock_guard lock(boot_screen_mutex);
    boot_screen_over = true;
    if (progress_block < 0) return;
    // Two vertical blanks: the display has taken vitaGL's frame buffer by then.
    sceDisplayWaitVblankStart();
    sceDisplayWaitVblankStart();
    sceKernelFreeMemBlock(progress_block);
    progress_block = -1;
    progress_pixels = nullptr;
}
