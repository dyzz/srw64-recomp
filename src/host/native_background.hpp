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
// Render-hook start, HD pack or not: the original battle skies are drawn whole too.
void initialize(const std::filesystem::path& output);
// Reads srw64-backgrounds-hd.json from the compiled art directory, if there is one.
void configure(const std::filesystem::path& art_directory, const std::filesystem::path& output);
// Render-hook init, after gpu::init (native_gpu.hpp).
void gpu_init();
void shutdown();
// The battle's sky layers that follow the camera's pitch (kinds 2, 3 and 9, 80084D90),
// without the bands (automatic aspect; at 4:3 they are drawn as the game draws them), in
// either image mode (the HD picture, or the original decoded from the ROM): rewrite draws
// them whole, enlarged by kSkyZoom about their bottom edge so the rows the camera uncovers
// above them are picture (a close-up's pitch 0 moves them 16.8 rows down), across the whole
// screen. 80095974's wrapper then draws them once, without its side copies or wrapped rows.
// The pictures stay 1:1 with the original; the enlargement is the drawing's.
inline constexpr float kSkyZoom = (240.f + 17.f) / 240.f;
bool battle_sky(const uint8_t* rdram, uint32_t slot, uint32_t sub);
// Game thread, right after the original drawer returned.
// True when the picture was drawn wider than the original's 4:3 (the starfield).
bool rewrite(uint8_t* rdram, const BackgroundDraw& draw);
}
