#pragma once
// Touch controls for phones by scene (docs/design/touch-controls.md): like a MOBA game,
// a stick wherever the left thumb lands, and a few buttons named for what they do in the
// scene at hand, always in the same places. Each button sends N64 and host bits, so the
// game, our pages and the hints take them as a controller. The layout is in millimetres;
// the shared UI (frontend.cpp) draws it, feeds it SDL's finger events and names the
// buttons in the reading language. No SDL here, for the tests.
#include "input_mode.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <string_view>

namespace srw64::touch_pad {

// N64 bits (input_bindings.hpp) and the host's own above them (input_mode.hpp).
namespace bits {
inline constexpr uint32_t A = 0x8000, B = 0x4000, Start = 0x1000, L = 0x0020, R = 0x0010,
    Up = 0x0800, Down = 0x0400, Left = 0x0200, Right = 0x0100;
inline constexpr uint32_t L2 = input::pad_l2, R2 = input::pad_r2, Option = input::pad_view;
}

// The places a button can take: the primary bottom right, the back button left of it,
// two more on the arc above, four small ones along the top edge (the first one always
// the settings window).
enum class Slot : uint8_t { Primary, Back, Arc2, Arc3, Top1, Top2, Top3, Top4, Count };
inline constexpr size_t slot_count = size_t(Slot::Count);

struct Action {
    uint32_t bits = 0;          // the keys it holds down
    std::string_view label;     // a UI label key (content/locales/*.json "ui")
    std::string_view command;   // or a page of ours it opens when let go (frontend.cpp choose())
    bool filled() const { return bits || !command.empty(); }
    bool operator==(const Action&) const = default;
};

enum class Stick : uint8_t {
    None,     // no stick
    Wide,     // anywhere in the left part of the screen, centred where the thumb lands
    Corner,   // fixed in the bottom left corner (our pages: their lists stay tappable)
    Hidden,   // the wide area, not drawn (dialogue: drag to read back)
};

enum class SceneId : uint8_t {
    Other,        // not recognised: everything, so no screen lacks a button
    Hidden,       // the settings window, the Library, the MOD manager: all touch
    Page,         // our pages: the battle confirmation, intermission, saves, title lists
    Attract,      // the logo, the opening, PRESS START
    TitleRing,
    Prologue,
    Dialogue,
    Choice,
    MapIdle,
    MapMenu,
    MoveSelect,
    TargetList,
    InfoWindow,
    GridList,
    BattleScene,
    AnyKey,       // results, defeat, the ending: continue
    Count
};

struct Scene {
    Stick stick = Stick::None;
    std::array<Action, slot_count> slots{};
    bool tap_primary = false;   // a touch outside the controls is the primary button
    const Action& operator[](Slot s) const { return slots[size_t(s)]; }
};

// docs/design/touch-controls.md §3.
inline Scene scene(SceneId id) {
    Scene s;
    auto set = [&](Slot slot, uint32_t b, std::string_view label) { s.slots[size_t(slot)] = {b, label, {}}; };
    auto open = [&](Slot slot, std::string_view command, std::string_view label) { s.slots[size_t(slot)] = {0, label, command}; };
    const auto settings = [&] { set(Slot::Top1, bits::Option, "touch_settings"); };
    switch (id) {
    case SceneId::Hidden:
        break;
    case SceneId::Other:
        s.stick = Stick::Wide;
        set(Slot::Primary, bits::A, "touch_ok"); set(Slot::Back, bits::B, "touch_back");
        set(Slot::Arc2, bits::L, "touch_l1"); set(Slot::Arc3, bits::R, "touch_r1");
        settings(); set(Slot::Top2, bits::Start, "touch_start");
        set(Slot::Top3, bits::L2, "touch_l2"); set(Slot::Top4, bits::R2, "touch_r2");
        break;
    case SceneId::Page:
        s.stick = Stick::Corner;
        set(Slot::Primary, bits::A, "touch_ok"); set(Slot::Back, bits::B, "touch_back");
        settings();
        break;
    // The title's corner buttons (the Library, MOD) become touch buttons along the top.
    case SceneId::Attract:
        set(Slot::Primary, bits::Start, "touch_start"); settings();
        open(Slot::Top3, "library-open", "library_open"); open(Slot::Top4, "mod-open", "mod_open");
        s.tap_primary = true;
        break;
    case SceneId::TitleRing:
        s.stick = Stick::Wide;
        set(Slot::Primary, bits::Start, "touch_ok"); settings();
        open(Slot::Top3, "library-open", "library_open"); open(Slot::Top4, "mod-open", "mod_open");
        break;
    case SceneId::Prologue:
        set(Slot::Primary, bits::A, "touch_next_page"); set(Slot::Arc2, bits::R | bits::Start, "touch_skip"); settings();
        s.tap_primary = true;
        break;
    case SceneId::Dialogue:
        s.stick = Stick::Hidden;
        set(Slot::Primary, bits::A, "touch_next_line");
        set(Slot::Arc2, bits::R2, "touch_fast"); set(Slot::Arc3, bits::L2, "touch_auto");
        settings(); set(Slot::Top2, bits::R | bits::Start, "touch_skip"); set(Slot::Top3, bits::L, "touch_history");
        s.tap_primary = true;
        break;
    case SceneId::Choice:
        s.stick = Stick::Wide;
        set(Slot::Primary, bits::A, "touch_ok"); settings();
        break;
    case SceneId::MapIdle:
        s.stick = Stick::Wide;
        set(Slot::Primary, bits::A, "touch_select"); set(Slot::Back, bits::B, "touch_info");
        set(Slot::Arc2, bits::L, "touch_prev_unit"); set(Slot::Arc3, bits::R, "touch_next_unit");
        settings(); set(Slot::Top3, bits::L2, "touch_prev_enemy"); set(Slot::Top4, bits::R2, "touch_next_enemy");
        break;
    case SceneId::MapMenu:
        s.stick = Stick::Wide;
        set(Slot::Primary, bits::A, "touch_ok"); set(Slot::Back, bits::B, "touch_back"); settings();
        break;
    case SceneId::MoveSelect:
        s.stick = Stick::Wide;
        set(Slot::Primary, bits::A, "touch_move_here"); set(Slot::Back, bits::B, "touch_cancel");
        set(Slot::Arc2, bits::R, "touch_farthest"); settings();
        break;
    case SceneId::TargetList:
        s.stick = Stick::Wide;
        set(Slot::Primary, bits::A, "touch_ok"); set(Slot::Back, bits::B, "touch_back");
        set(Slot::Arc2, bits::L, "touch_prev_target"); set(Slot::Arc3, bits::R, "touch_next_target"); settings();
        break;
    case SceneId::InfoWindow:
        s.stick = Stick::Wide;
        set(Slot::Primary, bits::A, "touch_ok"); set(Slot::Back, bits::B, "touch_close");
        set(Slot::Arc2, bits::L, "touch_prev_unit"); set(Slot::Arc3, bits::R, "touch_next_unit"); settings();
        break;
    case SceneId::GridList:
        s.stick = Stick::Wide;
        set(Slot::Primary, bits::A, "touch_ok"); set(Slot::Back, bits::B, "touch_back");
        set(Slot::Arc2, bits::L, "touch_prev_page"); set(Slot::Arc3, bits::R, "touch_next_page"); settings();
        break;
    case SceneId::BattleScene:
        set(Slot::Primary, bits::R2, "touch_skip_battle"); settings();
        break;
    case SceneId::AnyKey:
        set(Slot::Primary, bits::A, "touch_continue"); settings();
        s.tap_primary = true;
        break;
    default:
        break;
    }
    return s;
}

// A control's place in screen pixels: a circle (x, y the centre, r the radius) or a
// rounded bar (x, y its centre, w and h its size).
struct Place {
    bool round{};
    float x{}, y{}, r{}, w{}, h{};
};

struct Layout {
    float mm{};                      // pixels per millimetre
    float width{}, height{};
    std::array<Place, slot_count> slots{};
    float stick_radius{};
    float rest_x{}, rest_y{};        // where the stick rests, and the corner stick's centre
    float wide_right{}, wide_top{};  // the wide stick area: left of this, below this
    float corner_size{};             // the corner stick area, a square in the bottom left
    const Place& operator[](Slot s) const { return slots[size_t(s)]; }
};

// About 60 mm tall held sideways (the Seeker 61): everything fits that.
inline Layout layout(float width, float height, float px_per_mm) {
    Layout out;
    const float m = px_per_mm, w = width / m, h = height / m;
    out.mm = m; out.width = width; out.height = height;
    const auto circle = [&](Slot s, float x, float y, float r) { out.slots[size_t(s)] = {true, x * m, y * m, r * m, 2 * r * m, 2 * r * m}; };
    const auto bar = [&](Slot s, float x, float y, float bw, float bh) { out.slots[size_t(s)] = {false, x * m, y * m, 0, bw * m, bh * m}; };
    // The primary button and an arc of three around it.
    const float px = w - 12, py = h - 14, arc = 19;
    const float pi = 3.14159265f;
    const auto on_arc = [&](float degrees, float& x, float& y) {
        x = px + arc * std::cos(degrees * pi / 180); y = py - arc * std::sin(degrees * pi / 180);
    };
    float x, y;
    circle(Slot::Primary, px, py, 8);
    on_arc(180, x, y); circle(Slot::Back, x, y, 6.5f);
    on_arc(130, x, y); circle(Slot::Arc2, x, y, 6);
    on_arc(85, x, y); circle(Slot::Arc3, x, y, 6);
    bar(Slot::Top1, 11, 6, 18, 6); bar(Slot::Top2, 31, 6, 18, 6);
    bar(Slot::Top3, w - 31, 6, 18, 6); bar(Slot::Top4, w - 11, 6, 18, 6);
    out.stick_radius = 10 * m;
    out.rest_x = 16.5f * m; out.rest_y = (h - 15) * m;
    out.wide_right = width * .42f; out.wide_top = 12 * m;
    out.corner_size = 34 * m;
    return out;
}

// The slot under a point, among those the scene fills, with a little room for a thumb.
inline std::optional<Slot> hit(const Layout& layout, const Scene& scene, float x, float y) {
    const float slack = 1.5f * layout.mm;
    for (size_t i = 0; i < slot_count; ++i) {
        if (!scene.slots[i].filled()) continue;
        const auto& p = layout.slots[i];
        const bool inside = p.round ? (x - p.x) * (x - p.x) + (y - p.y) * (y - p.y) <= (p.r + slack) * (p.r + slack)
                                    : std::abs(x - p.x) <= p.w / 2 + slack && std::abs(y - p.y) <= p.h / 2 + slack;
        if (inside) return Slot(i);
    }
    return std::nullopt;
}

inline bool in_stick_area(const Layout& layout, const Scene& scene, float x, float y) {
    switch (scene.stick) {
    case Stick::Wide: case Stick::Hidden: return x < layout.wide_right && y > layout.wide_top;
    case Stick::Corner: return x < layout.corner_size && y > layout.height - layout.corner_size;
    default: return false;
    }
}

// Four ways, by the longer axis from the stick's centre; nothing within the dead zone.
inline uint32_t direction(const Layout& layout, float cx, float cy, float x, float y) {
    const float dx = x - cx, dy = y - cy;
    if (dx * dx + dy * dy < (2.5f * layout.mm) * (2.5f * layout.mm)) return 0;
    if (std::abs(dx) >= std::abs(dy)) return dx < 0 ? bits::Left : bits::Right;
    return dy < 0 ? bits::Up : bits::Down;
}

// The fingers, by SDL finger id. A finger keeps what it landed on while it moves: the
// stick follows its direction from where the thumb came down (from the corner stick's
// centre on our pages); a button finger moving onto another button takes that one.
class Fingers {
public:
    enum class Kind : uint8_t { Button, Stick, Tap };
    struct Finger {
        Kind kind{};
        Slot slot{};
        float cx{}, cy{}, x{}, y{};   // stick: centre and thumb
        uint32_t bits{};
    };
private:
    std::map<int64_t, Finger> held;
public:
    // True when the finger is the controls' (a button, the stick or the tap area).
    bool down(const Layout& layout, const Scene& scene, int64_t id, float x, float y) {
        if (const auto slot = hit(layout, scene, x, y)) {
            held[id] = {Kind::Button, *slot, 0, 0, x, y, scene[*slot].bits};
            return true;
        }
        if (in_stick_area(layout, scene, x, y)) {
            const bool corner = scene.stick == Stick::Corner;
            const float cx = corner ? layout.rest_x : x, cy = corner ? layout.rest_y : y;
            held[id] = {Kind::Stick, {}, cx, cy, x, y, direction(layout, cx, cy, x, y)};
            return true;
        }
        if (scene.tap_primary && scene[Slot::Primary].bits) {
            held[id] = {Kind::Tap, Slot::Primary, 0, 0, x, y, scene[Slot::Primary].bits};
            return true;
        }
        return false;
    }
    bool move(const Layout& layout, const Scene& scene, int64_t id, float x, float y) {
        const auto found = held.find(id);
        if (found == held.end()) return false;
        auto& f = found->second;
        f.x = x; f.y = y;
        if (f.kind == Kind::Stick) f.bits = direction(layout, f.cx, f.cy, x, y);
        else if (f.kind == Kind::Button)
            if (const auto over = hit(layout, scene, x, y)) { f.slot = *over; f.bits = scene[*over].bits; }
        return true;
    }
    bool up(int64_t id) { return held.erase(id) != 0; }
    void clear() { held.clear(); }
    bool owns(int64_t id) const { return held.contains(id); }
    const Finger* find(int64_t id) const { const auto f = held.find(id); return f == held.end() ? nullptr : &f->second; }
    bool empty() const { return held.empty(); }
    uint32_t buttons() const {
        uint32_t out = 0;
        for (const auto& [id, f] : held) out |= f.bits;
        return out;
    }
    bool pressed(Slot slot) const {
        for (const auto& [id, f] : held) if (f.kind != Kind::Stick && f.slot == slot) return true;
        return false;
    }
    // The stick finger, for drawing the stick under the thumb.
    const Finger* stick() const {
        for (const auto& [id, f] : held) if (f.kind == Kind::Stick) return &f;
        return nullptr;
    }
};

}
