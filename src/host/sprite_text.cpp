#include "sprite_text.hpp"
#include "native_dialogue.hpp"
#include "story_cards.hpp"
#include "localization/catalog.hpp"
#include "text/game_fonts.hpp"
#include "text/portable_text.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>

namespace srw64::sprite_text {
namespace {
using sprites::TextImage;
using sprites::TextJob;

// Resources (docs/native/native-title-and-story-images.md).
constexpr uint16_t kTitleAtlas = 623, kPressStart = 651, kMenuLast = 655;
constexpr uint16_t kIntroPalette = 5536, kIntroFirst = 5506, kIntroLast = 5535;
constexpr uint16_t kEndingPalette = 5564, kEndingFirst = 5570, kEndingLast = 5576, kEndingClose = 5576;
constexpr uint16_t kStaffFirst = 5544, kStaffLast = 5563;           // staff roll, between the epilogue and 終
constexpr uint16_t kCopyrightAtlas = 620, kCopyrightPalette = 621;  // boot copyright page (scene 622)
constexpr uint32_t kTitleSlot = 0x9C;          // 801C72C8: the card's title; 0x9D draws 第 N 話
constexpr uint32_t kStageScene = 0x0010F5F0;   // current stage scene, physical
constexpr const char* kMenuLabels[] = {"title_press_start", "title_start", "title_load", "title_continue", "title_option"};

std::u16string utf16(const std::string& text) {
    std::u16string out;
    for (size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        char32_t code;
        int extra;
        if (c < 0x80) { code = c; extra = 0; }
        else if ((c >> 5) == 6) { code = c & 0x1F; extra = 1; }
        else if ((c >> 4) == 14) { code = c & 0x0F; extra = 2; }
        else { code = c & 0x07; extra = 3; }
        if (i + extra >= text.size() + (extra ? 0 : 1)) break;
        for (int k = 1; k <= extra; ++k) code = (code << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
        i += extra + 1;
        if (code >= 0x10000) { code -= 0x10000; out += char16_t(0xD800 + (code >> 10)); out += char16_t(0xDC00 + (code & 0x3FF)); }
        else out += char16_t(code);
    }
    return out;
}

// Catalog form -> paragraphs; <STOP> leaves one blank line.
std::vector<std::u16string> paragraphs(std::string text) {
    if (const auto end = text.find("<END>"); end != std::string::npos) text.erase(end);
    std::vector<std::u16string> out;
    std::string line;
    for (size_t i = 0; i < text.size();) {
        if (text.compare(i, 4, "<BR>") == 0) { out.push_back(utf16(line)); line.clear(); i += 4; }
        else if (text.compare(i, 6, "<STOP>") == 0) { out.push_back(utf16(line)); out.emplace_back(); line.clear(); i += 6; }
        else line += text[i++];
    }
    out.push_back(utf16(line));
    return out;
}

// Fonts for the worker thread only, apart from the dialogue's own FontSets.
std::shared_ptr<const text::FontSet> fonts(const std::string& locale, bool bold) {
    static std::mutex mutex;
    static std::map<std::pair<std::string, bool>, std::shared_ptr<const text::FontSet>> cache;
    std::lock_guard lock(mutex);
    auto& set = cache[{locale, bold}];
    if (set) return set;
    // Bold is the variable fonts' Bold instance; the symbol font stays in the chain.
    return set = std::make_shared<const text::FontSet>(text::game_font_sources(locale, bold ? 700 : 0));
}

// Squared Euclidean distance transform of one row or column (Felzenszwalb & Huttenlocher).
void edt(const double* f, double* d, int n, std::vector<int>& v, std::vector<double>& z) {
    constexpr double inf = std::numeric_limits<double>::infinity();
    v.resize(size_t(n)); z.resize(size_t(n) + 1);
    const auto meet = [&](int q, int r) { return ((f[q] + double(q) * q) - (f[r] + double(r) * r)) / (2.0 * (q - r)); };
    int k = 0;
    v[0] = 0; z[0] = -inf; z[1] = inf;
    for (int q = 1; q < n; ++q) {
        double s = meet(q, v[k]);
        while (s <= z[k]) { --k; s = meet(q, v[k]); }
        ++k; v[k] = q; z[k] = s; z[k + 1] = inf;
    }
    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[k + 1] < q) ++k;
        d[q] = double(q - v[k]) * (q - v[k]) + f[v[k]];
    }
}

// Distance in texels from every texel to the nearest texel at least half covered.
std::vector<float> distance(const std::vector<float>& cover, int w, int h) {
    constexpr double far = 1e20;
    std::vector<double> grid(size_t(w) * h), line(size_t(std::max(w, h))), out(size_t(std::max(w, h)));
    std::vector<int> v;
    std::vector<double> z;
    for (size_t i = 0; i < grid.size(); ++i) grid[i] = cover[i] >= .5f ? 0.0 : far;
    for (int y = 0; y < h; ++y) {
        edt(&grid[size_t(y) * w], out.data(), w, v, z);
        std::copy(out.begin(), out.begin() + w, grid.begin() + size_t(y) * w);
    }
    std::vector<float> result(grid.size());
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) line[size_t(y)] = grid[size_t(y) * w + x];
        edt(line.data(), out.data(), h, v, z);
        for (int y = 0; y < h; ++y) result[size_t(y) * w + x] = float(std::sqrt(out[size_t(y)]));
    }
    return result;
}

