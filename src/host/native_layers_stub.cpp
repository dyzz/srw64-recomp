// The native HD layers still draw with raw Metal. Until they move to Plume
// (docs/design/three-platform-port.md, X1), other platforms leave every display
// list to RT64 unchanged: the game shows its Original images there.
#include "native_marker.hpp"
#include "native_portrait.hpp"
#include "native_sprite.hpp"

namespace srw64::marker {
void configure(const std::filesystem::path&) {}
bool replacement_enabled() { return false; }
void metal_init(plume::RenderDevice*) {}
void shutdown() {}
}
namespace srw64::portraits {
void configure(const std::filesystem::path&, const std::filesystem::path&) {}
void metal_init(plume::RenderDevice*) {}
void shutdown() {}
void rewrite(uint8_t*, const PortraitDraw&) {}
}
namespace srw64::sprites {
void configure(const std::filesystem::path&, const std::filesystem::path&) {}
void set_text(Describe) {}
void metal_init(plume::RenderDevice*) {}
void shutdown() {}
void rewrite(uint8_t*, const SceneDraw&) {}
void rewrite_grid(uint8_t*, const SceneDraw&) {}
// No native text pass: the caller keeps the original glyphs.
bool place_texts(uint8_t*, uint32_t, const float[4], const std::vector<PlacedText>&) { return false; }
TextCounts text_counts() { return {}; }
}
