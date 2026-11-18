#pragma once
// The numbers the active packages' new units and weapons get (mod_units.hpp assign): any
// file of a package that names one ("@final", "other.package@final") resolves it here.
#include "mod_library.hpp"
#include "mod_units.hpp"
#include <fstream>
#include <memory>
#include <mutex>

namespace srw64::mod_ids {
namespace fs=std::filesystem;
using json=nlohmann::json;

// The active packages' data/new_units.json, bottom first; an unreadable file is left out.
inline std::vector<std::pair<std::string,json>> documents(const std::vector<mods::Package>& packages) {
    std::vector<std::pair<std::string,json>> out;
    std::error_code error;
    for(const auto& p:packages) {
        const auto path=p.folder/"data"/"new_units.json";
        if(!p.active || !fs::is_regular_file(path,error))continue;
        std::ifstream in(path,std::ios::binary);
        auto document=json::parse(in,nullptr,false);
        if(!document.is_discarded() && document.is_object() && document.value("schema","")==mod_units::schema)
            out.emplace_back(p.id(),std::move(document));
    }
    return out;
}
inline mod_units::Numbers assign(const std::vector<mods::Package>& packages){return mod_units::assign(documents(packages));}

// The numbers of the packages active now, again when they change. Any thread.
struct Cache {
    std::mutex mutex;
    unsigned revision=~0u;
    mods::Version game{-1,-1,-1};
    std::shared_ptr<const mod_units::Numbers> numbers=std::make_shared<const mod_units::Numbers>();
};
inline Cache& cache(){static Cache value;return value;}
inline std::shared_ptr<const mod_units::Numbers> current() {
    auto& c=cache();
    const unsigned now=mods::revision();
    const auto game=mods::game_version();
    std::lock_guard lock(c.mutex);
    if(now!=c.revision || !(game==c.game)) {
        c.numbers=std::make_shared<const mod_units::Numbers>(assign(mods::installed(game)));
        c.revision=now;c.game=game;
    }
    return c.numbers;
}
// A unit or weapon a package names, as mod_units::resolve.
inline std::optional<unsigned> unit(const json& value,const std::string& package){return mod_units::resolve(value,package,*current(),mod_units::Of::unit);}
inline std::optional<unsigned> weapon(const json& value,const std::string& package){return mod_units::resolve(value,package,*current(),mod_units::Of::weapon);}
inline std::optional<unsigned> pilot(const json& value,const std::string& package){return mod_units::resolve(value,package,*current(),mod_units::Of::pilot);}
}
