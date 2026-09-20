#include "text/unicode.hpp"
#include "presentation/raster_image.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

// Test the compatibility entry points too, without pulling in a renderer header.
namespace srw64::dialogue {
std::u16string utf16(const std::string&);
std::string utf8(const std::u16string&);
}
namespace {
unsigned checks{};
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F function, const char* message) {
    ++checks;
    try { function(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error(message);
}
void unicode() {
    using srw64::text::to_utf16;
    using srw64::text::to_utf8;
    const std::vector<std::pair<std::string, std::u16string>> cases = {
        {"", u""}, {"ASCII", u"ASCII"}, {"日本語／简体中文", u"日本語／简体中文"},
        {"e\xCC\x81", u"e\u0301"}, {"\xF0\x9F\x9A\x80", u"\U0001F680"},
        {"\x7F", u"\u007F"}, {"\xC2\x80", u"\u0080"}, {"\xDF\xBF", u"\u07FF"},
        {"\xE0\xA0\x80", u"\u0800"}, {"\xED\x9F\xBF", u"\uD7FF"},
        {"\xEE\x80\x80", u"\uE000"}, {"\xEF\xBF\xBF", u"\uFFFF"},
        {"\xF0\x90\x80\x80", u"\U00010000"}, {"\xF4\x8F\xBF\xBF", u"\U0010FFFF"},
        {std::string("A\0B", 3), std::u16string(u"A\0B", 3)},
        {"\xEF\xBB\xBF" "text", u"\uFEFFtext"},
        {"👩‍👩‍👧‍👦", u"👩‍👩‍👧‍👦"},
    };
    for (const auto& [utf8, utf16] : cases) {
        check(to_utf16(utf8) == utf16, "UTF-8 reference conversion");
        check(to_utf8(utf16) == utf8, "UTF-16 reference conversion");
        check(srw64::dialogue::utf16(utf8) == utf16, "dialogue UTF-16 entry point");
        check(srw64::dialogue::utf8(utf16) == utf8, "dialogue UTF-8 entry point");
    }
    for (const std::string bad : {"\x80", "\xBF", "\xC0\x80", "\xC1\xBF", "\xC2", "\xC2" "A",
            "\xE0\x80\x80", "\xE1\x80", "\xE1\x80" "A", "\xED\xA0\x80", "\xED\xBF\xBF",
            "\xF0\x80\x80\x80", "\xF0\x90\x80", "\xF4\x90\x80\x80", "\xF5\x80\x80\x80", "\xFF"}) {
        rejects([&] { to_utf16(bad); }, "malformed UTF-8 accepted");
    }
    for (const std::u16string& bad : {std::u16string{char16_t(0xD800)}, std::u16string{char16_t(0xDFFF)},
            std::u16string{char16_t(0xD800), u'A'}, std::u16string{char16_t(0xD800), char16_t(0xDBFF)},
            std::u16string{u'A', char16_t(0xDC00)}}) {
        rejects([&] { to_utf8(bad); }, "unpaired UTF-16 surrogate accepted");
    }
    // Exercise every Unicode scalar, including supplementary planes and
    // noncharacters (valid scalar values). This is conversion, not normalization.
    for (uint32_t scalar = 0; scalar <= 0x10FFFF; ++scalar) {
        if (scalar >= 0xD800 && scalar <= 0xDFFF) continue;
        std::u16string value;
        if (scalar < 0x10000) value.push_back(static_cast<char16_t>(scalar));
        else {
            const auto rest = scalar - 0x10000;
            value.push_back(static_cast<char16_t>(0xD800 + (rest >> 10)));
            value.push_back(static_cast<char16_t>(0xDC00 + (rest & 0x3FF)));
        }
        check(to_utf16(to_utf8(value)) == value, "scalar round trip");
    }
    const auto decomposed = to_utf16("e\xCC\x81");
    check(decomposed.size() == 2 && decomposed != to_utf16("\xC3\xA9"), "converter normalized a combining sequence");
}
void surfaces() {
    using srw64::presentation::Bgra8Surface;
    Bgra8Surface image(3, 2);
    image.validate();
    check(image.row_bytes() == 12 && image.pixels.size() == 24, "tightly packed surface extent");
    check(std::all_of(image.pixels.begin(), image.pixels.end(), [](auto b) { return b == 0; }), "new surface is not transparent");
    image.pixels[0] = 12;
    auto copy = image; copy.pixels[0] = 13;
    check(image.pixels[0] == 12, "surface did not own its pixels");
    check(Bgra8Surface::required_bytes(8192, 8192) == size_t(268435456), "maximum extent overflow");
    for (const auto& [w, h] : std::vector<std::pair<uint32_t,uint32_t>>{
            {0, 1}, {1, 0}, {8193, 1}, {1, 8193}, {std::numeric_limits<uint32_t>::max(), 2}})
        rejects([&] { Bgra8Surface bad(w, h); }, "invalid extent accepted");
    image.pixels.pop_back();
    rejects([&] { image.validate(); }, "truncated pixel buffer accepted");
    image.pixels.resize(25);
    rejects([&] { image.validate(); }, "oversized pixel buffer accepted");
}
}
int main() {
    try { unicode(); surfaces(); std::cout << checks << " Unicode and pixel-contract checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
