// The PS Vita's data, prepared on a computer from the player's ROM (docs/design/psv-port.md).
// The console checks, imports and rewrites nothing at start (app::run_prepared): what it
// reads is made here, by the importer the other platforms run on their first launch.
//
//   srw64-vita-prepare ROM OUT DIALOGUE_TEXT
//
// ROM is the player's dump in any byte order; DIALOGUE_TEXT the dialogue text shipped with
// the game (content/dialogue). OUT gets what goes into ux0:data/Marchwind64 as it is:
//   game/rom.z64          the ROM, big-endian, checked against the supported one
//   game/art.pack         the importer's images
//   game/dialogue.json    the importer's dialogue data without its large string tables, its
//                         images named where the console finds them
//                         (ux0:data/Marchwind64/game/art.pack)
//   game/dialogue.bin     those tables (string_tables.hpp): the original records, and each
//                         locale's translations with the shipped dialogue text laid over them
//                         and its intro pages
//   game/prepared.json    what run_prepared reads first: the format, the ROM, the locales
//   game/blank.sram       a card of zeros, what the game starts from before it has saved
//                         (as the runtime fills a missing save file)
//   run/runtime-data/saves/, saves/slots/, shaders/   the folders the game writes into
//                         (shaders/: the GL renderer's compiled programs, kept between runs)
// The output is derived from the ROM: it is the player's own, never a release asset.
#include "app/launch.hpp"
#include "app/rom_import.hpp"
#include "app/rom_order.hpp"
#include "localization/dialogue_text.hpp"
#include "localization/string_tables.hpp"
#include "json/json.hpp"
#include <cstdio>
#include <iostream>

using namespace srw64::app;
using json = nlohmann::json;
namespace localization = srw64::localization;

namespace {
// Where the console's player folder is; the images are named from it.
constexpr const char* console_folder = "ux0:data/Marchwind64";

json read_json(const fs::path& path) {
    auto value = json::parse(read_text(path, 64u << 20));
    if (!value.is_object()) throw std::runtime_error("Expected a JSON object: " + path.string());
    return value;
}

// "art.pack#<offset>:<size>" as the console reads it.
std::string on_console(const std::string& reference) {
    if (!reference.starts_with("art.pack#")) throw std::runtime_error("Unexpected image reference: " + reference);
    return std::string(console_folder) + "/game/" + reference;
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
        std::printf("dialogue text %s: %zu entries, %u files\n", locale.c_str(), result.targets.size(), result.files);
    }
    data["intro_pages"] = std::move(pages);
    return problems;
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 4) throw std::runtime_error("Usage: srw64-vita-prepare ROM OUT DIALOGUE_TEXT");
        const fs::path rom = argv[1], out = fs::absolute(argv[2]), text = argv[3];
        const std::string spec(embedded_import_spec());
        const auto expected = json::parse(spec).at("rom_sha256").get<std::string>();
        const auto bytes = rom_big_endian(read_text(rom, 64u << 20));
        if (bytes.empty() || sha256_bytes(bytes) != expected)
            throw std::runtime_error("Not the supported ROM (Super Robot Taisen 64, Japan): " + rom.string());
        const auto game = out / "game";
        fs::create_directories(game);
        atomic_write(game / "rom.z64", bytes);
        atomic_write(game / "blank.sram", std::string(32768, '\0'));
        // The importer's cache, in the output only while it is made.
        const auto cache = out / ".import";
        std::error_code ignored;
        remove_tree(cache, ignored);
        const auto content = prepare_rom_content(game / "rom.z64", cache, spec, expected);
        const auto manifest = read_json(content / "manifest.json");
        auto data = read_json(content / manifest.at("dialogue").get<std::string>());
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
        fs::rename(content / "art.pack", game / "art.pack");
        atomic_write(game / "dialogue.json", data.dump() + "\n");
        atomic_write(game / "dialogue.bin", localization::string_tables::write(written));
        remove_tree(cache, ignored);
        json locales = json::array();
        for (const auto& [locale, catalog] : data.at("locale_catalogs").items()) locales.push_back(locale);
        atomic_write(game / "prepared.json",
                     json({{"schema", prepared_schema}, {"format", prepared_format}, {"rom_sha256", expected},
                           {"locale", "zh-Hans"}, {"locales", locales},
                           {"resolution_scale", manifest.at("resolution_scale")}}).dump(2) + "\n");
        for (const auto& folder : {out / "run" / "runtime-data" / "saves", out / "saves" / "slots", out / "shaders"}) fs::create_directories(folder);
        std::printf("SRW64_VITA_PREPARED %s (%zu dialogue text problems)\n", out.string().c_str(), problems);
        return problems ? 1 : 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "srw64-vita-prepare: %s\n", error.what());
        return 2;
    }
}
