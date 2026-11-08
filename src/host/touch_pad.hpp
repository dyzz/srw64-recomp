#pragma once
// Touch controls for phones by scene (docs/design/touch-controls.md): a fixed D-pad
// in the bottom left, and buttons named for what they do in the
// scene at hand, always in the same places. Each button sends N64 and host bits, so the
// game, our pages and the hints take them as a controller. The layout is in millimetres,
// and the player can move and resize each control (Custom, Editor);
// the shared UI (frontend.cpp) draws it, feeds it SDL's finger events and names the
// buttons in the reading language. No SDL here, for the tests.
#include "input_mode.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <string_view>

namespace srw64::touch_pad {

// N64 bits (input_bindings.hpp) and the host's own above them (input_mode.hpp).
namespace bits {
inline constexpr uint32_t A = 0x8000, B = 0x4000, Start = 0x1000, L = 0x0020, R = 0x0010, CRight = 0x0001,
    Up = 0x0800, Down = 0x0400, Left = 0x0200, Right = 0x0100;
inline constexpr uint32_t L2 = input::pad_l2, R2 = input::pad_r2, Option = input::pad_view;
}

// The places a button can take: the primary bottom right, the back button left of it,
// two more on the arc above, four small ones along the top edge (the first one always
// the settings window), plus a dialogue skip button above R1, the 2x speed switch
// in the top left (user, 2026-10-11), the stage info page beside it (docs/design/stage-info.md),
// the quick load of the suspend save after that (asked first; user, 2026-10-17) and a
// screenshot under the settings (user, 2026-10-26).
enum class Slot : uint8_t { Primary, Back, Arc2, Arc3, Top1, Top2, Top3, Top4, Skip, Speed, Info, Load, Shot, Count };
inline constexpr size_t slot_count = size_t(Slot::Count);

struct Action {
    uint32_t bits = 0;          // the keys it holds down
    std::string_view label;     // a UI label key (content/locales/*.json "ui")
    std::string_view command;   // or a page of ours it opens when let go (frontend.cpp choose())
    uint32_t long_bits = 0;     // held long_press_ms: these keys instead (its own keys wait for a short press)
    bool filled() const { return bits || !command.empty(); }
    bool operator==(const Action&) const = default;
};

// A press this long on a button with long_bits holds those keys; let go sooner, the button
// presses its own keys then, briefly (frontend.cpp touch_keep).
inline constexpr uint64_t long_press_ms = 500;

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
    Attract,      // PRESS START waiting
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
    BattlePage,   // the pre-battle page, laid out for touch: its buttons are these (user, 2026-10-03)
    BattleSpirits,// its spirit list
    Opening,      // the logo, the works flying past, a demo's unit coming in: START only
    FixedPage,    // fixed buttons: the whole set on our pages, the stick in the corner
    FixedTitle,   // fixed buttons: the whole set at the title, the Library and Battle Viewer for L2 and R2
    FixedDialogue,// fixed top row; auto/hold-fast, skip above R1; B position follows reading state
    Count
};

// Buttons by scene, or the same whole set everywhere (user, 2026-10-08: some screens
// lacked a button they needed). The scene table and its recognition stay for later.
inline constexpr bool by_scene = false;

// A scene's fixed counterpart: the whole set (Other), on our pages with the stick in the
// corner so their lists stay tappable, at the title with its two entries.
inline SceneId fixed(SceneId id) {
    switch (id) {
    case SceneId::Hidden: return SceneId::Hidden;
    case SceneId::Page: case SceneId::BattlePage: case SceneId::BattleSpirits: return SceneId::FixedPage;
    case SceneId::Attract: case SceneId::TitleRing: return SceneId::FixedTitle;
    // The skip is R + START together, out of one thumb's reach (a player, 2026-10-09).
    case SceneId::Dialogue: case SceneId::Prologue: return SceneId::FixedDialogue;
    default: return SceneId::Other;
    }
}

struct Scene {
    Stick stick = Stick::None;
    std::array<Action, slot_count> slots{};
    bool tap_primary = false;   // a touch outside the controls is the primary button
    const Action& operator[](Slot s) const { return slots[size_t(s)]; }
    bool operator==(const Scene&) const = default;
};

