// The GL-only host (SRW64_GL_ONLY, the PS Vita's path, docs/design/psv-port.md) does not
// compile the RetroArch filters, which need Plume. Their entry points here do nothing.
#include "post_filter.hpp"

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
