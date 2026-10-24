#pragma once
#include "json/json.hpp"

// The native dialogue data (SRW64_DIALOGUE_DATA, about 6 MB of JSON), parsed once for every
// module that reads it as the host starts: the dialogue, the name, link, upgrade and swap
// pages, the library. Six parses of their own took the PS Vita over a minute.
namespace srw64::dialogue_data {
// Null without SRW64_DIALOGUE_DATA, or when the file is unreadable or not JSON. With
// SRW64_LOCALE (the PS Vita's prepared data, which the launcher does not rewrite), that
// locale's row of locale_catalogs is laid over the document as the launcher lays it.
const nlohmann::json* document();
// battle_assets, kept after release() for the modules that read it later (the library).
const nlohmann::json& battle_assets();
// Start-up is over: the modules have taken what they keep, the document goes.
void release();
}
