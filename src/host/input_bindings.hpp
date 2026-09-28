#pragma once
// Remappable controls (docs/native/controls-remapping.md). Every N64 input and the
// host's own buttons is an Action; the keyboard and the controller each bind a list of
// inputs to it. The controller's defaults suit the Steam Deck; the keyboard's put each
// function where PCSX2 puts the controller button that has it. The window thread
// turns the pressed inputs into the N64 mask (bits 0-15, stick bits 16-19) and the host
// bits of input_mode.hpp every frame; the settings window's Controls page changes the
// bindings and presentation_settings.cpp keeps them in the user's input.json.
//
// No SDL here: the key and controller numbers below are SDL's own values (USB HID
// scancodes, SDL_GameControllerButton / Axis), which graphics.cpp static_asserts.
#include "input_mode.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <vector>

namespace srw64::input {

enum class Action : uint8_t {
    A, B, Z, Start, L, R, CUp, CDown, CLeft, CRight, DUp, DDown, DLeft, DRight,
    StickUp, StickDown, StickLeft, StickRight,
    Settings,   // the settings window (View)
    AuxLeft,    // dialogue: automatic reading on / off; idle map: previous enemy (L2)
    AuxRight,   // dialogue: fast-forward; idle map: next enemy; battle: end the animation (R2)
    Animation,  // the battle animation on / off on the pre-battle screens (Y)
    Language,   // the next reading language (L3; F7 also, a fixed shortcut)
    Images,     // Original / HD pictures (R3; F6 also, a fixed shortcut)
    Count
};
inline constexpr size_t action_count = size_t(Action::Count);

struct ActionInfo {
    std::string_view id;  // input.json and the debug interface
    uint32_t mask;
};
inline constexpr std::array<ActionInfo, action_count> actions{{
    {"a", 0x8000}, {"b", 0x4000}, {"z", 0x2000}, {"start", 0x1000}, {"l", 0x0020}, {"r", 0x0010},
    {"c_up", 0x0008}, {"c_down", 0x0004}, {"c_left", 0x0002}, {"c_right", 0x0001},
    {"d_up", 0x0800}, {"d_down", 0x0400}, {"d_left", 0x0200}, {"d_right", 0x0100},
    {"stick_up", 1u << 16}, {"stick_down", 1u << 17}, {"stick_left", 1u << 18}, {"stick_right", 1u << 19},
    {"settings", pad_view}, {"aux_left", pad_l2}, {"aux_right", pad_r2},
    {"animation", pad_animation}, {"language", pad_language}, {"images", pad_images},
}};
inline constexpr const ActionInfo& info(Action action) { return actions[size_t(action)]; }

namespace scancode {
inline constexpr int A = 4, D = 7, E = 8, F = 9, G = 10, H = 11, I = 12, J = 13, K = 14, L = 15, Q = 20, S = 22, T = 23,
    W = 26, X = 27, Z = 29, N1 = 30, N2 = 31, N3 = 32, N4 = 33,
    Return = 40, Escape = 41, Backspace = 42, Space = 44, Right = 79, Left = 80, Down = 81, Up = 82;
}
namespace pad_button {
inline constexpr uint8_t A = 0, B = 1, X = 2, Y = 3, Back = 4, Guide = 5, Start = 6, LeftStick = 7, RightStick = 8,
    LeftShoulder = 9, RightShoulder = 10, DUp = 11, DDown = 12, DLeft = 13, DRight = 14, Count = 21;
}
namespace pad_axis {
inline constexpr uint8_t LeftX = 0, LeftY = 1, RightX = 2, RightY = 3, TriggerLeft = 4, TriggerRight = 5, Count = 6;
}
// An axis counts as pressed past this, in either direction, as graphics.cpp always did.
inline constexpr int axis_threshold = 16000;

// A controller input: a button, or one direction of an axis.
struct PadInput {
    enum Kind : uint8_t {Button, AxisPlus, AxisMinus};
    Kind kind{};
    uint8_t index{};
    bool operator==(const PadInput&) const = default;
};
inline constexpr PadInput button(uint8_t index) { return {PadInput::Button, index}; }
inline constexpr PadInput axis(uint8_t index, int sign) { return {sign < 0 ? PadInput::AxisMinus : PadInput::AxisPlus, index}; }

struct Bindings {
    std::array<std::vector<int>, action_count> keys;       // SDL scancodes
    std::array<std::vector<PadInput>, action_count> pads;
    bool operator==(const Bindings&) const = default;
};

inline Bindings default_bindings() {
    Bindings b;
    const auto keys = [&](Action a, std::vector<int> v) { b.keys[size_t(a)] = std::move(v); };
    const auto pads = [&](Action a, std::vector<PadInput> v) { b.pads[size_t(a)] = std::move(v); };
    // The controller follows what the game's inputs are worth (docs/gameplay/original-controls.md):
    // A, B, START, the D-pad and the stick as the original; L1 / R1 for L / R. Z only repeats L
    // in lists and makes Z + START's reset to the title, so it has no button. The freed ones
    // serve the port: X held speeds the map cursor (C-left), Y the battle animation, the
    // stick clicks the language and Original / HD; the right stick stays the C buttons. View
    // opens the settings window and the triggers work the dialogue and the map's enemies.
    {
    using namespace pad_button;
    pads(Action::A, {button(A)}); pads(Action::B, {button(B)});
    pads(Action::Start, {button(Start)});
    pads(Action::L, {button(LeftShoulder)}); pads(Action::R, {button(RightShoulder)});
    pads(Action::CUp, {axis(pad_axis::RightY, -1)}); pads(Action::CDown, {axis(pad_axis::RightY, 1)});
    pads(Action::CLeft, {button(X), axis(pad_axis::RightX, -1)}); pads(Action::CRight, {axis(pad_axis::RightX, 1)});
    pads(Action::DUp, {button(DUp)}); pads(Action::DDown, {button(DDown)});
    pads(Action::DLeft, {button(DLeft)}); pads(Action::DRight, {button(DRight)});
    pads(Action::StickUp, {axis(pad_axis::LeftY, -1)}); pads(Action::StickDown, {axis(pad_axis::LeftY, 1)});
    pads(Action::StickLeft, {axis(pad_axis::LeftX, -1)}); pads(Action::StickRight, {axis(pad_axis::LeftX, 1)});
    pads(Action::Settings, {button(Back)});
    pads(Action::AuxLeft, {axis(pad_axis::TriggerLeft, 1)}); pads(Action::AuxRight, {axis(pad_axis::TriggerRight, 1)});
    pads(Action::Animation, {button(Y)});
    pads(Action::Language, {button(LeftStick)}); pads(Action::Images, {button(RightStick)});
    }
    // The keyboard is PCSX2's default layout (pcsx2/Input/InputManager.cpp,
    // GetKeyboardGenericBindingMapping; user, 2026-09-28), which stands a key for each
    // controller button: K L J I the face buttons (south, east, west, north), Q E L1 R1,
    // 1 3 L2 R2, 2 4 L3 R3, Enter and Backspace Menu and View, the arrows the D-pad, WASD
    // the left stick and T F G H the right one. Each key does what its button does above,
    // so Z has no key here either. The settings window also opens with Ctrl / Cmd + ,
    // (frontend.cpp).
    using namespace scancode;  // pad_button's names are out of scope here
    keys(Action::A, {K}); keys(Action::B, {L}); keys(Action::Start, {Return});
    keys(Action::L, {Q}); keys(Action::R, {E});
    keys(Action::CUp, {T}); keys(Action::CDown, {G}); keys(Action::CLeft, {J, F}); keys(Action::CRight, {H});
    keys(Action::DUp, {Up}); keys(Action::DDown, {Down}); keys(Action::DLeft, {Left}); keys(Action::DRight, {Right});
    keys(Action::StickUp, {W}); keys(Action::StickDown, {S}); keys(Action::StickLeft, {A}); keys(Action::StickRight, {D});
    keys(Action::Settings, {Backspace});
    keys(Action::AuxLeft, {N1}); keys(Action::AuxRight, {N3});
    keys(Action::Animation, {I});
    keys(Action::Language, {N2}); keys(Action::Images, {N4});
    return b;
}

// The keyboard before 0.3.0, on the N64's positions: Z A, X B, Space Z, Enter START, Q E
// L R, I K J L the C buttons, the arrows the D-pad and WASD the stick. The keys the debug
// interface holds keep it whatever the player bound (graphics.cpp), so scripts say "z" for
// A; the native pages' key code speaks it too (frontend.cpp follow_bindings).
inline Bindings classic_keys() {
    Bindings b;
    const auto keys = [&](Action a, std::vector<int> v) { b.keys[size_t(a)] = std::move(v); };
    using namespace scancode;
    keys(Action::A, {Z}); keys(Action::B, {X}); keys(Action::Z, {Space}); keys(Action::Start, {Return});
    keys(Action::L, {Q}); keys(Action::R, {E});
    keys(Action::CUp, {I}); keys(Action::CDown, {K}); keys(Action::CLeft, {J}); keys(Action::CRight, {L});
    keys(Action::DUp, {Up}); keys(Action::DDown, {Down}); keys(Action::DLeft, {Left}); keys(Action::DRight, {Right});
    keys(Action::StickUp, {W}); keys(Action::StickDown, {S}); keys(Action::StickLeft, {A}); keys(Action::StickRight, {D});
    return b;
}

// Gives ACTION the input alone: it replaces ACTION's inputs of that device, and any other
// action holding it loses it. An action left with nothing takes ACTION's former first
// input, so a swap never leaves a button unreachable.
template <class T>
void assign(std::array<std::vector<T>, action_count>& table, Action action, T input) {
    auto& mine = table[size_t(action)];
    const std::vector<T> former = mine;
    for (size_t i = 0; i < action_count; ++i) {
        if (i == size_t(action)) continue;
        auto& other = table[i];
        const auto before = other.size();
        std::erase(other, input);
        if (other.empty() && before && !former.empty() && former.front() != input) other.push_back(former.front());
    }
    mine = {input};
}
inline void assign_key(Bindings& b, Action action, int key) { assign(b.keys, action, key); }
inline void assign_pad(Bindings& b, Action action, PadInput input) { assign(b.pads, action, input); }
inline void clear_keys(Bindings& b, Action action) { b.keys[size_t(action)].clear(); }
inline void clear_pads(Bindings& b, Action action) { b.pads[size_t(action)].clear(); }

// The N64 and host bits of the pressed inputs. DOWN(scancode) says whether a key is down;
// BUTTON(index) and AXIS(index) read the controller.
template <class Down>
uint32_t key_mask(const Bindings& b, Down down) {
    uint32_t mask = 0;
    for (size_t i = 0; i < action_count; ++i)
        for (const int key : b.keys[i]) if (down(key)) mask |= actions[i].mask;
    return mask;
}
template <class ButtonDown, class AxisValue>
uint32_t pad_mask(const Bindings& b, ButtonDown button_down, AxisValue axis_value) {
    uint32_t mask = 0;
    for (size_t i = 0; i < action_count; ++i)
        for (const auto& input : b.pads[i]) {
            const bool on = input.kind == PadInput::Button ? bool(button_down(input.index))
                : int(axis_value(input.index)) * (input.kind == PadInput::AxisMinus ? -1 : 1) > axis_threshold;
            if (on) mask |= actions[i].mask;
        }
    return mask;
}
// The action bound to a key, if any (the first in table order).
inline const Action* action_of_key(const Bindings& b, int key) {
    static constexpr auto all = [] { std::array<Action, action_count> a{}; for (size_t i = 0; i < action_count; ++i) a[i] = Action(i); return a; }();
    for (size_t i = 0; i < action_count; ++i)
        if (std::find(b.keys[i].begin(), b.keys[i].end(), key) != b.keys[i].end()) return &all[i];
    return nullptr;
}

// The live bindings. The window thread reads them each frame; the Controls page changes
// them; a saver (presentation_settings.cpp) writes each change to input.json.
class LiveBindings {
public:
    Bindings get() const { std::lock_guard lock(mutex_); return value_; }
    uint64_t revision() const { std::lock_guard lock(mutex_); return revision_; }
    void set(const Bindings& value, bool save = true) {
        void (*saver)(const Bindings&) = nullptr;
        {
            std::lock_guard lock(mutex_);
            if (value == value_) return;
            value_ = value; ++revision_; saver = saver_;
        }
        if (save && saver) saver(value);
    }
    void set_saver(void (*saver)(const Bindings&)) { std::lock_guard lock(mutex_); saver_ = saver; }
private:
    mutable std::mutex mutex_;
    Bindings value_ = default_bindings();
    uint64_t revision_ = 0;
    void (*saver_)(const Bindings&) = nullptr;
};
inline LiveBindings& live_bindings() { static LiveBindings value; return value; }

}  // namespace srw64::input
