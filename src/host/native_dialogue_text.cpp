// Shared Unicode conversion; no platform text or rendering library is needed.
#include "text/unicode.hpp"

namespace srw64::dialogue {
std::u16string utf16(const std::string& value) { return text::to_utf16(value); }
std::string utf8(const std::u16string& value) { return text::to_utf8(value); }
}
