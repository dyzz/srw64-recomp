#pragma once
#include <filesystem>
#include "json/json.hpp"

// The battle viewer (docs/design/battle-viewer.md): plays one battle animation from the
// title screen through the title demo's own path. At the frame boundary the title overlay
// is put into mode 0x1C (the attract-mode demo, which loads only the battle animation
// overlay and fills both participant records D_800F97E0 + side x 0x1074 in 8009C2DC); after
// the original fill, the viewer writes its choices (unit, pilot, weapon animation, the
// defender's reaction, damage, HP, backgrounds...) over those records. No map, roster or
// save is touched; the animation returns to the title by itself (mode 0x1D).
//
// A request is the page's choice ({"choice": {attacker / defender {unit, pilot, weapon},
// reaction, damage, destroy, counter, counter_reaction, counter_damage, counter_destroy,
// scene [2], song}}; reactions are the record's codes, docs/design/battle-viewer.md §2.2),
// turned into the record fields when the demo fills them, or for tests the raw field
// writes ({"writes": [{side, offset, hex}]}); the debug server's `viewer.start` takes either.
namespace srw64::battle_viewer {
void install(const std::filesystem::path& output);
nlohmann::json request(const nlohmann::json& params);
nlohmann::json state();
// For the page (window thread): the title's サウンドセレクト songs [{song, text}] once the
// title overlay has run, the battle scene keys (locale "viewer_scene_<key>"), whether the
// page is open (it holds back the title's attract demo), a battle that has come back to
// the title (once), and whether a battle is waiting or playing.
nlohmann::json songs();
nlohmann::json scenes();
// The song the viewer plays when none is chosen: the attacker's (its pilot's, else its unit's).
int default_song(unsigned unit,unsigned actor);
// A character's own battle song (the pilots' table, ROM 0x7D6A0), or -1.
int pilot_song(unsigned actor);
void set_page_open(bool open);
// Any other host page over the title (the Library, MOD, the settings): the title holds
// still under it too, with no attract demo and no opening story.
void hold_title(bool hold);
// Listening on the page's BGM list: a song number plays on the title, -1 puts the title's
// own song back (also when the page closes or a battle starts).
void listen_song(int number);
int listened();
// viewer_start's song when there is none to play.
constexpr int no_music=-2;
bool take_returned();
bool busy();
}
