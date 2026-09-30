#pragma once
// Button prompts in UI text. The locale strings write tokens that expand to icons of
// content/fonts/SRW64Prompts.ttf (PromptFont glyphs, tools/content/build_prompt_font.py) in
// the Private Use Area, the last font of both text engines' chains:
//
// - Action tokens name an N64 input or a host button: {A} {B} {Z} {Start} {L} {R}, the
//   C buttons {CUp} {CDown} {CLeft} {CRight}, the D-pad {DUp} {DDown} {DLeft} {DRight},
//   {Settings}, the trigger functions {AuxL} {AuxR}, {Anim} (battle animation), {Lang},
//   {Images} (Original / HD); groups {DPad} {DUpDown}
//   {DLeftRight} {C} {CUpDown} {Stick}. They show what the player bound to it
//   (input_bindings.hpp): the controller button's icon, in the controller's own family,
//   in a controller hint; the key in a keyboard hint (an icon for letters, digits, Enter,
//   Esc, arrows and the like, else the key's name). A group left at its defaults shows one icon.
// - Key tokens are the native pages' fixed keys, which work whatever the bindings say:
//   {Esc} {Enter} {Tab} {Space} {Ctrl} {KeyUp} {KeyDown} {KeyLeft} {KeyRight} {Arrows}
//   {WASD} {F5} {F6} {F7}. Keyboard hints only. (The font's IJKL icon went with the old
//   keyboard layout; the C buttons are T G F H now, which have no icon.)
//
// Other braces, such as the "{n}" placeholders, are left as they are.
#include "input_bindings.hpp"
#include <cstdint>
#include <string>
#include <string_view>

