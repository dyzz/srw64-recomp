#pragma once
#include <set>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace srw64::app {
namespace fs = std::filesystem;
struct Options {
    fs::path rom, content, user_dir, import_save, export_save;
    // Set by the Windows/Linux launcher; ROMs inside this tree can be remembered relatively.
    fs::path portable_root, sync_user_dir;
    std::string first_language;
    bool sync_system{};
    // A compiled custom campaign (srw64.campaign-image.v1, docs/design/custom-campaign.md);
    // it plays from its own save library, user_dir/campaigns/<id>/saves.
    fs::path campaign;
    std::string campaign_id;   // the id the campaign declares; run_standalone reads it
    std::string language, export_format;
    std::optional<std::string> rules;
    unsigned resolution_scale{}; // Zero means use the prepared content default.
    bool new_game{}, mute{};
    // --debug keeps SRW64_DEBUG=1 through the environment scrub: the debug interface for
    // this run whatever the About page's switch says (loopback TCP, debug.json in the run
    // directory; docs/guide/debug-interface.md).
    bool debug{};
};
Options parse_options(std::span<const std::string_view> args, bool allow_rom_picker = false);
std::string usage();
// --export-save: writes the saved card for an emulator and returns the file; no ROM
// or game is needed.
fs::path export_save(const Options& options);
// --import-save without --rom: takes the file in as the card (save_library.hpp) under the
// user directory's lock and starts nothing; returns what was found and kept.
std::string import_save(const Options& options);
enum class Platform { Windows, MacOS, Linux };
using EnvironmentLookup = std::function<std::string(const char*)>;
fs::path default_user_dir(Platform platform, const EnvironmentLookup& lookup);
fs::path default_user_dir();
// An existing user-data directory beside the executable opts into local storage.
fs::path desktop_user_dir(const fs::path& game_dir, Platform platform, const EnvironmentLookup& lookup);
// The first launch's language, before the player picks one: the first of the system's
// preferred languages (BCP 47 or POSIX names, most preferred first) that is Chinese (any
// script or region: zh-Hans), Japanese or English; English when none is.
// SRW64_SYSTEM_LANGUAGE, when set, stands for the system's list (tests).
std::string first_language(const std::vector<std::string>& preferred);
std::vector<std::string> preferred_languages();
// Steam's own interface language from its registry.vdf text, as a language code for
// first_language ("schinese" and "tchinese" zh, "japanese" ja, "english" en, any other
// Steam name as it is); empty when the text names none. On Linux a game Steam started
// (SteamGameId or SteamAppId set) puts it before the locale: SteamOS runs games in
// en_US whatever language Steam shows.
std::string steam_language(std::string_view registry);
inline std::string system_language() { return first_language(preferred_languages()); }
fs::path content_path(const fs::path& root, const std::string& relative);
std::string read_text(const fs::path& path, size_t limit);
// A campaign id names its save directory: 1-64 letters, digits, '.', '_' or '-'.
bool valid_campaign_id(const std::string& id);
// SRW64_CAMPAIGN_DIRS lists folders as PATH does.
#ifdef _WIN32
inline constexpr char campaign_dir_separator=';';
#else
inline constexpr char campaign_dir_separator=':';
#endif
void atomic_write(const fs::path& path, std::string_view text);
// std::filesystem's absolute and canonical, except that on the PS Vita a path that starts with
// a device ("ux0:data/...", "app0:/...") already is one, where newlib's std::filesystem
// sees a relative path and puts the working directory (app0:) in front.
fs::path absolute_path(const fs::path& path);
fs::path canonical_path(const fs::path& path);
// One line of a launch's progress in the log (SRW64_BOOT): the step, the seconds since the
// program started and, on the PS Vita, the heap in use and the memory the system has left.
void boot_step(const std::string& what);
// Each step's line as boot_step makes it ("content 26.6 s free 31 MiB"): the PS Vita shows
// them on its screen before the graphics start.
void set_boot_listener(void (*listener)(const char* line));
// std::filesystem::remove_all, except on the PS Vita: newlib removes a directory as a file
// there (sceIoRemove), which always fails, and libstdc++'s remove_all retried it forever.
void remove_tree(const fs::path& path, std::error_code& error);
// The names in a folder, from one listing; empty when it cannot be read. On the PS Vita it
// stands in for exists(): Vita3K reports every lookup of a missing file as an error.
std::set<std::string> list_names(const fs::path& folder);
// exists(), from the folder's listing on the PS Vita (list_names), fs::exists elsewhere.
bool present(const fs::path& path);
// std::filesystem::copy_file, except on the PS Vita, where libstdc++'s fails with EINVAL
// (and under qemu-user): the bytes read and written through streams there.
bool copy_file(const fs::path& from, const fs::path& to, bool overwrite, std::error_code& error);
void copy_file(const fs::path& from, const fs::path& to, bool overwrite=false);
// The running program's file; empty when the system does not say.
fs::path executable_path();
// A file or directory shipped with the program: Contents/Resources/<name> in a
// macOS bundle, else <name> beside the executable; empty when there is none.
fs::path bundled_resource(const std::string& name);
// The folder the player put the game in: the one holding the .app on macOS, the
// executable's elsewhere; empty when the system does not say, and on Android, which has
// no such folder.
fs::path game_folder();
// <name> in game_folder(); empty when there is none.
fs::path beside_game(const std::string& name);
void set_environment(const std::string& key, const std::string& value);
void clear_runtime_environment();

// Thrown when another game (or one still exiting) holds the user directory; the desktop
// launcher shows it in the player's language (desktop.cpp).
struct PlayLockHeld : std::runtime_error { using std::runtime_error::runtime_error; };
// The user directory's lock: one game (or one import) at a time. An existing lock file is
// harmless; only an OS-held lock prevents another.
class UserLock {
    struct Handle;
    std::unique_ptr<Handle> handle;
public:
    // Waits this long for a game that is still exiting before throwing PlayLockHeld.
    explicit UserLock(const fs::path& user_dir,std::chrono::milliseconds wait=std::chrono::seconds(10));
    ~UserLock();
    UserLock(const UserLock&)=delete;
    UserLock& operator=(const UserLock&)=delete;
};
// The game holding a user directory, from the record its lock keeps in active.lock: its
// process id and start time, so a reused id is never taken for it. Empty when no other
// live game is recorded there (an older version, another platform's, or this process).
struct PlayLockHolder { unsigned long pid; std::uint64_t started; };
std::optional<PlayLockHolder> play_lock_holder(const fs::path& user_dir);
// Ends that game at once; its unsaved progress is lost. False when it could not be ended.
bool end_play_lock_holder(const PlayLockHolder& holder);
// The lock is held for the entire in-process host run.
class Session {
    std::unique_ptr<UserLock> lock;
    fs::path root, directory, saves_root;
    std::optional<fs::path> initial;
    std::string id;
    bool published{};
public:
    explicit Session(const Options& options);
    ~Session();
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;
    const fs::path& user_dir() const { return root; }
    // The save library: user_dir/saves, or a campaign's own beside it.
    const fs::path& saves_dir() const { return saves_root; }
    const fs::path& session_dir() const { return directory; }
    fs::path output_dir() const { return directory/"run"; }
    const std::optional<fs::path>& initial_save() const { return initial; }
    // After the host returns: drops what the run no longer needs (the runtime's ROM copy).
    // Older sessions are pruned when the next one starts.
    void release_run() const;
    // Call only after the host has joined its worker threads and returned success.
    // No save means no change to the latest committed session, including --new-game.
    // The card is published to saves/cartridge.sram (save_library.hpp).
    bool commit_save(const fs::path& host_save);
};
}
