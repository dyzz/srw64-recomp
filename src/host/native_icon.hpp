#pragma once
#include <cstdint>
// MOD map unit icons (art/icons/<unit>.png of the enabled packages, docs/design/mod-names-
// art-data.md §2.3). The icon drawer (801E2C5C -> 801E2734) writes one 16x16 texture
// rectangle per unit; for an icon a package replaces, the host keeps it as a marker and
// paints the package's picture there, in the side's variant (unit_icon_art.hpp), whether
// the images are HD or original.
namespace srw64::unit_icons {
// The draw hooks: once, after the portraits installed theirs.
void install();
// Render-hook init, after gpu::init (native_gpu.hpp).
void gpu_init();
void shutdown();
// Game thread: the icon drawer wrote [begin, end) for sprite slot `slot`, sub 0, after
// unit_icon_facing::after_draw turned it.
void rewrite(uint8_t* rdram, uint32_t slot, uint32_t begin, uint32_t end);
// Game thread: the drawer wrote a unit's shadow (sub 1) into [begin, end), after
// unit_shadow::after_draw moved a ground unit's. With the HD images it is drawn as a soft
// ellipse made here (no HD pack texture); the original images keep the ROM's.
void rewrite_shadow(uint8_t* rdram, uint32_t begin, uint32_t end);
}
