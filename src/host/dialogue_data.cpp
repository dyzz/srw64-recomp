#include "dialogue_data.hpp"
#include "app/runtime.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>

namespace srw64::dialogue_data {
namespace {
std::mutex mutex;
std::once_flag started;
std::thread loader;
bool loaded = false;
std::unique_ptr<nlohmann::json> data;
std::unique_ptr<localization::string_tables::Tables> string_tables;
nlohmann::json battle = nlohmann::json::object();

void load() {
    const char* path = std::getenv("SRW64_DIALOGUE_DATA");
    if (!path) return;
    std::ifstream source(path, std::ios::binary);
    auto parsed = std::make_unique<nlohmann::json>(nlohmann::json::parse(source, nullptr, false));
    if (parsed->is_discarded() || !parsed->is_object()) {
        std::fprintf(stderr, "SRW64_DIALOGUE_DATA_UNREADABLE %s\n", path);
        return;
    }
    if (const char* locale = std::getenv("SRW64_LOCALE"); locale && parsed->contains("locale_catalogs")) {
        const auto& catalogs = parsed->at("locale_catalogs");
        if (catalogs.contains(locale)) {
            const auto row = catalogs.at(locale);
            for (const auto& [key, value] : row.items()) (*parsed)[key] = value;
        }
    }
    if (const char* tables_path = std::getenv("SRW64_DIALOGUE_TABLES"); tables_path && *tables_path) {
        try {
            const auto bytes = app::read_text(tables_path, 256u << 20);
            string_tables = std::make_unique<localization::string_tables::Tables>(localization::string_tables::read(bytes));
        } catch (const std::exception& error) {
            std::fprintf(stderr, "SRW64_DIALOGUE_TABLES_UNREADABLE %s: %s\n", tables_path, error.what());
            return;
        }
    }
    if (parsed->contains("battle_assets")) battle = parsed->at("battle_assets");
    data = std::move(parsed);
}
// The load done, on whichever thread started it.
void wait() {
    std::call_once(started, [] { loader = std::thread(load); });
    std::lock_guard lock(mutex);
    if (!loaded) {
        if (loader.joinable()) loader.join();
        loaded = true;
    }
}
}

void preload() {
    std::call_once(started, [] { loader = std::thread(load); });
}

const nlohmann::json* document() {
    wait();
    return data.get();
}

const localization::string_tables::Tables* tables() {
    wait();
    return string_tables.get();
}

const nlohmann::json& battle_assets() {
    wait();
    return battle;
}

void release() {
    wait();
    std::lock_guard lock(mutex);
    data.reset();
    string_tables.reset();
}
}
