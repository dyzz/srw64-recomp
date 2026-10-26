#pragma once
// The prepared dialogue data's large string tables (dialogue.bin, tools/release/vita/prepare_data.cpp):
// the original records, each locale's translations and its intro pages, read without JSON. Their
// 13 MB as JSON took the PS Vita seconds to parse.
//
// Layout, little-endian: "SRW64ST1", u32 table count; per table: u32 name length, the name,
// u32 entry count, then per entry: u32 key length, the key, u32 value length, the value.
// Entries are in key order. Names: "source", "entries/<locale>", "intro/<locale>".
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace srw64::localization::string_tables {
using Table = std::map<std::string, std::string>;
using Tables = std::map<std::string, std::shared_ptr<const Table>>;
inline constexpr std::string_view magic = "SRW64ST1";

inline std::string write(const std::map<std::string, const Table*>& tables) {
    std::string out(magic);
    const auto number = [&](size_t value) {
        const uint32_t n = uint32_t(value);
        out.append(reinterpret_cast<const char*>(&n), sizeof n);
    };
    const auto text = [&](std::string_view value) { number(value.size()); out.append(value); };
    number(tables.size());
    for (const auto& [name, table] : tables) {
        text(name);
        number(table->size());
        for (const auto& [key, value] : *table) { text(key); text(value); }
    }
    return out;
}

inline Tables read(std::string_view bytes) {
    size_t at = 0;
    const auto take = [&](size_t size) {
        if (size > bytes.size() - at) throw std::runtime_error("Truncated string tables");
        const auto part = bytes.substr(at, size);
        at += size;
        return part;
    };
    const auto number = [&] {
        uint32_t n;
        std::memcpy(&n, take(sizeof n).data(), sizeof n);
        return size_t(n);
    };
    if (take(magic.size()) != magic) throw std::runtime_error("Not string tables (SRW64ST1)");
    Tables tables;
    for (size_t count = number(); count; --count) {
        std::string name(take(number()));
        auto table = std::make_shared<Table>();
        for (size_t entries = number(); entries; --entries) {
            std::string key(take(number()));
            // In key order: each goes in at the end.
            table->emplace_hint(table->end(), std::move(key), std::string(take(number())));
        }
        tables.emplace(std::move(name), std::move(table));
    }
    return tables;
}
}
