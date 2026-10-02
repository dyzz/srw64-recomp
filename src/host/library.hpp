#pragma once
#include "json/json.hpp"

// The title's Library (docs/native/library.md): every unit and every character the
// ROM defines, which the original has no screen for. Read from the player's ROM with
// the layouts of docs/data/original-data-catalog.md: base stats as the records hold
// them (no upgrades or parts), a unit's own weapons (the weapon list entries whose
// form slots name it), a pilot's level-1 stats with the linear growth of 800A6238,
// spirit commands and skill levels with the pilot level that brings each. Names and
// labels come from the reading language's catalog; art from battle_assets.
namespace srw64::library {
// Window thread, inside the reading language's localization scope. Built on first use
// and again when the language changes. {"units": [...], "pilots": [...], "labels": {},
// "weapon_labels": {}}. Records identical to an earlier one of the same name are left
// out. Both lists are grouped by work ("work" 0-24, -1 その他; "work_name", a unit's
// "model") in the order of the original's unreachable character / robot lists, and a
// name that still repeats within a work is numbered "(2)", "(3)" in that order.
const nlohmann::json& contents();
}
