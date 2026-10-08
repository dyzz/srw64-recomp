#include "desktop.hpp"
#include "rom_order.hpp"
#include "sha256.hpp"
#include <regex>
#include <stdexcept>

namespace srw64::app {
namespace {
std::string path_utf8(const fs::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
fs::path remembered_rom(const fs::path& file) {
    // A broken preference can be replaced by explicit selection, but it must
    // never be interpreted as a relative path, shell command or multiple lines.
    if (fs::is_symlink(file)) throw std::runtime_error("The saved ROM preference is a symlink.");
    const auto bytes = read_text(file, 16384);
    if (bytes.empty() || bytes.find('\0') != std::string::npos)
        throw std::runtime_error("The saved ROM preference is invalid.");
    const auto path = fs::path(std::u8string(reinterpret_cast<const char8_t*>(bytes.data()), bytes.size()));
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
fs::path validate_rom(const fs::path& path, const std::string& expected) {
    // Bound IO before hashing. The authoritative, exact identity/size checks
    // remain in run_standalone and the importer, on their own read buffer.
    const auto resolved = fs::canonical(path);
    if (!fs::is_regular_file(resolved) || fs::file_size(resolved) > 64u * 1024u * 1024u)
        throw std::runtime_error("Select a regular Super Robot Wars 64 ROM file.");
    // Any byte order (.z64, .v64, .n64); the launch loads a .z64 copy (rom_order.hpp).
    if (!rom_matches_any_order(resolved, expected))
        throw std::runtime_error("The ROM does not match Super Robot Wars 64 (Japan, Rev 0).\n"
                                 "Use an unmodified dump of your own game (.z64, .v64 or .n64).");
    return resolved;
}
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

std::string play_lock_message(const std::string& locale) {
#ifdef __APPLE__
    const bool mac = true;
#else
    const bool mac = false;
#endif
    if (locale == "ja")
        return std::string("Marchwind64 を起動できません。\n"
                           "Marchwind64 はすでに起動しているか、終了したゲームがまだ完全に閉じていません。\n"
                           "開いているゲームのウィンドウに切り替えてください。見つからない場合は、") +
               (mac ? "アクティビティモニタ" : "タスクマネージャー") +
               "で Marchwind64 を終了してから、もう一度起動してください。\nセーブデータは変更されていません。";
    if (locale == "en")
        return std::string("Unable to start Marchwind64.\n"
                           "Marchwind64 is already running, or a game you just closed has not finished exiting.\n"
                           "Switch to the open game window. If there is none, end Marchwind64 in ") +
               (mac ? "Activity Monitor" : "Task Manager") + " and start it again.\nYour saves have not been changed.";
    return std::string("无法启动 Marchwind64。\n"
                       "Marchwind64 已经在运行，或者刚关闭的游戏还没有完全退出。\n"
                       "请切换到已打开的游戏窗口；如果找不到窗口，请在") +
           (mac ? "活动监视器" : "任务管理器") + "中结束 Marchwind64 后重新启动。\n存档没有被改动。";
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
    // The first launch has no settings yet and starts in Simplified Chinese (host.cpp).
    const auto locale = [&] {
        auto chosen = options.language.empty() ? saved_locale(options.user_dir) : options.language;
        return chosen.empty() ? std::string("zh-Hans") : chosen;
    };
    try {
        options.user_dir = fs::absolute(options.user_dir.empty() ? default_user_dir() : options.user_dir);
        const auto preference = options.user_dir / "last-rom.txt";
        if (choose_another_rom) options.rom.clear();
        else if (options.rom.empty() && (fs::exists(preference) || fs::is_symlink(preference))) {
            try { options.rom = remembered_rom(preference); }
            catch (const std::exception& error) {
                ui.show_error(std::string(error.what()) + "\nSelect the ROM again; saves are unchanged.");
            }
        }
        // Only ROM selection may retry. Import, save and host failures below
        // must NOT restart a possibly already-initialized game in this process.
        for (;;) {
            if (options.rom.empty()) {
                const auto selected = ui.choose_rom();
                if (!selected) return 0; // Cancel: no directories or preferences written.
                options.rom = *selected;
            }
            try { options.rom = validate_rom(options.rom, expected_rom_sha256); break; }
            catch (const std::exception& error) {
                ui.show_error(std::string(error.what()) + "\nSelect another file or Cancel.");
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
        bool ready_called = false;
        const int result = launch(options, [&] {
            if (ready_called) throw std::logic_error("Desktop bootstrap entered the game twice");
            // The bootstrap owns Session's lock here. Keep this separate from
            // the save pointer; remembering a ROM cannot change game progress.
            atomic_write(preference, path_utf8(options.rom));
            ready_called = true;
        });
        if (result != 0)
            ui.show_error("The game stopped with exit code " + std::to_string(result) +
                          ".\nYour last committed save has not been replaced.\nUser data: " +
                          path_utf8(options.user_dir));
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
