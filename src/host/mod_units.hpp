#pragma once
#include "json/json.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

// New units and weapons (docs/design/mod-names-art-data.md §2.3.1): data/new_units.json of a
// package fills unused unit and weapon numbers, battle scene slots and the upgrade
// inheritance tables. The original's tables are fixed-size, so the numbers come from a list
// of ones nothing reads; the host lays the bytes out (weapon lists, animation records,
// inheritance rows) and they join the copy patches with the number MODs (mod_data.hpp).
namespace srw64::mod_units {
using json=nlohmann::json;
using Bytes=std::map<uint32_t,uint8_t>;

inline constexpr uint32_t resident(uint32_t ram){return ram-0x80076610+0x1000;}
inline constexpr uint32_t unit_records=0x71B80, unit_record_size=0x24, unit_count=363;
inline constexpr uint32_t weapon_records=0x74E90, weapon_record_size=0x10, weapon_count=1329;
// Weapon lists: a u32 offset per unit from 0x7E210 to rows of (weapon, five forms), FFFF last.
inline constexpr uint32_t weapon_lists=0x7E210, list_row=12, list_gap=0x82E40, list_gap_end=0x83110;
inline constexpr uint32_t poses=0x84E40, map_icons=0x100D78;
inline constexpr uint32_t battle_units=0x118610, battle_unit_count=354, battle_weapons=0x119970, battle_row=14;
inline constexpr uint32_t scenes=0x11E3D0, scene_count=1053;
// An animation record is read as 0x3C0 bytes from the bank at the weapon's offset.
inline constexpr uint32_t animation_offsets=0x11FC80, animation_bank=0x184990, animation_window=0x3C0;
inline constexpr uint32_t animation_spare=0x19AA40, animation_spare_end=0x19AA40+weapon_count*4;   // an unused copy of the offsets
// Upgrade inheritance (docs/gameplay/upgrade-inheritance.md): (new, old) pairs.
inline constexpr uint32_t predecessors=resident(0x800CA3A0), predecessor_rows=30;
inline constexpr uint32_t weapon_map=resident(0x800CB5E8), weapon_map_rows=114;

// Numbers nothing in the original reads (final-dancouga-mod.md §4.11, mod-names-art-data.md).
inline constexpr unsigned free_units[]={314,325};            // byte-identical ν Gundam copies
inline constexpr unsigned free_weapons[]={881,882,1209};     // copies on no unit's list
inline constexpr unsigned free_scenes[]={1051,1052,1003,1006,1025,1026,1029,1030,1032,1033};
inline constexpr unsigned free_icons[]={978,991};
// Predecessor pairs no script reaches (127->128, 130->132, 133->115), and the weapon rows of
// those swaps plus 223->224's sword row (224's change renumbers in place, 800ACB74).
inline constexpr unsigned free_predecessor_rows[]={7,9,11};
inline constexpr unsigned free_weapon_rows[]={75,28,29,30,33,34,35,36,37,38,39,47,48,49,50};

// What the packages so far have taken, so a later package or file does not reuse it.
struct Taken {
    uint32_t list_next=list_gap, animation_next=animation_spare;
    std::set<unsigned> units,weapons,scenes,predecessor_rows,weapon_rows;
    std::map<unsigned,unsigned> copies;   // a new unit -> the unit it copies
};

template<class T,size_t N> bool listed(const T (&list)[N],unsigned value) {
    for(const auto x:list)if(x==value)return true;
    return false;
}
inline bool number(const std::string& key,unsigned& out) {
    if(key.empty() || key.size()>4 || !std::all_of(key.begin(),key.end(),[](char c){return c>='0' && c<='9';}) || (key.size()>1 && key[0]=='0'))return false;
    out=unsigned(std::stoul(key));
    return true;
}
inline bool integer(const json& v,int64_t low,int64_t high) {
    return v.is_number_integer() && v.get<int64_t>()>=low && v.get<int64_t>()<=high;
}

// One file into bytes, all or nothing; `below` is what earlier files and packages wrote.
inline std::string parse(const json& document,std::span<const uint8_t> rom,const Bytes& below,Bytes& out,Taken& taken) {
    if(!document.is_object() || document.value("schema","")!="srw64.new-units.v1")return "schema 要是 srw64.new-units.v1";
    for(const auto& [key,_]:document.items())
        if(key!="schema" && key!="units" && key!="weapons" && key!="scenes")return "未知分区 "+key+"（units、weapons、scenes）";
    Bytes bytes;
    Taken t=taken;
    const auto at=[&](uint32_t a)->uint8_t{
        if(const auto f=bytes.find(a);f!=bytes.end())return f->second;
        if(const auto f=below.find(a);f!=below.end())return f->second;
        return a<rom.size()?rom[a]:0;
    };
    const auto u16=[&](uint32_t a){return unsigned(at(a))<<8|at(a+1);};
    const auto u32=[&](uint32_t a){return uint32_t(u16(a))<<16|u16(a+2);};
    const auto put8=[&](uint32_t a,unsigned v){bytes[a]=uint8_t(v);};
    const auto put16=[&](uint32_t a,unsigned v){put8(a,v>>8);put8(a+1,v&255);};
    const auto put32=[&](uint32_t a,uint32_t v){put16(a,v>>16);put16(a+2,v&0xFFFF);};
    const auto copy=[&](uint32_t to,uint32_t from,uint32_t size){for(uint32_t i=0;i<size;++i)put8(to+i,at(from+i));};
    // The weapons a unit's list gives it (rows naming the unit among their forms).
    const auto carried=[&](unsigned unit) {
        std::vector<unsigned> list;
        for(uint32_t row=weapon_lists+u32(weapon_lists+unit*4);u16(row)!=0xFFFF && row+list_row<=rom.size();row+=list_row)
            for(unsigned k=1;k<6;++k)if(u16(row+k*2)==unit){list.push_back(u16(row));break;}
        return list;
    };

    // Scenes first: animations may name them.
    const auto& scene_part=document.value("scenes",json::object());
    if(!scene_part.is_object())return "scenes 要是对象";
    for(const auto& [key,triple]:scene_part.items()) {
        unsigned n;
        if(!number(key,n) || !listed(free_scenes,n))return "scenes."+key+"：可用的号是 1051、1052、1003、1006、1025、1026、1029、1030、1032、1033";
        if(!t.scenes.insert(n).second)return "scenes."+key+"：已被别的包用了";
        if(!triple.is_array() || triple.size()!=3 || !integer(triple[0],1,0xFFFF) || !integer(triple[1],1,0xFFFF) || !integer(triple[2],1,0xFFFF))
            return "scenes."+key+"：要是 [场景, 图集, 调色板] 三个资源号";
        for(unsigned k=0;k<3;++k)put16(scenes+n*6+k*2,triple[k].get<unsigned>());
    }

    // Weapons.
    const auto& weapon_part=document.value("weapons",json::object());
    if(!weapon_part.is_object())return "weapons 要是对象";
    std::map<unsigned,unsigned> weapon_inherits;   // new weapon -> the old one its upgrades come from
    for(const auto& [key,entry]:weapon_part.items()) {
        unsigned n;
        const std::string where="weapons."+key;
        if(!number(key,n) || !listed(free_weapons,n))return where+"：可用的号是 881、882、1209";
        if(!t.weapons.insert(n).second)return where+"：已被别的包用了";
        if(!entry.is_object())return where+"：要是对象";
        for(const auto& [name,_]:entry.items())
            if(name!="copy" && name!="animation" && name!="inherits_from")return where+"：未知字段 "+name+"（copy、animation、inherits_from）";
        if(!entry.contains("copy") || !integer(entry["copy"],0,weapon_count-1))return where+".copy：要写照抄的武器号（0–1328）";
        const unsigned source=entry["copy"].get<unsigned>();
        copy(weapon_records+n*weapon_record_size,weapon_records+source*weapon_record_size,weapon_record_size);
        copy(battle_weapons+n*battle_row,battle_weapons+source*battle_row,battle_row);   // [0] keeps the source's quotes
        if(entry.contains("inherits_from")) {
            if(!integer(entry["inherits_from"],0,weapon_count-1))return where+".inherits_from：要是武器号";
            weapon_inherits[n]=entry["inherits_from"].get<unsigned>();
        }
        const json animation=entry.value("animation",json{{"copy",source}});
        if(!animation.is_object())return where+".animation：要是 {\"copy\": 武器号} 或整条记录";
        if(animation.contains("copy")) {
            if(animation.size()!=1 || !integer(animation["copy"],0,weapon_count-1))return where+".animation.copy：要是武器号，且不和别的字段同写";
            put32(animation_offsets+n*4,u32(animation_offsets+animation["copy"].get<unsigned>()*4));
            continue;
        }
        // The record (battle_graphics.py parse_animation): camera, hit, action, defender, sounds,
        // FFFF, actors of 16 bytes, FFFF; at most 8 sounds, 24 actors and the 0x3C0 window.
        static const std::set<std::string> fields{"camera","hit","action","defender","sounds","actors"};
        for(const auto& [name,_]:animation.items())if(!fields.count(name))return where+".animation：未知字段 "+name;
        std::vector<uint8_t> record;
        const auto push16=[&](int v){record.push_back(uint8_t(unsigned(v)>>8));record.push_back(uint8_t(v&255));};
        for(const char* name:{"camera","hit","action","defender"}) {
            if(!animation.contains(name) || !integer(animation[name],0,0x7FFF))return where+".animation."+name+"：要是整数";
            push16(animation[name].get<int>());
        }
        const auto& sounds=animation.value("sounds",json::array());
        if(!sounds.is_array() || sounds.size()>8)return where+".animation.sounds：最多 8 个音效号";
        for(const auto& s:sounds){if(!integer(s,-2,0x7FFF))return where+".animation.sounds：音效号是整数（-2 静音）";push16(s.get<int>());}
        push16(-1);
        const auto& actors=animation.value("actors",json::array());
        if(!actors.is_array() || actors.empty() || actors.size()>24)return where+".animation.actors：要 1–24 个";
        for(size_t k=0;k<actors.size();++k) {
            const auto& a=actors[k];
            const std::string here=where+".animation.actors["+std::to_string(k)+"]";
            if(!a.is_object())return here+"：要是对象";
            if(!integer(a.value("scene",json()),0,scene_count-1))return here+".scene：要是战斗场景总表的号（0–1052）";
            push16(a["scene"].get<int>());
            for(const char* name:{"x","y","z"}){if(!integer(a.value(name,json(0)),-32768,32767))return here+"."+name+"：要是整数";push16(a.value(name,0));}
            for(const char* name:{"h4","h5","h6"}){if(!integer(a.value(name,json(0)),0,0xFFFF))return here+"."+name+"：要是 0–65535";push16(a.value(name,0));}
            if(!integer(a.value("behavior",json()),3,382))return here+".behavior：要是行为例程号（3–382）";
            push16(a["behavior"].get<int>());
        }
        push16(-1);
        if(record.size()>animation_window)return where+".animation：记录太长";
        const uint32_t size=uint32_t((record.size()+15)&~size_t(15));
        if(t.animation_next+size>animation_spare_end)return where+".animation：动画记录的空位用完了";
        for(size_t i=0;i<record.size();++i)put8(t.animation_next+uint32_t(i),record[i]);
        put32(animation_offsets+n*4,t.animation_next-animation_bank);
        t.animation_next+=size;
    }

    // Units.
    const auto& unit_part=document.value("units",json::object());
    if(!unit_part.is_object())return "units 要是对象";
    for(const auto& [key,entry]:unit_part.items()) {
        unsigned n;
        const std::string where="units."+key;
        if(!number(key,n) || !listed(free_units,n))return where+"：可用的号是 314、325";
        if(!t.units.insert(n).second)return where+"：已被别的包用了";
        if(!entry.is_object())return where+"：要是对象";
        for(const auto& [name,_]:entry.items())
            if(name!="copy" && name!="pose" && name!="icon" && name!="weapons" && name!="inherits_from")
                return where+"：未知字段 "+name+"（copy、pose、icon、weapons、inherits_from）";
        if(!entry.contains("copy") || !integer(entry["copy"],0,battle_unit_count-1))return where+".copy：要写照抄的机体号（0–353，要有战斗记录）";
        const unsigned source=entry["copy"].get<unsigned>();
        t.copies[n]=source;
        copy(unit_records+n*unit_record_size,unit_records+source*unit_record_size,unit_record_size);
        // 変形, 分離 and 合体 (+1C's 0x2, 0x40, 0x100) need the unit in the form and combination
        // tables, which a new number is in none of: the copy drops them, and the status pages
        // stop naming them (Final Dancouga showed 分離, copied from 224).
        put8(unit_records+n*unit_record_size+0x1E,at(unit_records+n*unit_record_size+0x1E)&~0x01);
        put8(unit_records+n*unit_record_size+0x1F,at(unit_records+n*unit_record_size+0x1F)&~0x42);
        copy(battle_units+n*battle_row,battle_units+source*battle_row,battle_row);
        copy(poses+n*6,poses+source*6,6);
        copy(map_icons+n*2,map_icons+source*2,2);
        if(entry.contains("pose")) {
            const auto& p=entry["pose"];
            if(!p.is_array() || p.size()!=3 || !integer(p[0],1,0xFFFF) || !integer(p[1],1,0xFFFF) || !integer(p[2],1,0xFFFF))
                return where+".pose：要是 [场景, 图集, 调色板] 三个资源号";
            for(unsigned k=0;k<3;++k)put16(poses+n*6+k*2,p[k].get<unsigned>());
        }
        if(entry.contains("icon")) {
            if(!integer(entry["icon"],688,1009))return where+".icon：要是地图图标资源号（688–1009；没人用的是 978、991）";
            put16(map_icons+n*2,entry["icon"].get<unsigned>());
        }
        if(!entry.contains("weapons") || !entry["weapons"].is_array() || entry["weapons"].empty())return where+".weapons：要是武器号的数组";
        std::vector<unsigned> weapons;
        for(const auto& w:entry["weapons"]) {
            if(!integer(w,0,weapon_count-1))return where+".weapons：武器号要在 0–1328";
            weapons.push_back(w.get<unsigned>());
        }
        const uint32_t size=uint32_t(weapons.size()+1)*list_row;
        if(t.list_next+size>list_gap_end)return where+".weapons：武器表的空隙用完了";
        put32(weapon_lists+n*4,t.list_next-weapon_lists);
        for(size_t k=0;k<weapons.size();++k) {
            const uint32_t row=t.list_next+uint32_t(k)*list_row;
            put16(row,weapons[k]);put16(row+2,n);
            for(unsigned f=2;f<6;++f)put16(row+f*2,0xFFFF);
        }
        for(unsigned f=0;f<6;++f)put16(t.list_next+uint32_t(weapons.size())*list_row+f*2,0xFFFF);
        t.list_next+=size;
        if(!entry.contains("inherits_from"))continue;
        if(!integer(entry["inherits_from"],0,unit_count-1))return where+".inherits_from：要是机体号";
        const unsigned from=entry["inherits_from"].get<unsigned>();
        // The predecessor pair (new, old); the first pair naming the new unit wins (800AA814).
        bool have=false;
        for(unsigned r=0;r<predecessor_rows;++r)if(u16(predecessors+r*4)==n)have=u16(predecessors+r*4+2)==from;
        if(!have) {
            const auto free=std::find_if(std::begin(free_predecessor_rows),std::end(free_predecessor_rows),[&](unsigned r){return !t.predecessor_rows.count(r);});
            if(free==std::end(free_predecessor_rows))return where+".inherits_from：前任表的空位用完了";
            t.predecessor_rows.insert(*free);
            put16(predecessors+*free*4,n);put16(predecessors+*free*4+2,from);
        }
        // Weapon rows (new, old), looked up by the old weapon, first row wins (800AA8F4).
        const auto old_ones=carried(from);
        for(const unsigned w:weapons) {
            const auto inherits=weapon_inherits.find(w);
            const unsigned old=inherits!=weapon_inherits.end()?inherits->second:w;
            if(std::find(old_ones.begin(),old_ones.end(),old)==old_ones.end())continue;   // nothing to carry
            std::optional<unsigned> row;
            for(unsigned r=0;r<weapon_map_rows && !row;++r)
                if(!(listed(free_weapon_rows,r) && !t.weapon_rows.count(r)) && u16(weapon_map+r*4+2)==old)row=r;
            if(row) {
                if(u16(weapon_map+*row*4)!=w)return where+".weapons："+std::to_string(old)+" 的段数已经映射给武器 "+std::to_string(u16(weapon_map+*row*4));
                continue;
            }
            const auto free=std::find_if(std::begin(free_weapon_rows),std::end(free_weapon_rows),[&](unsigned r){return !t.weapon_rows.count(r);});
            if(free==std::end(free_weapon_rows))return where+".weapons：武器映射表的空位用完了";
            t.weapon_rows.insert(*free);
            put16(weapon_map+*free*4,w);put16(weapon_map+*free*4+2,old);
        }
    }
    taken=t;
    for(const auto& [a,v]:bytes)out[a]=v;
    return {};
}
}