enum class Reading : uint8_t { Inactive, Active, History, Skipping };

// docs/design/touch-controls.md §3.
inline Scene scene(SceneId id, Reading reading = Reading::Active) {
    Scene s;
    auto set = [&](Slot slot, uint32_t b, std::string_view label) { s.slots[size_t(slot)] = {b, label, {}}; };
    auto open = [&](Slot slot, std::string_view command, std::string_view label) { s.slots[size_t(slot)] = {0, label, command}; };
    // The settings window, the game speed (game_speed.hpp) and a screenshot wherever there are controls.
    const auto settings = [&] {
        set(Slot::Top1, bits::Option, "touch_settings"); open(Slot::Speed, "speed-toggle", "touch_speed");
        open(Slot::Shot, "screenshot", "touch_screenshot");
    };
    switch (id) {
    case SceneId::Hidden:
        break;
    case SceneId::Other: case SceneId::FixedPage: case SceneId::FixedTitle: case SceneId::FixedDialogue:
        s.stick = id == SceneId::FixedPage ? Stick::Corner : Stick::Wide;
        set(Slot::Primary, bits::A, "touch_ok"); set(Slot::Back, bits::B, "touch_back");
        set(Slot::Arc2, bits::L, "touch_l1"); set(Slot::Arc3, bits::R, "touch_r1");
        settings(); set(Slot::Top2, bits::Start, "touch_start");
        set(Slot::Top3, bits::L2, "touch_l2"); set(Slot::Top4, bits::R2, "touch_r2");
        // The title's Library, Battle Viewer and MOD are its own corner, as on a desktop (user, 2026-11-03).
        if (id != SceneId::FixedTitle) {
            // The stage info page: there is a stage everywhere but the title.
            open(Slot::Info, "stage-info-open", "touch_stage_info");
            // Back to the suspend save; frontend.cpp leaves it out where the game would not take it.
            open(Slot::Load, "quick-load-ask", "touch_quick_load");
        }
        // Back held: the map overview (C-right on the idle map, the phone's only way to it;
        // user, 2026-10-31). Only on the game's own screens, where C-right is at most "any key".
        if (id == SceneId::Other) s.slots[size_t(Slot::Back)].long_bits = bits::CRight;
        if (id == SceneId::FixedDialogue) {
            // Keep START, L2 and R2 in place (user, 2026-10-09). The two round
            // shoulder buttons use the reader's auto toggle and hold-fast inputs.
            set(Slot::Arc2, bits::L2, "touch_auto"); set(Slot::Arc3, bits::R2, "touch_hold_fast");
            set(Slot::Skip, bits::R | bits::Start, "touch_skip");
        }
        break;
    case SceneId::Page:
        s.stick = Stick::Corner;
        set(Slot::Primary, bits::A, "touch_ok"); set(Slot::Back, bits::B, "touch_back");
        settings();
        break;
    // The title's corner (the Library, the Battle Viewer, MOD) is tapped where it is, as on a desktop.
    case SceneId::Attract:
        set(Slot::Primary, bits::Start, "touch_start"); settings();
        s.tap_primary = true;
        break;
    case SceneId::Opening:
        set(Slot::Primary, bits::Start, "touch_start"); settings();
        s.tap_primary = true;
        break;
    case SceneId::TitleRing:
        s.stick = Stick::Wide;
        set(Slot::Primary, bits::Start, "touch_ok"); settings();
        break;
    case SceneId::Prologue:
        set(Slot::Primary, bits::A, "touch_next_page"); set(Slot::Arc2, bits::R | bits::Start, "touch_skip"); settings();
        s.tap_primary = true;
        break;
    case SceneId::Dialogue:
        s.stick = Stick::Hidden;
        set(Slot::Primary, bits::A, "touch_next_line");
        set(Slot::Arc2, bits::L2, "touch_auto"); set(Slot::Arc3, bits::R2, "touch_hold_fast");
        settings(); set(Slot::Skip, bits::R | bits::Start, "touch_skip"); set(Slot::Top3, bits::L, "touch_history");
        s.tap_primary = true;
        break;
    case SceneId::Choice:
        s.stick = Stick::Wide;
        set(Slot::Primary, bits::A, "touch_ok"); settings();
        break;
    case SceneId::MapIdle:
        s.stick = Stick::Wide;
        set(Slot::Primary, bits::A, "touch_select"); set(Slot::Back, bits::B, "touch_info");
        s.slots[size_t(Slot::Back)].long_bits = bits::CRight;
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
    // The page's own buttons become these; the corner stick picks the response left and right.
    case SceneId::BattlePage:
        s.stick = Stick::Corner;
        open(Slot::Primary, "battle-confirm", "battle_confirm"); set(Slot::Back, bits::B, "touch_back");
        open(Slot::Arc2, "battle-weapon", "battle_change_weapon"); open(Slot::Arc3, "battle-spirits", "battle_spirits");
        settings(); open(Slot::Top4, "battle-animation", "touch_animation");
        break;
    case SceneId::BattleSpirits:
        s.stick = Stick::Corner;
        set(Slot::Primary, bits::A, "touch_ok"); set(Slot::Back, bits::B, "touch_back"); settings();
        break;
    default:
        break;
    }
    if (id == SceneId::FixedDialogue && reading != Reading::Inactive) {
        // The former B position opens history; while skipping it keeps B's cancel.
        set(Slot::Back, reading == Reading::Active ? bits::L : bits::B,
            reading == Reading::Active ? "touch_history" : reading == Reading::Skipping ? "touch_stop_skip" : "touch_back");
        if (reading == Reading::History)
            for (const auto slot : {Slot::Arc2, Slot::Arc3, Slot::Skip, Slot::Top3, Slot::Top4})
                s.slots[size_t(slot)] = {};
    }
    return s;
}