struct Line { text::TextLayout layout; size_t index = 0; double width = 0; bool blank = false; };

Style menu_style(bool press) {
    Style s;
    s.size = press ? 11.5 : 14; s.max_width = press ? 150 : 110; s.min_size = 8;
    s.fill_top[0] = s.fill_top[1] = s.fill_top[2] = 1;
    s.fill_bottom[0] = .72f; s.fill_bottom[1] = .76f; s.fill_bottom[2] = .84f;
    s.outline[0] = .04f; s.outline[1] = .05f; s.outline[2] = .09f;
    s.outline_px = 1.0; s.shadow_px = .8; s.shadow_alpha = .45;
    return s;
}
Style card_style(double size) {
    Style s;
    s.size = size; s.min_size = 11; s.pitch = 1.3;
    s.fill_bottom[0] = .80f; s.fill_bottom[1] = .83f; s.fill_bottom[2] = .90f;
    s.outline[0] = s.outline[1] = s.outline[2] = .03f;
    s.outline_px = 1.6; s.shadow_px = 1.2; s.shadow_alpha = .55;
    return s;
}
Style page_style(const std::string& locale, bool center) {
    Style s;
    s.bold = false; s.center = center;
    s.size = locale == "en" ? 13 : 13.5; s.min_size = 8; s.pitch = locale == "en" ? 1.45 : 1.62;
    s.width = 256; s.max_height = 204;
    for (int c = 0; c < 3; ++c) { s.fill_top[c] = .95f; s.fill_bottom[c] = .88f; s.outline[c] = 0; }
    s.outline_px = 1.1; s.outline_alpha = .8; s.shadow_px = .8; s.shadow_alpha = .5;
    return s;
}
// Staff roll: centred, rows about 30 units apart like the original pages.
Style staff_style(const std::string& locale) {
    Style s = page_style(locale, true);
    s.width = 288; s.pitch = locale == "en" ? 2.0 : 2.15; s.max_height = 236;
    return s;
}
// Copyright page: left-aligned lines under the © column, as in the original.
Style copyright_style(const std::string& locale) {
    Style s = page_style(locale, false);
    s.size = locale == "en" ? 12.5 : 13; s.pitch = 1.45; s.width = 288; s.max_height = 196;
    return s;
}

// Hash for cache keys: the style's numbers and the text.
std::string style_key(const Style& s) {
    char buffer[160];
    std::snprintf(buffer, sizeof buffer, "%d%d/%.2f/%.2f/%.2f/%.1f/%.1f/%.1f/%.2f/%.2f", s.bold, s.center, s.size, s.min_size,
                  s.pitch, s.width, s.max_width, s.max_height, s.outline_px, s.shadow_px);
    return buffer;
}

void job_for(TextJob& job, const Style& style, const std::string& locale, const std::string& text, const char* kind) {
    job.key = std::string(kind) + "|" + locale + "|" + style_key(style) + "|" + text;
    job.render = [style, locale, text] { return draw(style, locale, text); };
}

