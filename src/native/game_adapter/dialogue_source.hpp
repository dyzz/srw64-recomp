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
    // Every glyph sets its own texture image (8008EB5C and the other text passes): resource
    // 0, the 504x504 atlas, below glyph 0x597, and resource 1, the 504x252 continuation
    // with the rarer kanji (侮 悔 燃 ...), from 0x597 on (sltiu 0x597 at 8008EE64). Both are
    // 504 wide, so the command is FD4800FB either way; only the header's height differs.
    return command==0xFD4800FB && header0==0x000501F8 &&
        (header1==0x01F80000U || header1==0x00FC0000U);
}
}
