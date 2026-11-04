#include "guest_null_page.hpp"
#include <cstdio>
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif !defined(__SWITCH__) && !defined(__vita__)
#include <sys/mman.h>
#endif

// The recompiled code reaches guest address a at rdram + (a - 0x80000000) in a 4 GiB
// reservation (librecomp/src/recomp.cpp); only the first 512 MiB are readable. A null pointer
// plus an offset, guest 0x00000038, lands at rdram + 0x80000038: no access, a fault here, a TLB
// miss on the console.
//
// The original walks its map units in many places and reads each one's record (+0x28 flags,
// +0x30, +0x38 the pilot) without asking whether there still is one. A unit an event withdraws
// during the player's phase (Erlich persuaded in S56, Master Asia in the story MOD, a probe
// stage's 3D58) loses its record pointer at once but keeps its roster flag and its map icon
// until the phase ends, so moving anyone, or confirming the move, read through 0 (801F9590 and
// 801C3E50, 2026-11-04, Windows and Mac). Patching each walk would never be done: the 64 KiB on
// either side of address 0 read zeros instead, which every one of them already takes as "no
// pilot, no flags, nothing to do". Writes there still fault, so a store through a stray pointer
// is still a crash with a dump, not memory quietly overwritten.
namespace srw64::guest_null_page {
namespace {
constexpr uint64_t from = 0x7FFF0000, around = 0x20000;   // guest -0x10000 .. +0x10000
constexpr uint64_t reservation = 0x100000000ULL;          // as librecomp's rdram: 4 GiB
}
void open(uint8_t* rdram) {
#if defined(_WIN32)
    DWORD old = 0;
    const bool ok = VirtualProtect(rdram + from, around, PAGE_READONLY, &old) != 0;
#elif !defined(__SWITCH__) && !defined(__vita__)
    const bool ok = mprotect(rdram + from, around, PROT_READ) == 0;
#else
    const bool ok = false;   // the Switch and the Vita lay guest memory out their own way
    (void)rdram;
#endif
    std::fprintf(stderr, "SRW64_NULL_PAGE %s\n", ok ? "read-only zeros" : "unavailable");
}

uint8_t* image(size_t size) {
    static size_t made = 0;
    static uint8_t* const base = [size]() -> uint8_t* {
        if (size == 0 || size > 0x20000000) return nullptr;
#if defined(_WIN32)
        auto* p = static_cast<uint8_t*>(VirtualAlloc(nullptr, reservation, MEM_RESERVE, PAGE_NOACCESS));
        if (!p) return nullptr;
        if (!VirtualAlloc(p, size, MEM_COMMIT, PAGE_READWRITE) || !VirtualAlloc(p + from, around, MEM_COMMIT, PAGE_READONLY)) {
            VirtualFree(p, 0, MEM_RELEASE);
            return nullptr;
        }
#elif !defined(__SWITCH__) && !defined(__vita__)
        void* m = mmap(nullptr, reservation, PROT_NONE, MAP_ANON | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
        if (m == MAP_FAILED) return nullptr;
        auto* p = static_cast<uint8_t*>(m);
        if (mprotect(p, size, PROT_READ | PROT_WRITE) != 0 || mprotect(p + from, around, PROT_READ) != 0) {
            munmap(p, reservation);
            return nullptr;
        }
#else
        uint8_t* p = nullptr;
#endif
        if (p) made = size;
        return p;
    }();
    return base && size <= made ? base : nullptr;
}
}
