#pragma once
// The RT64 texture pack (rt64.json beside the HD art: map unit icons, the dialogue frame, the
// world map's surface, space, battle ground and props) on the GL renderer, which keys the
// tiles it decodes as RT64 does (n64gl::Interpreter::replacements) and draws the pack's
// picture instead, as RT64's texture cache would: the same texture coordinates over the HD
// texels, straight alpha, under the image mode and the map-icon preference.
#include <cstdint>
#include <filesystem>

namespace srw64::hd_replace {
// Reads <art>/rt64.json. Without it nothing is replaced.
void configure(const std::filesystem::path& art);
// Whether the pack has a picture for the key (any thread, after configure).
bool has(uint64_t key);
// The picture's GL texture, loaded the first time (render thread) and sampled as the tile is
// (wrap modes 0 clamp, 1 repeat, 2 mirror; linear or nearest), or 0 when it is not drawn now:
// original images, a map unit icon with HD icons off, or a picture that would not load.
uint32_t texture(uint64_t key, uint8_t wrap_s, uint8_t wrap_t, bool linear, uint16_t& width, uint16_t& height);
// Textures not drawn for a while go when the held ones pass their budget (render thread).
void end_frame();
}  // namespace srw64::hd_replace