// A control's place in screen pixels: a circle (x, y the centre, r the radius) or a
// rounded bar (x, y its centre, w and h its size).
struct Place {
    bool round{};
    float x{}, y{}, r{}, w{}, h{};
    float scale = 1;    // the player's size, for its label
};

struct Layout {
    float mm{};                      // pixels per millimetre
    float width{}, height{};
    std::array<Place, slot_count> slots{};
    float stick_radius{};
    float rest_x{}, rest_y{};        // where the stick rests, and the corner stick's centre
    float wide_right{}, wide_top{};  // the wide stick area: left of this, below this
    float corner_size{};             // the corner stick area, a square in the bottom left
    float dpad_cell{};               // a cross of five squares, fixed at rest_x/rest_y
    const Place& operator[](Slot s) const { return slots[size_t(s)]; }
};

// What the player can change on the layout screen (docs/design/touch-controls.md §8), for
// each slot and the D-pad: a scene's buttons take a slot's place, so one layout serves them all.
inline constexpr size_t control_count = slot_count + 1;
inline constexpr size_t dpad_control = slot_count;
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

// How far a control's centre stays from the screen's edges, so all of it shows.
inline float kept(float at, float half, float size) { return half * 2 >= size ? size / 2 : std::clamp(at, half, size - half); }

