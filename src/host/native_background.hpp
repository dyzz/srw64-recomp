#pragma once
#include <cstdint>
#include <filesystem>
namespace plume { struct RenderDevice; }
// Whole HD intermission backgrounds (docs/native/native-backgrounds-hd.md): sprite
// mode 4 (80095974) draws the 320x240 CI8 background in strips; after each draw the
// host keeps one rectangle as a marker, blanks the rest and paints the HD image there,
// strip by strip with the same screen and texture rectangles.
namespace srw64::backgrounds {
struct BackgroundDraw {
    uint32_t dl_begin = 0, dl_end = 0;  // physical RDRAM range written by 80095974
    uint32_t slot = 0, sub = 0;
};
// Reads srw64-backgrounds-hd.json from the compiled art directory, if there is one.
void configure(const std::filesystem::path& art_directory, const std::filesystem::path& output);
void metal_init(plume::RenderDevice* device);
void shutdown();
// Game thread, right after the original drawer returned.
void rewrite(uint8_t* rdram, const BackgroundDraw& draw);
}
