#pragma once
#include "campaign_library.hpp"

// Entering a custom campaign from the title, and back to the main game, without leaving
// the game (docs/design/custom-campaign.md §8). A campaign keeps its own saves: the game's
// SRAM becomes the campaign's (librecomp's change_save_file: <config>/saves/campaigns/<id>/),
// starting from its library's card, and the save store publishes to that library. The
// main game's SRAM file is left as it was, so the launcher still commits it to the main
// library when the game ends. Window thread, on the title screen only.
namespace srw64::campaign_switch {
// A save library the program started with, and the campaigns' folder: both are needed.
bool available();
void enter(const campaign_library::Entry& entry);
void leave();
}
