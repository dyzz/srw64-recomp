#pragma once
// A save made with a package's new units, loaded without that package (docs/design/
// mod-names-art-data.md §2.3.2, new-unit-ids.md §3.6): a unit number past the ROM's has no
// rows without the package that names it (table_window.hpp), and one whose name now has
// another number would come back as some other unit. The load is turned back: a banner names
// the package and the game returns to the title (quick_load::request_title) as soon as it
// stands where that works.
//
// Which package and name a unit came from goes beside the record, as the story state does
// (story_mod sidecars, by the record's hash): <keep>/mod-units/<hash>.json. A save without
// the file still turns back when one of its units has no package now, naming none.
//
// A unit or weapon whose name has another number now (a package added since took its number)
// is given the new one before the game reads the record (`remap`), and the record put back as
// it was afterwards (`undo`), so every sidecar found by the record's hash still is.
#include "guest_memory.hpp"
#include "mod_data.hpp"
#include "notices.hpp"
#include "quick_load.hpp"
#include "story_mod.hpp"
#include "json/json.hpp"
#include "localization/catalog.hpp"
#include "localization/locale_fallbacks.hpp"
#include <atomic>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace srw64::mod_saves {
using json=nlohmann::json;
namespace fs=std::filesystem;
inline constexpr uint32_t pool=0x8016A210, pool_size=0x54, pool_count=140;   // the player's unit instances
inline constexpr uint32_t game_mode=0x8015DA02;   // as quick_load.cpp reads it
inline constexpr const char* schema="srw64.mod-units-state.v2";
inline constexpr const char* legacy_schema="srw64.mod-units-state.v1";

// Where a record keeps the player's units and weapons (new-unit-ids.md §3.6): 140 unit
// records, +0 the unit number << 6 | 6 bits (0xFFFF empty), and 700 weapon slots of 6 bytes,
// +0 the weapon number (0xFFFF empty). The intermission's (800927A4 reads it) and the
// suspend's player bank (800936A0's first pass).
// And 100 pilot records, +0 the character (0xFFFF none): the intermission's 12 bytes each
// (80092498's), the suspend's player bank 0x14 each (80092E78 writes them after the units).
// The suspend has two more banks, the enemy's and the third party's: 80093278 runs three
// passes, side k reading its own pools (units 8016A210 + k x 0x2DF0, pilots 80172F40 +
// k x 0x1DB0, weapons 80178F80 + k x 0x6270) and writing 30 units, 30 pilots and 150 weapon
// slots of each other side in the same formats (new-unit-ids.md §10.7).
struct Bank { uint32_t units; unsigned unit_count; uint32_t unit_stride,pilots; unsigned pilot_count; uint32_t pilot_stride,weapons; unsigned weapon_count; };
inline constexpr unsigned unit_records=140, weapon_slots=700, weapon_stride=6, pilot_records=100;
inline std::vector<Bank> banks(story_mod::Record kind) {
    if(kind==story_mod::Record::slot)return {{0x801C2710,unit_records,0x10,0x801C2FD0,pilot_records,0xC,0x801C3480,weapon_slots}};
    return {{0x800FC2A0,unit_records,0x14,0x800FCD90,pilot_records,0x14,0x800FD560,weapon_slots},
            {0x800FE6F4,30,0x14,0x800FEBA4,30,0x14,0x800FF054,150},     // the enemy's
            {0x800FE94C,30,0x14,0x800FEDFC,30,0x14,0x800FF3D8,150}};    // the third party's
}
// A pilot record keeps the character in nine bits (800923B4, 80092E78 mask & 0x1FF) and flags
// above them (0x200, 0x400: the suspend's enemy bank has 0x200 on every one): the character
// is the low nine bits, and a new number goes in under the same flags.
inline constexpr unsigned pilot_bits=0x1FF;
inline std::vector<std::pair<uint32_t,unsigned>> record_pilots(const uint8_t* ram,story_mod::Record kind) {
    std::vector<std::pair<uint32_t,unsigned>> out;
    for(const auto& b:banks(kind))
        for(unsigned i=0;i<b.pilot_count;++i) {
            const uint32_t at=b.pilots+i*b.pilot_stride;
            const unsigned word=guest::read(ram,at,2);
            if(word!=0xFFFF)out.emplace_back(at,word&pilot_bits);
        }
    return out;
}
// Our characters in use (the pilot table, 100 × 0x4C at 80172F40: +0 in use, +2 the character);
// with `sides` 3 the enemy's and the third party's tables after it too.
inline constexpr uint32_t pilot_pool=0x80172F40, pilot_side=0x1DB0;
inline std::set<unsigned> pilots(const uint8_t* ram,unsigned sides=1) {
    std::set<unsigned> out;
    for(uint32_t side=0;side<sides;++side)
        for(uint32_t i=0;i<pilot_records;++i) {
            const uint32_t at=pilot_pool+side*pilot_side+i*0x4C;
            if(guest::read(ram,at,1))out.insert(guest::read(ram,at+2,2));
        }
    return out;
}
// The record's numbers: each unit or weapon record's address and number.
inline std::vector<std::pair<uint32_t,unsigned>> record_units(const uint8_t* ram,story_mod::Record kind) {
    std::vector<std::pair<uint32_t,unsigned>> out;
    for(const auto& b:banks(kind))
        for(unsigned i=0;i<b.unit_count;++i) {
            const uint32_t at=b.units+i*b.unit_stride;
            const unsigned word=guest::read(ram,at,2);
            if(word!=0xFFFF)out.emplace_back(at,word>>6);
        }
    return out;
}
inline std::vector<std::pair<uint32_t,unsigned>> record_weapons(const uint8_t* ram,story_mod::Record kind) {
    std::vector<std::pair<uint32_t,unsigned>> out;
    for(const auto& b:banks(kind))
        for(unsigned i=0;i<b.weapon_count;++i) {
            const uint32_t at=b.weapons+i*weapon_stride;
            const unsigned number=guest::read(ram,at,2);
            if(number!=0xFFFF)out.emplace_back(at,number);
        }
    return out;
}

