#pragma once
#include "native_sprite.hpp"
// Story text the game draws as images, redrawn natively in the reading language
// (docs/native/native-title-and-story-images.md): the title menu words, chapter title
// cards, and the opening and ending pages. Game host only (fonts, catalogs, dialogue text).
namespace srw64::sprite_text {
// Game thread: the text this scene sprite shows in the reading language, if it is one.
bool describe(const uint8_t* rdram, const sprites::SceneId& id, sprites::TextJob& job);
// Styled text as the worker draws it; exposed for the component test.
struct Style {
    bool bold = true, center = true;
    double size = 14, min_size = 9, pitch = 1.25;    // N64 px; line pitch / size
    double width = 0, max_width = 0, max_height = 0; // wrap width (0: one line per paragraph), limits
    float fill_top[3]{1, 1, 1}, fill_bottom[3]{1, 1, 1}, outline[3]{0, 0, 0};
    double outline_px = 1, outline_alpha = 1, shadow_px = 0, shadow_alpha = 0;
};
// Catalog-form text (<BR> line, <STOP> blank line, <END>) -> premultiplied RGBA at
// `density` texels per N64 pixel.
sprites::TextImage draw(const Style& style, const std::string& locale, const std::string& text, double density = 8);
}
