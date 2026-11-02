#pragma once
// Debug: the next battle quote of a context forced to one record (quote.force): a recording
// that must show a particular line of the generic pool (16 attack lines drawn at random)
// instead of taking the battle again and again. The conditional-quote lookup 8022245C answers
// 1000 when no conditional quote applies and the caller then draws from the generic segment
// (docs/data/battle-quotes.md); the wrapper answers the forced record there once instead.
#include <atomic>
namespace srw64::quote_force {
inline std::atomic<int> text{-1};     // the record to say, -1 none
inline std::atomic<int> slot{0x2C};   // the quote slot it applies to: +0x2C the attack, +0x848 being hit
inline std::atomic<unsigned> served{0}, seen{0};
inline std::atomic<int> last_base{0}, last_answer{0};
}
