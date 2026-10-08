#include "ui_text.hpp"
#include "menu_widen.hpp"
#include "battle_page.hpp"
#include "game_hooks.hpp"
#include "native_dialogue.hpp"
#include "native_sprite.hpp"
#include "sprite_text.hpp"
#include "presentation/image_mode.hpp"
#include "localization/catalog.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

uint64_t srw64_current_vi();
namespace srw64::ui_text {
namespace {
using json = nlohmann::json;
using sprite_text::Style;

// The resident text engine (docs/native/native-ui-text.md §1): physical addresses.
constexpr uint32_t kLabels = 0x15CB00, kLabelSize = 0x34, kLabelCount = 60;     // single-line labels
constexpr uint32_t kBodies = 0xFBAB0, kBodySize = 0x218, kBodyCount = 2;         // dialogue bodies
constexpr uint32_t kNumbers = 0x1613E0, kNumberSize = 8, kNumberCount = 20;      // number pool
constexpr uint32_t kPaletteHandles = 0x178B52;                                    // s16 x 8: resources 2 3 4 8 7 5 6 1160
constexpr uint32_t kHandles = 0x160340, kHandleSize = 20, kHandleCount = 200;
constexpr unsigned kLine = 14, kNarrow = 8, kWide = 14, kWideFirst = 0x13B, kCell = 8, kLabelGlyphs = 64;
// The ROM's glyphs carry a pixel of space on their left, the font's capitals hardly any: a
// translation starts that much in, off the frame its window draws (Will on the map panel).
constexpr int kInset = 1;
constexpr uint16_t kLongDash = 0xD3;   // ー: the minus sign of printed numbers

std::mutex mutex;
bool installed = false, disabled = false;
std::ofstream log;
std::set<std::string> logged;
json last[2] = {json::array(), json::array()};
struct Counts { uint64_t passes = 0, drawn = 0, mismatched = 0, labels = 0, translated = 0, glyphs = 0, numbers = 0, reader = 0; } counts;

uint32_t word(const uint8_t* ram, uint32_t a) { uint32_t v; std::memcpy(&v, ram + (a & 0x1FFFFFFF), 4); return v; }
uint16_t half(const uint8_t* ram, uint32_t a) { uint16_t v; std::memcpy(&v, ram + ((a & 0x1FFFFFFF) ^ 2), 2); return v; }
uint8_t byte(const uint8_t* ram, uint32_t a) { return ram[(a & 0x1FFFFFFF) ^ 3]; }
void put(uint8_t* ram, uint32_t a, uint32_t v) { std::memcpy(ram + (a & 0x1FFFFFFF), &v, 4); }

void note(const std::string& key, json fields) {
    if (!log.is_open() || logged.size() > 20000 || !logged.insert(key).second) return;
    fields["vi"] = srw64_current_vi();
    log << fields.dump() << '\n';
    log.flush();
}

// A palette handle's data address, as 8008A11C returns it.
uint32_t palette_data(const uint8_t* ram, unsigned palette) {
    const int16_t handle = int16_t(half(ram, kPaletteHandles + 2 * palette));
    if (handle < 0 || uint32_t(handle) >= kHandleCount) return 0;
    const uint32_t record = kHandles + uint32_t(handle) * kHandleSize;
    return word(ram, record + 0x10) & 0x1FFFFFFF;
}
void colour(const uint8_t* ram, unsigned palette, float out[3]) {
    out[0] = out[1] = out[2] = 1;
    const uint32_t data = palette_data(ram, palette);
    if (!data || data + 12 > 0x800000) return;
    const uint16_t c = half(ram, data + 8 + 2);   // colour 1: the glyphs' face
    out[0] = float((c >> 11) & 31) / 31.f; out[1] = float((c >> 6) & 31) / 31.f; out[2] = float((c >> 1) & 31) / 31.f;
}

// A pass's records as 8008DC40 / 8008EB5C emit them: a palette load (FD10) per record,
// then one G_TEXRECT (E4, E1, F1) per drawn glyph.
struct Record { uint32_t palette = 0; std::vector<uint32_t> rects; };
std::vector<Record> records(const uint8_t* ram, uint32_t begin, uint32_t end) {
    std::vector<Record> out;
    for (uint32_t p = begin; p + 8 <= end; p += 8) {
        const uint32_t w0 = word(ram, p);
        if ((w0 & 0xFFFF0000u) == 0xFD100000u) out.push_back({(word(ram, p + 4) & 0x1FFFFFFF) - 8, {}});
        else if ((w0 >> 24) == 0xE4 && !out.empty() && p + 24 <= end &&
                 (word(ram, p + 8) >> 24) == 0xE1 && (word(ram, p + 16) >> 24) == 0xF1) {
            out.back().rects.push_back(p);
            p += 16;
        }
    }
    return out;
}
// Top-left of a TEXRECT in quarter pixels, as the engine masks it.
std::pair<uint32_t, uint32_t> corner(const uint8_t* ram, uint32_t rect) {
    const uint32_t w1 = word(ram, rect + 4);
    return {(w1 >> 12) & 0xFFF, w1 & 0xFFF};
}
bool at(const uint8_t* ram, uint32_t rect, int x, int y) {
    const auto [cx, cy] = corner(ram, rect);
    return cx == ((uint32_t(x) << 2) & 0xFFF) && cy == ((uint32_t(y) << 2) & 0xFFF);
}

Style label_style(const std::string& locale, double max_width, bool single) {
    Style s;
    s.bold = false; s.center = false;
    // One size per language: over the room a label has it is squeezed first, and only
    // then a little smaller.
    s.size = locale == "en" ? 12 : 12.5; s.min_size = s.size - 1.5; s.pitch = kLine / s.size;
    s.max_width = max_width;
    s.condense_min = .7;
    s.origin = single ? Style::Origin::center : Style::Origin::left;
    for (int c = 0; c < 3; ++c) { s.fill_top[c] = s.fill_bottom[c] = 1; s.outline[c] = 0; }
    s.outline_px = .8; s.outline_alpha = .65; s.shadow_px = .7; s.shadow_alpha = .45;
    return s;
}
// The number pool's two digit sets in 1159: white with a black outline (types 1, 2, 7, 8),
// white with a grey shadow (the others).
Style number_style(bool outlined) {
    Style s;
    s.bold = true; s.center = false; s.origin = Style::Origin::center;
    s.size = 9.5; s.min_size = 9.5; s.pitch = kCell / s.size;
    for (int c = 0; c < 3; ++c) { s.fill_top[c] = s.fill_bottom[c] = 1; s.outline[c] = outlined ? 0.f : .51f; }
    if (outlined) { s.outline_px = .9; s.outline_alpha = 1; }
    else { s.outline_px = .25; s.outline_alpha = 1; s.shadow_px = 1; s.shadow_alpha = 1; }
    return s;
}
sprites::PlacedText placed(const Style& style, const std::string& locale, const std::string& text, const char* kind,
                           float x, float y, const float tint[3]) {
    sprites::PlacedText item;
    item.job.key = std::string(kind) + "|" + locale + "|" + sprite_text::key(style) + "|" + text;
    item.job.render = [style, locale, text] { return sprite_text::draw(style, locale, text); };
    std::copy(tint, tint + 3, item.job.tint);
    item.x = x; item.y = y;
    return item;
}
// The blank an original line ends on: full-width punctuation fills only the left of its
// cell, so the ink of 。 and the like stops half a cell early; other glyphs leave a pixel.
int trailing_blank(const std::string& drawn) {
    for (const char* mark : {"。", "、", "！", "？", "」", "』", "）"}) {
        const size_t n = std::strlen(mark);
        if (drawn.size() >= n && drawn.compare(drawn.size() - n, n, mark) == 0) return int(kWide) / 2;
    }
    return 1;
}
std::string catalog_form(const std::string& text) {
    std::string out;
    for (const char c : text) {
        if (c == '\n') out += "<BR>";
        else if (c == '\f') break;
        else out += c;
    }
    return out;
}
// The weapon markers (格 射 P B MAP) in the symbol font, at U+E000 + their glyph id.
std::string icon(uint16_t code) {
    if (code != 0xF1 && code != 0xF2 && code != 0xF3 && code != 0xF4 && code != 0x23F) return {};
    const uint32_t c = 0xE000 + code;
    return {char(0xE0 | (c >> 12)), char(0x80 | ((c >> 6) & 0x3F)), char(0x80 | (c & 0x3F))};
}
// A label's text split into lines and at runs of two spaces or more.
std::vector<std::string> split(const std::string& text) {
    std::vector<std::string> out;
    std::string part;
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '\n' || text[i] == '\f' || (text[i] == ' ' && i + 1 < text.size() && text[i + 1] == ' ')) {
            if (!part.empty()) out.push_back(part);
            part.clear();
            while (i < text.size() && (text[i] == ' ' || text[i] == '\n' || text[i] == '\f')) ++i;
            continue;
        }
        part += text[i++];
    }
    if (!part.empty()) out.push_back(part);
    return out;
}
std::string number_text(unsigned type, unsigned value) {
    char buffer[16];
    switch (type) {
        case 1: case 7: std::snprintf(buffer, sizeof buffer, "+%u", value); break;
        case 2: case 8: std::snprintf(buffer, sizeof buffer, "-%u", value); break;
        case 3: case 9: std::snprintf(buffer, sizeof buffer, "%5u", value); break;
        case 4: case 10: return "?????";
        case 5: case 11: std::snprintf(buffer, sizeof buffer, "%3u", value); break;
        default: return "???";
    }
    return buffer;
}

