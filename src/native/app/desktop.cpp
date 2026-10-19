#include "desktop.hpp"
#include "rom_order.hpp"
#include "sha256.hpp"
#include "user_sync.hpp"
#include <algorithm>
#include <regex>
#include <stdexcept>
#include <utility>

namespace srw64::app {
namespace {
std::string path_utf8(const fs::path& path, bool generic = false) {
    const auto value = generic ? path.generic_u8string() : path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
constexpr std::string_view relative_rom_header="srw64.rom-relative.v1\n";
bool inside(const fs::path& path, const fs::path& root) {
    const auto relative=path.lexically_relative(root);
    return !relative.empty() && !relative.has_root_path() && *relative.begin()!="..";
}
std::string rom_preference(const Options& options) {
    if (!options.portable_root.empty() && inside(options.rom,fs::weakly_canonical(options.portable_root))) {
        const auto relative=options.rom.lexically_relative(fs::weakly_canonical(options.user_dir));
        const auto portable=path_utf8(relative,true);
        if (!relative.empty() && !relative.has_root_path() && portable.find('\\')==std::string::npos)
            return std::string(relative_rom_header)+portable;
    }
    return path_utf8(options.rom);
}
fs::path remembered_rom(const fs::path& file) {
    // A broken preference can be replaced by explicit selection, but it must
    // never be interpreted as an unversioned relative path or shell command.
    if (fs::is_symlink(file)) throw std::runtime_error("The saved ROM preference is a symlink.");
    auto bytes = read_text(file, 16384);
    const bool relative=bytes.starts_with(relative_rom_header);
    if (relative) bytes.erase(0,relative_rom_header.size());
    if (bytes.empty() || bytes.find_first_of(std::string("\0\r\n",3)) != std::string::npos)
        throw std::runtime_error("The saved ROM preference is invalid.");
    // Versioned preferences use portable separators, including older Windows-written files.
    if (relative) std::replace(bytes.begin(),bytes.end(),'\\','/');
    const auto path = fs::path(std::u8string(reinterpret_cast<const char8_t*>(bytes.data()), bytes.size()));
    if (relative) {
        if (path.has_root_path()) throw std::runtime_error("The relative ROM preference is rooted.");
        return (file.parent_path()/path).lexically_normal();
    }
    if (!path.is_absolute()) throw std::runtime_error("The saved ROM path is not absolute.");
    return path;
}
// The language the player chose (presentation.json, written by launch.cpp); empty when
// there is none or it cannot be read.
std::string saved_locale(const fs::path& user_dir) {
    try {
        const auto text = read_text(user_dir / "presentation.json", 65536);
        std::smatch found;
        if (std::regex_search(text, found, std::regex(R"re("locale"\s*:\s*"([^"]+)")re"))) return found[1];
    } catch (const std::exception&) {}
    return {};
}
fs::path validate_rom(const fs::path& path, const std::string& expected, const std::string& locale) {
    // Bound IO before hashing. The authoritative, exact identity/size checks
    // remain in run_standalone and the importer, on their own read buffer.
    // A remembered ROM that was moved or deleted, or a folder: the player's message, not
    // the file system's.
    std::error_code missing;
    const auto resolved = fs::canonical(path, missing);
    if (missing || !fs::is_regular_file(resolved, missing))
        throw std::runtime_error(rom_refused_message(RomProblem::missing, path, locale));
    // Any byte order (.z64, .v64, .n64); the launch loads a .z64 copy (rom_order.hpp). Only a
    // 32 MB file can match, so a bigger one is not hashed; rom_problem names what it is (an
    // archive is "not an N64 ROM" whatever its size), as on every other platform.
    if (fs::file_size(resolved) > 64u * 1024u * 1024u || !rom_matches_any_order(resolved, expected))
        throw std::runtime_error(rom_refused_message(rom_problem(resolved), resolved, locale));
    return resolved;
}
}

std::string player_locale(const Options& options) {
    if (!options.language.empty()) return options.language;
    try {
        const auto saved = saved_locale(options.user_dir.empty() ? default_user_dir() : options.user_dir);
        if (!saved.empty()) return saved;
    } catch (const std::exception&) {}
    return "zh-Hans";
}

fs::path launcher_rom(const fs::path& named, const fs::path& user_dir, const fs::path& game_dir) {
    std::error_code error;
    if (!named.empty() && fs::is_regular_file(named, error)) return named;
    // Any byte order: the launch turns a .v64 or .n64 dump into .z64 itself.
    for (const char* name : {"rom.z64", "rom.n64", "rom.v64"})
        for (const auto& folder : {user_dir, game_dir})
            if (!folder.empty() && fs::is_regular_file(folder / name, error)) return folder / name;
    return {};
}

Options prepare_desktop_launch(Options options, const fs::path& game_dir, Platform platform,
                               const EnvironmentLookup& lookup, const fs::path& named_rom) {
    const auto installed=[&]{return lookup ? default_user_dir(platform,lookup) : default_user_dir();};
    const bool local_requested=fs::exists(game_dir/"user-data") || fs::is_symlink(game_dir/"user-data");
    const bool save_only=!options.export_save.empty() || (options.rom.empty() && !options.import_save.empty());
    if (options.user_dir.empty()) {
        options.user_dir=(!save_only && local_requested)
            ? desktop_user_dir(game_dir,platform,lookup) : installed();
        if (!save_only && local_requested) {
            options.portable_root=fs::absolute(game_dir);
            options.sync_system=true;
            // Local use still works without a system directory; report skipped sync on exit.
            try {options.sync_user_dir=fs::weakly_canonical(installed());}catch(const std::runtime_error&){}
        }
    } else if (inside(fs::weakly_canonical(options.user_dir),fs::weakly_canonical(game_dir))) {
        // Explicit --user-dir always stays isolated, even inside the game's tree.
        options.portable_root=fs::absolute(game_dir);
    }
    options.user_dir=fs::absolute(options.user_dir);
    if (save_only) return options;
    options.first_language=options.portable_root.empty() ? "zh-Hans" : "en";
    if (options.rom.empty()) options.rom=launcher_rom(named_rom,options.user_dir,game_dir);
    return options;
}

Options prepare_windows_launch(Options options, const fs::path& game_dir,
                               const EnvironmentLookup& lookup, const fs::path& named_rom) {
    return prepare_desktop_launch(std::move(options),game_dir,Platform::Windows,lookup,named_rom);
}
Options prepare_linux_launch(Options options, const fs::path& game_dir,
                             const EnvironmentLookup& lookup, const fs::path& named_rom) {
    return prepare_desktop_launch(std::move(options),game_dir,Platform::Linux,lookup,named_rom);
}

std::string play_lock_message(const std::string& locale) {
#ifdef __APPLE__
    constexpr const char* monitor_ja="アクティビティモニタ";
    constexpr const char* monitor_en="Activity Monitor";
    constexpr const char* monitor_zh="活动监视器";
#elif defined(_WIN32)
    constexpr const char* monitor_ja="タスクマネージャー";
    constexpr const char* monitor_en="Task Manager";
    constexpr const char* monitor_zh="任务管理器";
#else
    constexpr const char* monitor_ja="システムモニター";
    constexpr const char* monitor_en="System Monitor";
    constexpr const char* monitor_zh="系统监视器";
#endif
    if (locale == "ja")
        return std::string("Marchwind64 を起動できません。\n"
                           "Marchwind64 はすでに起動しているか、終了したゲームがまだ完全に閉じていません。\n"
                           "開いているゲームのウィンドウに切り替えてください。見つからない場合は、") +
               monitor_ja +
               "で Marchwind64 を終了してから、もう一度起動してください。\nセーブデータは変更されていません。";
    if (locale == "en")
        return std::string("Unable to start Marchwind64.\n"
                           "Marchwind64 is already running, or a game you just closed has not finished exiting.\n"
                           "Switch to the open game window. If there is none, end Marchwind64 in ") +
               monitor_en + " and start it again.\nYour saves have not been changed.";
    return std::string("无法启动 Marchwind64。\n"
                       "Marchwind64 已经在运行，或者刚关闭的游戏还没有完全退出。\n"
                       "请切换到已打开的游戏窗口；如果找不到窗口，请在") +
           monitor_zh + "中结束 Marchwind64 后重新启动。\n存档没有被改动。";
}

PlayLockQuestion play_lock_question(const std::string& locale) {
    if (locale == "ja")
        return {"Marchwind64 を起動できません。\n"
                "別の Marchwind64 が同じセーブデータと設定を使用しています（バックグラウンドにあるか、応答していない可能性があります）。\n"
                "それを終了して起動しますか？保存していない進行状況は失われます。保存済みのセーブデータはそのままです。",
                "終了して起動", "キャンセル"};
    if (locale == "en")
        return {"Unable to start Marchwind64.\n"
                "Another Marchwind64 is using the same saves and settings (it may be in the background or not responding).\n"
                "End it and start? Its unsaved progress will be lost; saved games are kept.",
                "End and Start", "Cancel"};
    return {"无法启动 Marchwind64。\n"
            "另一个 Marchwind64 正在使用同一份存档和设置（可能在后台，或者已经没有响应）。\n"
            "要结束它并启动吗？它没有保存的进度会丢失，已保存的存档不受影响。",
            "结束并启动", "取消"};
}

int run_desktop(Options options, const std::string& expected_rom_sha256,
                const DesktopUi& ui, const DesktopLaunch& launch, bool choose_another_rom) {
    if (!ui.choose_rom || !ui.show_error || !launch)
        throw std::invalid_argument("Incomplete desktop callbacks");
    // Default language is applied only after any missing local settings have been imported.
    const auto locale = [&] {
        auto chosen = options.language.empty() ? saved_locale(options.user_dir) : options.language;
        return chosen.empty() ? (options.first_language.empty()?std::string("zh-Hans"):options.first_language) : chosen;
    };
    try {
        options.user_dir = fs::absolute(options.user_dir.empty() ? default_user_dir() : options.user_dir);
        const auto preference = options.user_dir / "last-rom.txt";
        if (choose_another_rom) options.rom.clear();
        else if (options.rom.empty() && (fs::exists(preference) || fs::is_symlink(preference))) {
            try { options.rom = remembered_rom(preference); }
            catch (const std::exception& error) {
                ui.show_error(std::string(error.what()) + "\n" + rom_choose_again(locale(), true));
            }
        }
        // Only ROM selection may retry. Import, save and host failures below
        // must NOT restart a possibly already-initialized game in this process.
        for (;;) {
            if (options.rom.empty()) {
                std::optional<fs::path> selected;
                try { selected = ui.choose_rom(); }
                catch (const DesktopPickerUnavailable&) {
                    // Not a cancel: say where the ROM goes, or the game just closes (Game Mode).
                    ui.show_error(rom_needed_message(locale(), options.user_dir / "rom.z64"));
                    return 1;
                }
                if (!selected) return 0; // Cancel: no directories or preferences written.
                options.rom = *selected;
            }
            try { options.rom = validate_rom(options.rom, expected_rom_sha256, locale()); break; }
            catch (const std::exception& error) {
                ui.show_error(std::string(error.what()) + "\n" + rom_choose_again(locale()));
                options.rom.clear();
            }
        }
        // Another game holding the user directory can be ended here, before anything else
        // is touched; Session takes the lock for real. Briefly, for one that is exiting.
        fs::create_directories(options.user_dir);
        for (;;) {
            try { UserLock probe(options.user_dir, std::chrono::seconds(3)); break; }
            catch (const PlayLockHeld&) {
                const auto holder = play_lock_holder(options.user_dir);
                if (!holder || !ui.confirm) throw;
                const auto question = play_lock_question(locale());
                if (!ui.confirm(question.message, question.accept, question.cancel)) return 0;
                if (!end_play_lock_holder(*holder)) throw;
            }
        }
        if (options.sync_system && !options.sync_user_dir.empty())
            import_missing_user_data(options.user_dir,options.sync_user_dir);
        if (options.language.empty() && !options.first_language.empty() &&
            !fs::exists(options.user_dir/"presentation.json")) options.language=options.first_language;
        bool ready_called = false;
        const int result = launch(options, [&] {
            if (ready_called) throw std::logic_error("Desktop bootstrap entered the game twice");
            // The bootstrap owns Session's lock here. Keep this separate from
            // the save pointer; remembering a ROM cannot change game progress.
            atomic_write(preference, rom_preference(options));
            ready_called = true;
        });
        if (result != 0)
            ui.show_error("The game stopped with exit code " + std::to_string(result) +
                          ".\nYour last committed save has not been replaced.\nUser data: " +
                          path_utf8(options.user_dir));
        if (result==0 && ready_called && options.sync_system) {
            try {
                if (options.sync_user_dir.empty()) throw std::runtime_error("The system user-data directory is unavailable");
                const auto saved=saved_locale(options.user_dir);
                sync_user_data(options.user_dir,options.sync_user_dir,saved.empty()?locale():saved,ui.confirm);
            } catch (const std::exception& error) {
                // The game's own save is already committed. A failed backup must never reset it.
                ui.show_error(std::string("System user-data synchronization was not completed.\n")+error.what()+
                              "\nYour game-directory saves are kept in: "+path_utf8(options.user_dir));
            }
        }
        return result;
    } catch (const PlayLockHeld&) {
        ui.show_error(play_lock_message(locale()));
        return 2;
    } catch (const std::exception& error) {
        ui.show_error(std::string("Unable to start Marchwind64.\n") + error.what() +
                      "\nNo automatic save reset or cache deletion was performed.");
        return 2;
    }
}
}
