#pragma once
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

// The update check's comparisons (update_check.hpp), free of SDL and JSON so the base
// tests can run them (tests/native_update.cpp).
namespace srw64::update {
// "0.3.10" > "0.3.9", "0.4" > "0.3.9"; anything after the numbers ("-dirty") is ignored.
inline bool newer(std::string_view candidate, std::string_view current) {
    const auto parts = [](std::string_view text) {
        std::vector<unsigned long> numbers;
        size_t at = 0;
        while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
            unsigned long n = 0;
            while (at < text.size() && text[at] >= '0' && text[at] <= '9') n = n * 10 + unsigned(text[at++] - '0');
            numbers.push_back(n);
            if (at < text.size() && text[at] == '.') ++at;
            else break;
        }
        return numbers;
    };
    auto a = parts(candidate), b = parts(current);
    if (a.empty()) return false;
    a.resize((std::max)(a.size(), b.size()));
    b.resize(a.size());
    return a > b;
}
// The website's language for a game locale: zh, en, ja.
inline std::string site_language(std::string_view locale) {
    if (locale.starts_with("zh")) return "zh";
    if (locale.starts_with("ja")) return "ja";
    return "en";
}
}
