#pragma once
// HD pictures as the Switch's NRO carries them (tools/release/switch/prepare-hd): beside
// each picture of the HD pack, <stem>.mwtex holds it premultiplied, with its mip chain, in a
// GPU-compressed format (BC7; the Switch's driver decodes ASTC on the CPU), zstd-packed. The GL
// renderer uploads the levels as they are (native_gl.cpp); a quarter of RGBA8's memory, and no
// JPEG to decode.
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace srw64::hd_texture {
struct Compressed {
    uint32_t format = 0;          // the GL internal format (GL_COMPRESSED_RGBA_ASTC_4x4_KHR, ...; GL_R8 for indices, GL_RGBA8 for pixel art)
    uint32_t width = 0, height = 0, levels = 0;
    std::shared_ptr<const std::vector<uint8_t>> data;   // level 0 first, each level's blocks in a row
};
// The prepared copy of `picture` (its .mwtex), when there is one and this build reads them.
bool load(const std::filesystem::path& picture, Compressed& out);
// Whether `picture` or its prepared copy is there (always, on the Switch: see hd_texture.cpp).
bool exists(const std::filesystem::path& picture);
// A compressed format's block size in texels (4 x 4 for ASTC 4x4), 0 for an unknown format.
void block_size(uint32_t format, uint32_t& width, uint32_t& height);
// One level's bytes in a compressed format.
size_t level_bytes(uint32_t format, uint32_t width, uint32_t height);
}  // namespace srw64::hd_texture