// About 60 mm tall held sideways (the Seeker 61): everything fits that.
inline Layout layout(float width, float height, float px_per_mm, const Customs& customs = {}) {
    Layout out;
    const float m = px_per_mm, w = width / m, h = height / m;
    out.mm = m; out.width = width; out.height = height;
    const auto circle = [&](Slot s, float x, float y, float r) { out.slots[size_t(s)] = {true, x * m, y * m, r * m, 2 * r * m, 2 * r * m}; };
    const auto bar = [&](Slot s, float x, float y, float bw, float bh) { out.slots[size_t(s)] = {false, x * m, y * m, 0, bw * m, bh * m}; };
    // The primary button and an arc of three around it.
    // Sizes after the user's first try (2026-10-03: a little big): 13 mm primary, 9-10 mm others.
    const float px = w - 10, py = h - 11.5f, arc = 15.5f;
    const float pi = 3.14159265f;
    const auto on_arc = [&](float degrees, float& x, float& y) {
        x = px + arc * std::cos(degrees * pi / 180); y = py - arc * std::sin(degrees * pi / 180);
    };
    float x, y;
    circle(Slot::Primary, px, py, 6.5f);
    on_arc(180, x, y); circle(Slot::Back, x, y, 5);
    on_arc(130, x, y); circle(Slot::Arc2, x, y, 4.6f);
    on_arc(85, x, y); circle(Slot::Arc3, x, y, 4.6f);
    circle(Slot::Skip, x, y - 12.2f, 4.6f);
    // Settings is everywhere but seldom wanted: small, tucked in the corner (user, 2026-10-03).
    bar(Slot::Top1, 6.5f, 3.5f, 10, 4); bar(Slot::Top2, 20.5f, 5, 15, 5);
    bar(Slot::Top3, w - 26.5f, 5, 15, 5); bar(Slot::Top4, w - 9.5f, 5, 15, 5);
    bar(Slot::Speed, 33.5f, 3.5f, 8, 4);   // small as settings, right of START
    bar(Slot::Info, 44.5f, 3.5f, 9, 4);    // the stage info page, right of 2x
    bar(Slot::Load, 55.5f, 3.5f, 9, 4);    // the quick load, right of it
    bar(Slot::Shot, 6.5f, 9, 10, 4);       // the screenshot, under settings
    out.stick_radius = 8.5f * m;
    out.rest_x = 14.5f * m; out.rest_y = (h - 13) * m;
    out.wide_right = width * .42f; out.wide_top = 10 * m;
    out.corner_size = 30 * m;
    out.dpad_cell = 8 * m;
    for (size_t i = 0; i < slot_count; ++i) {
        const auto& c = customs[i];
        auto& p = out.slots[i];
        p.scale = c.scale / 100.f; p.r *= p.scale; p.w *= p.scale; p.h *= p.scale;
        if (c.moved) { p.x = c.x * width; p.y = c.y * height; }
        p.x = kept(p.x, p.w / 2, width); p.y = kept(p.y, p.h / 2, height);
    }
    const auto& dpad = customs[dpad_control];
    out.dpad_cell *= dpad.scale / 100.f;
    if (dpad.moved) { out.rest_x = dpad.x * width; out.rest_y = dpad.y * height; }
    out.rest_x = kept(out.rest_x, 1.5f * out.dpad_cell, width); out.rest_y = kept(out.rest_y, 1.5f * out.dpad_cell, height);
    return out;
}

inline bool inside(const Place& p, float x, float y, float slack) {
    return p.round ? (x - p.x) * (x - p.x) + (y - p.y) * (y - p.y) <= (p.r + slack) * (p.r + slack)
                   : std::abs(x - p.x) <= p.w / 2 + slack && std::abs(y - p.y) <= p.h / 2 + slack;
}

// The slot under a point, among those the scene fills, with a little room for a thumb. The
// player may set buttons over each other: the one whose centre is nearest takes the touch.
inline std::optional<Slot> hit(const Layout& layout, const Scene& scene, float x, float y) {
    const float slack = 1.5f * layout.mm;
    std::optional<Slot> found;
    float nearest = 0;
    for (size_t i = 0; i < slot_count; ++i) {
        const auto& p = layout.slots[i];
        if (!scene.slots[i].filled() || !inside(p, x, y, slack)) continue;
        const float d = std::hypot(x - p.x, y - p.y);
        if (!found || d < nearest) { found = Slot(i); nearest = d; }
    }
    return found;
}

// The cross's whole square and a thumb's room: a touch in a gap between the arms is the
// D-pad's (no direction), never a tap on the page under it, which took the page's focus
// away (a player, 2026-10-12).
inline bool in_stick_area(const Layout& layout, const Scene& scene, float x, float y) {
    if (scene.stick == Stick::None) return false;
    const float reach = 1.5f * layout.dpad_cell + 1.5f * layout.mm;
    return std::abs(x - layout.rest_x) <= reach && std::abs(y - layout.rest_y) <= reach;
}

