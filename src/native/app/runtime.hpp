#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace srw64::app {
namespace fs = std::filesystem;
struct Options {
    fs::path rom, content, user_dir, import_save, export_save;
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
Options parse_options(std::span<const std::string_view> args);
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
// The running program's file; empty when the system does not say.
fs::path executable_path();
// A file or directory shipped with the program: Contents/Resources/<name> in a
// macOS bundle, else <name> beside the executable; empty when there is none.
fs::path bundled_resource(const std::string& name);
// <name> in the folder the player put the game in: beside the .app on macOS, beside
// the executable elsewhere; empty when there is none (and on Android, which has no
// such folder).
fs::path beside_game(const std::string& name);
void set_environment(const std::string& key, const std::string& value);
void clear_runtime_environment();

// The user directory's lock: one game (or one import) at a time. An existing lock file is
// harmless; only an OS-held lock prevents another.
class UserLock {
    struct Handle;
    std::unique_ptr<Handle> handle;
public:
    explicit UserLock(const fs::path& user_dir);
    ~UserLock();
    UserLock(const UserLock&)=delete;
    UserLock& operator=(const UserLock&)=delete;
};
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
