#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
// HD art the host makes from the player's ROM at run time, so the HD pack ships none of
// the original pixels (docs/native/native-ui-text.md §4). CPU only; any thread once
// initialize() has run.
// - Window frames and page arrows: grid scenes of the 1295 line atlas and the 1302 arrow
//   atlas. The scene is composed from the ROM only to learn where its lines run; they are
//   then drawn anew at 4x (user's choice, "A"): every line and shadow pixel a round-capped
//   stroke joined to its neighbours, diagonals included, antialiased. A stroke texel keeps
//   the palette index of the nearest original pixel, so the colour cycling that runs light
//   along the frames still plays. Crop and table match tools/hd_ai/frame_hd.py
//   (tests/native_rom_art.cpp).
// - The BANPRESTO logo and GAME OVER: flat-colour scenes, upscaled (flat_image below).
namespace srw64::rom_art {
// The ROM the runtime loaded; it must outlive every call.
void initialize(std::span<const uint8_t> rom);
bool ready();

struct FrameSpec { uint16_t scene = 0, atlas = 0, palette = 0; };
// The frame scenes and the atlas and palette each is drawn with.
const std::vector<FrameSpec>& frames();
std::optional<FrameSpec> frame(uint16_t scene);

// An HD colour-index image: `scale` texels per original pixel, cropped to the cells the
// scene uses; `origin` is the crop's corner in scene pixels. `palette` is the scene's
// reference palette as RGBA8 (R in the low byte), entry 0 transparent.
struct IndexImage {
    int width = 0, height = 0, scale = 0;     // width, height in original pixels
    int origin[2]{};
    std::vector<uint8_t> index;               // (width*scale) x (height*scale)
    std::vector<uint8_t> coverage;            // same size: how much of the texel the strokes cover
    std::vector<uint8_t> source;              // the composed original crop, width x height
    std::array<uint32_t, 256> palette{};
};
// Empty (width 0) when the scene has no picture or does not decode.
IndexImage frame_image(const FrameSpec& spec);

// Flat-colour scenes upscaled 8x (the BANPRESTO logo 619 and GAME OVER 614): every pixel
// split between its two nearest key colours, each colour group upscaled and smoothed,
// the strongest group winning with a steep soft-argmax. Same method as
// tools/hd_ai/flat_scene_hd.py (Pillow's filters are matched closely, not bit for bit).
struct FlatSpec { uint16_t scene = 0, atlas = 0, palette = 0; };
const std::vector<FlatSpec>& flat_scenes();
// Straight-alpha RGBA8 at 8x the scene's frame; empty (width 0) when it does not decode.
struct RgbaImage { int width = 0, height = 0; std::vector<uint8_t> rgba; };
RgbaImage flat_image(const FlatSpec& spec);
}
