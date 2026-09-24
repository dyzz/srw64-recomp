#pragma once
#include "presentation/raster_image.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
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
    bool halved{};  // the closing punctuation at the end takes half its width
};
struct TextPage { size_t first_line{}, line_count{}, start{}, end{}; };
// Page layout for dialogue (docs/design/dialogue-typesetting.md). The lines per
// page follow from the minimum line spacing; the leftover height is shared out
// up to the maximum. With rank_breaks, page ends are chosen by dynamic
// programming: fewest pages, then fewest ends mid-sentence, then at a comma,
// then fewest last lines of one or two characters (one English word).
struct PageStyle {
    double height=35;
    double min_spacing=1.22, max_spacing=1.22;   // line pitch / font size
    bool rank_breaks=false;
    bool halve_line_end=false;                   // CJK: halvable_marks at a line end may take half width
    std::vector<size_t> forced;                  // UTF-16 offsets where a page must start
    std::vector<size_t> sentence_ends;           // extra offsets that end a sentence (original page breaks)
};
// The characters the ranking and the half-width rule look at. They are listed
// in docs/design/dialogue-typesetting.md; the Python reference uses the same.
inline constexpr std::u16string_view sentence_end_marks=u"。！？…!?.」』）”\"";
inline constexpr std::u16string_view comma_marks=u"，、；：—,;:";
inline constexpr std::u16string_view halvable_marks=u"。，、；：」』）》】〕";
inline constexpr std::u16string_view opening_marks=u"“「『（《【〔\"";   // not counted in a short last line
struct TextColor { uint8_t r=255, g=255, b=255, a=255; };
struct TextClip { double x{}, y{}, width{}, height{}; };
struct TextDraw {
    double x{}, y{}, scale=1;
    size_t first_line{}, line_count=std::numeric_limits<size_t>::max();
    size_t revealed_utf16=std::numeric_limits<size_t>::max();
    TextColor color;
    std::optional<TextClip> clip;
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
    // The pitch between lines: 1.22 x size, or what the PageStyle chose.
    double line_height() const;
    // Pages of height; a paged layout returns the pages it was built with.
    std::vector<TextPage> pages(double height) const;
    bool paged() const;
    // Draws only selected lines, clipped to the supplied surface. Layout pins
    // font bytes and glyph positions even after the FontSet/file is destroyed.
    // Reveal is rounded DOWN to a grapheme boundary; a shaping cluster (e.g. a
    // ligature) is drawn only when its entire source span has been revealed.
    // Source and destination use premultiplied BGRA8; no extra alpha multiply.
    void draw(presentation::Bgra8Surface& target,const TextDraw& options={}) const;
};
class FontSet {
    struct State;
    struct Builder;
    std::shared_ptr<State> state_;
    friend class TextLayout;
public:
    // Explicit ordered fallback, whole graphemes only. Reads immutable font
    // bytes once; never searches system font directories or silently uses tofu.
    explicit FontSet(const std::vector<FontSource>& sources);
    TextLayout layout(std::u16string text,double font_size,double width,
                      std::string locale="en") const;
    // Lines broken from the chosen page starts; pages() returns those pages.
    TextLayout layout(std::u16string text,double font_size,double width,
                      std::string locale,const PageStyle& style) const;
    // What the page ranking works from, for the Python reference
    // (tests/data/dialogue-paging-cases.json): the legal breaks, each
    // grapheme's advance in its paragraph, and the greedy line from every
    // start the ranking may try (0, legal breaks, forced starts, line ends).
    struct PagingTrace {
        std::vector<size_t> clusters, legal;
        std::vector<double> advances;
        std::vector<TextLine> lines;
        size_t lines_per_page{};
        double pitch{};
    };
    PagingTrace trace(std::u16string text,double font_size,double width,
                      std::string locale,const PageStyle& style) const;
};
}
