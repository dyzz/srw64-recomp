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
    fs::path rom, content, user_dir, import_save;
    std::string language;
    std::optional<std::string> rules;
    unsigned resolution_scale{}; // Zero means use the prepared content default.
    bool new_game{}, mute{};
};
Options parse_options(std::span<const std::string_view> args);
std::string usage();
enum class Platform { Windows, MacOS, Linux };
using EnvironmentLookup = std::function<std::string(const char*)>;
fs::path default_user_dir(Platform platform, const EnvironmentLookup& lookup);
fs::path default_user_dir();
fs::path content_path(const fs::path& root, const std::string& relative);
std::string read_text(const fs::path& path, size_t limit);
void atomic_write(const fs::path& path, std::string_view text);
void set_environment(const std::string& key, const std::string& value);
void clear_runtime_environment();

// The lock is held for the entire in-process host run. An existing lock file is
// harmless; only an OS-held lock prevents another launch.
class Session {
    struct Lock;
    std::unique_ptr<Lock> lock;
    fs::path root, directory;
    std::optional<fs::path> initial;
    std::string id;
    bool published{};
public:
    explicit Session(const Options& options);
    ~Session();
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;
    const fs::path& user_dir() const { return root; }
    const fs::path& session_dir() const { return directory; }
    fs::path output_dir() const { return directory/"run"; }
    const std::optional<fs::path>& initial_save() const { return initial; }
    // Call only after the host has joined its worker threads and returned success.
    // No save means no change to the latest committed session, including --new-game.
    bool commit_save(const fs::path& host_save);
};
}
