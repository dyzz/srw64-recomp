// The game's audio microcode as native code (docs/design/switch-port.md, "The audio
// microcode"). The recompiled microcode (audio-probe/audio.cpp) runs the RSP's program an
// instruction at a time, each vector instruction through sse2neon: a third of a core on the
// Switch with the title music playing, half in a battle. The game's microcode is the
// standard audio ABI, which mupen64plus-rsp-hle writes as plain loops over the samples
// (src/thirdparty/mupen64plus-rsp-hle). On the Switch that is what runs; elsewhere the
// recompiled microcode stays, and SRW64_AUDIO_HLE tries this one:
//   SRW64_AUDIO_HLE=1        the native code plays
//   SRW64_AUDIO_HLE=compare  the recompiled microcode plays; the native code runs each task
//                            on a copy of the memory and what it wrote is compared
#include "audio_hle.hpp"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include "../thirdparty/mupen64plus-rsp-hle/hle_internal.h"
#include "../thirdparty/mupen64plus-rsp-hle/ucodes.h"

void HleVerboseMessage(void*, const char*, ...) {}
void HleInfoMessage(void*, const char*, ...) {}
static void report(const char* level, const char* message, va_list arguments) {
    static int said = 0;
    if (said++ >= 20) return;                    // an unknown command would repeat every task
    char text[256];
    std::vsnprintf(text, sizeof text, message, arguments);
    std::fprintf(stderr, "SRW64_AUDIO_HLE %s: %s\n", level, text);
}
void HleErrorMessage(void*, const char* message, ...) {
    va_list arguments; va_start(arguments, message); report("error", message, arguments); va_end(arguments);
}
void HleWarnMessage(void*, const char* message, ...) {
    va_list arguments; va_start(arguments, message); report("warning", message, arguments); va_end(arguments);
}
// The task's end, which the caller (ultramodern's task thread) reports to the game itself.
void rsp_break(struct hle_t*, unsigned int) {}
}

RspExitReason srw64_audio_probe(uint8_t*, uint32_t);   // the recompiled microcode

