#pragma once
#include "presentation/raster_image.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace srw64::text {
// All offsets are UTF-16 code units. No normalization or OS locale is applied.
// Boundary analysis does not require a font and also accepts embedded NUL.
std::vector<size_t> grapheme_ends(std::u16string_view text);
struct FontSource { std::filesystem::path path; long face_index=0; };
struct TextLine {
    size_t start{}, end{};
    double width{};
    bool emergency_break{}, overflow{};
};
struct TextPage { size_t first_line{}, line_count{}, start{}, end{}; };
struct TextColor { uint8_t r=255, g=255, b=255, a=255; };
struct TextDraw {
    double x{}, y{}, scale=1;
    size_t first_line{}, line_count=std::numeric_limits<size_t>::max();
    size_t revealed_utf16=std::numeric_limits<size_t>::max();
    TextColor color;
};
class FontSet;
class TextLayout {
    struct Data;
    std::shared_ptr<const Data> data_;
    explicit TextLayout(std::shared_ptr<const Data> data);
    friend class FontSet;
public:
    TextLayout()=default;
    const std::u16string& text() const;
    const std::vector<size_t>& clusters() const;
    const std::vector<TextLine>& lines() const;
    const std::string& locale() const;
    double font_size() const;
    double line_height() const;
    std::vector<TextPage> pages(double height) const;
    // Draws only selected lines, clipped to the supplied surface. Layout pins
    // font bytes and glyph positions even after the FontSet/file is destroyed.
    // Reveal is rounded DOWN to a grapheme boundary; a shaping cluster (e.g. a
    // ligature) is drawn only when its entire source span has been revealed.
    // Source and destination use premultiplied BGRA8; no extra alpha multiply.
    void draw(presentation::Bgra8Surface& target,const TextDraw& options={}) const;
};
class FontSet {
    struct State;
    std::shared_ptr<State> state_;
    friend class TextLayout;
public:
    // Explicit ordered fallback, whole graphemes only. Reads immutable font
    // bytes once; never searches system font directories or silently uses tofu.
    explicit FontSet(const std::vector<FontSource>& sources);
    TextLayout layout(std::u16string text,unsigned font_size,double width,
                      std::string locale="en") const;
};
}