float brightest(const sprites::SceneId& id, int channel) {
    int best = 1;
    double lum = -1;
    for (int i = 1; i < 14; ++i) {
        const uint16_t c = id.colors[i];
        if (!(c & 1)) continue;
        const double l = .3 * ((c >> 11) & 31) + .59 * ((c >> 6) & 31) + .11 * ((c >> 1) & 31);
        if (l > lum) { lum = l; best = i; }
    }
    const uint16_t c = id.colors[best];
    return float((c >> (11 - 5 * channel)) & 31) / 31.f;
}
}

sprites::TextImage draw(const Style& style, const std::string& locale, const std::string& body, double density) {
    const auto set = fonts(locale, style.bold);
    const auto source = paragraphs(body);
    std::vector<Line> lines;
    double size = style.size, block_width = 0;
    for (;; size -= .5) {
        lines.clear(); block_width = 0;
        bool overflow = false;
        for (const auto& paragraph : source) {
            if (paragraph.empty()) { lines.push_back({{}, 0, 0, true}); continue; }
            auto layout = set->layout(paragraph, size, style.width > 0 ? style.width : 100000, locale);
            for (size_t i = 0; i < layout.lines().size(); ++i) {
                const auto& line = layout.lines()[i];
                overflow = overflow || line.overflow || (style.max_width > 0 && line.width > style.max_width);
                block_width = std::max(block_width, line.width);
                lines.push_back({layout, i, line.width, false});
            }
        }
        const double height = lines.size() * size * style.pitch;
        if (size - .5 < style.min_size || (!overflow && (style.max_height <= 0 || height <= style.max_height))) break;
    }
    const double pitch = size * style.pitch;
    const double pad = style.outline_px + style.shadow_px + 1;
    TextImage image;
    image.units[0] = float(block_width + 2 * pad);
    image.units[1] = float(lines.size() * pitch + 2 * pad);
    image.width = uint32_t(std::ceil(image.units[0] * density));
    image.height = uint32_t(std::ceil(image.units[1] * density));
    if (image.width > 4096 || image.height > 4096) {
        const double fit = std::min(4096.0 / image.width, 4096.0 / image.height);
        return draw(style, locale, body, density * fit);
    }
    presentation::Bgra8Surface surface(image.width, image.height);
    std::vector<float> top_of(image.height, -1);    // texel row -> top of its glyph line, in texels
    double glyph_height = size * 1.22;
    for (size_t n = 0; n < lines.size(); ++n) {
        const auto& line = lines[n];
        const double box_top = pad + n * pitch;
        if (line.blank) continue;
        glyph_height = line.layout.line_height();
        const double y = box_top + (pitch - glyph_height) / 2;
        const double x = pad + (style.center ? (block_width - line.width) / 2 : 0);
        text::TextDraw options;
        options.x = x * density; options.y = y * density; options.scale = density;
        options.first_line = line.index; options.line_count = 1;
        line.layout.draw(surface, options);
        for (int r = int(box_top * density); r < int((box_top + pitch) * density) && r < int(image.height); ++r)
            if (r >= 0) top_of[size_t(r)] = float(y * density);
    }
    const int w = int(image.width), h = int(image.height);
    std::vector<float> cover(size_t(w) * h);
    for (size_t i = 0; i < cover.size(); ++i) cover[i] = surface.pixels[i * 4 + 3] / 255.f;
    const auto dist = distance(cover, w, h);
    const float radius = float(style.outline_px * density);
    std::vector<float> ring(cover.size());
    for (size_t i = 0; i < ring.size(); ++i)
        ring[i] = std::max(cover[i], std::clamp(radius + .5f - dist[i], 0.f, 1.f)) * float(style.outline_alpha);
    const int shift = int(std::lround(style.shadow_px * density));
    image.rgba.resize(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        float t = 0;
        if (top_of[size_t(y)] >= 0) t = std::clamp(float((y - top_of[size_t(y)]) / (glyph_height * density)), 0.f, 1.f);
        float fill[3];
        for (int c = 0; c < 3; ++c) fill[c] = style.fill_top[c] + (style.fill_bottom[c] - style.fill_top[c]) * t;
        for (int x = 0; x < w; ++x) {
            const size_t i = size_t(y) * w + x;
            float a = 0, rgb[3] = {0, 0, 0};
            if (shift > 0 && x >= shift && y >= shift) a = ring[size_t(y - shift) * w + (x - shift)] * float(style.shadow_alpha);
            const float o = ring[i];
            for (int c = 0; c < 3; ++c) rgb[c] = style.outline[c] * o + rgb[c] * (1 - o);
            a = o + a * (1 - o);
            const float f = cover[i];
            for (int c = 0; c < 3; ++c) rgb[c] = fill[c] * f + rgb[c] * (1 - f);
            a = f + a * (1 - f);
            for (int c = 0; c < 3; ++c) image.rgba[i * 4 + c] = uint8_t(std::lround(std::clamp(rgb[c], 0.f, 1.f) * 255));
            image.rgba[i * 4 + 3] = uint8_t(std::lround(std::clamp(a, 0.f, 1.f) * 255));
        }
    }
    return image;
}

