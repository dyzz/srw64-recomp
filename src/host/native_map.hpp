#pragma once
#include <cstdint>
#include <filesystem>
namespace plume { struct RenderDevice; }
// HD tactical maps (docs/design/hd-pipeline-plan.md §3.5): the game still issues its
// 16x16 cell rectangles; after each map draw the host keeps one rectangle as a marker,
// blanks the rest, and paints the whole HD map there with the palette the game loaded.
namespace srw64::hdmap {
struct MapDraw {
    uint32_t dl_begin = 0, dl_end = 0;   // physical RDRAM range written by 800945D4
    uint16_t layout = 0;                 // map layout resource being drawn
    int32_t camera_x = 0, camera_y = 0;  // map pixels at screen (0,0)
};
void configure(const std::filesystem::path& output);
void metal_init(plume::RenderDevice* device);
void shutdown();
// Game thread, right after the original drawer returned.
void rewrite(uint8_t* rdram, const MapDraw& draw);
}
