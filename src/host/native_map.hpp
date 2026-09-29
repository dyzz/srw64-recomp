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
    bool overview = false;               // 800943E0 scaling: the whole map (less its border) on one screen
    float view_left = -1;                // the widened view's x = 0 in the picture, or < 0 (wide_map.hpp)
};
void configure(const std::filesystem::path& output);
// Render-hook init, after gpu::init (native_gpu.hpp).
void gpu_init();
void shutdown();
// Game thread, right after the original drawer returned.
void rewrite(uint8_t* rdram, const MapDraw& draw);
// Set once at start (battle_page.cpp): true for a window frame that keeps its original
// image although the image mode is HD. Unset, every frame follows the image mode.
void set_original_frames(bool (*keep)(uint16_t layout));
// The terrain panel (801E2D54) magnifies the cursor cell from the tile atlas; with an HD
// map it shows that cell's block of the whole-map base instead (the "pseudo tile").
struct PanelDraw {
    uint32_t dl_begin = 0, dl_end = 0;   // physical RDRAM range written by 801E2D54
};
void rewrite_panel(uint8_t* rdram, const PanelDraw& draw);
}
