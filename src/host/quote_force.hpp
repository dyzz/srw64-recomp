#pragma once
// Debug: the next battle quotes forced to given records (quote.force): a recording that
// must show particular lines of the generic pool (16 attack lines drawn at random) instead
// of taking the battle again and again. Up to `slots` records wait at once (a whole
// exchange: the attack, being hit, the counter); each generic draw takes the first waiting
// record that lies in the drawing speaker's segment of the context. The generic draw 80222B14(record, slot,
// context) picks from the speaker's segment of the context (ROM 0x1161C0, 36 bytes a voice
// part: nine (start, count) pairs, record 5813 + start + n; docs/data/battle-quotes.md); the
// wrapper writes the forced record into the slot instead, once, when it lies in that
// segment, which names the speaker and the context at once (a conditional quote the slot
// already holds gives way too).
#include <atomic>
#include <cstddef>
#include <cstdint>
namespace srw64::quote_force {
constexpr size_t slots = 8;
inline std::atomic<int> pending[slots]{-1, -1, -1, -1, -1, -1, -1, -1};   // the records to say, -1 free
inline std::atomic<unsigned> served{0}, seen{0};   // seen: generic draws since start
inline std::atomic<int> served_ids[slots]{-1, -1, -1, -1, -1, -1, -1, -1};   // the last records forced, oldest first
inline void clear() {
    for (auto& p : pending) p = -1;
    for (auto& s : served_ids) s = -1;
}
inline bool add(int record) {   // false when every slot is taken
    for (auto& p : pending) { int none = -1; if (p.compare_exchange_strong(none, record)) return true; }
    return false;
}
inline void note_served(int record) {
    for (auto& s : served_ids) { int none = -1; if (s.compare_exchange_strong(none, record)) return; }
}
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
