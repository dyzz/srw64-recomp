// The GL-only host (SRW64_GL_ONLY, the PS Vita's path, docs/design/psv-port.md) does not
// compile the layers that need RT64 or Plume: the HD battle backgrounds and portraits and the
// RetroArch filters. Their entry points here leave the original drawing as it is.
#include "native_background.hpp"
#include "native_portrait.hpp"
#include "post_filter.hpp"

namespace srw64::backgrounds {
void initialize(const std::filesystem::path&) {}
void configure(const std::filesystem::path&, const std::filesystem::path&) {}
void gpu_init() {}
void shutdown() {}
bool battle_sky(const uint8_t*, uint32_t, uint32_t) { return false; }
bool rewrite(uint8_t*, const BackgroundDraw&) { return false; }
}  // namespace srw64::backgrounds

namespace srw64::portraits {
void configure(const std::filesystem::path&, const std::filesystem::path&) {}
void gpu_init() {}
void shutdown() {}
void rewrite(uint8_t*, const PortraitDraw&) {}
}  // namespace srw64::portraits

namespace srw64::post_filter {
Status status() { return {}; }
bool active() { return false; }
void init(Backend, const std::filesystem::path&) {}
void apply(plume::RenderCommandList*, plume::RenderFramebuffer*, int, int, int, int, float, bool) {}
void shutdown() {}
}  // namespace srw64::post_filter

// stb_image's implementation, which RT64's texture cache carries in the other hosts.
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
