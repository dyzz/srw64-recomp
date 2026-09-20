#include "desktop.hpp"
#include "sha256.hpp"
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
fs::path validate_rom(const fs::path& path, const std::string& expected) {
    // Bound IO before hashing. The authoritative, exact identity/size checks
    // remain in run_standalone and the importer, on their own read buffer.
    const auto resolved = fs::canonical(path);
    if (!fs::is_regular_file(resolved) || fs::file_size(resolved) > 64u * 1024u * 1024u)
        throw std::runtime_error("Select a regular Super Robot Wars 64 ROM file.");
    if (sha256_file(resolved) != expected)
        throw std::runtime_error("The ROM does not match Super Robot Wars 64 (Japan, Rev 0).\n"
                                 "Use an unmodified, big-endian .z64 dump of your own game.");
    return resolved;
}
}

int run_desktop(Options options, const std::string& expected_rom_sha256,
                const DesktopUi& ui, const DesktopLaunch& launch, bool choose_another_rom) {
    if (!ui.choose_rom || !ui.show_error || !launch)
        throw std::invalid_argument("Incomplete desktop callbacks");
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
    } catch (const std::exception& error) {
        ui.show_error(std::string("Unable to start SRW64 Recompiled.\n") + error.what() +
                      "\nNo automatic save reset or cache deletion was performed.");
        return 2;
    }
}
}
