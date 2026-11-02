#pragma once
// Debug: the next battle quote of a context forced to one record (quote.force): a recording
// that must show a particular line of the generic pool (16 attack lines drawn at random)
// instead of taking the battle again and again. The conditional-quote lookup 8022245C answers
// 1000 when no conditional quote applies and the caller then draws from the generic segment
// (docs/data/battle-quotes.md); the wrapper answers the forced record there once instead.
#include <atomic>
namespace srw64::quote_force {
inline std::atomic<int> text{-1};     // the record to say, -1 none
inline std::atomic<int> context{0};   // the situation it applies to: 0 attack, 1 shot down, ... 8
inline std::atomic<unsigned> served{0};
}
