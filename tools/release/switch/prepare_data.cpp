// The Switch's data, prepared on a computer from the player's ROM: what the console makes on its
// own first launch (app/console_data.hpp), for development, where the console should not
// spend that time on each fresh folder.
//
//   srw64-switch-prepare ROM OUT DIALOGUE_TEXT
//
// ROM is the player's dump in any byte order; DIALOGUE_TEXT the dialogue text shipped with
// the game (content/dialogue); OUT gets what goes into /switch/Marchwind64 on the SD card as it
// is (console_data.hpp lists it). The output is derived from the ROM: it is the player's own,
// never a release asset.
#include "app/console_data.hpp"
#include <cstdio>
#include <iostream>

using namespace srw64::app;

int main(int argc, char** argv) {
    try {
        if (argc != 4) throw std::runtime_error("Usage: srw64-switch-prepare ROM OUT DIALOGUE_TEXT");
        const fs::path out = fs::absolute(argv[2]);
        // Where the console's player folder is (sdmc:, the default device): the images are named from it.
        const auto problems = prepare_console_data(argv[1], out, argv[3], "/switch/Marchwind64");
        std::printf("SRW64_SWITCH_PREPARED %s (%zu dialogue text problems)\n", out.string().c_str(), problems);
        return problems ? 1 : 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "srw64-switch-prepare: %s\n", error.what());
        return 2;
    }
}
