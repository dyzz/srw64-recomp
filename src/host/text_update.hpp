#pragma once
#include "json/json.hpp"
#include "localization/catalog.hpp"
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

// Text updates (docs/design/text-updates.md): the newest text from the website, over the text
// the program shipped with. The update check's /latest.json names one list in `text`, the
// whole text file by file with a short hash, for every release up to the list's `release`.
// take() reads it when it is newer than the one installed, keeps the files the installed
// update has the same, downloads the others, checks them against the catalogs as the game
// reads them, and installs them in SRW64_TEXT_UPDATES as r<revision>/. The dialogue layer
// (native_dialogue.cpp) then reads them between the bundled text and the player's own:
// dialogue files, data-text entries and UI labels, and the Library's names (library.cpp).
// A program newer than the list's `release` ships newer text and drops the update.
namespace srw64::text_update {
enum class State { None, Downloading, Installed, Failed };
struct Pack {
    int revision = 0;
    std::string date;
    nlohmann::json notes;  // {"zh","tw","en","ja"}: one line for the About page
    std::filesystem::path dir;
};
struct Status {
    State state = State::None;
    std::optional<Pack> pack;  // the installed one, used unless bypassed
    bool bypassed = false;     // the About page's "bundled text"
    std::string error;         // Failed
    uint64_t serial = 0;       // changes with every new status, for redraws
};
// What a pack lays over the catalogs, read once when it is installed or found.
struct Layer {
    int revision = 0;
    std::filesystem::path dir;
    std::map<std::string, localization::Catalog::Table> entries, ui;  // by locale
    // The Library's names (content/locales/library-names.json), whole, when the pack has them.
    std::optional<nlohmann::json> library_names;
    std::filesystem::path dialogue(const std::string& locale) const { return dir / "dialogue" / locale; }
    // The catalog with this pack's entries and labels.
    localization::Snapshot apply(const localization::Snapshot& base) const;
};

// Each call finds the installed pack first, once: SRW64_TEXT_UPDATES, this build's version.
Status status();
// The pack in use: none when there is none or the player chose the bundled text.
std::shared_ptr<const Layer> layer();
// The About page's switch: reloads the text either way.
void set_bypassed(bool bypassed);
// The update check's thread, after it read /latest.json: installs the list's files when
// the website's revision is newer, then reloads the text and says so. Throws nothing.
void take(const nlohmann::json& latest, const std::string& user_agent);
// The pack's note in a website language (update::site_language, or "tw"), English otherwise.
std::string note(const Pack& pack, std::string_view language);
}
