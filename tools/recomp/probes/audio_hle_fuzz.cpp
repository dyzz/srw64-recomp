// The native audio microcode (src/host/audio_hle.cpp, mupen64plus-rsp-hle) against the
// recompiled one, command by command, without the game: random samples and parameters
// through each audio command on two copies of a memory image, the buffers compared after
// every task. Each side keeps its own filter states across the tasks of a case.
//
//   tools/recomp/probes/run_audio_hle_fuzz.sh [cases per command] [seed]
//
// The memory image is build/recomp/audio-probe/rdram-after-native.bin (the audio probe's),
// which holds the microcode's data at 0xCF4D0.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "librecomp/rsp.hpp"

extern "C" {
#include "../../../src/thirdparty/mupen64plus-rsp-hle/hle_internal.h"
#include "../../../src/thirdparty/mupen64plus-rsp-hle/ucodes.h"
void HleVerboseMessage(void*, const char*, ...) {}
void HleInfoMessage(void*, const char*, ...) {}
void HleErrorMessage(void*, const char* message, ...) { va_list a; va_start(a, message); std::vfprintf(stderr, message, a); va_end(a); std::fputc('\n', stderr); }
void HleWarnMessage(void*, const char* message, ...) { va_list a; va_start(a, message); std::vfprintf(stderr, message, a); va_end(a); std::fputc('\n', stderr); }
void rsp_break(struct hle_t*, unsigned int) {}
}

RspExitReason srw64_audio_probe(uint8_t* rdram, uint32_t ucode_addr);
static RspUcodeFunc* select_ucode(const OSTask*) { return srw64_audio_probe; }