bool active() {
    if (disabled || !dialogue::reader_configured()) return false;
    // Japanese in original image mode is the original screen; any translation always draws.
    // The original battle confirmation, when chosen so, counts as original image mode.
    const bool hd = (!presentation::image_mode.enabled() || presentation::image_mode.current() == 1) &&
                    !battle_page::original_screen();
    return hd || localization::catalog().locale != "ja";
}

void drawn(uint8_t* ram, uint32_t begin, uint32_t end, bool front) {
    if (end <= begin || end > 0x800000 || !active()) return;
    std::lock_guard lock(mutex);
    ++counts.passes;
    const auto list = records(ram, begin, end);
    // The records the engine drew, in its order: bodies (back pass), labels by slot, numbers.
    unsigned bodies = 0;
    if (!front)
        for (uint32_t n = 0; n < kBodyCount; ++n) {
            const uint8_t status = byte(ram, kBodies + n * kBodySize + 2);
            bodies += status >= 1 && status <= 3;
        }
    std::vector<uint32_t> labels;
    for (uint32_t n = 0; n < kLabelCount; ++n) {
        const int8_t kind = int8_t(byte(ram, kLabels + n * kLabelSize + 2));
        if (front ? (uint8_t(kind) == 2 || kind == 3) : (kind == 1 || kind == 3)) labels.push_back(n);
    }
    const bool with_numbers = list.size() == bodies + labels.size() + 1;
    if (!with_numbers && list.size() != bodies + labels.size()) {
        ++counts.mismatched;
        note("count:" + std::to_string(front) + ":" + std::to_string(list.size()) + ":" + std::to_string(bodies + labels.size()),
             {{"kind", "mismatch"}, {"front", front}, {"records", list.size()}, {"bodies", bodies}, {"labels", labels.size()}});
        return;
    }
    const std::string locale = localization::catalog().locale;
    std::vector<sprites::PlacedText> items;
    std::vector<uint32_t> replaced;
    json shown = json::array();
    float bounds[4] = {1e9f, 1e9f, -1e9f, -1e9f};
    const auto cover = [&](uint32_t rect) {
        const auto [x0, y0] = corner(ram, rect);
        const uint32_t w0 = word(ram, rect);
        bounds[0] = std::min(bounds[0], x0 / 4.f); bounds[1] = std::min(bounds[1], y0 / 4.f);
        bounds[2] = std::max(bounds[2], ((w0 >> 12) & 0xFFF) / 4.f); bounds[3] = std::max(bounds[3], (w0 & 0xFFF) / 4.f);
    };
    // 1. Check every label against its record and read what it shows.
    struct Cell { int x, y, width; uint16_t code; bool icon; };
    struct Label {
        uint32_t slot = 0, address = 0;
        uint16_t id = 0;
        int x = 0, y = 0;
        unsigned palette = 0;
        const Record* record = nullptr;
        std::vector<Cell> cells;
        std::string text, drawn;
        bool translated = false, consumed = false;
    };
    std::vector<Label> found;
    for (size_t i = 0; i < labels.size(); ++i) {
        Label l;
        l.record = &list[bodies + i];
        l.slot = labels[i]; l.address = kLabels + l.slot * kLabelSize;
        l.palette = byte(ram, l.address + 3) & 7;
        l.x = int32_t(word(ram, l.address + 4)); l.y = int32_t(word(ram, l.address + 8));
        if (l.record->palette != palette_data(ram, l.palette)) {
            ++counts.mismatched;
            note("palette:" + std::to_string(l.slot), {{"kind", "mismatch"}, {"what", "palette"}, {"slot", l.slot}});
            return;
        }
        if (l.record->rects.empty()) continue;
        // The original cells: narrow 8, wide 14, space 8, 0xFFFE a new line 14 lower. The
        // fetcher does not check the length: a long label runs on into the next slot.
        int cx = l.x, cy = l.y;
        for (unsigned k = 0; k < kLabelGlyphs; ++k) {
            const uint16_t code = half(ram, l.address + 0xC + 2 * k);
            if (code == 0xFFFF || code == 0xFFFD) break;
            if (code == 0xFFFE) { cx = l.x; cy += kLine; continue; }
            const int width = code == 0 || code < kWideFirst ? kNarrow : kWide;
            // A glyph neither the map nor the symbol font names (map icons) stays original.
            const std::string name = dialogue::glyph_string(code);
            if (code) l.cells.push_back({cx, cy, width, code, icon(code).empty() && (name.empty() || name.rfind("〔", 0) == 0)});
            cx += width;
        }
        if (l.cells.size() != l.record->rects.size() || !at(ram, l.record->rects.front(), l.cells.front().x, l.cells.front().y)) {
            ++counts.mismatched;
            note("cells:" + std::to_string(l.slot) + ":" + std::to_string(l.cells.size()),
                 {{"kind", "mismatch"}, {"what", "cells"}, {"slot", l.slot}, {"cells", l.cells.size()}, {"rects", l.record->rects.size()}});
            return;
        }
        if (dialogue::reader_owns(l.slot, l.x, l.y)) { ++counts.reader; continue; }
        l.id = half(ram, l.address);
        l.drawn = dialogue::glyph_text(ram, l.address + 0xC, kLabelGlyphs);
        l.text = dialogue::label_text(ram, l.address);
        l.translated = !l.text.empty() && l.text != l.drawn;
        found.push_back(std::move(l));
    }
    // Bodies the reader does not show (choice and objective windows): the current page,
    // lines 16 apart, drawn whole once it is all there.
    struct Body { const Record* record; uint32_t address; int x, y; unsigned palette; };
    std::vector<Body> bodies_found;
    for (uint32_t n = 0, k = 0; n < kBodyCount && !front; ++n) {
        const uint32_t address = kBodies + n * kBodySize;
        const uint8_t status = byte(ram, address + 2);
        if (status < 1 || status > 3) continue;
        const Record& record = list[k++];
        const unsigned palette = byte(ram, address + 3) & 7;
        const int x = int32_t(word(ram, address + 4)), y = int32_t(word(ram, address + 8));
        if (record.palette != palette_data(ram, palette) || record.rects.empty() || dialogue::reader_owns(n, x, y)) continue;
        // The page the engine draws: skip to it past the 0xFFFD page ends, then to 0xFFFF / 0xFFFD.
        const unsigned page = byte(ram, address + 0x214) && byte(ram, address + 0x215) ? byte(ram, address + 0x215) : 0;
        unsigned k2 = 0, seen = 0;
        while (seen < page && k2 < 256) seen += half(ram, address + 0xC + 2 * k2++) == 0xFFFD;
        unsigned glyphs = 0;
        int cx = x, cy = y;
        bool first = true, placed_ok = true;
        for (; k2 < 256; ++k2) {
            const uint16_t code = half(ram, address + 0xC + 2 * k2);
            if (code == 0xFFFF || code == 0xFFFD) break;
            if (code == 0xFFFE) { cx = x; cy += 16; continue; }
            const int width = code == 0 || code < kWideFirst ? kNarrow : kWide;
            if (code) {
                if (first && !at(ram, record.rects.front(), cx, cy)) placed_ok = false;
                first = false; ++glyphs;
            }
            cx += width;
        }
        if (!placed_ok || glyphs != record.rects.size()) {
            note("body:" + std::to_string(n) + ":" + std::to_string(glyphs), {{"kind", "mismatch"}, {"what", "body"}, {"glyphs", glyphs}, {"rects", record.rects.size()}});
            continue;
        }
        bodies_found.push_back({&record, address, x, y, palette});
    }
    for (const auto& body : bodies_found) {
        const std::string text = dialogue::body_page(ram, body.address);
        if (text.empty()) continue;
        int widest = 0, line = 0;
        for (unsigned k2 = 0; k2 < 256; ++k2) {
            const uint16_t code = half(ram, body.address + 0xC + 2 * k2);
            if (code == 0xFFFF) break;
            if (code == 0xFFFE || code == 0xFFFD) { line = 0; continue; }
            line += code == 0 || code < kWideFirst ? kNarrow : kWide;
            widest = std::max(widest, line);
        }
        Style style = label_style(locale, 0, false);
        std::string shown_text = catalog_form(text);
        style.pitch = 16 / style.size;
        style.width = widest * 1.1 + 6;
        // A label the original prints right after a one-line body (発進！スイームルグ|クリア)
        // joins it as one sentence in the run's width, on the labels' line pitch so it shares
        // their baseline.
        for (auto& l : found) {
            if (shown_text.find("<BR>") != std::string::npos) break;
            if (l.id == 0 || l.consumed || l.cells.empty() || l.palette != body.palette) continue;
            const auto& c0 = l.cells.front();
            if (std::abs(c0.y - body.y) > 2 || c0.x < body.x + widest - 2 || c0.x > body.x + widest + int(kNarrow)) continue;
            shown_text += (locale == "en" ? " " : "") + (l.text.empty() ? l.drawn : l.text);
            style.pitch = kLine / style.size;
            style.width = 0;
            style.max_width = l.cells.back().x + l.cells.back().width - body.x - trailing_blank(l.drawn);
            l.consumed = true;
            break;
        }
        float tint[3];
        colour(ram, body.palette, tint);
        items.push_back(placed(style, locale, shown_text, "body", float(body.x), float(body.y), tint));
        for (const uint32_t rect : body.record->rects) { replaced.push_back(rect); cover(rect); }
        shown.push_back({{"body", body.address}, {"x", body.x}, {"y", body.y}, {"text", text}});
        note("body:" + locale + ":" + text, {{"kind", "body"}, {"record", half(ram, body.address)}, {"text", text}});
    }
    // 2. A translated sentence with a gap for a number: the number the caller printed in
    // that gap moves into the sentence, which then reads in its own word order.
    struct Part { int x, y, width; };
    const auto parts_of = [](const Label& l) {
        std::vector<Part> parts;
        const Cell* previous = nullptr;
        for (const auto& c : l.cells) {
            if (c.icon) { previous = nullptr; continue; }
            if (previous && c.y == previous->y && c.x - (previous->x + previous->width) < 2 * int(kNarrow))
                parts.back().width = c.x + c.width - parts.back().x;
            else parts.push_back({c.x, c.y, c.width});
            previous = &c;
        }
        return parts;
    };
    std::vector<std::string> composed(found.size());
    for (size_t i = 0; i < found.size(); ++i) {
        Label& l = found[i];
        if (l.id == 0 || !l.translated) continue;
        const auto parts = parts_of(l);
        std::vector<std::string> fills;
        for (size_t k = 0; k + 1 < parts.size(); ++k) {
            if (parts[k].y != parts[k + 1].y) continue;
            const int gap0 = parts[k].x + parts[k].width, gap1 = parts[k + 1].x;
            for (auto& n : found) {
                if (n.id != 0 || n.consumed || n.cells.empty() || n.cells.front().y != parts[k].y) continue;
                const int n0 = n.cells.front().x, n1 = n.cells.back().x + n.cells.back().width;
                if (n0 >= gap0 - int(kNarrow) && n1 <= gap1 + int(kNarrow)) {
                    std::string value = n.drawn;
                    value.erase(0, value.find_first_not_of(' '));
                    value.erase(value.find_last_not_of(' ') + 1);
                    fills.push_back(value);
                    n.consumed = true;
                    break;
                }
            }
        }
        if (fills.empty()) continue;
        // Put them in the translation's own gaps (runs of two spaces), in order.
        std::string out;
        size_t used = 0;
        for (size_t c = 0; c < l.text.size();) {
            if (l.text[c] == ' ' && c + 1 < l.text.size() && l.text[c + 1] == ' ') {
                while (c < l.text.size() && l.text[c] == ' ') ++c;
                if (used < fills.size()) out += locale == "en" ? " " + fills[used++] + " " : fills[used++];
                else out += ' ';
                continue;
            }
            out += l.text[c++];
        }
        if (used < fills.size()) { for (auto& n : found) if (n.consumed) n.consumed = false; continue; }  // no gap to hold them
        out.erase(0, out.find_first_not_of(' '));
        out.erase(out.find_last_not_of(' ') + 1);
        composed[i] = out;
    }
    // What starts where on each line: label parts, printed numbers, number pool entries.
    // A piece of text may run until the next of these on its line.
    struct Start { int x, y; int owner; };   // owner: label index, -1 for the number pool
    std::vector<Start> starts;
    for (size_t i = 0; i < found.size(); ++i)
        for (const auto& part : parts_of(found[i])) starts.push_back({part.x, part.y, int(i)});
    for (uint32_t n = 0; n < kNumberCount; ++n) {
        const uint32_t entry = kNumbers + n * kNumberSize;
        const unsigned type = byte(ram, entry);
        if (!type || (front ? type < 7 : type >= 7)) continue;
        const std::string text = number_text(type, half(ram, entry + 2));
        const size_t lead = std::min(text.find_first_not_of(' '), text.size());
        starts.push_back({half(ram, entry + 4) + int(kCell * lead), half(ram, entry + 6), -1});
    }
    // Labels starting in one column on nearby lines share a window, so the widest original
    // among them is the room the window has: no less (ムゲ兵 under ムゲ小型戦闘機), and,
    // since the frame is out of sight, no more (変形 among the unit commands).
    // 0 for a label alone in its column.
    const auto column = [&](int x, int y) {
        std::vector<std::pair<int, int>> lines;   // y, original width
        for (size_t i = 0; i < found.size(); ++i) {
            if (found[i].consumed || found[i].id == 0) continue;
            const auto parts = parts_of(found[i]);
            if (!parts.empty() && std::abs(parts.front().x - x) <= 1) lines.push_back({parts.front().y, parts.front().width});
        }
        std::sort(lines.begin(), lines.end());
        const auto at = std::find_if(lines.begin(), lines.end(), [y](const auto& line) { return line.first == y; });
        if (at == lines.end()) return 0;
        int widest = 0;
        for (auto k = at; k + 1 != lines.end() && (k + 1)->first - k->first <= 24; ++k) widest = std::max(widest, (k + 1)->second);
        for (auto k = at; k != lines.begin() && k->first - (k - 1)->first <= 24; --k) widest = std::max(widest, (k - 1)->second);
        return widest ? std::max(widest, at->second) : 0;
    };
    // A label the original continues on the same line where it ends (ROMカートリッジ|のデータを
    // ロードします): the translation reads as one sentence from the first piece's start, in
    // the run's own width. The follower is alone in its column, so table cells stay apart.
    std::vector<int> run(found.size(), 0);   // label -> the original run's width it now holds
    for (size_t i = 0; i < found.size(); ++i) {
        Label& a = found[i];
        if (a.id == 0 || a.consumed) continue;
        const auto pa = parts_of(a);
        if (pa.size() != 1) continue;
        for (bool joined = true; joined;) {
            joined = false;
            const int end = pa.front().x + (run[i] ? run[i] : pa.front().width);
            for (size_t j = 0; j < found.size(); ++j) {
                Label& b = found[j];
                if (j == i || b.id == 0 || b.consumed || b.palette != a.palette || (!a.translated && !b.translated)) continue;
                const auto pb = parts_of(b);
                if (pb.size() != 1 || std::abs(pb.front().y - pa.front().y) > 2) continue;
                if (pb.front().x < end - 2 || pb.front().x > end + int(kNarrow) || column(pb.front().x, pb.front().y)) continue;
                const std::string first = composed[i].empty() ? a.text : composed[i];
                const std::string second = composed[j].empty() ? (b.text.empty() ? b.drawn : b.text) : composed[j];
                composed[i] = first + (locale == "en" ? " " : "") + second;
                run[i] = pb.front().x + pb.front().width - pa.front().x - trailing_blank(b.drawn) + 1;
                a.translated = true; b.consumed = true; joined = true;
                break;
            }
        }
    }
    const auto widened = menu_widen::windows();
    const auto room = [&](size_t self, int x, int y, int width) {
        int next = 316;   // the screen's right edge, less a margin
        for (const auto& start : starts) {
            if (start.owner >= 0 && (size_t(start.owner) == self || found[size_t(start.owner)].consumed)) continue;
            if (std::abs(start.y - y) <= 6 && start.x > x + 2) next = std::min(next, start.x - 4);
        }
        // Text as drawn keeps the original's width (its narrow kana set it); a translation
        // takes its column's width less the pixel the ROM's glyphs leave on their right, or
        // alone may run on a little where nothing follows.
        const int widest = column(x, y);
        double spare = !found[self].translated ? width + 2.0
                     : run[self] ? run[self] - 1.0 : widest ? widest - 1.0 : width * 1.35 + 4;
        // In a menu drawn wider than the original's (menu_widen.hpp), up to its inner edge.
        if (found[self].translated)
            for (const auto& w : widened)
                if (x >= w.x0 && x < w.x1 && y >= w.y0 - 2 && y <= w.y1) spare = w.x1 - x;
        const double limit = std::min(double(next - x), spare) - (found[self].translated ? kInset : 0);
        return std::max(double(width) - (found[self].translated ? 0 : 2), limit);
    };
    // Text printed glyph by glyph (カラオケ lyrics are one label per glyph): the ROM's kana are
    // half-width cells, the font's full-width, so where a run of touching cells is too
    // narrow for the font it is set as one line: each glyph gets a share of the run's width
    // by its font width, all narrowed alike and at the same size.
    struct Fit { float x; Style style; };
    std::map<std::pair<size_t, size_t>, Fit> fitted;   // (label, cell)
    {
        struct Glyph { size_t label, cell; int x, y, width; double em; };
        std::vector<Glyph> glyphs;
        for (size_t i = 0; i < found.size(); ++i) {
            const Label& l = found[i];
            if (l.id != 0 || l.consumed) continue;
            for (size_t k = 0; k < l.cells.size(); ++k) {
                const auto& c = l.cells[k];
                if (c.icon) continue;
                const std::string name = dialogue::glyph_string(c.code);
                const bool narrow = !icon(c.code).empty() || name.empty() || uint8_t(name[0]) < 0x80;
                glyphs.push_back({i, k, c.x, c.y, c.width, narrow ? .6 : 1.0});
            }
        }
        std::sort(glyphs.begin(), glyphs.end(), [](const Glyph& a, const Glyph& b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
        const Style base = label_style("ja", 0, true);
        for (size_t a = 0, b = 0; a < glyphs.size(); a = b) {
            // Cells a space (8) or more apart start a new run: the lyrics' own gaps stay.
            for (b = a + 1; b < glyphs.size() && glyphs[b].y == glyphs[a].y; ++b) {
                const int gap = glyphs[b].x - (glyphs[b - 1].x + glyphs[b - 1].width);
                if (gap < -1 || gap >= int(kNarrow)) break;
            }
            double ems = 0;
            for (size_t n = a; n < b; ++n) ems += glyphs[n].em;
            const double span = glyphs[b - 1].x + glyphs[b - 1].width - glyphs[a].x, f = span / (ems * base.size);
            if (b - a < 2 || f >= 1) continue;
            const double narrowed = std::floor(f * 50) / 50;   // a few widths, so glyphs share renders
            double left = glyphs[a].x;
            for (size_t n = a; n < b; ++n) {
                const double share = span * glyphs[n].em / ems;
                Style s = base;
                s.min_size = s.size;
                s.max_width = glyphs[n].em * s.size * narrowed;
                s.condense_min = narrowed - .005;
                fitted[{glyphs[n].label, glyphs[n].cell}] = {float(left + share / 2), s};
                left += share;
            }
        }
    }
    // A printed number padded between its sign and its digits ("+     0", "+    5") has the
    // digits right-aligned but the sign at the string's own start, so rows whose numbers are
    // padded to different widths (the map's 資金 / 経験値 reward box) show their signs a cell
    // apart. Give the sign the leftmost column its row group uses, so it lines up the way the
    // digits already do. Signs written tight against their digits ("+30", "+10%") keep theirs.
    std::map<size_t, float> sign_x;   // label -> where its leading sign goes
    {
        struct Sign { size_t label; int x, y, right; };
        std::vector<Sign> signs;
        for (size_t i = 0; i < found.size(); ++i) {
            const Label& l = found[i];
            if (l.id != 0 || l.consumed || l.cells.size() < 2) continue;
            const auto& lead = l.cells.front();
            const std::string mark = lead.code == kLongDash ? std::string("-") : dialogue::glyph_string(lead.code);
            if (lead.icon || (mark != "+" && mark != "-")) continue;
            // Padded: a gap between the sign's cell and the digits that follow it.
            const auto& next = l.cells[1];
            if (next.y != lead.y || next.x <= lead.x + lead.width) continue;
            const auto& last = l.cells.back();
            signs.push_back({i, lead.x, lead.y, last.x + last.width});
        }
        for (const auto& a : signs) {
            int column = a.x;
            for (const auto& b : signs)
                if (std::abs(b.y - a.y) > int(kLine) && b.right == a.right)
                    column = std::min(column, b.x);
            if (column != a.x) sign_x[a.label] = float(column) + kNarrow / 2.f;
        }
    }
    // 3. Draw.
    for (size_t i = 0; i < found.size(); ++i) {
        Label& l = found[i];
        const Record& record = *l.record;
        float tint[3];
        colour(ram, l.palette, tint);
        const auto take = [&](size_t k) { replaced.push_back(record.rects[k]); cover(record.rects[k]); };
        std::string text = composed[i].empty() ? l.text : composed[i];
        if (l.consumed) {
            // Printed into a translated sentence.
            for (size_t k = 0; k < l.cells.size(); ++k) take(k);
        } else if (l.id == 0) {
            // Text the caller printed (numbers, sprintf): each glyph in its own cell keeps
            // the columns.
            for (size_t k = 0; k < l.cells.size(); ++k) {
                const auto& c = l.cells[k];
                if (c.icon) continue;
                std::string glyph = icon(c.code);
                if (glyph.empty()) glyph = c.code == kLongDash ? std::string("-") : dialogue::glyph_string(c.code);
                // Cell by cell the regular face in every language: Condensed digits leave gaps.
                if (const auto fit = fitted.find({i, k}); fit != fitted.end())
                    items.push_back(placed(fit->second.style, "ja", glyph, "glyph", fit->second.x, float(c.y), tint));
                else {
                    float x = c.x + c.width / 2.f;
                    if (k == 0) if (const auto moved = sign_x.find(i); moved != sign_x.end()) x = moved->second;
                    items.push_back(placed(label_style("ja", 0, true), "ja", glyph, "glyph", x, float(c.y), tint));
                }
                take(k);
            }
            counts.glyphs += l.cells.size();
        } else if (!text.empty()) {
            // A weapon's list name: the original's marker glyphs, not the letters standing for them.
            if (const auto mark = icon(l.cells.front().code); !mark.empty()) {
                for (const char* letter : {"格", "射"}) if (text.rfind(letter, 0) == 0) { text = mark + text.substr(std::strlen(letter)); break; }
            }
            if (const auto mark = icon(l.cells.back().code); !mark.empty()) {
                for (const char* letter : {"MAP", "P", "B"}) {
                    const size_t n = std::strlen(letter);
                    if (text.size() >= n && text.compare(text.size() - n, n, letter) == 0) { text = text.substr(0, text.size() - n) + mark; break; }
                }
            }
            // Glyphs with no name stay as the original draws them.
            for (size_t at = text.find("〔"); at != std::string::npos; at = text.find("〔")) {
                const size_t close = text.find("〕", at);
                if (close == std::string::npos) break;
                text.erase(at, close + std::strlen("〕") - at);
            }
            const auto parts = parts_of(l);
            const auto pieces = split(text);
            const int inset = l.translated ? kInset : 0;
            if (parts.empty() || pieces.empty()) {
            } else if (composed[i].empty() && pieces.size() == parts.size() && parts.size() > 1) {
                // Runs of two spaces or more hold places for numbers other labels draw:
                // each part goes to where the original part starts.
                for (size_t k = 0; k < parts.size(); ++k)
                    items.push_back(placed(label_style(locale, room(i, parts[k].x, parts[k].y, parts[k].width), false), locale, pieces[k], "label",
                                           float(parts[k].x + inset), float(parts[k].y), tint));
            } else {
                int widest = 0;
                for (const auto& part : parts) widest = std::max(widest, part.x + part.width - parts.front().x);
                const bool one_line = parts.back().y == parts.front().y && text.find('\n') == std::string::npos;
                const double width = one_line ? room(i, parts.front().x, parts.front().y, widest) : widest * 1.15 + 3;
                items.push_back(placed(label_style(locale, width, false), locale, catalog_form(text), "label",
                                       float(parts.front().x + inset), float(l.cells.front().icon ? parts.front().y : l.y), tint));
            }
            counts.translated += l.translated;
            for (size_t k = 0; k < l.cells.size(); ++k) if (!l.cells[k].icon) take(k);
        }
        ++counts.labels;
        shown.push_back({{"slot", l.slot}, {"record", l.id}, {"x", l.x}, {"y", l.y}, {"palette", l.palette},
                         {"text", text}, {"translated", l.translated}, {"consumed", l.consumed}});
        note("label:" + locale + ":" + text, {{"kind", "label"}, {"record", l.id}, {"text", text},
             {"original", l.drawn}, {"translated", l.translated}, {"front", front}});
    }
    if (with_numbers) {
        const Record& record = list.back();
        size_t next = 0;
        bool fits = true;
        std::vector<std::pair<std::string, std::array<int, 3>>> numbers;   // text, x, y, type
        for (uint32_t n = 0; n < kNumberCount; ++n) {
            const uint32_t entry = kNumbers + n * kNumberSize;
            const unsigned type = byte(ram, entry);
            if (!type || (front ? type < 7 : type >= 7)) continue;
            numbers.push_back({number_text(type, half(ram, entry + 2)), {half(ram, entry + 4), half(ram, entry + 6), int(type)}});
        }
        size_t total = 0;
        for (const auto& [text, where] : numbers) total += text.size();
        if (total != record.rects.size()) fits = false;
        for (const auto& [text, where] : numbers) {
            if (!fits) break;
            if (!text.empty() && !at(ram, record.rects[next], where[0], where[1])) { fits = false; break; }
            const bool outlined = where[2] == 1 || where[2] == 2 || where[2] == 7 || where[2] == 8;
            const float white[3] = {1, 1, 1};
            for (size_t k = 0; k < text.size(); ++k, ++next) {
                const uint32_t rect = record.rects[next];
                replaced.push_back(rect); cover(rect);
                if (text[k] == ' ') continue;
                items.push_back(placed(number_style(outlined), "ja", std::string(1, text[k]), "number",
                                       where[0] + kCell * float(k) + kCell / 2.f, float(where[1]), white));
            }
            ++counts.numbers;
            shown.push_back({{"number", text}, {"x", where[0]}, {"y", where[1]}, {"type", where[2]}});
        }
        if (!fits) {
            ++counts.mismatched;
            note("numbers:" + std::to_string(front), {{"kind", "mismatch"}, {"what", "numbers"}, {"rects", record.rects.size()}, {"chars", total}});
            // Labels still go native; the numbers keep their original glyphs.
            replaced.erase(std::remove_if(replaced.begin(), replaced.end(), [&](uint32_t r) {
                return std::find(record.rects.begin(), record.rects.end(), r) != record.rects.end(); }), replaced.end());
            items.erase(std::remove_if(items.begin(), items.end(), [](const sprites::PlacedText& t) {
                return t.job.key.rfind("number|", 0) == 0; }), items.end());
        }
    }
    last[front] = shown;
    if (items.empty() || replaced.empty()) return;
    std::sort(replaced.begin(), replaced.end());
    const uint32_t marker = replaced.front();
    if (!sprites::place_texts(ram, marker, bounds, items)) return;
    for (const uint32_t rect : replaced)
        if (rect != marker)
            for (uint32_t k = 0; k < 24; k += 4) put(ram, rect + k, 0);
    ++counts.drawn;
}
}

namespace {
// Map battle figures (80209900, tactical map overlay): per cell 0-5 the 1159 cell index at
// 80228530, a shown flag at 80228536 and x, y words at 80228548. Cell indices: 0 blank,
// 1 '+', 2 '-', 3-12 the outlined digits, 13-22 the shadowed ones.
constexpr uint32_t kDamageCells = 0x228530, kDamageShown = 0x228536, kDamagePlaces = 0x228548;
void damage_drawn(uint8_t* ram, uint32_t begin, uint32_t end) {
    if (end <= begin || end > 0x800000 || !active()) return;
    std::lock_guard lock(mutex);
    const auto list = records(ram, begin, end);
    if (list.size() != 1) return;
    const auto& rects = list.front().rects;
    std::vector<sprites::PlacedText> items;
    float bounds[4] = {1e9f, 1e9f, -1e9f, -1e9f};
    size_t next = 0;
    std::string figure;
    for (uint32_t k = 0; k < 6; ++k) {
        if (!byte(ram, kDamageShown + k)) continue;
        if (next >= rects.size()) return;
        const uint32_t rect = rects[next++];
        const unsigned cell = byte(ram, kDamageCells + k);
        const int x = int32_t(word(ram, kDamagePlaces + 8 * k)), y = int32_t(word(ram, kDamagePlaces + 8 * k + 4));
        if (!at(ram, rect, x, y)) { note("damage-place", {{"kind", "mismatch"}, {"what", "damage"}}); return; }
        const auto [x0, y0] = corner(ram, rect);
        const uint32_t w0 = word(ram, rect);
        bounds[0] = std::min(bounds[0], x0 / 4.f); bounds[1] = std::min(bounds[1], y0 / 4.f);
        bounds[2] = std::max(bounds[2], ((w0 >> 12) & 0xFFF) / 4.f); bounds[3] = std::max(bounds[3], (w0 & 0xFFF) / 4.f);
        const std::string glyph = cell == 1 ? "+" : cell == 2 ? "-" : cell >= 3 && cell <= 12 ? std::string(1, char('0' + cell - 3))
                                  : cell >= 13 && cell <= 22 ? std::string(1, char('0' + cell - 13)) : std::string();
        figure += glyph.empty() ? " " : glyph;
        if (glyph.empty()) continue;
        const float white[3] = {1, 1, 1};
        items.push_back(placed(number_style(cell <= 12), "ja", glyph, "number", x + kCell / 2.f, float(y), white));
    }
    if (next != rects.size() || items.empty()) return;
    if (!sprites::place_texts(ram, rects.front(), bounds, items)) return;
    for (const uint32_t rect : rects)
        if (rect != rects.front())
            for (uint32_t k = 0; k < 24; k += 4) put(ram, rect + k, 0);
    ++counts.numbers;
    // Which tactical-map state and sub-state is showing the figure. Needed to
    // replay the same display after a battle whose animation the player aborted.
    note("damage:" + figure, {{"kind", "damage"}, {"figure", figure},
        {"map_state", byte(ram, 0x172EB0)}, {"map_sub", byte(ram, 0x172EB2)},
        {"step", int32_t(word(ram, 0x22722C))}, {"step_frame", int32_t(word(ram, 0x22731C))},
        {"cell", int16_t(half(ram, 0x227BC6))}, {"side", byte(ram, 0x227B60)}});
}

// The number beside a battle banner (8009504C, sprite mode 10: クリティカル 3000). After the
// banner it resets the mode (E3000C00, 0), then per drawn cell loads 8x16 texels of 1159 at
// T = 40, S = 144 + 8 * digit (LOADTILE S in 8-bit texels: half the pixel), sets the tile
// size and draws one TEXRECT; leading zeros take no cell. Redrawn cell by cell in the
// banners' green and black rim (sprite_text.cpp banner_style), narrowed into the cell.
Style banner_digit_style() {
    Style s;
    s.bold = true; s.center = false; s.origin = Style::Origin::center;
    s.size = 15; s.min_size = 15; s.pitch = 16 / s.size;
    s.max_width = kCell; s.condense_min = .7;
    const float top[3] = {.90f, 1.f, .90f}, bottom[3] = {.29f, .87f, .55f};
    for (int c = 0; c < 3; ++c) { s.fill_top[c] = top[c]; s.fill_bottom[c] = bottom[c]; s.outline[c] = .02f; }
    s.outline_px = 1.0; s.outline_alpha = 1; s.shadow_px = .7; s.shadow_alpha = .45;
    return s;
}
void banner_number_drawn(uint8_t* ram, uint32_t begin, uint32_t end) {
    if (end <= begin || end > 0x800000 || !active()) return;
    uint32_t from = 0;
    for (uint32_t p = begin; p + 8 <= end; p += 8)
        if (word(ram, p) == 0xE3000C00 && word(ram, p + 4) == 0) from = p + 8;
    if (!from) return;
    std::lock_guard lock(mutex);
    std::vector<uint32_t> rects;
    std::vector<sprites::PlacedText> items;
    float bounds[4] = {1e9f, 1e9f, -1e9f, -1e9f};
    uint32_t load = 0;
    std::string figure;
    for (uint32_t p = from; p + 24 <= end; p += 8) {
        const uint32_t w0 = word(ram, p);
        if (w0 >> 24 == 0xF4) { load = w0; continue; }
        if (w0 >> 24 != 0xE4 || word(ram, p - 8) >> 24 != 0xF2 || word(ram, p + 8) >> 24 != 0xE1 || word(ram, p + 16) >> 24 != 0xF1) continue;
        const unsigned s = ((load >> 12) & 0xFFF) / 2;
        if (!load || (load & 0xFFF) != 40 * 4 || s < 144 || (s - 144) % 8 || s > 144 + 9 * 8) {
            note("banner-number", {{"kind", "mismatch"}, {"what", "banner number"}});
            return;
        }
        const auto [x0, y0] = corner(ram, p);
        bounds[0] = std::min(bounds[0], x0 / 4.f); bounds[1] = std::min(bounds[1], y0 / 4.f);
        bounds[2] = std::max(bounds[2], ((w0 >> 12) & 0xFFF) / 4.f); bounds[3] = std::max(bounds[3], (w0 & 0xFFF) / 4.f);
        const std::string glyph(1, char('0' + (s - 144) / 8));
        figure += glyph;
        const float white[3] = {1, 1, 1};
        items.push_back(placed(banner_digit_style(), "ja", glyph, "banner-digit", x0 / 4.f + kCell / 2.f, y0 / 4.f, white));
        rects.push_back(p);
        p += 16;
    }
    if (items.empty() || !sprites::place_texts(ram, rects.front(), bounds, items)) return;
    for (const uint32_t rect : rects)
        if (rect != rects.front())
            for (uint32_t k = 0; k < 24; k += 4) put(ram, rect + k, 0);
    ++counts.numbers;
    note("banner-number:" + figure, {{"kind", "banner number"}, {"figure", figure}});
}
}

void configure(const std::filesystem::path& output) {
    if (installed) return;
    installed = true;
    if (const char* value = std::getenv("SRW64_NATIVE_UI_TEXT"); value && std::string(value) == "0") disabled = true;
    if (!output.empty()) log.open(output / "ui-text.jsonl");
    srw64_game_hooks.ui_text_drawn = drawn;
    srw64_game_hooks.damage_drawn = damage_drawn;
    srw64_game_hooks.banner_number_drawn = banner_number_drawn;
}

json state() {
    std::lock_guard lock(mutex);
    const auto texts = sprites::text_counts();
    return {{"enabled", installed && !disabled}, {"passes", counts.passes}, {"drawn", counts.drawn},
            {"mismatched", counts.mismatched}, {"labels", counts.labels}, {"translated", counts.translated},
            {"glyphs", counts.glyphs}, {"numbers", counts.numbers}, {"reader_owned", counts.reader},
            {"items_placed", texts.placed}, {"items_waiting", texts.waiting}, {"back", last[0]}, {"front", last[1]}};
}
}
