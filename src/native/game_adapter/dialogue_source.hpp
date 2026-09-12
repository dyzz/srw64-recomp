#pragma once
#include "localization/catalog.hpp"

namespace srw64::game_adapter {
// JP resident 8008C9C0 passes a0=0 to text lookup 8008C510 at
// 8008CA5C..8008CA6C. This adapter handles that standard dialogue path only.
// Other table consumers must provide their own table identity, never guess it
// from a numeric text ID or from the translated display string.
inline localization::TextKey standard_dialogue_key(uint16_t id) {
    return localization::TextKey::base(0,id);
}
inline bool dialogue_font_image(uint32_t command,uint32_t header0,uint32_t header1) {
    // The draw path uses the runtime 504x504 atlas, in BOTH ROM variants.
    // Original resource 1 is 504x252 on disk, but is not the bound draw image.
    // Verified against JP live RDRAM and FD4800FB commands, not source headers.
    return command==0xFD4800FB && header0==0x000501F8 &&
        header1==0x01F80000U;
}
}
