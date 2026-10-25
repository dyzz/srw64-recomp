#pragma once
#include "runtime.hpp"
namespace srw64::app {
// The data run_prepared reads, made from the player's ROM by the importer the other platforms
// run on their first launch (docs/design/switch-port.md). The Switch makes it on its first
// launch; tools/release/switch/prepare_data.py makes the same on a computer.
//
// OUT gets what goes into the player folder as it is:
//   game/rom.z64          the ROM, big-endian, checked against the supported one
//   game/art.pack         the importer's images
//   game/dialogue.json    the importer's dialogue data without its large string tables, its
//                         images named where the console finds them (FOLDER/game/art.pack)
//   game/dialogue.bin     those tables (string_tables.hpp): the original records, and each
//                         locale's translations with the shipped dialogue text laid over them
//                         and its intro pages
//   game/prepared.json    what run_prepared reads first: the format, the ROM, the locales;
//                         written last, so an import cut short is made again
//   game/blank.sram       a card of zeros, what the game starts from before it has saved
//                         (as the runtime fills a missing save file)
//   run/runtime-data/saves/, saves/slots/, shaders/   the folders the game writes into
//                         (shaders/: the GL renderer's compiled programs, kept between runs)
// ROM is the player's dump in any byte order; DIALOGUE_TEXT the dialogue text shipped with
// the game (content/dialogue); FOLDER the player folder as the console names it. The output
// is derived from the ROM: it is the player's own, never a release asset. Returns how many
// dialogue text lines had problems (each on stderr).
size_t prepare_console_data(const fs::path& rom, const fs::path& out, const fs::path& dialogue_text,
                            const std::string& folder);
// Whether OUT/game holds data that run_prepared takes for this ROM and this program.
bool console_data_current(const fs::path& out, const std::string& rom_sha256);
}
