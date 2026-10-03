#pragma once
// Which scene the game is in, for the phone's touch controls (touch_pad.hpp,
// docs/design/touch-controls.md §3-4). The input callback works it out each time the
// game reads the controller, from the game's state in RDRAM, and the shared UI reads the
// result: the UI thread never reads guest memory. A wrong guess only shows other buttons;
// they still send the same N64 keys.
#include "touch_pad.hpp"
#include <atomic>
#include <cstdint>

namespace srw64::touch_scene {

// The top-level mode (800801A4's table index) and the tactical map's UI block
// (docs/gameplay/original-controls.md §3, the tactical UI states).
inline constexpr uint32_t mode_address = 0x8015DA02, map_state_address = 0x80172EB0, map_sub_address = 0x80172EB2;

inline touch_pad::SceneId decide(unsigned mode, unsigned map_state, unsigned map_sub, int title_major, bool dialogue) {
    using touch_pad::SceneId;
    switch (mode) {
    case 2: case 0x1A: case 0x1C:   // the battle animation (its lines run on their own)
        return SceneId::BattleScene;
    case 1: case 7:                 // the logo, the title overlay (opening, PRESS START, prologue pages)
        if (title_major == 3) return SceneId::TitleRing;
        return dialogue ? SceneId::Dialogue : SceneId::Attract;
    case 0x20:                      // the ending
        return SceneId::AnyKey;
    default:
        break;
    }
    // Everywhere else a line waiting to be read comes first (a script's choice shows with
    // its question: drag to choose, tap to answer).
    if (dialogue) return SceneId::Dialogue;
    if (mode == 3 || mode == 0x11 || mode == 0x16 || mode == 0x22) {
        switch (map_state) {
        case 5: case 6: return SceneId::MapIdle;
        case 8: case 0x3A: case 0xD: case 0x16: return SceneId::MapMenu;
        case 0xC: return map_sub == 0 ? SceneId::MoveSelect : SceneId::MapMenu;
        case 0x17: case 0x19: return SceneId::TargetList;
        case 0x1B: case 0x22: case 0x3C: return SceneId::InfoWindow;
        default:
            if (map_state >= 0x2E && map_state <= 0x37) return SceneId::InfoWindow;
            break;
        }
    }
    return SceneId::Other;
}

inline std::atomic<uint8_t>& current() {
    static std::atomic<uint8_t> scene{uint8_t(touch_pad::SceneId::Other)};
    return scene;
}

}
