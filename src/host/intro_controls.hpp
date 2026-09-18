#pragma once
#include <cstdint>

namespace srw64::intro {
// Called under the adapter mutex. A chord must start in the text sequence;
// carrying it from a menu into a sequence never requests a skip.
struct Controls {
    static constexpr uint16_t chord=0x1010; // R + START
    bool active{}, pending{};
    uint16_t previous{}, consumed{};
    uint16_t input(uint16_t buttons) {
        consumed &= buttons;
        if(active && (buttons & chord)==chord && (previous & chord)!=chord) {
            pending=true;
            consumed |= chord;
        }
        previous=buttons;
        return buttons & ~consumed;
    }
    void scene(bool value) {
        active=value;
        if(!active)pending=false;
        // Retain consumed buttons until physical release, including after DMA.
    }
    bool take(bool ready) {
        if(!active || !pending || !ready)return false;
        pending=false;active=false;return true;
    }
};
}
