#pragma once
// N64 dumps come in three byte orders: big-endian .z64 (the game's own, header 80 37 12 40),
// .v64 with every 16-bit pair swapped (37 80 40 12) and .n64 with every 32-bit word
// reversed (40 12 37 80). The supported ROM is identified by the .z64 SHA-256, so a .v64
// or .n64 dump is turned into .z64 first; the file's name or extension does not matter.
#include "runtime.hpp"
#include "sha256.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>

namespace srw64::app {

// The bytes in big-endian (.z64) order, or empty when the header is none of the three.
inline std::string rom_big_endian(std::string bytes) {
    if (bytes.size() < 4 || bytes.size() % 4) return {};
    const auto b = [&](size_t i) { return uint8_t(bytes[i]); };
    if (b(0) == 0x80 && b(1) == 0x37 && b(2) == 0x12 && b(3) == 0x40) return bytes;
    if (b(0) == 0x37 && b(1) == 0x80 && b(2) == 0x40 && b(3) == 0x12) {
        for (size_t i = 0; i < bytes.size(); i += 2) std::swap(bytes[i], bytes[i + 1]);
        return bytes;
    }
    if (b(0) == 0x40 && b(1) == 0x12 && b(2) == 0x37 && b(3) == 0x80) {
        for (size_t i = 0; i < bytes.size(); i += 4) { std::swap(bytes[i], bytes[i + 3]); std::swap(bytes[i + 1], bytes[i + 2]); }
        return bytes;
    }
    return {};
}

inline std::string sha256_bytes(const std::string& bytes) {
    Sha256 hash;
    hash.update({reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()});
    return hash.finish();
}

// Whether the file is the supported ROM in any of the three byte orders.
inline bool rom_matches_any_order(const std::filesystem::path& path, const std::string& expected) {
    if (sha256_file(path) == expected) return true;
    const auto z64 = rom_big_endian(read_text(path, 64u << 20));
    return !z64.empty() && sha256_bytes(z64) == expected;
}

// Why a file is not the supported ROM, for the message the player sees: an archive or other
// file (a .zip or .7z is the usual one), the wrong size (cut short or padded), or another or a
// patched game.
enum class RomProblem { not_n64, wrong_size, wrong_game };
struct RomRefused : std::runtime_error {
    RomProblem problem;
    std::filesystem::path path;
    RomRefused(RomProblem problem, std::filesystem::path path)
        : std::runtime_error("ROM does not match the supported baseline: " + path.string()), problem(problem), path(std::move(path)) {}
};
inline constexpr uintmax_t supported_rom_size = 32u << 20;
inline RomProblem rom_problem(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    std::string head(4, '\0');
    std::ifstream file(path, std::ios::binary);
    if (!file.read(head.data(), 4) || rom_big_endian(head).empty()) return RomProblem::not_n64;
    return !error && size == supported_rom_size ? RomProblem::wrong_game : RomProblem::wrong_size;
}
// The player's message for a refused ROM, in their language (zh-Hans, ja or en; anything
// else gets Simplified Chinese). Android's launcher says the same (RomCheck.java).
inline std::string rom_refused_message(RomProblem problem, const std::filesystem::path& path, const std::string& locale) {
    const auto u8 = path.u8string();
    const std::string file(reinterpret_cast<const char*>(u8.data()), u8.size());
    if (locale == "ja") {
        const char* why = problem == RomProblem::not_n64
            ? "N64 の ROM ファイルではありません。.zip や .7z などの圧縮ファイルなら、先に展開して中の .z64／.v64／.n64 を使ってください。"
            : problem == RomProblem::wrong_size ? "ROM のサイズが違います（32 MB のはずです）。ファイルが壊れているか、改変されている可能性があります。"
            : "対応している ROM ではありません。改変していないスーパーロボット大戦64（日本版 Rev 0）が必要です（翻訳パッチ版や他のゲームは使えません）。";
        return std::string("Marchwind64 を起動できません。\n") + why + "\n\nファイル：" + file;
    }
    if (locale == "en") {
        const char* why = problem == RomProblem::not_n64
            ? "This is not an N64 ROM. If you have a .zip or .7z archive, extract it first and use the .z64, .v64 or .n64 file inside."
            : problem == RomProblem::wrong_size ? "The ROM has the wrong size (it should be 32 MB); the file may be incomplete or modified."
            : "This is not the supported ROM: it must be an unmodified Super Robot Taisen 64 (Japan, Rev 0), not a translated or patched one, nor another game.";
        return std::string("Unable to start Marchwind64.\n") + why + "\n\nFile: " + file;
    }
    const char* why = problem == RomProblem::not_n64
        ? "这不是 N64 ROM 文件。如果是 .zip、.7z 等压缩包，请先解压，再用里面的 .z64／.v64／.n64 文件。"
        : problem == RomProblem::wrong_size ? "ROM 大小不对（应为 32 MB），文件可能不完整或被改过。"
        : "这不是支持的 ROM：需要未修改的超级机器人大战 64 日版 Rev 0（不能是汉化版、补丁版或其他游戏）。";
    return std::string("无法启动 Marchwind64。\n") + why + "\n\n文件：" + file;
}

// The path to load: the file itself when it is already .z64, else a .z64 copy kept in the
// user directory (rom-z64.z64), written once and reused while it still matches.
inline std::filesystem::path z64_rom(const std::filesystem::path& path, const std::filesystem::path& user_dir,
                                     const std::string& expected) {
    if (sha256_file(path) == expected) return path;
    const auto z64 = rom_big_endian(read_text(path, 64u << 20));
    if (z64.empty() || sha256_bytes(z64) != expected) throw RomRefused(rom_problem(path), path);
    const auto copy = user_dir / "rom-z64.z64";
    std::error_code error;
    if (!(std::filesystem::is_regular_file(copy, error) && sha256_file(copy) == expected)) {
        std::filesystem::create_directories(user_dir);
        atomic_write(copy, z64);
    }
    return copy;
}

}  // namespace srw64::app