// Four fixed arms; the centre, corners and outside the cross release the direction. A
// direction held keeps a thumb's room more (1.5 mm) before it lets go, so a thumb rocking on
// an arm's edge is not a second press (a player, 2026-10-12: one tap moved two rows).
inline uint32_t direction(const Layout& layout, float cx, float cy, float x, float y, uint32_t held = 0) {
    const float dx = x - cx, dy = y - cy, half = layout.dpad_cell / 2, room = 1.5f * layout.mm;
    const auto arm = [&](float along, float across, float more) {
        return std::abs(across) <= half + more && std::abs(along) > half - more && std::abs(along) <= 3 * half + more;
    };
    if ((held & (bits::Up | bits::Down)) && arm(dy, dx, room) && (dy < 0) == bool(held & bits::Up)) return held & (bits::Up | bits::Down);
    if ((held & (bits::Left | bits::Right)) && arm(dx, dy, room) && (dx < 0) == bool(held & bits::Left)) return held & (bits::Left | bits::Right);
    if (arm(dy, dx, 0)) return dy < 0 ? bits::Up : bits::Down;
    if (arm(dx, dy, 0)) return dx < 0 ? bits::Left : bits::Right;
    return 0;
}

// The fingers, by SDL finger id. A finger keeps what it landed on while it moves: the
// direction uses the fixed cross; a button finger moving onto another button takes that one.
class Fingers {
public:
    enum class Kind : uint8_t { Button, Stick, Tap, Blocked };
    struct Finger {
        Kind kind{};
        Slot slot{};
        float cx{}, cy{}, x{}, y{};   // stick: centre and thumb
        uint32_t bits{};
        bool long_press{};             // its long_bits are held: letting go opens nothing
        uint32_t on_release{};         // a button with long_bits, let go before long_press_ms: its keys
    };
private:
    std::map<int64_t, Finger> held;
public:
    // True when the finger is the controls' (a button, the stick or the tap area).
    bool down(const Layout& layout, const Scene& scene, int64_t id, float x, float y) {
        if (const auto slot = hit(layout, scene, x, y)) {
            held[id] = {Kind::Button, *slot, 0, 0, x, y, scene[*slot].long_bits ? 0 : scene[*slot].bits};
            held[id].on_release = scene[*slot].long_bits ? scene[*slot].bits : 0;
            return true;
        }
        if (in_stick_area(layout, scene, x, y)) {
            const float cx = layout.rest_x, cy = layout.rest_y;
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
        if (f.kind == Kind::Blocked || f.long_press) return true;
        f.x = x; f.y = y;
        if (f.kind == Kind::Stick) f.bits = direction(layout, f.cx, f.cy, x, y, f.bits);
        else if (f.kind == Kind::Button)
            if (const auto over = hit(layout, scene, x, y)) {
                f.slot = *over;
                f.bits = scene[*over].long_bits ? 0 : scene[*over].bits;
                f.on_release = scene[*over].long_bits ? scene[*over].bits : 0;
            }
        return true;
    }
    bool up(int64_t id) { return held.erase(id) != 0; }
    // A button with long_bits held long enough: it holds them from now on, and letting go
    // presses nothing.
    bool lengthen(const Scene& scene, int64_t id) {
        const auto found = held.find(id);
        if (found == held.end()) return false;
        auto& f = found->second;
        if (f.kind != Kind::Button || f.long_press || !scene[f.slot].long_bits) return false;
        f.long_press = true; f.bits = scene[f.slot].long_bits; f.on_release = 0;
        return true;
    }
    // A held finger must not become a different action when the reader changes mode.
    // Keep ownership until release, so its synthetic mouse-up cannot reach a page.
    void reconcile(const Scene& before, const Scene& after) {
        for (auto& [id, f] : held) {
            const bool changed = f.kind == Kind::Button ? !(before[f.slot] == after[f.slot]) :
                f.kind == Kind::Tap ? !(before[Slot::Primary] == after[Slot::Primary]) || !after.tap_primary :
                f.kind == Kind::Stick && after.stick == Stick::None;
            if (changed) { f.kind = Kind::Blocked; f.bits = 0; f.on_release = 0; }
        }
    }
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
        for (const auto& [id, f] : held) if ((f.kind == Kind::Button || f.kind == Kind::Tap) && f.slot == slot) return true;
        return false;
    }
    // The stick finger, for drawing the stick under the thumb.
    const Finger* stick() const {
        for (const auto& [id, f] : held) if (f.kind == Kind::Stick) return &f;
        return nullptr;
    }
};