namespace {
constexpr uint32_t kList = 0x700000, kInput = 0x710000, kDump = 0x720000, kState = 0x730000, kBook = 0x740000, kSpan = 0x9C0;
std::vector<uint8_t> recompiled, native;
hle_t hle{};
unsigned char task_memory[0x1000];
uint64_t seed = 1;
uint32_t rnd() { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return uint32_t(seed >> 11); }
int range(int low, int high) { return low + int(rnd() % uint32_t(high - low + 1)); }

void put16(uint32_t address, int value) {
    const int16_t v = int16_t(value);
    for (auto* memory : {&recompiled, &native}) std::memcpy(memory->data() + (address ^ 2), &v, 2);
}
void put8(uint32_t address, int value) {
    for (auto* memory : {&recompiled, &native}) (*memory)[address ^ 3] = uint8_t(value);
}
int16_t get16(const std::vector<uint8_t>& memory, uint32_t address) {
    int16_t v; std::memcpy(&v, memory.data() + (address ^ 2), 2); return v;
}
// Samples as a game has them: mostly moderate, sometimes the full range.
int sample() { return rnd() % 8 ? range(-9000, 9000) : range(-32768, 32767); }

// One task: the commands between a clear of all the buffers and a save of all of them.
// Returns the words of the buffers that differ.
unsigned run(const std::vector<uint32_t>& commands, const char* what, int task) {
    std::vector<uint32_t> list{0x02000000u, kSpan};
    list.insert(list.end(), commands.begin(), commands.end());
    list.insert(list.end(), {0x08000000u, kSpan, 0x06000000u, kDump});
    OSTask t{};
    t.t.type = 2;
    t.t.ucode = decltype(t.t.ucode)(0x800C4910u);
    t.t.ucode_data = decltype(t.t.ucode_data)(0x800CF4D0u);
    t.t.ucode_data_size = 0x800;
    t.t.data_ptr = decltype(t.t.data_ptr)(0x80000000u | kList);
    t.t.data_size = uint32_t(list.size() * 4);
    for (auto* memory : {&recompiled, &native}) {
        std::memcpy(memory->data() + kList, list.data(), list.size() * 4);
        std::memset(memory->data() + kDump, 0, kSpan);
    }
    if (!recomp::rsp::run_task(recompiled.data(), &t)) { std::fprintf(stderr, "the recompiled task failed\n"); std::exit(6); }
    hle.dram = native.data();
    hle.dmem = task_memory;
    std::memcpy(task_memory + 0xFC0, &t, sizeof t);
    alist_process_audio(&hle);
    unsigned bad = 0;
    for (uint32_t at = 0; at < kSpan; at += 2) {
        const int a = get16(native, kDump + at), b = get16(recompiled, kDump + at);
        if (a == b) continue;
        if (!bad++ ) std::fprintf(stderr, "  %s task %d: buffer+%03x native=%d recompiled=%d", what, task, at, a, b);
    }
    if (bad) std::fprintf(stderr, " (%u words)\n", bad);
    return bad;
}

void buffers(std::vector<uint32_t>& c, int in, int out, int count) { c.insert(c.end(), {0x08000000u | uint32_t(in), uint32_t(out) << 16 | uint32_t(count)}); }
void load(std::vector<uint32_t>& c, int to, int count, uint32_t from) { buffers(c, to, 0, count); c.insert(c.end(), {0x04000000u, from}); }
// Fresh samples at kInput into the buffer at `to`.
void fresh(std::vector<uint32_t>& c, int to, int count) {
    for (int at = 0; at < count; at += 2) put16(kInput + at, sample());
    load(c, to, count, kInput);
}

unsigned resample(int tasks) {
    unsigned bad = 0;
    const int pitch = rnd() % 4 ? range(0x1000, 0xFFFF) : range(1, 0xFFFF);
    for (int task = 0; task < tasks; ++task) {
        std::vector<uint32_t> c;
        fresh(c, 0x20, 0x300);
        buffers(c, 0x20, 0x400, 0x140);
        c.insert(c.end(), {0x05000000u | (task ? 0u : 0x10000u) | uint32_t(pitch), kState});
        bad += run(c, "RESAMPLE", task);
    }
    return bad;
}

// The output over the input's end, as the game's driver places a short voice: in at
// out + count + 2 and thereabouts, where the last outputs land on samples still to be read.
unsigned resample_over(int tasks) {
    unsigned bad = 0;
    const int pitch = range(0x400, 0x3000);
    const int count = range(1, 4) * 0x10, out = 0x100 + range(0, 8) * 0x10, in = out + count + range(-3, 3) * 2;
    for (int task = 0; task < tasks; ++task) {
        std::vector<uint32_t> c;
        fresh(c, out & ~0x1F, 0x200);
        buffers(c, in, out, count);
        c.insert(c.end(), {0x05000000u | (task ? 0u : 0x10000u) | uint32_t(pitch), kState});
        bad += run(c, "RESAMPLE over its input", task);
    }
    return bad;
}

unsigned adpcm(int tasks) {
    unsigned bad = 0;
    for (int at = 0; at < 0x80; at += 2) put16(kBook + at, range(-2200, 2200));
    for (int task = 0; task < tasks; ++task) {
        std::vector<uint32_t> c;
        c.insert(c.end(), {0x0B000080u, kBook});
        for (int frame = 0; frame < 20; ++frame) {
            put8(kInput + frame * 9, range(0, 12) << 4 | range(0, 3));
            for (int k = 1; k < 9; ++k) put8(kInput + frame * 9 + k, rnd() & 0xFF);
        }
        load(c, 0, 0xC0, kInput);
        buffers(c, 0, 0x140, 0xA0);
        const bool loop = task == 2;
        if (loop) {
            for (int at = 0; at < 0x20; at += 2) put16(kState + 0x100 + at, sample());
            c.insert(c.end(), {0x0F000000u, kState + 0x100});
        }
        c.insert(c.end(), {0x01000000u | (task == 0 ? 0x10000u : loop ? 0x20000u : 0u), kState});
        bad += run(c, "ADPCM", task);
    }
    return bad;
}

unsigned envmixer(int tasks) {
    unsigned bad = 0;
    const bool aux = rnd() % 4 != 0;
    for (int task = 0; task < tasks; ++task) {
        std::vector<uint32_t> c;
        const int count = range(1, 10) * 0x20;
        fresh(c, 0, 0x140);
        // What is mixed onto: silence or what other voices left.
        if (rnd() % 2) { fresh(c, 0x560, 0x140); fresh(c, 0x6A0, 0x140); fresh(c, 0x7E0, 0x140); fresh(c, 0x1A0, 0x140); }
        buffers(c, 0, 0x560, count);
        c.insert(c.end(), {0x08080000u | 0x6A0u, 0x7E0u << 16 | 0x1A0u});
        if (task == 0) {
            const int vol_l = range(0, 0x7FFF), vol_r = range(0, 0x7FFF);
            c.insert(c.end(), {0x09060000u | uint32_t(vol_l), 0u, 0x09040000u | uint32_t(vol_r), 0u});
            // A target and the rate that goes towards it, as the game's driver gives them.
            for (int side = 0; side < 2; ++side) {
                const int target = range(0, 0x7FFF);
                const bool up = target >= (side ? vol_r : vol_l);
                const uint32_t rate = up ? uint32_t(range(0x10000, 0x18000)) : uint32_t(range(0x8000, 0xFFFF));
                c.insert(c.end(), {(side ? 0x09000000u : 0x09020000u) | uint32_t(target), rate});
            }
            c.insert(c.end(), {0x09080000u | uint32_t(range(0, 0x7FFF)), uint32_t(range(0, 0x7FFF))});
        }
        c.insert(c.end(), {0x03000000u | (task ? 0u : 0x10000u) | (aux ? 0x80000u : 0u), kState});
        bad += run(c, aux ? "ENVMIXER aux" : "ENVMIXER", task);
    }
    return bad;
}

unsigned mixer(int) {
    std::vector<uint32_t> c;
    fresh(c, 0, 0x140);
    fresh(c, 0x400, 0x140);
    buffers(c, 0, 0, range(1, 10) * 0x20);
    c.insert(c.end(), {0x0C000000u | uint32_t(rnd() & 0xFFFF), 0u << 16 | 0x400u});
    return run(c, "MIXER", 0);
}

unsigned polef(int tasks) {
    unsigned bad = 0;
    for (int at = 0; at < 0x20; at += 2) put16(kBook + at, range(-3000, 3000));
    const int gain = range(0, 0x7FFF);
    for (int task = 0; task < tasks; ++task) {
        std::vector<uint32_t> c;
        c.insert(c.end(), {0x0B000020u, kBook});
        fresh(c, 0, 0x140);
        buffers(c, 0, 0x400, 0x140);
        c.insert(c.end(), {0x0E000000u | (task ? 0u : 0x10000u) | uint32_t(gain), kState});
        bad += run(c, "POLEF", task);
    }
    return bad;
}

unsigned moves(int) {
    std::vector<uint32_t> c;
    fresh(c, 0, 0x140);
    fresh(c, 0x200, 0x140);
    c.insert(c.end(), {0x0A000000u | uint32_t(range(0, 8) * 0x20), uint32_t(0x600 + range(0, 4) * 0x20) << 16 | uint32_t(range(1, 8) * 0x20)});
    buffers(c, 0, 0x400, range(1, 10) * 0x20);
    c.insert(c.end(), {0x0D000000u, 0u << 16 | 0x200u});
    return run(c, "DMEMMOVE+INTERLEAVE", 0);
}
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::ifstream source(argv[1], std::ios::binary);
    std::vector<uint8_t> image{std::istreambuf_iterator<char>(source), {}};
    if (image.size() != 0x800000) { std::fprintf(stderr, "no 8 MiB memory image at %s\n", argv[1]); return 4; }
    for (size_t at = 0; at < image.size(); at += 4) { std::swap(image[at], image[at + 3]); std::swap(image[at + 1], image[at + 2]); }
    const int cases = argc > 2 ? std::atoi(argv[2]) : 200;
    seed = argc > 3 ? std::strtoull(argv[3], nullptr, 0) : 0x64;
    recomp::rsp::constants_init();
    recomp::rsp::set_callbacks({select_ucode});
    struct Command { const char* name; unsigned (*run)(int); int tasks; };
    const Command commands[] = {{"RESAMPLE", resample, 3}, {"RESAMPLE over its input", resample_over, 3}, {"ADPCM", adpcm, 4}, {"ENVMIXER", envmixer, 4}, {"MIXER", mixer, 1},
                                {"POLEF", polef, 3}, {"DMEMMOVE+INTERLEAVE", moves, 1}};
    int failed = 0;
    for (const Command& command : commands) {
        int bad_cases = 0;
        for (int n = 0; n < cases; ++n) {
            recompiled = image; native = image; hle = hle_t{};
            const uint64_t at = seed;
            const unsigned bad = command.run(command.tasks);
            if (bad && ++bad_cases <= 3) std::fprintf(stderr, "  ^ %s case %d seed %llx\n", command.name, n, (unsigned long long)at);
            if (bad && bad_cases > 3) { /* the rest are counted only */ }
        }
        std::printf("%-22s %d of %d cases differ\n", command.name, bad_cases, cases);
        failed += bad_cases != 0;
    }
    return failed ? 1 : 0;
}