// The unit numbers of the player's instances in use (+0 non-zero, +2 the unit); with `sides`
// 3 the enemy's and the third party's pools after it too.
inline constexpr uint32_t pool_side=0x2DF0;
inline std::set<unsigned> units(const uint8_t* ram,unsigned sides=1) {
    std::set<unsigned> out;
    for(uint32_t side=0;side<sides;++side)
        for(uint32_t i=0;i<pool_count;++i) {
            const uint32_t at=pool+side*pool_side+i*pool_size;
            if(guest::read(ram,at,1))out.insert(guest::read(ram,at+2,2));
        }
    return out;
}
// How many sides a record keeps: the suspend all three, the intermission only ours.
inline unsigned sides(story_mod::Record kind){return kind==story_mod::Record::slot?1:3;}
inline bool new_unit(unsigned unit){return unit>=table_window::rom_units;}
inline fs::path sidecar(uint64_t hash) {
    char name[32];std::snprintf(name,sizeof name,"%016llx.json",static_cast<unsigned long long>(hash));
    const auto& keep=story_mod::state().keep;
    const auto folder=keep?keep():fs::path();
    return folder.empty()?fs::path():folder/"mod-units"/name;
}
inline std::string name_in(const json& name,const std::string& locale) {
    if(name.is_string())return name.get<std::string>();
    if(!name.is_object())return {};
    for(const auto& key:localization::fallbacks(locale))
        if(const auto found=name.find(key);found!=name.end() && found->is_string())return found->get<std::string>();
    for(const auto& [_,value]:name.items())if(value.is_string())return value.get<std::string>();
    return {};
}