// The layout screen shows every control at once: the whole set, and the skip only dialogue has.
inline Scene edit_scene() {
    auto s = scene(SceneId::FixedDialogue, Reading::Inactive);
    s.slots[size_t(Slot::Arc2)] = scene(SceneId::Other)[Slot::Arc2];
    s.slots[size_t(Slot::Arc3)] = scene(SceneId::Other)[Slot::Arc3];
    s.stick = Stick::Wide; s.tap_primary = false;
    return s;
}

// What a slot is in dialogue where its usual button is another: the layout screen names
// both, so all the buttons there are show (a player, 2026-11-07). A short name: it shares the button.
inline std::string_view dialogue_label(size_t slot) {
    switch (Slot(slot)) {
    case Slot::Back: return "touch_history";
    case Slot::Arc2: return "touch_auto";
    case Slot::Arc3: return "touch_fast";
    default: return {};
    }
}

// The control under a point on the layout screen, the nearest if several.
inline std::optional<size_t> control_at(const Layout& layout, float x, float y) {
    const float slack = 1.5f * layout.mm;
    std::optional<size_t> found;
    float nearest = 0;
    const auto consider = [&](size_t control, float cx, float cy) {
        const float d = std::hypot(x - cx, y - cy);
        if (!found || d < nearest) { found = control; nearest = d; }
    };
    for (size_t i = 0; i < slot_count; ++i)
        if (inside(layout.slots[i], x, y, slack)) consider(i, layout.slots[i].x, layout.slots[i].y);
    const float reach = 1.5f * layout.dpad_cell + slack;
    if (std::abs(x - layout.rest_x) <= reach && std::abs(y - layout.rest_y) <= reach) consider(dpad_control, layout.rest_x, layout.rest_y);
    return found;
}

// The layout screen's working copy, kept apart until the player keeps it. One pointer at a
// time; the Layout it is given is the one made from customs().
class Editor {
    Customs now;
    std::optional<size_t> chosen;
    bool pressed = false, moving = false;
    float start_x{}, start_y{}, grab_x{}, grab_y{};
public:
    bool grid = false;
    explicit Editor(const Customs& start) : now(start) {}
    const Customs& customs() const { return now; }
    std::optional<size_t> selected() const { return chosen; }
    bool dragging() const { return moving; }
    // Down on a control chooses it, and it follows the pointer once that has moved a little,
    // so a tap to choose it does not nudge it. Down on nothing chooses nothing: the size
    // slider is then all the controls'.
    void press(const Layout& layout, float x, float y) {
        chosen = control_at(layout, x, y);
        pressed = chosen.has_value(); moving = false;
        if (!chosen) return;
        const bool dpad = *chosen == dpad_control;
        start_x = x; start_y = y;
        grab_x = (dpad ? layout.rest_x : layout.slots[*chosen].x) - x;
        grab_y = (dpad ? layout.rest_y : layout.slots[*chosen].y) - y;
    }
    void move(const Layout& layout, float x, float y) {
        if (!pressed) return;
        if (!moving && std::hypot(x - start_x, y - start_y) < layout.mm) return;
        moving = true;
        float cx = x + grab_x, cy = y + grab_y;
        if (grid) {
            const float step = grid_mm * layout.mm;
            cx = std::round(cx / step) * step; cy = std::round(cy / step) * step;
        }
        const bool dpad = *chosen == dpad_control;
        const auto& p = layout.slots[dpad ? 0 : *chosen];
        const float half_w = dpad ? 1.5f * layout.dpad_cell : p.w / 2, half_h = dpad ? 1.5f * layout.dpad_cell : p.h / 2;
        auto& c = now[*chosen];
        c.moved = true;
        c.x = kept(cx, half_w, layout.width) / layout.width; c.y = kept(cy, half_h, layout.height) / layout.height;
    }
    void release() { pressed = moving = false; }
    // With none chosen: the size they all share, or 100 % when each has its own.
    unsigned scale() const {
        if (chosen) return now[*chosen].scale;
        for (const auto& c : now) if (c.scale != now[0].scale) return 100;
        return now[0].scale;
    }
    void set_scale(unsigned percent) {
        percent = std::clamp(percent, min_scale, max_scale);
        if (chosen) now[*chosen].scale = percent;
        else for (auto& c : now) c.scale = percent;
    }
    void reset() { now = {}; chosen.reset(); }
};

}
