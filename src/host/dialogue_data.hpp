#pragma once
#include "json/json.hpp"
#include "localization/string_tables.hpp"

// The native dialogue data (SRW64_DIALOGUE_DATA), parsed once for every module that reads it
// as the host starts: the dialogue, the name, link, upgrade, swap, ability, save and battle
// pages, the library. Each parsing it again took the PS Vita over a minute in all.
namespace srw64::dialogue_data {
// Start reading it on a thread of its own, so that it overlaps the rest of the start
// (the PS Vita's graphics); document() waits for it.
void preload();
// Null without SRW64_DIALOGUE_DATA, or when the file is unreadable or not JSON. With
// SRW64_LOCALE (the PS Vita's prepared data, which the launcher does not rewrite), that
// locale's row of locale_catalogs is laid over the document as the launcher lays it.
const nlohmann::json* document();
// The large string tables of prepared data (SRW64_DIALOGUE_TABLES, string_tables.hpp): the
// original records, the translations, the intro pages. Null without them.
const localization::string_tables::Tables* tables();
// battle_assets, kept after release() for the modules that read it later (the library).
const nlohmann::json& battle_assets();
// Start-up is over: the modules have taken what they keep, the document goes.
void release();
}
