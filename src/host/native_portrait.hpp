#pragma once
#include <cstdint>
#include <filesystem>
namespace plume { struct RenderDevice; }
// Whole-image HD portraits (docs/native/native-portraits-hd.md): sprite mode 7 (800964E4)
// draws every portrait as nine 32x32 tiles; after each draw the host keeps one rectangle as
// a marker, blanks the rest, and paints one fixed-size image (768x768) there.
namespace srw64::portraits {
struct PortraitDraw {
    uint32_t dl_begin = 0, dl_end = 0;  // physical RDRAM range written by 800964E4
};
// Reads srw64-portraits-hd.json from the compiled art directory, if there is one.
void configure(const std::filesystem::path& art_directory, const std::filesystem::path& output);
void metal_init(plume::RenderDevice* device);
void shutdown();
// Game thread, right after the original drawer returned.
void rewrite(uint8_t* rdram, const PortraitDraw& draw);
}
