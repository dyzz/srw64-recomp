#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
namespace plume { struct RenderDevice; }
// Whole-image HD portraits (docs/native/native-portraits-hd.md): sprite mode 7 (800964E4)
// draws every portrait as nine 32x32 tiles; after each draw the host keeps one rectangle as
// a marker, blanks the rest, and paints one fixed-size image (768x768) there.
namespace srw64::portraits {
struct PortraitDraw {
    uint32_t dl_begin = 0, dl_end = 0;  // physical RDRAM range written by 800964E4
};
// The draw hooks, with or without the HD pack (MOD portraits need them): once, after
// marker/hdmap installed theirs.
void install();
// Reads srw64-portraits-hd.json from the compiled art directory, if there is one.
void configure(const std::filesystem::path& art_directory, const std::filesystem::path& output);
// Each pilot's original portrait (battle_assets.portraits: image, palette and the page
// picture's path), for MOD pictures named by pilot number (art/portraits/<pilot>.png).
struct PilotImage { uint16_t image = 0, palette = 0; std::string path; };
void set_pilot_images(std::map<unsigned, PilotImage> images);
// The enabled packages' picture for a page portrait given by its original path, or "".
std::string mod_picture(const std::string& original_path);
// The same by pilot number, for pages whose picture is not the pilot's page portrait (the
// save slots' 64x64 faces), or "".
std::string mod_picture_of(unsigned pilot);
// Render-hook init, after gpu::init (native_gpu.hpp).
void gpu_init();
void shutdown();
// Game thread, right after the original drawer returned.
void rewrite(uint8_t* rdram, const PortraitDraw& draw);
}
