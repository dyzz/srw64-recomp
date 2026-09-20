#pragma once
// Strict, locale-independent conversion. Indices used by dialogue remain UTF-16
// code-unit offsets; this converter deliberately does not segment graphemes.
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace srw64::text {
inline std::u16string to_utf16(std::string_view value) {
    std::u16string result;
    result.reserve(value.size());
    for (size_t i = 0; i < value.size();) {
        const auto first = static_cast<uint8_t>(value[i++]);
        uint32_t scalar = first;
        unsigned remaining = 0;
        uint32_t minimum = 0;
        if (first < 0x80) {}
        else if (first >= 0xC2 && first <= 0xDF) { scalar = first & 0x1F; remaining = 1; minimum = 0x80; }
        else if (first >= 0xE0 && first <= 0xEF) { scalar = first & 0x0F; remaining = 2; minimum = 0x800; }
        else if (first >= 0xF0 && first <= 0xF4) { scalar = first & 0x07; remaining = 3; minimum = 0x10000; }
        else throw std::runtime_error("Invalid UTF-8 leading byte");
        if (remaining > value.size() - i) throw std::runtime_error("Truncated UTF-8 sequence");
        while (remaining--) {
            const auto next = static_cast<uint8_t>(value[i++]);
            if ((next & 0xC0) != 0x80) throw std::runtime_error("Invalid UTF-8 continuation byte");
            scalar = (scalar << 6) | (next & 0x3F);
        }
        if (scalar < minimum || scalar > 0x10FFFF || (scalar >= 0xD800 && scalar <= 0xDFFF))
            throw std::runtime_error("Invalid UTF-8 scalar value");
        if (scalar < 0x10000) result.push_back(static_cast<char16_t>(scalar));
        else {
            scalar -= 0x10000;
            result.push_back(static_cast<char16_t>(0xD800 + (scalar >> 10)));
            result.push_back(static_cast<char16_t>(0xDC00 + (scalar & 0x3FF)));
        }
    }
    return result;
}

inline std::string to_utf8(std::u16string_view value) {
    std::string result;
    result.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        uint32_t scalar = value[i];
        if (scalar >= 0xD800 && scalar <= 0xDBFF) {
            if (i + 1 == value.size()) throw std::runtime_error("Truncated UTF-16 surrogate pair");
            const uint32_t low = value[++i];
            if (low < 0xDC00 || low > 0xDFFF) throw std::runtime_error("Invalid UTF-16 surrogate pair");
            scalar = 0x10000 + ((scalar - 0xD800) << 10) + low - 0xDC00;
        } else if (scalar >= 0xDC00 && scalar <= 0xDFFF) throw std::runtime_error("Unpaired UTF-16 low surrogate");
        if (scalar < 0x80) result.push_back(static_cast<char>(scalar));
        else if (scalar < 0x800) {
            result.push_back(static_cast<char>(0xC0 | (scalar >> 6)));
            result.push_back(static_cast<char>(0x80 | (scalar & 0x3F)));
        } else if (scalar < 0x10000) {
            result.push_back(static_cast<char>(0xE0 | (scalar >> 12)));
            result.push_back(static_cast<char>(0x80 | ((scalar >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (scalar & 0x3F)));
        } else {
            result.push_back(static_cast<char>(0xF0 | (scalar >> 18)));
            result.push_back(static_cast<char>(0x80 | ((scalar >> 12) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | ((scalar >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (scalar & 0x3F)));
        }
    }
    return result;
}
}