struct State {
    std::mutex mutex;
    json packages=json::object();     // hash -> its sidecar state, for records still in this run
    std::atomic<bool> turn_back{false};
    // Game thread only: frame boundaries the title return has been usable, whether it is
    // asked, boundaries since, whether the banner went up.
    unsigned ready{},waited{};
    bool asked{},posted{};
    std::string text;                  // the banner, under mutex
    std::map<unsigned,unsigned> moved,moved_pilots; // the record's unit numbers `remap` changed, old -> new, under mutex
    bool missing_legacy_dancouga{};
};
// Frame boundaries (two to a VI at 120 Hz): the map settled before asking, and the time the
// press gets before a new one when it ended without the title.
inline constexpr unsigned settle=60, patience=120;
inline State& state(){static State value;return value;}

// After the game serializes a record: the packages of its new units, beside it.
inline void serialized(const uint8_t* ram,bool lasting,story_mod::Record kind) {
    const auto owners=mod_data::unit_owners();
    const auto numbers=mod_ids::current();
    json packages=json::object();
    json added=json::array(),keys=json::object(),weapon_keys=json::object(),pilot_keys=json::object();
    for(const auto unit:units(ram,sides(kind)))
        if(const auto o=owners.find(unit);o!=owners.end()) {
            packages[o->second.package]=o->second.name;added.push_back(unit);
            if(const auto k=numbers->unit_keys.find(unit);k!=numbers->unit_keys.end())keys[std::to_string(unit)]=k->second;
        }
    for(const auto& [_,weapon]:record_weapons(ram,kind))
        if(const auto k=numbers->weapon_keys.find(weapon);k!=numbers->weapon_keys.end())weapon_keys[std::to_string(weapon)]=k->second;
    // New characters: their package and name, by the numbers the record has.
    const auto pilot_owners=mod_data::pilot_owners();
    for(const auto& [_,pilot]:record_pilots(ram,kind))
        if(const auto o=pilot_owners.find(pilot);o!=pilot_owners.end()) {
            packages[o->second.package]=o->second.name;
            if(const auto k=numbers->pilot_keys.find(pilot);k!=numbers->pilot_keys.end())pilot_keys[std::to_string(pilot)]=k->second;
        }
    const auto hash=story_mod::record_hash(ram,kind);
    auto& s=state();
    const json entry={{"schema",schema},{"packages",packages},{"units",added},{"keys",keys},{"weapon_keys",weapon_keys},{"pilot_keys",pilot_keys}};
    {std::lock_guard lock(s.mutex);if(packages.empty())s.packages.erase(std::to_string(hash));else s.packages[std::to_string(hash)]=entry;}
    if(!lasting || packages.empty())return;
    if(const auto file=sidecar(hash);!file.empty()) {
        std::error_code ec;
        fs::create_directories(file.parent_path(),ec);
        std::ofstream(file)<<entry.dump()<<'\n';
    }
}
// The sidecar state of the record the buffer holds (this run's, else its file): null when none.
inline json saved_state(const uint8_t* ram,story_mod::Record kind) {
    const auto hash=story_mod::record_hash(ram,kind);
    auto& s=state();
    json saved;
    {std::lock_guard lock(s.mutex);if(const auto p=s.packages.find(std::to_string(hash));p!=s.packages.end())saved=*p;}
    if(saved.is_null())
        if(const auto file=sidecar(hash);!file.empty() && fs::exists(file)) {
            std::ifstream in(file);
            const auto document=json::parse(in,nullptr,false);
            if(document.is_object() && (document.value("schema",json())==schema ||
                                        document.value("schema",json())==legacy_schema))saved=document;
        }
    return saved;
}

// The words `remap` changed, to put back.
using Undo=std::vector<std::pair<uint32_t,uint16_t>>;

