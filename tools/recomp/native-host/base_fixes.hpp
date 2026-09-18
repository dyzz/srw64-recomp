#pragma once
#include "recomp.h"
#include <cstdint>

// Corrections that are always on, for original defects where the game contradicts
// its own data and no playable behaviour is lost (docs/base-fixes.md). Unlike the
// optional rules in rule_fixes.hpp these have no switch: the hook wrappers in
// game_hooks.cpp apply them on every run.
namespace srw64::base_fixes {

// Pilot records: 100 per side of 0x4C bytes, the player's first (800A32EC, 800A84F8).
inline constexpr uint32_t pilot_table=0x80172F40,pilot_side_stride=0x1DB0;
// BUG05. The kill backup for the five W pilots (801614E0) is raised with every
// player kill by 800A4FBC and kept in the save, and 800A5054 copies it into a new
// pilot record so a pilot who left keeps his kills. It never looks at the side,
// and for sides 1 and 2 that same field (+0x14) is the dummy count read by
// 801F6E3C, so a hostile 五飛 gets one dummy per kill he scored for the player —
// his own deployment records ask for none. Restore only into the player table;
// both callers (800A84F8, 80210758) fill records that are meant to keep kills.
inline bool player_pilot(uint32_t pilot){return pilot-pilot_table<pilot_side_stride;}
}
