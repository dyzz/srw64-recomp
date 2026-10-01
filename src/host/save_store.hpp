#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

// The player's save library inside the game (docs/design/save-slots-autosave.md §2.2):
// the original's SRAM transfers (80090E5C) for a slot are answered from an extended
// slot file while a page has mapped one, and every cartridge write is published to
// saves/cartridge.sram at once. Off unless SRW64_SAVE_LIBRARY names the library (the
// standalone app does); debug runs keep the plain two-slot cartridge.
namespace srw64::save_store {
// `initial` is the card the host started from; without one the game formats a blank.
void configure(const std::filesystem::path& directory,const std::optional<std::filesystem::path>& initial);
bool enabled();
// 80090E5C (game thread): true when the store answered the transfer itself.
bool transfer(uint8_t* ram,unsigned direction,uint32_t cart,uint32_t buffer,uint32_t length);
// While it lives, the game's slot `index` (0/1) is extended slot `number` (3..99).
class Window {
    unsigned index;
public:
    Window(unsigned index,unsigned number);
    ~Window();
    Window(const Window&)=delete;
    Window& operator=(const Window&)=delete;
};
// The next game read of slot 0 comes from extended slot `number`: the title's ロード,
// which the intermission overlay performs a few frames after the page confirms.
void arm_load(unsigned number);
void disarm();
std::vector<unsigned> extended();
std::optional<unsigned> next_free();
}
