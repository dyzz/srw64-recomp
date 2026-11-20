#pragma once
// A campaign's own tactical maps (docs/design/level-editor.md §4, tools/mods/map_tool.py).
// A stage with one still names an original map: the game loads that map's number, with
// its music, space rules, battle backgrounds and palette cycles, and asks the resource
// loader (80089E9C) for its layout, tile atlas and palette. While the stage is the one
// registered, those three numbers are served the campaign map's bytes instead: 80089BBC
// (the descriptor: decoded size, where the stream is) points at a window of the ROM's
// address space the host answers (8007F704), as the packages' own resources are
// (mod_tables.hpp). The loader only asks for a resource that is not already in memory, so
// the original must not be resident when the stage loads its map.
#include "guest_memory.hpp"
#include "json/json.hpp"
#include "table_window.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace srw64::map_mod {
namespace fs=std::filesystem;
using json=nlohmann::json;

inline constexpr const char* image_schema="srw64.map-image.v1";
// The top of the resource window (table_window.hpp), a megabyte for each of the three.
inline constexpr uint32_t slot=0x100000, window=table_window::resource_window_end-3*slot;

struct Map {
    fs::path folder;
    std::array<uint16_t,3> numbers{};                // the original's layout, atlas, palette
    std::array<uint32_t,3> sizes{};                  // decoded
    std::array<std::vector<uint8_t>,3> streams;      // as the loader reads them
    bool hd{};                                       // the folder has the HD mode's picture (meta.json)
};
struct State {
    std::mutex mutex;
    std::shared_ptr<const Map> current;
    std::map<std::string,std::shared_ptr<const Map>> loaded;   // by folder
};
inline State& state(){static State s;return s;}

inline std::vector<uint8_t> unhex(const std::string& hex) {
    if(hex.size()%2)throw std::runtime_error("十六进制长度是奇数");
    const auto digit=[](char c)->unsigned {
        if(c>='0' && c<='9')return unsigned(c-'0');
        if(c>='A' && c<='F')return unsigned(c-'A'+10);
        if(c>='a' && c<='f')return unsigned(c-'a'+10);
        throw std::runtime_error("不是十六进制");
    };
    std::vector<uint8_t> out(hex.size()/2);
    for(size_t i=0;i<out.size();++i)out[i]=uint8_t(digit(hex[i*2])<<4|digit(hex[i*2+1]));
    return out;
}
// The decoded size, then an LZSS stream of literals only: a flag byte 0xFF before every
// eight bytes (mod_sprites.hpp stream, src/srw64_rom/resources.py lz_decode).
inline std::vector<uint8_t> stream(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out;
    for(int shift=24;shift>=0;shift-=8)out.push_back(uint8_t(data.size()>>shift));
    for(size_t i=0;i<data.size();i+=8) {
        out.push_back(0xFF);
        for(size_t k=i;k<std::min(data.size(),i+8);++k)out.push_back(data[k]);
    }
    return out;
}
inline uint16_t be16(const std::vector<uint8_t>& bytes,size_t at){return uint16_t(bytes.at(at)<<8|bytes.at(at+1));}

// A compiled map (map.json of srw64.map-image.v1). Throws with what is wrong.
inline Map parse(const json& document,const fs::path& folder={}) {
    if(!document.is_object() || document.value("schema","")!=image_schema)throw std::runtime_error("schema 不是 "+std::string(image_schema));
    Map map;map.folder=folder;
    const char* names[]={"layout","atlas","palette"};
    for(size_t i=0;i<3;++i) {
        const auto number=document.at("replaces").at(names[i]).get<unsigned>();
        if(number>=table_window::rom_resources)throw std::runtime_error(std::string(names[i])+" 不是原版资源号");
        map.numbers[i]=uint16_t(number);
        const auto bytes=unhex(document.at(std::string(names[i])+"_hex").get<std::string>());
        map.sizes[i]=uint32_t(bytes.size());
        map.streams[i]=stream(bytes);
        if(map.streams[i].size()>slot)throw std::runtime_error(std::string(names[i])+" 太大");
        // As the ROM's are: a layout (kind 7, its cells and draw groups inside it), a 512 x 512
        // 8-bit atlas, 256 colours.
        if(i==0) {
            if(bytes.size()<8 || be16(bytes,0)!=7 || !be16(bytes,2) || be16(bytes,4)%2 || be16(bytes,6)%2 ||
               8+size_t(be16(bytes,4))*be16(bytes,6)+size_t(be16(bytes,2))*6>bytes.size())throw std::runtime_error("layout 不是地图布局");
        } else if(i==1) {
            if(bytes.size()!=8+512*512 || be16(bytes,0)!=8 || be16(bytes,2)!=512 || be16(bytes,4)!=512)throw std::runtime_error("atlas 不是 512×512 的 8 位图集");
        } else if(bytes.size()!=8+512 || be16(bytes,0)!=3 || be16(bytes,2)!=512)throw std::runtime_error("palette 不是 256 色调色板");
    }
    if(map.numbers[0]==map.numbers[1] || map.numbers[0]==map.numbers[2] || map.numbers[1]==map.numbers[2])throw std::runtime_error("三个资源号要各不相同");
    map.hd=document.value("hd",false);
    return map;
}
// Game thread, at a scene's registration: the stage's own map (its folder), or none.
// Returns the map now in use; throws when the folder's map.json cannot be used (none then).
inline std::shared_ptr<const Map> activate(const fs::path& folder) {
    auto& s=state();
    std::shared_ptr<const Map> map;
    if(!folder.empty()) {
        const auto key=folder.string();
        std::unique_lock lock(s.mutex);
        const auto found=s.loaded.find(key);
        if(found!=s.loaded.end())map=found->second;
        else {
            lock.unlock();
            std::ifstream in(folder/"map.json");
            const auto document=json::parse(in,nullptr,false);
            std::string why;
            try {
                if(document.is_discarded())throw std::runtime_error("map.json 读不出来");
                map=std::make_shared<const Map>(parse(document,folder));
            } catch(const std::exception& error){why=error.what();}
            lock.lock();
            if(!map){s.current.reset();throw std::runtime_error(key+"："+why);}
            s.loaded[key]=map;
        }
    }
    std::lock_guard lock(s.mutex);
    s.current=map;
    return map;
}
inline std::shared_ptr<const Map> current(){auto& s=state();std::lock_guard lock(s.mutex);return s.current;}

// 80089BBC wrapper: the decoded size of one of the three and its stream's place, as an
// offset from the resource table. False for every other number.
inline bool descriptor(uint16_t number,uint32_t& size,uint32_t& offset) {
    const auto map=current();
    if(!map)return false;
    for(size_t i=0;i<3;++i)if(map->numbers[i]==number) {
        size=map->sizes[i];offset=window+uint32_t(i)*slot-table_window::resource_table;
        return true;
    }
    return false;
}
// 8007F704 wrapper: a read of the window. False when `at` is not in it.
inline bool read(uint32_t at,uint8_t* ram,uint32_t destination,uint32_t size) {
    if(at<window || at>=window+3*slot)return false;
    const size_t which=(at-window)/slot,within=(at-window)%slot;
    if(within+size>slot || !guest::valid(destination|0x80000000u,size))throw std::runtime_error("map window: invalid read");
    const auto map=current();
    for(uint32_t i=0;i<size;++i)
        guest::write8(ram,destination+i,map && within+i<map->streams[which].size()?map->streams[which][within+i]:0);
    return true;
}
}