// The old package borrowed a real ROM number. Restrict its migration to the player's
// instance, and only rewrite weapons in that instance's saved range. If the sidecar
// was lost (e.g. a cartridge export), the three borrowed weapons identify the old unit.
inline bool old_dancouga_at(const uint8_t* ram,const Bank& bank,uint32_t at,bool owned) {
    const auto word=guest::read(ram,at,2);
    if(word>>6!=mod_units::old_dancouga)return false;
    const unsigned count=word&0x3F,first=guest::read(ram,at+bank.unit_stride-2,2);
    if(first>bank.weapon_count || count>bank.weapon_count-first)return false;
    const unsigned pilot_index=guest::read(ram,at+bank.unit_stride-7,1);
    if(owned && pilot_index<bank.pilot_count &&
        (guest::read(ram,bank.pilots+pilot_index*bank.pilot_stride,2)&pilot_bits)==171)return true;
    for(const auto& [old,_]:mod_units::old_dancouga_weapons) {
        bool found=false;
        for(unsigned i=first;i<first+count;++i) {
            const auto weapon=bank.weapons+i*weapon_stride;
            if(guest::read(ram,weapon,2)==old && guest::read(ram,weapon+2,2)==mod_units::old_dancouga)
                {found=true;break;}
        }
        if(!found)return false;
    }
    return true;
}

// Before the game reads a record (the buffer filled): every new unit and weapon whose name
// the sidecar knows and an active package gives another number now takes that number.
// A name no package gives now stays as it was (`restored` turns the load back).
inline Undo remap(uint8_t* ram,story_mod::Record kind) {
    Undo undo;
    auto& s=state();
    {std::lock_guard lock(s.mutex);s.moved.clear();s.moved_pilots.clear();s.missing_legacy_dancouga=false;}
    const auto source=saved_state(ram,kind);
    const json saved=source.is_object()?source:json::object();
    const auto numbers=mod_ids::current();
    const auto moves=[](const json& keys,const std::map<std::string,unsigned>& now) {
        std::map<unsigned,unsigned> out;
        if(!keys.is_object())return out;
        for(const auto& [number,key]:keys.items()) {
            if(!key.is_string())continue;
            unsigned old=0;
            try{old=unsigned(std::stoul(number));}catch(...){continue;}
            const auto found=now.find(key.get<std::string>());
            if(found!=now.end() && found->second!=old)out[old]=found->second;
        }
        return out;
    };
    const auto units=moves(saved.value("keys",json::object()),numbers->units);
    const auto weapons=moves(saved.value("weapon_keys",json::object()),numbers->weapons);
    const auto people=moves(saved.value("pilot_keys",json::object()),numbers->pilots);
    const auto replace=[&](uint32_t at,unsigned value) {
        undo.emplace_back(at,uint16_t(guest::read(ram,at,2)));
        guest::write16(ram,at,uint16_t(value));
    };
    unsigned changed_units=0,changed_weapons=0,changed_pilots=0;
    bool missing_legacy=false, moved_legacy=false;
    unsigned legacy_unit=0;
    std::map<unsigned,unsigned> legacy_weapons;
    const auto package=std::string(mod_units::dancouga_package);
    const auto legacy_packages=saved.value("packages",json::object());
    const auto legacy_units=saved.value("units",json::array());
    const bool owned=saved.value("schema",json())==legacy_schema && legacy_packages.contains(package) &&
        legacy_units.is_array() && std::find(legacy_units.begin(),legacy_units.end(),mod_units::old_dancouga)!=legacy_units.end();
    const auto player_bank=banks(kind).front();
    for(unsigned i=0;i<player_bank.unit_count;++i) {
        const auto at=player_bank.units+i*player_bank.unit_stride;
        if(!old_dancouga_at(ram,player_bank,at,owned))continue;
        const auto target=numbers->units.find(package+"@final");
        if(target==numbers->units.end() || !mod_data::unit_copy(target->second)) {missing_legacy=true;continue;}
        legacy_unit=target->second;
        for(const auto& [old,name]:mod_units::old_dancouga_weapons)
            if(const auto w=numbers->weapons.find(package+"@"+name);w!=numbers->weapons.end())legacy_weapons.emplace(old,w->second);
        if(legacy_weapons.size()!=mod_units::old_dancouga_weapons.size()) {missing_legacy=true;continue;}
        const unsigned word=guest::read(ram,at,2), first=guest::read(ram,at+player_bank.unit_stride-2,2);
        replace(at,legacy_unit<<6 | (word&0x3F));
        ++changed_units;
        moved_legacy=true;
        for(unsigned n=first;n<first+(word&0x3F);++n) {
            const auto weapon=player_bank.weapons+n*weapon_stride;
            if(const auto found=legacy_weapons.find(guest::read(ram,weapon,2));found!=legacy_weapons.end()) {
                replace(weapon,found->second);
                ++changed_weapons;
            }
            if(guest::read(ram,weapon+2,2)==mod_units::old_dancouga)replace(weapon+2,legacy_unit);
        }
    }
    if(!people.empty())for(const auto& [at,pilot]:record_pilots(ram,kind))
        if(const auto m=people.find(pilot);m!=people.end()) {
            replace(at,(guest::read(ram,at,2)&~pilot_bits)|(m->second&pilot_bits));
            ++changed_pilots;
        }
    if(!units.empty())for(const auto& [at,unit]:record_units(ram,kind))
        if(const auto m=units.find(unit);m!=units.end()) {
            replace(at,m->second<<6 | (guest::read(ram,at,2)&0x3F));
            ++changed_units;
        }
    if(!units.empty() || !weapons.empty())for(const auto& [at,weapon]:record_weapons(ram,kind)) {
        if(const auto m=weapons.find(weapon);m!=weapons.end()) {
            replace(at,m->second);
            ++changed_weapons;
        }
        if(const auto m=units.find(guest::read(ram,at+2,2));m!=units.end())replace(at+2,m->second);
    }
    {std::lock_guard lock(s.mutex);s.moved=units;s.moved_pilots=people;s.missing_legacy_dancouga=missing_legacy;}
    if(undo.empty())return undo;
    json from=json::object();
    for(const auto& [a,b]:units)from[std::to_string(a)]=b;
    json weapon_from=json::object();
    for(const auto& [a,b]:weapons)weapon_from[std::to_string(a)]=b;
    if(moved_legacy) {
        from[std::to_string(mod_units::old_dancouga)]=legacy_unit;
        for(const auto& [a,b]:legacy_weapons)weapon_from[std::to_string(a)]=b;
    }
    json pilot_from=json::object();
    for(const auto& [a,b]:people)pilot_from[std::to_string(a)]=b;
    std::fprintf(stderr,"SRW64_MOD_SAVE %s\n",json({{"action","remapped"},{"record",kind==story_mod::Record::slot?"slot":"suspend"},
                                                    {"units",from},{"weapons",weapon_from},{"pilots",pilot_from},{"unit_records",changed_units},
                                                    {"weapon_slots",changed_weapons},{"pilot_records",changed_pilots}}).dump().c_str());
    return undo;
}
// After the game read it: the record as it was saved.
inline void undo(uint8_t* ram,const Undo& changed) {
    for(auto i=changed.rbegin();i!=changed.rend();++i)guest::write16(ram,i->first,i->second);
}

