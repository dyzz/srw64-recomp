#include "console_data.hpp"
#include "launch.hpp"
#include "rom_import.hpp"
#include "rom_order.hpp"
#include "sha256.hpp"
#include "localization/dialogue_text.hpp"
#include "localization/string_tables.hpp"
#include "json/json.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <stdexcept>

#ifndef SRW64_VERSION
#define SRW64_VERSION "?"
#endif

namespace srw64::app {
namespace {
using json = nlohmann::json;
namespace localization = srw64::localization;

// What made the data: the release, whose dialogue text it carries, and the importer's metadata.
// Data from another is made again.
std::string data_build() {
    return std::string(SRW64_VERSION) + " " + sha256_bytes(std::string(embedded_import_spec())).substr(0, 16);
}

json read_json(const fs::path& path) {
    auto value = json::parse(read_text(path, 64u << 20));
    if (!value.is_object()) throw std::runtime_error("Expected a JSON object: " + path.string());
    return value;
}

// The shipped dialogue text laid over each locale's catalog, as the host's load_text lays it
// at every start on the other platforms; the intro pages beside them.
size_t merge_dialogue_text(json& data, const fs::path& text) {
    localization::initialize(data);
    json pages = json::object();
    size_t problems = 0;
    for (const auto& [locale, catalog] : localization::registered()) {
        auto result = localization::dialogue_text::load({text / locale}, *catalog);
        for (const auto& problem : result.problems)
            std::fprintf(stderr, "%s/%s:%u %s %s\n", locale.c_str(), problem.path.c_str(), unsigned(problem.line),
                         problem.key.c_str(), problem.message.c_str());
        problems += result.problems.size();
        auto& entries = data.at("locale_catalogs").at(locale)["entries"];
        if (!entries.is_object()) entries = json::object();
        for (auto& [key, value] : result.targets) entries[key] = std::move(value);
        for (auto& [key, value] : result.intro) pages[locale][key] = std::move(value);
        for (auto& [key, value] : result.intro_source)
            if (!pages["ja"].contains(key)) pages["ja"][key] = std::move(value);
        std::fprintf(stderr, "dialogue text %s: %zu entries, %u files\n", locale.c_str(), result.targets.size(), result.files);
    }
    data["intro_pages"] = std::move(pages);
    return problems;
}
}

size_t prepare_console_data(const fs::path& rom, const fs::path& out, const fs::path& text, const std::string& folder) {
    const std::string spec(embedded_import_spec());
    const auto expected = json::parse(spec).at("rom_sha256").get<std::string>();
    // Each step's seconds, kept in prepared.json: how long a console took, for its log and
    // bug reports (the Switch's console.log only starts with the game).
    const auto started = std::chrono::steady_clock::now();
    auto mark = started;
    json seconds = json::object();
    const auto lap = [&](const char* step) {
        const auto now = std::chrono::steady_clock::now();
        seconds[step] = std::round(std::chrono::duration<double>(now - mark).count() * 100) / 100;
        mark = now;
    };
    const auto game = out / "game";
    fs::create_directories(game);
    std::error_code ignored;
    fs::remove(game / "prepared.json", ignored);
    boot_step("prepare: rom");
    const auto bytes = rom_big_endian(read_text(rom, 64u << 20));
    if (bytes.empty() || sha256_bytes(bytes) != expected)
        throw std::runtime_error("Not the supported ROM (Super Robot Taisen 64, Japan): " + rom.string());
    if (rom != game / "rom.z64") atomic_write(game / "rom.z64", bytes);
    atomic_write(game / "blank.sram", std::string(32768, '\0'));
    // The importer's cache, in the output only while it is made.
    const auto cache = out / ".import";
    remove_tree(cache, ignored);
    lap("rom");
    const auto content = prepare_rom_content(game / "rom.z64", cache, spec, expected);
    lap("import");
    boot_step("prepare: dialogue");
    const auto manifest = read_json(content / "manifest.json");
    auto data = read_json(content / manifest.at("dialogue").get<std::string>());
    // "art.pack#<offset>:<size>" as the console reads it.
    const auto on_console = [&](const std::string& reference) {
        if (!reference.starts_with("art.pack#")) throw std::runtime_error("Unexpected image reference: " + reference);
        return folder + "/game/" + reference;
    };
    for (auto& [id, portrait] : data.at("name_entry_assets").at("portraits").items())
        portrait["original"] = on_console(portrait.at("original").get<std::string>());
    if (data.contains("battle_assets"))
        for (const char* group : {"units", "portraits"})
            for (auto& [id, art] : data["battle_assets"].at(group).items()) art["path"] = on_console(art.at("path").get<std::string>());
    const auto problems = merge_dialogue_text(data, text);
    // The large string tables apart, read on the console without JSON.
    using Table = localization::string_tables::Table;
    std::map<std::string, Table> tables;
    tables["source"] = data.at("source_entries").get<Table>();
    data.erase("source_entries");
    data.erase("entries");  // the base locale's, in its row too
    for (auto& [locale, row] : data.at("locale_catalogs").items()) {
        tables["entries/" + locale] = row.at("entries").get<Table>();
        row.erase("entries");
    }
    for (const auto& [locale, pages] : data.at("intro_pages").items()) tables["intro/" + locale] = pages.get<Table>();
    data.erase("intro_pages");
    std::map<std::string, const Table*> written;
    for (const auto& [name, table] : tables) written.emplace(name, &table);
    lap("dialogue");
    boot_step("prepare: write");
    fs::remove(game / "art.pack", ignored);
    fs::rename(content / "art.pack", game / "art.pack");
    atomic_write(game / "dialogue.json", data.dump() + "\n");
    atomic_write(game / "dialogue.bin", localization::string_tables::write(written));
    remove_tree(cache, ignored);
    for (const auto& made : {out / "run" / "runtime-data" / "saves", out / "saves" / "slots", out / "shaders"}) fs::create_directories(made);
    lap("write");
    seconds["total"] = std::round(std::chrono::duration<double>(mark - started).count() * 100) / 100;
    json locales = json::array();
    for (const auto& [locale, catalog] : data.at("locale_catalogs").items()) locales.push_back(locale);
    atomic_write(game / "prepared.json",
                 json({{"schema", prepared_schema}, {"format", prepared_format}, {"rom_sha256", expected},
                       {"build", data_build()}, {"locale", "zh-Hans"}, {"locales", locales},
                       {"resolution_scale", manifest.at("resolution_scale")}, {"seconds", seconds}}).dump(2) + "\n");
    boot_step("prepare: done");
    return problems;
}

bool console_data_current(const fs::path& out, const std::string& rom_sha256) {
    const auto game = out / "game";
    if (!list_names(game).contains("prepared.json")) return false;
    try {
        const auto about = json::parse(read_text(game / "prepared.json", 65536));
        return about.value("schema", "") == prepared_schema && about.value("format", 0u) == prepared_format &&
               about.value("rom_sha256", "") == rom_sha256 && about.value("build", "") == data_build();
    } catch (const std::exception&) {
        return false;
    }
}
}
