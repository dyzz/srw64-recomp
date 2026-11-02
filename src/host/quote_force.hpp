#pragma once
// Debug: the next battle quote of a context forced to one record (quote.force): a recording
// that must show a particular line of the generic pool (16 attack lines drawn at random)
// instead of taking the battle again and again. The generic draw 80222B14(record, slot,
// context) picks from the speaker's segment of the context (ROM 0x1161C0, 36 bytes a voice
// part: nine (start, count) pairs, record 5813 + start + n; docs/data/battle-quotes.md); the
// wrapper writes the forced record into the slot instead, once, when it lies in that
// segment, which names the speaker and the context at once (a conditional quote the slot
// already holds gives way too).
#include <atomic>
#include <cstddef>
#include <cstdint>
namespace srw64::quote_force {
inline std::atomic<int> text{-1};     // the record to say, -1 none
inline std::atomic<unsigned> served{0}, seen{0};   // seen: generic draws since start
inline std::atomic<int> last_voice{-1}, last_context{-1}, last_drawn{-1};   // the last generic draw
inline const uint8_t* rom{};          // the ROM image, for the segment table
inline size_t rom_bytes{};
constexpr uint32_t segment_table = 0x1161C0;
constexpr int first_record = 5813;
// Whether record `forced` is in voice part `voice`'s generic segment of `context`.
inline bool in_segment(int voice, int context, int forced) {
    if (!rom || voice < 0 || context < 0 || context > 8) return false;
    const size_t entry = segment_table + size_t(voice) * 36 + size_t(context) * 4;
    if (entry + 4 > rom_bytes) return false;
    const int start = (rom[entry] << 8) | rom[entry + 1], count = (rom[entry + 2] << 8) | rom[entry + 3];
    const int n = forced - first_record - start;
    return n >= 0 && n < count;
}
}
