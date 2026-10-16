// Display list statistics for the PS Vita study (docs/design/psv-port.md, V0): which
// F3DEX2 and RDP commands, combiner settings, other modes and texture formats the game's
// display lists use, and how many draws a frame holds, so the Vita renderer's scope is
// known before it is written. A development aid: it does nothing unless the host runs with
// the debug interface (SRW64_DEBUG=1) and SRW64_GBI_STATS names the JSON file to write.
// The lists are walked as RT64 receives them (the native dialogue's edited copy, the wide
// map's extended commands included); nothing here changes what RT64 draws.
#pragma once
#include <cstddef>
#include <cstdint>

namespace srw64::gbi_stats {
bool enabled();
// One display list from a graphics task: `list` holds its top-level commands (the native
// dialogue's edited copy, or RDRAM itself), `rdram` everything the list calls, as RT64 reads
// them; `start` is the task's data pointer as a physical address, `size` the list's bytes.
void record(const uint8_t* list, const uint8_t* rdram, uint32_t start, uint32_t size);
// Write the JSON; called at the renderer's end and now and then while the game runs.
void flush();
}
