#pragma once
// What the player can change on the phone controls' layout screen (docs/design/touch-controls.md
// §8): where each slot and the D-pad sit and how big they are. Apart from touch_pad.hpp so that
// presentation_settings.hpp can keep it without touch_pad's enums: their AnyKey and None are
// X11 macros on Linux, where rt64_application.h brings in Xlib ahead of graphics.cpp's headers.
#include <array>
#include <cstddef>
#include <string_view>

namespace srw64::touch_pad {

// One per slot (touch_pad::Slot, in its order) and then the D-pad: a scene's buttons take a
// slot's place, so one layout serves them all. touch_pad.hpp checks the count.
inline constexpr size_t control_count = 14;
inline constexpr size_t dpad_control = control_count - 1;
// presentation.json's touch_layout, in Slot order and then the D-pad.
inline constexpr std::array<std::string_view, control_count> control_ids = {
    "ok", "back", "l1", "r1", "settings", "start", "l2", "r2", "skip", "speed", "info", "load", "shot", "dpad"};
inline constexpr unsigned min_scale = 60, max_scale = 200, scale_step = 5;
inline constexpr float grid_mm = 4;

struct Custom {
    bool moved = false;
    float x = 0, y = 0;     // moved: its centre, a share of the screen's width and height
    unsigned scale = 100;   // percent of its size
    bool operator==(const Custom&) const = default;
};
using Customs = std::array<Custom, control_count>;

}
