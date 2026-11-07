#include "hd_texture.hpp"
#include "app/runtime.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#ifdef __SWITCH__
#include <array>
#include <chrono>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <unistd.h>
#include <zstd.h>
std::string srw64_switch_hd_art();
#endif

namespace srw64::hd_texture {
namespace {
#ifdef __SWITCH__
// The reads under way, for the stall watchdog (stall_watchdog.cpp): on 2026-11-04 the render
// thread waited more than ten seconds on the SD card to open an HD picture while the sprite
// worker waited on another (docs/design/switch-port.md), and nothing told which or how long.
using Clock = std::chrono::steady_clock;
struct Reading { std::string path; Clock::time_point since; bool busy = false; };
std::mutex reading_mutex;
std::array<Reading, 8> reading;
struct Watched {
    size_t slot = reading.size();
    explicit Watched(const std::filesystem::path& path) {
        std::lock_guard lock(reading_mutex);
        for (size_t i = 0; i < reading.size(); ++i)
            if (!reading[i].busy) { reading[i] = {path.string(), Clock::now(), true}; slot = i; break; }
    }
    ~Watched() {
        if (slot == reading.size()) return;
        std::lock_guard lock(reading_mutex);
        reading[slot].busy = false;
    }
};
// The prepared pictures in one file (tools/release/switch/pack_hd.py): opening a file on the
// SD card walks its folder entry by entry, half a second in art/battle-sprites whatever the
// file's size, so the pack is opened once and read at an offset. A picture it does not hold
// is read as a file of its own: a MOD's, or all of them from an HD pack without art.pack.
struct PackEntry { uint64_t hash, offset; uint32_t size, reserved; };
static_assert(sizeof(PackEntry) == 24);
struct Pack {
    int file = -1;
    std::string root;   // the HD folder with its slash
    std::vector<PackEntry> entries;   // sorted by hash
    std::mutex mutex;   // a read is a seek and a read
};
uint64_t fnv1a(std::string_view text) {
    uint64_t value = 0xCBF29CE484222325ull;
    for (const unsigned char byte : text) value = (value ^ byte) * 0x100000001B3ull;
    return value;
}
Pack& pack() {
    static Pack* opened = [] {
        auto* p = new Pack;
        const std::filesystem::path art = srw64_switch_hd_art();
        if (art.empty()) return p;
        const auto folder = art.parent_path();
        const auto path = folder / "art.pack";
        const int file = ::open(path.c_str(), O_RDONLY);
        if (file < 0) return p;
        uint32_t head[3] = {};
        const auto entries = [&] {
            if (::read(file, head, sizeof head) != ssize_t(sizeof head) || head[0] != 0x4B50574D || head[1] != 1)
                return false;
            p->entries.resize(head[2]);
            const auto bytes = ssize_t(head[2] * sizeof(PackEntry));
            return ::read(file, p->entries.data(), size_t(bytes)) == bytes;
        }();
        if (!entries) {
            std::fprintf(stderr, "SRW64_HD_PACK_BAD %s\n", path.c_str());
            ::close(file);
            p->entries.clear();
            return p;
        }
        p->file = file;
        p->root = folder.string() + "/";
        std::fprintf(stderr, "SRW64_HD_PACK %zu pictures\n", p->entries.size());
        return p;
    }();
    return *opened;
}
// `path`'s bytes from the pack; false when it is not in there.
bool from_pack(const std::string& path, std::string& out) {
    auto& p = pack();
    if (p.file < 0 || path.rfind(p.root, 0) != 0) return false;
    const uint64_t hash = fnv1a(std::string_view(path).substr(p.root.size()));
    const auto found = std::lower_bound(p.entries.begin(), p.entries.end(), hash,
                                        [](const PackEntry& e, uint64_t h) { return e.hash < h; });
    if (found == p.entries.end() || found->hash != hash) return false;
    out.resize(found->size);
    std::lock_guard lock(p.mutex);
    return ::lseek(p.file, off_t(found->offset), SEEK_SET) == off_t(found->offset) &&
           ::read(p.file, out.data(), out.size()) == ssize_t(out.size());
}
double ms_since(Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }
#endif
// The file: "MWTX", version 1, the GL format, width, height, level count (little-endian
// 32-bit words), then the zstd frame of every level's blocks, level 0 first.
constexpr uint32_t kMagic = 0x5854574D, kVersion = 1;
std::filesystem::path prepared(const std::filesystem::path& picture) {
    auto path = picture;
    return path.replace_extension(".mwtex");
}
}  // namespace

void block_size(uint32_t format, uint32_t& width, uint32_t& height) {
    switch (format) {
    case 0x93B0: width = 4; height = 4; break;   // GL_COMPRESSED_RGBA_ASTC_4x4_KHR
    case 0x93B4: width = 6; height = 6; break;   // GL_COMPRESSED_RGBA_ASTC_6x6_KHR
    case 0x93B7: width = 8; height = 8; break;   // GL_COMPRESSED_RGBA_ASTC_8x8_KHR
    case 0x8E8C: width = 4; height = 4; break;   // GL_COMPRESSED_RGBA_BPTC_UNORM (BC7)
    default: width = height = 0; break;
    }
}

size_t level_bytes(uint32_t format, uint32_t width, uint32_t height) {
    if (format == 0x8229) return size_t(width) * height;   // GL_R8: a map's palette indices
    if (format == 0x8058) return size_t(width) * height * 4;   // GL_RGBA8: pixel art, uncompressed
    uint32_t bw, bh;
    block_size(format, bw, bh);
    if (!bw) return 0;
    return size_t((width + bw - 1) / bw) * ((height + bh - 1) / bh) * 16;
}

bool exists(const std::filesystem::path& picture) {
#ifdef __SWITCH__
    // The pack the NRO carries or that sits beside it is whole as prepare-hd made it; a lookup on
    // the SD card costs milliseconds and the layers name some 1,700 pictures at start.
    (void)picture;
    return true;
#endif
    std::error_code error;
    return std::filesystem::exists(picture, error) || std::filesystem::exists(prepared(picture), error);
}

bool load(const std::filesystem::path& picture, Compressed& out) {
#ifdef __SWITCH__
    const Watched watched(prepared(picture));
    const auto started = Clock::now();
    std::string file;
    double looked = 0;
    if (!from_pack(prepared(picture).string(), file)) {
        std::error_code missing;
        if (!std::filesystem::is_regular_file(prepared(picture), missing)) return false;
        looked = ms_since(started);
        file = app::read_text(prepared(picture), 512u << 20);   // whole, not a byte at a time
    }
    // An SD card read slow enough to hold a frame up: which file, the lookup and the read.
    if (const double total = ms_since(started); total >= 300)
        std::fprintf(stderr, "SRW64_SLOW_READ %.0f ms (lookup %.0f, read %.0f) %zu bytes %s\n", total, looked, total - looked,
                     file.size(), prepared(picture).string().c_str());
    uint32_t head[6];
    if (file.size() < sizeof head) return false;
    std::memcpy(head, file.data(), sizeof head);
    if (head[0] != kMagic || head[1] != kVersion) return false;
    Compressed c{head[2], head[3], head[4], head[5], nullptr};
    size_t bytes = 0;
    for (uint32_t level = 0, w = c.width, h = c.height; level < c.levels; ++level, w = std::max(1u, w / 2), h = std::max(1u, h / 2))
        bytes += level_bytes(c.format, w, h);
    if (!bytes) return false;
    auto data = std::make_shared<std::vector<uint8_t>>(bytes);
    const size_t made = ZSTD_decompress(data->data(), bytes, file.data() + sizeof head, file.size() - sizeof head);
    if (ZSTD_isError(made) || made != bytes) {
        std::fprintf(stderr, "SRW64_HD_TEXTURE_BAD %s\n", prepared(picture).string().c_str());
        return false;
    }
    c.data = std::move(data);
    out = std::move(c);
    return true;
#else
    (void)picture; (void)out;
    return false;
#endif
}
}  // namespace srw64::hd_texture

#ifdef __SWITCH__
// For the stall watchdog: each HD picture still being read, and for how long.
std::string srw64_hd_reads_in_flight() {
    using namespace srw64::hd_texture;
    std::lock_guard lock(reading_mutex);
    std::string out;
    for (const auto& r : reading)
        if (r.busy) {
            char line[32];
            std::snprintf(line, sizeof line, "%s%.1fs ", out.empty() ? "" : "; ", ms_since(r.since) / 1000);
            out += line + r.path;
        }
    return out.empty() ? "none" : out;
}
#endif
