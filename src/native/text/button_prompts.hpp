#pragma once
// Button prompts in UI text. The locale strings write tokens such as "{A}", "{L1}",
// "{View}" or "{Esc}"; these become Private Use Area characters of
// content/fonts/SRW64Prompts.ttf (PromptFont glyphs, tools/content/build_prompt_font.py),
// the last font of both text engines' chains. Controller tokens take the icons of the
// controller in use: the Steam Deck has Xbox letters with L1/R1/L2/R2 shoulders, a Switch
// controller shows the button in that position, as the hints name buttons by position
// (bottom confirms, right goes back). Keyboard tokens are the same everywhere. Other
// braces, such as the "{n}" placeholders, are left as they are.
#include <cstdint>
#include <string>
#include <string_view>

namespace srw64::text {

enum class PadFamily : uint8_t {Xbox, Deck, PlayStation, Nintendo};

struct Prompt {
    std::string_view token;
    char32_t xbox, deck, playstation, nintendo;
};

inline constexpr Prompt prompts[] = {
    // Face buttons, by position: A bottom, B right, X left, Y top.
    {"A", 0xE800, 0xE800, 0xE804, 0xE808},
    {"B", 0xE801, 0xE801, 0xE805, 0xE809},
    {"X", 0xE802, 0xE802, 0xE806, 0xE80A},
    {"Y", 0xE803, 0xE803, 0xE807, 0xE80B},
    // Shoulders and triggers.
    {"L1", 0xE810, 0xE814, 0xE814, 0xE818},
    {"R1", 0xE811, 0xE815, 0xE815, 0xE819},
    {"L2", 0xE812, 0xE816, 0xE816, 0xE81A},
    {"R2", 0xE813, 0xE817, 0xE817, 0xE81B},
    // The two small buttons in the middle.
    {"View", 0xE820, 0xE820, 0xE822, 0xE824},
    {"Menu", 0xE821, 0xE821, 0xE823, 0xE825},
    // D-pad and sticks.
    {"DPad", 0xE830, 0xE830, 0xE830, 0xE830},
    {"DUp", 0xE831, 0xE831, 0xE831, 0xE831},
    {"DDown", 0xE832, 0xE832, 0xE832, 0xE832},
    {"DLeft", 0xE833, 0xE833, 0xE833, 0xE833},
    {"DRight", 0xE834, 0xE834, 0xE834, 0xE834},
    {"DUpDown", 0xE835, 0xE835, 0xE835, 0xE835},
    {"DLeftRight", 0xE836, 0xE836, 0xE836, 0xE836},
    {"LStick", 0xE838, 0xE838, 0xE838, 0xE838},
    {"RStick", 0xE839, 0xE839, 0xE839, 0xE839},
    {"RStickUp", 0xE83A, 0xE83A, 0xE83A, 0xE83A},
    {"RStickDown", 0xE83B, 0xE83B, 0xE83B, 0xE83B},
    {"RStickUpDown", 0xE83C, 0xE83C, 0xE83C, 0xE83C},
    // Keyboard keys; letter keys stay plain text.
    {"Esc", 0xE840, 0xE840, 0xE840, 0xE840},
    {"Enter", 0xE841, 0xE841, 0xE841, 0xE841},
    {"Tab", 0xE842, 0xE842, 0xE842, 0xE842},
    {"Space", 0xE843, 0xE843, 0xE843, 0xE843},
    {"Ctrl", 0xE844, 0xE844, 0xE844, 0xE844},
    {"KeyLeft", 0xE845, 0xE845, 0xE845, 0xE845},
    {"KeyUp", 0xE846, 0xE846, 0xE846, 0xE846},
    {"KeyRight", 0xE847, 0xE847, 0xE847, 0xE847},
    {"KeyDown", 0xE848, 0xE848, 0xE848, 0xE848},
    {"Arrows", 0xE849, 0xE849, 0xE849, 0xE849},
    {"WASD", 0xE84A, 0xE84A, 0xE84A, 0xE84A},
    {"IJKL", 0xE84B, 0xE84B, 0xE84B, 0xE84B},
    {"F5", 0xE84C, 0xE84C, 0xE84C, 0xE84C},
    {"F6", 0xE84D, 0xE84D, 0xE84D, 0xE84D},
    {"F7", 0xE84E, 0xE84E, 0xE84E, 0xE84E},
};

inline char32_t prompt_for(const Prompt& prompt, PadFamily family) {
    switch (family) {
    case PadFamily::Deck: return prompt.deck;
    case PadFamily::PlayStation: return prompt.playstation;
    case PadFamily::Nintendo: return prompt.nintendo;
    default: return prompt.xbox;
    }
}

// Replaces every known "{Token}" with its icon for FAMILY.
inline std::string expand_prompts(std::string_view text, PadFamily family) {
    std::string result;
    result.reserve(text.size());
    size_t at = 0;
    while (at < text.size()) {
        const auto open = text.find('{', at);
        if (open == std::string_view::npos) break;
        const auto close = text.find('}', open + 1);
        if (close == std::string_view::npos) break;
        const auto token = text.substr(open + 1, close - open - 1);
        const Prompt* found = nullptr;
        for (const auto& prompt : prompts)
            if (prompt.token == token) {found = &prompt; break;}
        result.append(text.substr(at, (found ? open : close + 1) - at));
        if (found) {
            // Private Use Area code points take three UTF-8 bytes.
            const char32_t c = prompt_for(*found, family);
            result += char(0xE0 | (c >> 12));
            result += char(0x80 | ((c >> 6) & 0x3F));
            result += char(0x80 | (c & 0x3F));
        }
        at = close + 1;
    }
    result.append(text.substr(at));
    return result;
}

}  // namespace srw64::text
