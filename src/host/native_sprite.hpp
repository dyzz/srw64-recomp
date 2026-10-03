#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace plume { struct RenderDevice; }
// Scene sprites drawn as one image (docs/native/native-title-and-story-images.md).
// Sprite modes 11-13 (80096CD8, texture rectangles) and 14-16 (8009761C, textured
// quads under the sprite's matrix) draw a scene frame part by part; the filter then
// leaves a seam at every part edge. After such a draw the host keeps one primitive
// as a marker, blanks the others and paints one image in its place:
// - HD frames from the art pack (title logo and flames), in HD image mode only;
// - text drawn natively in the reading language (title menu, chapter title cards,
//   opening and ending pages), always: these never show the original images.
namespace srw64::sprites {
struct SceneDraw {
    uint32_t dl_begin = 0, dl_end = 0;  // physical RDRAM range the drawer wrote
    uint32_t slot = 0, sub = 0;
    bool quads = false;                 // 8009761C (vertices and G_QUAD) rather than 80096CD8 (TEXRECT)
};
// What the drawn sprite is, from its sub-record and the resource handle table.
struct SceneId {
    uint32_t slot = 0, sub = 0;
    uint16_t scene = 0, atlas = 0, palette = 0;
    uint8_t frame = 0;
    uint16_t colors[16]{};              // the palette's first 16 RGBA5551 entries
    uint32_t palette_data = 0;          // the palette resource in RDRAM (header, then the colours)
};
// Text the host draws for a sprite: premultiplied RGBA, `units` wide and tall in
// N64 pixels, placed on the original frame by `anchor`.
struct TextImage {
    uint32_t width = 0, height = 0;
    float units[2]{};
    float origin[2]{};                  // Anchor::origin: the image point placed on the given position
    std::vector<uint8_t> rgba;
};
struct TextJob {
    std::string key;                    // same key, same image: the cache identity
    std::function<TextImage()> render;  // runs on a worker thread
    enum class Anchor { center, top, origin } anchor = Anchor::center;
    float tint[3]{1, 1, 1};             // multiplies the image colour (title menu palettes)
};
// Game thread: the text a sprite shows, or false to leave it to the images.
using Describe = bool (*)(const uint8_t* rdram, const SceneId&, TextJob&);
void configure(const std::filesystem::path& art_directory, const std::filesystem::path& output);
// The battle viewer's thumbnail of a scene (its key, battle_viewer::scenes()) from the art
// pack's battle sprites (tools/hd_ai/cutin_hd.py scenes); empty without one.
std::string viewer_scene_image(const std::string& key);
// A scene image the host makes at run time (rom_art.cpp: BANPRESTO, GAME OVER) instead of
// an art-pack file, drawn the same way and before any file for the same scene. `render`
// runs on the worker, returns premultiplied RGBA8 (units unused), and runs again when the
// texture was released. Queued at once, so it is usually ready by its first draw.
void add_generated_image(uint16_t scene, uint16_t atlas, uint16_t palette, std::vector<uint8_t> frames,
                         std::function<TextImage()> render);
void set_text(Describe describe);
// Render-hook init, after gpu::init (native_gpu.hpp).
void gpu_init();
void shutdown();
// Game thread, right after the original drawer returned.
void rewrite(uint8_t* rdram, const SceneDraw& draw);
// After rewrite: the title's flames (scene 686) still drawn by the original's tiles (no HD
// frame took them): they repeat every 256 pixels, and a picture wider than 4:3 draws them
// again a period to each side (docs/design/deck-16x10.md).
bool repeats_across(const uint8_t* rdram, const SceneDraw& draw);
// Mode 9 scenes (800945D4: 16x16 cells of a type-6 grid scene), game thread: when the
// text describer names the scene (battle HUD banners and badges), its cells go and the
// text is drawn centred on them. Other scenes are left alone.
void rewrite_grid(uint8_t* rdram, const SceneDraw& draw);
// Original UI text (docs/native/native-ui-text.md), game thread: one native draw for a
// whole text pass, in place of the texture rectangle at `marker` (G_TEXRECT, E1, F1,
// after a SETTILESIZE the tag replaces), which is resized to `bounds` (x0, y0, x1, y1).
// Each item's image origin goes on its (x, y) in N64 screen pixels. False when the host
// cannot draw text: the caller then leaves the original glyphs. The caller blanks the rest.
struct PlacedText {
    TextJob job;
    float x = 0, y = 0;
};
bool place_texts(uint8_t* rdram, uint32_t marker, const float bounds[4], const std::vector<PlacedText>& items);
// Text jobs placed and drawn, for the caller's summary.
struct TextCounts { uint64_t placed = 0, waiting = 0; };
TextCounts text_counts();
}
