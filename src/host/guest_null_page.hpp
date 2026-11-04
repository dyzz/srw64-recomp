#pragma once
#include <cstddef>
#include <cstdint>

namespace srw64::guest_null_page {
// Guest reads near address 0 (a null record pointer plus a small offset) read zeros instead of
// faulting; writes there still fault. See guest_null_page.cpp. Call once, before guest code runs.
void open(uint8_t* rdram);
// One reusable guest RAM image of `size` bytes (at most 512 MiB) laid out like the game's own:
// readable and writable up to `size`, zeros around address 0, no access elsewhere, for the
// original routines run on a copy (battle_page.cpp). nullptr where that layout is not available
// (the Switch, the Vita) or a first call asked for less. Game thread only.
uint8_t* image(size_t size);
}
