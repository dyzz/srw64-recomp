#pragma once
// The phone's on-screen controller (docs/design/android-port.md §6): where each control
// sits in millimetres, which control a finger is on, and the buttons the held fingers
// press. The shared UI (frontend.cpp) draws it and feeds it SDL's finger events; the
// buttons join the controller's in graphics.cpp, so every page and hint takes them as a
// controller. No SDL here, for the tests.
#include "input_mode.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <string_view>

namespace srw64::touch_pad {

enum class Control : uint8_t { DPad, A, B, L1, L2, R1, R2, Option, Start, Count };
inline constexpr size_t control_count = size_t(Control::Count);

// N64 bits (input_bindings.hpp) and the host's own above them (input_mode.hpp).
namespace bits {
inline constexpr uint32_t A = 0x8000, B = 0x4000, Start = 0x1000, L = 0x0020, R = 0x0010,
    Up = 0x0800, Down = 0x0400, Left = 0x0200, Right = 0x0100;
}

inline constexpr uint32_t mask(Control control) {
    switch (control) {
        case Control::A: return bits::A;
        case Control::B: return bits::B;
        case Control::L1: return bits::L;
        case Control::R1: return bits::R;
        case Control::L2: return input::pad_l2;
        case Control::R2: return input::pad_r2;
        case Control::Option: return input::pad_view;   // the settings window
        case Control::Start: return bits::Start;
        default: return 0;   // the D-pad's bits follow the finger
    }
}

// A control's place in screen pixels: a circle (x, y the centre, w the radius) or a
// rounded bar (x, y the top left, w and h its size).
struct Shape {
    Control control{};
    bool round{};
    float x{}, y{}, w{}, h{};
    std::string_view label;
};

struct Layout {
    std::array<Shape, control_count> shapes{};
    float mm{};   // pixels per millimetre
    const Shape& operator[](Control c) const { return shapes[size_t(c)]; }
};

// Thumb-sized controls at the corners (user, 2026-10-03): the D-pad bottom left, A and B
// bottom right, L2 above L1 and R2 above R1 down the sides, OPTION and START small at the
// bottom centre. Labels stay English in every language.
inline Layout layout(float width, float height, float px_per_mm) {
    Layout out;
    out.mm = px_per_mm;
    const float m = px_per_mm, w = width / m, h = height / m;
    const auto circle = [&](Control c, float x, float y, float r, std::string_view label) {
        out.shapes[size_t(c)] = {c, true, x * m, y * m, r * m, r * m, label};
    };
    const auto bar = [&](Control c, float cx, float cy, float bw, float bh, std::string_view label) {
        out.shapes[size_t(c)] = {c, false, (cx - bw / 2) * m, (cy - bh / 2) * m, bw * m, bh * m, label};
    };
    // A phone held sideways is about 60 mm tall (the Seeker 61): the corners' controls
    // keep at least 5 mm apart there.
    circle(Control::DPad, 16.5f, h - 15, 11.5f, "");
    circle(Control::A, w - 10, h - 17.5f, 6.5f, "A");
    circle(Control::B, w - 23.5f, h - 8, 6, "B");
    bar(Control::L2, 13, 11, 18, 8.5f, "L2");
    bar(Control::L1, 13, 22.5f, 18, 8.5f, "L1");
    bar(Control::R2, w - 13, 11, 18, 8.5f, "R2");
    bar(Control::R1, w - 13, 22.5f, 18, 8.5f, "R1");
    bar(Control::Option, w / 2 - 9, h - 4.5f, 14, 5, "OPTION");
    bar(Control::Start, w / 2 + 9, h - 4.5f, 14, 5, "START");
    return out;
}

// The control under a point, with a little room around each for a thumb.
inline std::optional<Control> hit(const Layout& layout, float x, float y) {
    const float slack = 1.5f * layout.mm;
    for (const auto& s : layout.shapes) {
        if (s.round) {
            const float r = s.w + slack;
            if ((x - s.x) * (x - s.x) + (y - s.y) * (y - s.y) <= r * r) return s.control;
        } else if (x >= s.x - slack && x <= s.x + s.w + slack && y >= s.y - slack && y <= s.y + s.h + slack) {
            return s.control;
        }
    }
    return std::nullopt;
}

// The D-pad's direction for a finger: four ways, by the longer axis from the centre;
// nothing within the dead zone at the middle.
inline uint32_t direction(const Layout& layout, float x, float y) {
    const auto& pad = layout[Control::DPad];
    const float dx = x - pad.x, dy = y - pad.y;
    if (dx * dx + dy * dy < (2.5f * layout.mm) * (2.5f * layout.mm)) return 0;
    if (std::abs(dx) >= std::abs(dy)) return dx < 0 ? bits::Left : bits::Right;
    return dy < 0 ? bits::Up : bits::Down;
}

// The fingers on the controls, by SDL finger id. A finger keeps the control it landed on
// while it moves (a thumb slides), except that a button finger moving onto another button
// takes that one; the D-pad finger follows its direction wherever it goes.
class Fingers {
    struct Held { Control control; uint32_t bits; };
    std::map<int64_t, Held> held;
    static uint32_t bits_for(const Layout& layout, Control control, float x, float y) {
        return control == Control::DPad ? direction(layout, x, y) : mask(control);
    }
public:
    // True when the finger landed on a control (the event is the controller's then).
    bool down(const Layout& layout, int64_t id, float x, float y) {
        const auto control = hit(layout, x, y);
        if (!control) return false;
        held[id] = {*control, bits_for(layout, *control, x, y)};
        return true;
    }
    bool move(const Layout& layout, int64_t id, float x, float y) {
        const auto found = held.find(id);
        if (found == held.end()) return false;
        auto& h = found->second;
        if (h.control != Control::DPad)
            if (const auto over = hit(layout, x, y); over && *over != Control::DPad) h.control = *over;
        h.bits = bits_for(layout, h.control, x, y);
        return true;
    }
    bool up(int64_t id) { return held.erase(id) != 0; }
    void clear() { held.clear(); }
    bool owns(int64_t id) const { return held.contains(id); }
    bool empty() const { return held.empty(); }
    uint32_t buttons() const {
        uint32_t out = 0;
        for (const auto& [id, h] : held) out |= h.bits;
        return out;
    }
    // Which controls show pressed.
    bool pressed(Control control) const {
        for (const auto& [id, h] : held) if (h.control == control && h.bits) return true;
        return false;
    }
};

}
