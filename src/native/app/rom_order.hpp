#pragma once
// N64 dumps come in three byte orders: big-endian .z64 (the game's own, header 80 37 12 40),
// .v64 with every 16-bit pair swapped (37 80 40 12) and .n64 with every 32-bit word
// reversed (40 12 37 80). The supported ROM is identified by the .z64 SHA-256, so a .v64
// or .n64 dump is turned into .z64 first; the file's name or extension does not matter.
#include "runtime.hpp"
#include "sha256.hpp"

#include <cstdint>
#include <filesystem>
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

// The path to load: the file itself when it is already .z64, else a .z64 copy kept in the
// user directory (rom-z64.z64), written once and reused while it still matches.
inline std::filesystem::path z64_rom(const std::filesystem::path& path, const std::filesystem::path& user_dir,
                                     const std::string& expected) {
    if (sha256_file(path) == expected) return path;
    const auto z64 = rom_big_endian(read_text(path, 64u << 20));
    if (z64.empty() || sha256_bytes(z64) != expected) throw std::runtime_error("ROM does not match the supported baseline");
    const auto copy = user_dir / "rom-z64.z64";
    std::error_code error;
    if (!(std::filesystem::is_regular_file(copy, error) && sha256_file(copy) == expected)) {
        std::filesystem::create_directories(user_dir);
        atomic_write(copy, z64);
    }
    return copy;
}

}  // namespace srw64::app