// After the game restores a record: turn the load back when one of its units is a new number
// no active package adds, or one whose name has another number now (`remap` moved those it
// could).
inline void restored(const uint8_t* ram,story_mod::Record kind) {
    const auto owners=mod_data::unit_owners();
    const auto numbers=mod_ids::current();
    const auto hash=story_mod::record_hash(ram,kind);
    auto& s=state();
    const json saved=saved_state(ram,kind);
    std::map<unsigned,unsigned> moved,moved_pilots;
    bool missing_legacy;
    {std::lock_guard lock(s.mutex);moved.swap(s.moved);moved_pilots.swap(s.moved_pilots);
     missing_legacy=std::exchange(s.missing_legacy_dancouga,false);}
    // The sidecar's keys by the numbers the units have now.
    // Held in locals: a range-for over value(...).items() iterates a temporary already gone.
    const json saved_keys=saved.is_object()?saved.value("keys",json::object()):json::object();
    const json saved_pilot_keys=saved.is_object()?saved.value("pilot_keys",json::object()):json::object();
    std::map<unsigned,std::string> keys;
    if(saved_keys.is_object())
        for(const auto& [number,key]:saved_keys.items()) {
            unsigned n=0;
            try{n=unsigned(std::stoul(number));}catch(...){continue;}
            if(!key.is_string())continue;
            if(const auto m=moved.find(n);m!=moved.end())n=m->second;
            keys[n]=key.get<std::string>();
        }
    json missing=json::array();
    if(missing_legacy)missing.push_back(mod_units::old_dancouga);
    for(const auto unit:units(ram,sides(kind))) {
        if(!new_unit(unit))continue;
        bool fine=owners.count(unit);
        if(const auto k=keys.find(unit);fine && k!=keys.end()) {
            const auto now=numbers->units.find(k->second);
            fine=now!=numbers->units.end() && now->second==unit;
        }
        if(!fine)missing.push_back(unit);
    }
    // And a new character no active package adds, or whose name has another number now.
    std::map<unsigned,std::string> pilot_keys;
    if(saved_pilot_keys.is_object())
        for(const auto& [number,key]:saved_pilot_keys.items()) {
            unsigned n=0;
            try{n=unsigned(std::stoul(number));}catch(...){continue;}
            if(!key.is_string())continue;
            if(const auto m=moved_pilots.find(n);m!=moved_pilots.end())n=m->second;
            pilot_keys[n]=key.get<std::string>();
        }
    const auto pilot_owners=mod_data::pilot_owners();
    json missing_pilots=json::array();
    for(const auto pilot:pilots(ram,sides(kind))) {
        if(pilot<table_window::rom_pilots)continue;
        bool fine=pilot_owners.count(pilot);
        if(const auto k=pilot_keys.find(pilot);fine && k!=pilot_keys.end()) {
            const auto now=numbers->pilots.find(k->second);
            fine=now!=numbers->pilots.end() && now->second==pilot;
        }
        if(!fine)missing_pilots.push_back(pilot);
    }
    if(missing.empty() && missing_pilots.empty())return;
    json packages=saved.is_object()?saved.value("packages",json::object()):json::object();
    if(missing_legacy && !packages.contains(mod_units::dancouga_package))
        packages[mod_units::dancouga_package]="Final Dancouga";
    const auto& catalog=localization::catalog();
    std::string names;
    for(const auto& [id,name]:packages.items()) {
        const auto shown=name_in(name,catalog.locale);
        names+=(names.empty()?"":"、")+(shown.empty()?id:shown);
    }
    std::string text=catalog.ui(names.empty()?"mod_save_needs_unknown":"mod_save_needs");
    if(const auto at=text.find("{name}");at!=std::string::npos)text.replace(at,6,names);
    {std::lock_guard lock(s.mutex);s.text=text;}
    s.turn_back=true;
    std::fprintf(stderr,"SRW64_MOD_SAVE %s\n",json({{"action","turned_back"},{"record",kind==story_mod::Record::slot?"slot":"suspend"},
                                                    {"units",missing},{"pilots",missing_pilots},{"packages",packages},{"sidecar",sidecar(hash).filename().string()}}).dump().c_str());
}
// Game thread, every frame boundary (after quick_load::frame): the title once the game has
// stood where the title return works for a moment (asked during the restore, mode 0x11, the
// map can let the press go by), asked again when a press ends without it; the banner when
// first asked and again at the title.
inline void frame(const uint8_t* ram) {
    auto& s=state();
    if(!s.turn_back.load()){s.ready=s.waited=0;s.asked=s.posted=false;return;}
    const auto post=[&]{std::lock_guard lock(s.mutex);notices::post("mod-save",s.text);};
    const unsigned mode=guest::read(ram,game_mode,1);
    if(mode==1) {   // the logo, then the title
        if(s.asked)post();
        s.turn_back=false;return;
    }
    if(!s.asked) {
        s.ready=quick_load::usable_title() && mode!=0x11?s.ready+1:0;
        if(s.ready<settle)return;
        quick_load::request_title();s.asked=true;s.waited=0;
        if(!s.posted){post();s.posted=true;}
        return;
    }
    if(++s.waited>=patience && quick_load::usable_title()){s.asked=false;s.ready=0;}
}
}