bool describe(const uint8_t* rdram, const sprites::SceneId& id, TextJob& job) {
    const auto catalog = localization::snapshot();
    const std::string locale = catalog->locale;
    const bool cjk = locale != "en";
    // Title ring menu and PRESS START BUTTON; the palette's brightest colour tints it.
    if (id.atlas == kTitleAtlas && id.scene >= kPressStart && id.scene <= kMenuLast) {
        const auto label = catalog->ui(kMenuLabels[id.scene - kPressStart]);
        job_for(job, menu_style(id.scene == kPressStart), locale, label, "menu");
        for (int c = 0; c < 3; ++c) job.tint[c] = brightest(id, c);
        return true;
    }
    // Chapter title card: 第 N 話 (slot 0x9D) and the title (slot 0x9C).
    if (id.palette == story_cards::palette) {
        const bool number = id.scene >= story_cards::number_first && id.scene <= story_cards::number_last;
        uint16_t record = 0;
        if (number && id.slot != kTitleSlot) {
            std::string label = catalog->ui("intermission_episode");
            const auto at = label.find("{n}");
            if (at != std::string::npos) label.replace(at, 3, std::to_string(id.scene - story_cards::number_first + 1));
            job_for(job, card_style(24), locale, label, "chapter-number");
            return true;
        }
        if (number) {
            // The two 第１話 placeholder cards: the stage's own title record.
            record = uint16_t(281 + rdram[kStageScene ^ 3]);
        } else {
            for (const auto& card : story_cards::cards)
                if (card.scene == id.scene) { record = card.text; break; }
        }
        if (!record) return false;
        std::string title = dialogue::ui_text(rdram, record);
        if (title.empty()) return false;
        if (cjk) title = "\xE3\x80\x8C" + title + "\xE3\x80\x8D";   // 「」
        Style style = card_style(cjk ? 19 : 17);
        style.width = 280; style.max_height = 64;
        job_for(job, style, locale, title, "chapter-title");
        job.anchor = TextJob::Anchor::top;
        return true;
    }
    // Opening, ending, staff roll and copyright pages: the dialogue text files' @intro:<resource> entries.
    const bool intro = id.palette == kIntroPalette && id.atlas >= kIntroFirst && id.atlas <= kIntroLast;
    const bool ending = id.palette == kEndingPalette && id.atlas >= kEndingFirst && id.atlas <= kEndingLast;
    const bool staff = id.palette == kEndingPalette && id.atlas >= kStaffFirst && id.atlas <= kStaffLast;
    const bool copyright = id.palette == kCopyrightPalette && id.atlas == kCopyrightAtlas;
    if (intro || ending || staff || copyright) {
        std::string words = dialogue::page_text(locale, id.atlas);
        if (words.empty()) words = dialogue::page_text("ja", id.atlas);
        if (words.empty()) return false;
        if (id.atlas == kEndingClose) job_for(job, card_style(30), locale, words, "ending-close");
        else if (staff) job_for(job, staff_style(locale), locale, words, "staff");
        else if (copyright) job_for(job, copyright_style(locale), locale, words, "copyright");
        else job_for(job, page_style(locale, ending), locale, words, intro ? "intro" : "ending");
        return true;
    }
    return false;
}
}
