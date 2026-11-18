#pragma once
// The table window at run time (table_window.hpp, docs/design/new-unit-ids.md §3): the game's
// reads of it, the name records of new numbers, the map icons' copy in RDRAM. The bytes are
// upgrades::game_byte's: the ROM's rows under the rules file's and the packages' bytes, the
// new rows the packages' alone (mod_data.hpp refresh puts them in the patches).
#include "guest_memory.hpp"
#include "mod_sprites.hpp"
#include "table_window.hpp"
#include "upgrade_rules.hpp"
#include <map>
#include <memory>
#include <mutex>
#include <span>

namespace srw64::mod_tables {
namespace tw=table_window;

// The packages' own resources (mod_sprites.hpp): each one's LZSS stream laid out in the
// resource window, 16-aligned. Set by mod_data refresh; read on any thread.
struct Resources {
    std::map<uint16_t,std::pair<uint32_t,uint32_t>> at;   // number -> (window address, decoded size)
    std::vector<uint8_t> bytes;                           // the window from resource_window
};
struct ResourceState {
    std::mutex mutex;
    std::shared_ptr<const Resources> current=std::make_shared<const Resources>();
};
inline ResourceState& resource_state(){static ResourceState value;return value;}
inline std::shared_ptr<const Resources> resources() {
    auto& s=resource_state();
    std::lock_guard lock(s.mutex);
    return s.current;
}
inline void publish_resources(const std::map<unsigned,std::vector<uint8_t>>& decoded) {
    auto r=std::make_shared<Resources>();
    for(const auto& [number,data]:decoded) {
        while(r->bytes.size()%16)r->bytes.push_back(0);
        const auto stream=mod_sprites::stream(data);
        if(tw::resource_window+r->bytes.size()+stream.size()>tw::resource_window_end)break;
        r->at[uint16_t(number)]={tw::resource_window+uint32_t(r->bytes.size()),uint32_t(data.size())};
        r->bytes.insert(r->bytes.end(),stream.begin(),stream.end());
    }
    auto& s=resource_state();
    std::lock_guard lock(s.mutex);
    s.current=std::move(r);
}
// 80089BBC wrapper: a package resource's decoded size and its offset from the resource
// table (where the loader reads its stream, 4 bytes on). False for the ROM's own.
inline bool resource(uint16_t number,uint32_t& size,uint32_t& offset) {
    if(number<tw::rom_resources)return false;
    const auto r=resources();
    const auto found=r->at.find(number);
    if(found==r->at.end())return false;
    size=found->second.second;offset=found->second.first-tw::resource_table;
    return true;
}

// 8007F704 wrapper: a read of the window. False when `rom` is not in it.
inline bool read(std::span<const uint8_t> rom,uint32_t at,uint8_t* ram,uint32_t destination,uint32_t size) {
    if(at>=tw::resource_window && at<tw::resource_window_end) {
        if(at+size>tw::resource_window_end || !guest::valid(destination|0x80000000u,size))throw std::runtime_error("resource window: invalid read");
        const auto r=resources();
        for(uint32_t i=0;i<size;++i) {
            const size_t k=at-tw::resource_window+i;
            guest::write8(ram,destination+i,k<r->bytes.size()?r->bytes[k]:0);
        }
        return true;
    }
    if(!tw::inside(at))return false;
    if(!tw::inside(at+size-1) || !guest::valid(destination|0x80000000u,size))
        throw std::runtime_error("table window: invalid read");
    for(uint32_t i=0;i<size;++i)guest::write8(ram,destination+i,upgrades::game_byte(rom,at+i));
    return true;
}

// 8008C510 wrapper: table 0's descriptor of a new number's name record, which reads the
// record it copies (the unit's or weapon's it was copied from) at its window slot.
inline bool descriptor(std::span<const uint8_t> rom,uint16_t table,uint16_t id,uint32_t& offset,uint32_t& size) {
    if(table || !tw::name_of(id))return false;
    std::shared_ptr<const upgrades::NameSources> names;
    {std::lock_guard lock(upgrades::patched().mutex);names=upgrades::patched().names;}
    uint32_t from=0;
    // A number no active package gives reads number 0's (a save turned back, mod_saves.hpp):
    // never the original's look past the end of table 0.
    if(!upgrades::name_source(rom,*names,id,from,size) || size>tw::name_slot)return false;
    offset=tw::base(tw::names)+uint32_t(id-tw::private_first)*tw::name_slot-upgrades::text_table;
    return true;
}

// The tactical map's icon table, moved to free RDRAM (generate_cpu.py MAP_ICON_READERS):
// the ROM's 363 as the game reads them, then the new units'. Again whenever the packages
// change and when the tactical map's overlay comes in (its copy patches go to the overlay's
// own table, which nothing reads now).
inline void fill_map_icons(std::span<const uint8_t> rom,uint8_t* ram) {
    for(unsigned n=0;n<tw::map_icon_slots;++n) {
        const uint32_t from=tw::row(tw::map_icons,n);
        guest::write8(ram,tw::map_icons_copy+n*2,upgrades::game_byte(rom,from));
        guest::write8(ram,tw::map_icons_copy+n*2+1,upgrades::game_byte(rom,from+1));
    }
}
}
