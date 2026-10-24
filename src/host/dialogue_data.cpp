#include "dialogue_data.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>

namespace srw64::dialogue_data {
namespace {
std::mutex mutex;
bool loaded = false;
std::unique_ptr<nlohmann::json> data;
nlohmann::json battle = nlohmann::json::object();

void load() {
    loaded = true;
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
    if (parsed->contains("battle_assets")) battle = parsed->at("battle_assets");
    data = std::move(parsed);
}
}

const nlohmann::json* document() {
    std::lock_guard lock(mutex);
    if (!loaded) load();
    return data.get();
}

const nlohmann::json& battle_assets() {
    {
        std::lock_guard lock(mutex);
        if (!loaded) load();
    }
    return battle;
}

void release() {
    std::lock_guard lock(mutex);
    loaded = true;
    data.reset();
}
}
