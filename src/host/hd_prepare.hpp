#pragma once
// RecompLauncher's setup step (tools/release/recomplauncher.json `prepare`, docs/guide/release.md):
// `Marchwind64 --prepare` installs the newest HD pack into the user directory without a
// window, printing `progress <0..1> <text>` lines for the launcher's setup screen, so a
// player who installs from the launcher gets HD by default. The pack is the one the
// website's /latest.json names (update_check.hpp): downloaded into the user directory,
// checked against its SHA-256, unpacked into a staging folder and renamed into place, so a
// failure leaves whatever pack was there. A pack of that version already where the game
// looks first (launch.cpp: the user directory, beside the game, bundled) means nothing to do.
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>

namespace srw64::hd {
// `fraction` runs 0..1 over the whole step; `text` is what shows under it.
using Progress = std::function<void(double fraction, const std::string& text)>;
// The version in a pack folder's hd.json; nullopt when there is no pack there, "" for a
// pack from before HD versions.
std::optional<std::string> pack_version(const std::filesystem::path& folder);
// Unpacks a downloaded pack (hd/ at its top, as build_release.py zips it, or the folder's
// contents at the top) into user_dir/hd through a staging folder, replacing any pack
// there. Returns the installed version. Throws with the reason; the pack that was there
// stays in that case.
std::string install_pack(const std::filesystem::path& zip, const std::filesystem::path& user_dir, const Progress& progress);
// The whole step for the user directory the game would use. Prints the progress lines to
// `out`; returns 0 when the pack is installed or there is none to install, else 3 with
// the reason on stderr (recomplauncher.json names that code).
int prepare(const std::filesystem::path& user_dir, std::ostream& out);
}
