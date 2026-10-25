#include "hd_texture.hpp"
#include "app/runtime.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#ifdef __SWITCH__
#include <zstd.h>
#endif

namespace srw64::hd_texture {
namespace {
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
    std::error_code missing;
    if (!std::filesystem::is_regular_file(prepared(picture), missing)) return false;
    const std::string file = app::read_text(prepared(picture), 512u << 20);   // whole, not a byte at a time
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
