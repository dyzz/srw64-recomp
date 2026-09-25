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
    // Over max_width a line is first narrowed horizontally down to this share of its width;
    // the size only drops when that is not enough.
    double condense_min = 1;
    float fill_top[3]{1, 1, 1}, fill_bottom[3]{1, 1, 1}, outline[3]{0, 0, 0};
    double outline_px = 1, outline_alpha = 1, shadow_px = 0, shadow_alpha = 0;
    // TextImage::origin: the top of the first line at the block's left, centre or right.
    enum class Origin { left, center, right } origin = Origin::left;
    // A plate behind the text (HUD banners and badges): at least min_plate units, the
    // text centred on it; drawn when plate[3] > 0, with a border when border_px > 0.
    float plate[4]{}, border[3]{};
    double plate_pad = 0, border_px = 0, min_plate[2]{};
};
// Catalog-form text (<BR> line, <STOP> blank line, <END>) -> premultiplied RGBA at
// `density` texels per N64 pixel.
sprites::TextImage draw(const Style& style, const std::string& locale, const std::string& text, double density = 8);
// The style's numbers as a cache key fragment.
std::string key(const Style& style);
}