namespace srw64::text {

enum class PadFamily : uint8_t {Xbox, Deck, PlayStation, Nintendo};

struct FamilyGlyph {
    char32_t xbox, deck, playstation, nintendo;
    char32_t operator()(PadFamily family) const {
        switch (family) {
        case PadFamily::Deck: return deck;
        case PadFamily::PlayStation: return playstation;
        case PadFamily::Nintendo: return nintendo;
        default: return xbox;
        }
    }
};
inline constexpr FamilyGlyph same(char32_t c) { return {c, c, c, c}; }

// The icon of a controller input, or 0 when the font has none (Guide, paddles, touchpad).
// Face buttons go by position: a Switch controller shows the four-dot icon.
inline char32_t pad_glyph(const input::PadInput& p, PadFamily family) {
    namespace b = input::pad_button;
    namespace a = input::pad_axis;
    if (p.kind == input::PadInput::Button) {
        switch (p.index) {
        case b::A: return FamilyGlyph{0xE800, 0xE800, 0xE804, 0xE808}(family);
        case b::B: return FamilyGlyph{0xE801, 0xE801, 0xE805, 0xE809}(family);
        case b::X: return FamilyGlyph{0xE802, 0xE802, 0xE806, 0xE80A}(family);
        case b::Y: return FamilyGlyph{0xE803, 0xE803, 0xE807, 0xE80B}(family);
        case b::Back: return FamilyGlyph{0xE820, 0xE820, 0xE822, 0xE824}(family);
        case b::Start: return FamilyGlyph{0xE821, 0xE821, 0xE823, 0xE825}(family);
        case b::LeftShoulder: return FamilyGlyph{0xE810, 0xE814, 0xE814, 0xE818}(family);
        case b::RightShoulder: return FamilyGlyph{0xE811, 0xE815, 0xE815, 0xE819}(family);
        case b::DUp: return 0xE831;
        case b::DDown: return 0xE832;
        case b::DLeft: return 0xE833;
        case b::DRight: return 0xE834;
        case b::LeftStick: return 0xE83D;   // L3
        case b::RightStick: return 0xE83E;  // R3
        default: return 0;
        }
    }
    switch (p.index) {
    case a::LeftX: case a::LeftY: return 0xE838;
    case a::RightY: return p.kind == input::PadInput::AxisMinus ? 0xE83A : 0xE83B;
    case a::RightX: return 0xE839;
    case a::TriggerLeft: return FamilyGlyph{0xE812, 0xE816, 0xE816, 0xE81A}(family);
    case a::TriggerRight: return FamilyGlyph{0xE813, 0xE817, 0xE817, 0xE81B}(family);
    default: return 0;
    }
}

// The icon of a key, or 0 for keys shown by name (punctuation, the keypad and the like).
inline char32_t key_glyph(int scancode) {
    if (scancode >= 4 && scancode <= 29) return char32_t(0xE850 + scancode - 4);   // A-Z
    if (scancode >= 30 && scancode <= 38) return char32_t(0xE871 + scancode - 30); // 1-9
    switch (scancode) {
    case 39: return 0xE870;             // 0
    case 42: return 0xE87A;             // Backspace
    case 225: case 229: return 0xE87B;  // Shift
    case 226: case 230: return 0xE87C;  // Alt
    case 76: return 0xE87D;             // Delete
    case 41: return 0xE840;             // Escape
    case 40: case 88: return 0xE841;    // Return, keypad Enter
    case 43: return 0xE842;             // Tab
    case 44: return 0xE843;             // Space
    case 224: case 228: return 0xE844;  // Ctrl
    case 80: return 0xE845;             // Left
    case 82: return 0xE846;             // Up
    case 79: return 0xE847;             // Right
    case 81: return 0xE848;             // Down
    case 62: return 0xE84C;             // F5
    case 63: return 0xE84D;             // F6
    case 64: return 0xE84E;             // F7
    default: return 0;
    }
}

struct KeyToken {std::string_view token; char32_t glyph;};
inline constexpr KeyToken key_tokens[] = {
    {"Esc", 0xE840}, {"Enter", 0xE841}, {"Tab", 0xE842}, {"Space", 0xE843}, {"Ctrl", 0xE844},
    {"KeyLeft", 0xE845}, {"KeyUp", 0xE846}, {"KeyRight", 0xE847}, {"KeyDown", 0xE848},
    {"Arrows", 0xE849}, {"WASD", 0xE84A}, {"F5", 0xE84C}, {"F6", 0xE84D}, {"F7", 0xE84E},
};

using input::Action;
struct ActionToken {
    std::string_view token;
    std::array<Action, 4> members;  // one action, or a group
    uint8_t count;
    FamilyGlyph pad_group;          // a group's controller icon while it is at its defaults (0: none)
    char32_t key_group;             // and its keyboard icon
};
inline constexpr ActionToken action_tokens[] = {
    {"A", {Action::A}, 1, {}, 0}, {"B", {Action::B}, 1, {}, 0}, {"Z", {Action::Z}, 1, {}, 0},
    {"Start", {Action::Start}, 1, {}, 0}, {"L", {Action::L}, 1, {}, 0}, {"R", {Action::R}, 1, {}, 0},
    {"CUp", {Action::CUp}, 1, {}, 0}, {"CDown", {Action::CDown}, 1, {}, 0},
    {"CLeft", {Action::CLeft}, 1, {}, 0}, {"CRight", {Action::CRight}, 1, {}, 0},
    {"DUp", {Action::DUp}, 1, {}, 0}, {"DDown", {Action::DDown}, 1, {}, 0},
    {"DLeft", {Action::DLeft}, 1, {}, 0}, {"DRight", {Action::DRight}, 1, {}, 0},
    {"Settings", {Action::Settings}, 1, {}, 0}, {"AuxL", {Action::AuxLeft}, 1, {}, 0}, {"AuxR", {Action::AuxRight}, 1, {}, 0},
    {"Anim", {Action::Animation}, 1, {}, 0}, {"Lang", {Action::Language}, 1, {}, 0}, {"Images", {Action::Images}, 1, {}, 0},
    {"DPad", {Action::DUp, Action::DDown, Action::DLeft, Action::DRight}, 4, same(0xE830), 0xE849},
    {"DUpDown", {Action::DUp, Action::DDown}, 2, same(0xE835), 0},
    {"DLeftRight", {Action::DLeft, Action::DRight}, 2, same(0xE836), 0},
    {"C", {Action::CUp, Action::CDown, Action::CLeft, Action::CRight}, 4, same(0xE839), 0},
    {"CUpDown", {Action::CUp, Action::CDown}, 2, same(0xE83C), 0},
    {"Stick", {Action::StickUp, Action::StickDown, Action::StickLeft, Action::StickRight}, 4, same(0xE838), 0xE84A},
};

// Where a hint is shown. KEY_LABEL names a key that has no icon (SDL's key name for the
// layout); BINDINGS may be null for the defaults.
struct PromptContext {
    bool pad = false;
    PadFamily family = PadFamily::Xbox;
    const input::Bindings* bindings = nullptr;
    std::string (*key_label)(int scancode) = nullptr;
};

inline void append_utf8(std::string& out, char32_t c) {
    // Private Use Area code points take three UTF-8 bytes.
    out += char(0xE0 | (c >> 12));
    out += char(0x80 | ((c >> 6) & 0x3F));
    out += char(0x80 | (c & 0x3F));
}

inline std::string expand_prompts(std::string_view text, const PromptContext& ctx) {
    static const auto defaults = input::default_bindings();
    const auto& bindings = ctx.bindings ? *ctx.bindings : defaults;
    // One action's input; true when it came out as an icon.
    const auto render = [&](std::string& out, Action action) {
        const size_t i = size_t(action);
        if (ctx.pad) {
            if (bindings.pads[i].empty()) {out += "\xe2\x80\x94"; return false;}  // em dash: unbound
            const auto& p = bindings.pads[i].front();
            if (const char32_t c = pad_glyph(p, ctx.family)) {append_utf8(out, c); return true;}
            out += p.kind == input::PadInput::Button ? "(" + std::to_string(p.index) + ")" : "(axis)";
            return false;
        }
        if (bindings.keys[i].empty()) {out += "\xe2\x80\x94"; return false;}
        const int key = bindings.keys[i].front();
        if (const char32_t c = key_glyph(key)) {append_utf8(out, c); return true;}
        out += ctx.key_label ? ctx.key_label(key) : std::to_string(key);
        return false;
    };
    std::string result;
    result.reserve(text.size());
    size_t at = 0;
    while (at < text.size()) {
        const auto open = text.find('{', at);
        if (open == std::string_view::npos) break;
        const auto close = text.find('}', open + 1);
        if (close == std::string_view::npos) break;
        const auto token = text.substr(open + 1, close - open - 1);
        std::string out;
        bool found = false;
        for (const auto& k : key_tokens)
            if (k.token == token) {append_utf8(out, k.glyph); found = true; break;}
        for (const auto& t : action_tokens) {
            if (found || t.token != token) continue;
            found = true;
            bool at_defaults = true;
            for (uint8_t m = 0; m < t.count; ++m) {
                const size_t i = size_t(t.members[m]);
                at_defaults &= ctx.pad ? bindings.pads[i] == defaults.pads[i] : bindings.keys[i] == defaults.keys[i];
            }
            const char32_t group = t.count > 1 && at_defaults ? (ctx.pad ? t.pad_group(ctx.family) : t.key_group) : 0;
            if (group) {append_utf8(out, group); break;}
            // Names of a group read "I/K"; icons need no separator.
            bool icon_before = true;
            for (uint8_t m = 0; m < t.count; ++m) {
                std::string one;
                const bool icon = render(one, t.members[m]);
                if (m && !(icon && icon_before)) out += "/";
                out += one;
                icon_before = icon;
            }
        }
        result.append(text.substr(at, (found ? open : close + 1) - at));
        result += out;
        at = close + 1;
    }
    result.append(text.substr(at));
    return result;
}

}  // namespace srw64::text