namespace srw64::audio_hle {
namespace {
constexpr size_t kMemory = 8u << 20;             // the Expansion Pak's 8 MiB
bool bisecting = false;                          // compare: a trial task, the recompiled microcode
const OSTask* current = nullptr;                 // the task get_microcode was asked for
unsigned char task_memory[0x1000];               // DMEM as the list reader sees it: the task at 0xFC0

hle_t& state() {
    static hle_t hle{};                          // the segments, buffers and volumes between tasks
    return hle;
}

void run_on(uint8_t* memory) {
    hle_t& hle = state();
    hle.dram = memory;
    hle.dmem = task_memory;
    std::memcpy(task_memory + 0xFC0, current, sizeof(OSTask));
    alist_process_audio(&hle);
}

RspExitReason native(uint8_t* rdram, uint32_t) {
    run_on(rdram);
    return RspExitReason::Broke;
}

// Development: both, the recompiled microcode on the game's memory and the native code on
// its own. The two keep their filters' states between tasks in the game's memory, each in a
// layout of its own, so the native code cannot be fed what the recompiled one left. Its
// memory is the game's except the bytes it wrote itself and the game has not written since
// (the game's own writes show as a change since the recompiled task ended). What is judged
// is the task's product: the samples its SAVEBUFFs store (the output and the reverb's
// delay lines), 16 bits each.
RspExitReason both(uint8_t* rdram, uint32_t ucode) {
    static std::unique_ptr<uint8_t[]> own(new uint8_t[kMemory]), before(new uint8_t[kMemory]), left(new uint8_t[kMemory]);
    static std::unique_ptr<uint8_t[]> mine(new uint8_t[kMemory]());   // the bytes the native code wrote
    static unsigned long long tasks = 0, samples = 0, differ = 0, tasks_differ = 0, off_by_one = 0;
    static double recompiled_ms = 0, native_ms = 0;
    static int worst = 0, shown = 0;
    using clock = std::chrono::steady_clock;
    if (tasks == 0) std::memcpy(left.get(), rdram, kMemory);
    for (size_t at = 0; at < kMemory; ++at)
        if (!mine[at] || rdram[at] != left[at]) { own[at] = rdram[at]; mine[at] = 0; }
    std::memcpy(before.get(), own.get(), kMemory);
    static std::unique_ptr<uint8_t[]> real_before(new uint8_t[kMemory]);
    std::memcpy(real_before.get(), rdram, kMemory);
    static hle_t state_before; state_before = state();
    // The product: each SAVEBUFF (6) of the list with the count of the SETBUFF (8) before it.
    const uint32_t list = uint32_t(current->t.data_ptr) & 0x7FFFFF, size = uint32_t(current->t.data_size);
    static std::vector<std::pair<uint32_t, uint32_t>> saved;   // every SAVEBUFF's address and byte count
    saved.clear();
    uint32_t last_count = 0;
    for (uint32_t at = 0; at + 8 <= size && list + at + 8 <= kMemory; at += 8) {
        uint32_t w1, w2;
        std::memcpy(&w1, rdram + list + at, 4); std::memcpy(&w2, rdram + list + at + 4, 4);
        if (w1 >> 24 == 8 && !(w1 >> 16 & 8)) last_count = w2 & 0xFFFF;   // SETBUFF, not its A_AUX form
        if (w1 >> 24 == 6 && last_count) saved.emplace_back(w2 & 0x7FFFFF, last_count);
    }
    const auto t0 = clock::now();
    const RspExitReason reason = srw64_audio_probe(rdram, ucode);
    const auto t1 = clock::now();
    run_on(own.get());
    const auto t2 = clock::now();
    recompiled_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
    native_ms += std::chrono::duration<double, std::milli>(t2 - t1).count();
    std::memcpy(left.get(), rdram, kMemory);
    for (size_t at = 0; at < kMemory; ++at) mine[at] |= own[at] != before[at];
    unsigned long long task_differ = 0;
    for (const auto& [out, count] : saved)
    for (uint32_t at = out; at + 2 <= out + count && at + 2 <= kMemory; at += 2) {
        int16_t native_sample, real;
        std::memcpy(&native_sample, own.get() + at, 2);
        std::memcpy(&real, rdram + at, 2);
        ++samples;
        if (native_sample == real) continue;
        const int off = std::abs(int(native_sample) - int(real));
        ++task_differ;
        off_by_one += off == 1;
        worst = std::max(worst, off);
        if (off > 1 && shown < 12) {
            ++shown;
            std::fprintf(stderr, "SRW64_AUDIO_HLE differs task=%llu at=%06x native=%d recompiled=%d\n", tasks, at, native_sample, real);
        }
    }
    static bool bisected = false;
    if (worst > 1 && !bisected) {
        bisected = true;
        // Which command: the list's first n commands, then the whole of the buffers saved,
        // on each side from the memory it had before this task.
        static std::unique_ptr<uint8_t[]> try_real(new uint8_t[kMemory]), try_own(new uint8_t[kMemory]);
        const hle_t state_after = state();
        const OSTask* const task = current;
        const uint32_t scratch = 0x7E0000, dump = 0x7F0000, span = 0x9C0;
        for (uint32_t n = 1; n * 8 <= size; ++n) {
            OSTask cut = *task;
            cut.t.data_ptr = decltype(cut.t.data_ptr)(0x80000000u | scratch);
            cut.t.data_size = n * 8 + 24;
            const uint32_t tail[4] = {0x08000000u, span, 0x06000000u, dump};
            for (uint8_t* memory : {try_real.get(), try_own.get()}) {
                std::memcpy(memory, memory == try_real.get() ? real_before.get() : before.get(), kMemory);
                const uint32_t head[2] = {0x02000000u, span};   // the buffers start clear on both sides
                std::memcpy(memory + scratch, head, 8);
                std::memcpy(memory + scratch + 8, real_before.get() + list, n * 8);
                std::memcpy(memory + scratch + 8 + n * 8, tail, sizeof tail);
                std::memset(memory + dump, 0, span);
            }
            bisecting = true;
            recomp::rsp::run_task(try_real.get(), &cut);
            bisecting = false;
            state() = state_before;
            current = &cut;
            run_on(try_own.get());
            unsigned bad = 0; uint32_t first = 0; int first_native = 0, first_real = 0;
            for (uint32_t at = 0; at < span; at += 2) {
                int16_t a, b;
                std::memcpy(&a, try_own.get() + dump + at, 2); std::memcpy(&b, try_real.get() + dump + at, 2);
                if (a != b && !bad++) { first = at; first_native = a; first_real = b; }
            }
            uint32_t w1, w2;
            std::memcpy(&w1, real_before.get() + list + (n - 1) * 8, 4); std::memcpy(&w2, real_before.get() + list + (n - 1) * 8 + 4, 4);
            if (bad) {
                std::fprintf(stderr, "SRW64_AUDIO_HLE first command that differs: #%u %08x %08x: %u words, first at buffer+%03x native=%d recompiled=%d\n",
                             n, w1, w2, bad, first, first_native, first_real);
                for (uint32_t k = n > 12 ? n - 12 : 0; k < n; ++k) {
                    std::memcpy(&w1, real_before.get() + list + k * 8, 4); std::memcpy(&w2, real_before.get() + list + k * 8 + 4, 4);
                    std::fprintf(stderr, "    %08x %08x\n", w1, w2);
                }
                break;
            }
        }
        current = task;
        state() = state_after;
    }
    differ += task_differ;
    tasks_differ += task_differ != 0;
    if (++tasks % 300 == 0)
        std::fprintf(stderr, "SRW64_AUDIO_HLE compare tasks=%llu samples=%llu differ=%llu (by one: %llu) in %llu tasks worst=%d; "
                     "per task recompiled %.3f ms native %.3f ms\n",
                     tasks, samples, differ, off_by_one, tasks_differ, worst, recompiled_ms / tasks, native_ms / tasks);
    return reason;
}

enum class Mode { Recompiled, Native, Compare };
Mode mode() {
    static const Mode chosen = [] {
        const char* asked = std::getenv("SRW64_AUDIO_HLE");
#ifdef __SWITCH__
        // The Switch starts with no environment: the native code, always.
        (void)asked;
        return Mode::Native;
#else
        if (!asked || !*asked || std::string(asked) == "0") return Mode::Recompiled;
        const Mode picked = std::string(asked) == "compare" ? Mode::Compare : Mode::Native;
        std::fprintf(stderr, "SRW64_AUDIO_HLE %s\n", picked == Mode::Compare ? "compare" : "native");
        return picked;
#endif
    }();
    return chosen;
}
}

RspUcodeFunc* microcode(const OSTask* task) {
    if (bisecting) return srw64_audio_probe;
    current = task;
    switch (mode()) {
    case Mode::Native: return native;
    case Mode::Compare: return both;
    default: return srw64_audio_probe;
    }
}
}
