#pragma once
#include "json/json.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// The player's save library inside the game (docs/design/save-slots-autosave.md §2.2):
// the original's SRAM transfers (80090E5C) for a slot or the suspend area are answered
// from a record file while a page or an autosave has mapped one, and every cartridge
// write is published to saves/cartridge.sram at once. Off unless SRW64_SAVE_LIBRARY
// names the library (the standalone app does); debug runs keep the plain cartridge.
namespace srw64::save_store {
namespace fs=std::filesystem;
// `initial` is the card the host started from; without one the game formats a blank.
void configure(const fs::path& directory,const std::optional<fs::path>& initial);
bool enabled();
fs::path library_directory();
// 80090E5C (game thread): true when the store answered the transfer itself.
bool transfer(uint8_t* ram,unsigned direction,uint32_t cart,uint32_t buffer,uint32_t length);
// While it lives, the game's slot `index` (0/1) is the slot record `record`.
class Window {
    unsigned index;
public:
    Window(unsigned index,const fs::path& record);
    ~Window();
    Window(const Window&)=delete;
    Window& operator=(const Window&)=delete;
};
// While it lives, the suspend area is the suspend record `record`.
class SuspendWindow {
public:
    explicit SuspendWindow(const fs::path& record);
    ~SuspendWindow();
    SuspendWindow(const SuspendWindow&)=delete;
    SuspendWindow& operator=(const SuspendWindow&)=delete;
};
// The next game read of slot 0 comes from `record`: the title's ロード, which the
// intermission overlay performs a few frames after the page confirms.
void arm_load(const fs::path& record);
// Suspend reads come from `record` until disarm_suspend(): the title's コンティニュー
// reads it once to check and once more to restore the map.
void arm_suspend(const fs::path& record);
void disarm_suspend();
std::optional<fs::path> armed_suspend();
void disarm();

// Autosave choices, kept in saves/settings.json (the settings window's セーブ page).
struct Settings { bool autosave{true}; unsigned intermission{3}, turn{5}; };
Settings settings();
void set_settings(const Settings& settings);

fs::path slot_path(unsigned number);
std::vector<unsigned> extended();
std::optional<unsigned> next_free();
struct AutoSave { bool turn; unsigned sequence; fs::path record; nlohmann::json about; };
std::vector<AutoSave> autosaves();
// Writes a new autosave record through `write` (which runs the game's own save with
// the record mapped and may add to `about`), then its .json `about`, then keeps the
// newest `keep` of its kind.
bool autosave(bool turn,unsigned keep,nlohmann::json& about,const std::function<void(const fs::path&)>& write);
// Moves a slot or autosave record, with its .json, to trash/.
bool trash(const fs::path& record);
// import/: emulator files the player put there, with what each holds; export/: the
// card written for every emulator.
nlohmann::json import_candidates();
std::optional<unsigned> import_slot(const std::string& file,unsigned index,bool repair,std::string& error);
std::vector<fs::path> export_all(std::string& error);
}
