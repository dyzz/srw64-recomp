#pragma once
#include <filesystem>

// Autosaves (docs/design/save-slots-autosave.md §2.4), only with the save library:
// in the original's slot format when the intermission is entered after a map and
// when 次のマップへ leaves it; in its suspend format once per player turn, when the map
// first stands idle with no event running. The random numbers of a turn autosave are
// kept beside it and put back after the map restarts from it.
namespace srw64::autosave {
void configure(const std::filesystem::path& directory);
// The title's ロード chose a turn autosave: its random numbers wait for the restore.
void prepare_turn_load(const std::filesystem::path& record);
}
