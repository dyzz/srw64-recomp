// Checks a cut-down ICU data library (tools/release/icu_data.py) where it ships:
// grapheme and line breaks as src/native/text/portable_text.cpp asks for them,
// against the breaks the full data gives (EXPECTED, from ICU 78 with all its data).
// Exit 0 when every break matches; built and run by the macOS, Linux and Windows
// dependency steps next to the ICU libraries they ship.
#include <unicode/ubrk.h>
#include <unicode/uloc.h>
#include <unicode/ustring.h>
#include <cstdio>
#include <string>
#include <vector>

namespace {
struct Case { const char* tag; const char* text; const char* graphemes; const char* lines; };
// Breaks as UTF-16 offsets, comma-separated.
#include "icu_check_cases.inc"

std::string join(const std::vector<int>& values) {
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) out += (i ? "," : "") + std::to_string(values[i]);
    return out;
}

bool breaks(UBreakIteratorType kind, const char* locale, const std::u16string& text, std::string& out) {
    UErrorCode status = U_ZERO_ERROR;
    UBreakIterator* it = ubrk_open(kind, locale, reinterpret_cast<const UChar*>(text.data()), int32_t(text.size()), &status);
    if (U_FAILURE(status)) { std::fprintf(stderr, "ubrk_open(%s): %s\n", locale, u_errorName(status)); return false; }
    std::vector<int> found;
    for (int32_t p = ubrk_first(it); (p = ubrk_next(it)) != UBRK_DONE;) found.push_back(p);
    ubrk_close(it);
    out = join(found);
    return true;
}
}

int main(int argc, char** argv) {
    const bool print = argc > 1 && std::string(argv[1]) == "--print";
    int failures = 0;
    for (const Case& c : CASES) {
        char locale[ULOC_FULLNAME_CAPACITY]; int32_t parsed = 0; UErrorCode status = U_ZERO_ERROR;
        uloc_forLanguageTag(c.tag, locale, sizeof locale, &parsed, &status);
        status = U_ZERO_ERROR; uloc_setKeywordValue("lb", "strict", locale, sizeof locale, &status);
        std::u16string text(std::char_traits<char>::length(c.text) + 1, u'\0'); int32_t length = 0; status = U_ZERO_ERROR;
        u_strFromUTF8(reinterpret_cast<UChar*>(text.data()), int32_t(text.size()), &length, c.text, -1, &status);
        text.resize(size_t(length));
        std::string graphemes, lines;
        if (!breaks(UBRK_CHARACTER, "root", text, graphemes) || !breaks(UBRK_LINE, locale, text, lines)) { ++failures; continue; }
        if (print) { std::printf("    {\"%s\", \"%s\", \"%s\", \"%s\"},\n", c.tag, c.text, graphemes.c_str(), lines.c_str()); continue; }
        if (graphemes != c.graphemes || lines != c.lines) {
            std::fprintf(stderr, "%s: graphemes %s (want %s), lines %s (want %s)\n", c.tag, graphemes.c_str(), c.graphemes, lines.c_str(), c.lines);
            ++failures;
        }
    }
    if (!print) std::printf("ICU breaks: %zu cases, %d failed\n", sizeof CASES / sizeof CASES[0], failures);
    return failures ? 1 : 0;
}
