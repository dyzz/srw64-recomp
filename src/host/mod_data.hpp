#pragma once
#include "mod_library.hpp"
#include "mod_ids.hpp"
#include "mod_tables.hpp"
#include "mod_units.hpp"
#include "upgrade_rules.hpp"
#include <atomic>
#include <functional>
#include <optional>
#include <cstdio>
#include <map>
#include <mutex>
#include <span>

// Number MODs (docs/design/mod-packages.md §5, docs/design/mod-names-art-data.md §2.3):
// data/units.json, data/pilots.json and data/weapons.json of the enabled packages, the
// upper package winning, become bytes of the ROM records.
//
//   data/units.json    { "184": { "hp": 5200, "terrain": { "sea": "A" } } }   by unit number
//   data/pilots.json   { "133": { "melee": 160, "terrain": { "space": "A" } } } by pilot number
//   data/weapons.json  { "731": { "power": 3600, "range_max": 4, "post_move": true,
//                                 "attack": "ranged", "requires": "newtype" } }         by weapon number
//
// The game keeps none of these tables in RAM: every use reads the record again through
// 8007F704, after which the copy patches apply. A unit's HP, EN, move, mobility, armor,
// limit, terrain, +0x14 and upgrade cap are read again on every stat recompute (800A5254,
// run for all units on entering the map and the intermission), a weapon's power, max range
// and air rank too (800A642C); every other weapon byte at creation and on every save load
// (800A8E5C). A pilot's record is rebuilt whole (800A7F8C) on load and level-up. So the
// bytes join the upgrade rules' copy patches (upgrade_rules.hpp) and a saved game follows
// them when loaded. A pilot's record may be shared (800A6398 maps pilot -> record), and a
// weapon number sits in the lists of every unit that carries it.
namespace srw64::mod_data {
namespace fs=std::filesystem;
using json=nlohmann::json;
using Bytes=std::map<uint32_t,uint8_t>;

inline constexpr uint32_t unit_records=0x71B80, unit_record_size=0x24, unit_count=363, unit_terrain=0x0E;
inline constexpr uint32_t pilot_map=0x800CA9C4-0x80075610, pilot_count=360;   // s16 per pilot, -1 none
inline constexpr uint32_t pilot_records=0x7A1A0, pilot_record_size=0x10, pilot_record_count=257, pilot_terrain=0x07;
inline constexpr uint32_t weapon_records=0x74E90, weapon_record_size=0x10, weapon_count=1329, weapon_terrain=0x09;
inline constexpr std::array<std::string_view,4> channels={"air","land","sea","space"};
enum class Kind { units, pilots, weapons };
inline constexpr std::array<std::string_view,3> files={"units.json","pilots.json","weapons.json"};

// A number field: its record offset, width, the values a file may give, and the scale the
// record keeps (a weapon's power in hundreds).
struct Field {
    std::string_view name;
    uint32_t offset;
    unsigned bytes;          // 1 or 2, big-endian
    bool is_signed;
    int low, high;           // the values a file may give
    int scale=1;             // stored as value / scale; the value must be a multiple
};
inline constexpr Field unit_fields[]={
    {"hp",0x00,2,false,1,65535},{"en",0x02,2,false,0,65535},{"move",0x06,1,false,1,30},
    {"mobility",0x08,2,false,0,65535},{"armor",0x0A,2,false,0,65535},{"limit",0x0C,2,false,0,65535},
    {"repair_cost",0x14,2,false,0,65535},{"upgrade_cap",0x20,1,false,0,15},
    // Copied when an instance is built (800A6E68) or a save rebuilds it (800A8CE8), not at
    // recompute: the のりかえ class (runtime +0x1A, 801C5618), the funds for shooting it
    // down (+0x1E, 801F6588) and the experience it gives (+0x27, 801F60D4). Class 8 is the
    // one the ROM leaves free: 801C5618 counts classes 0-7 in eight u16s, and the generator
    // masks its index with 7 (generate_cpu.py), so 8 counts in class 0's, which no unit uses.
    {"swap_class",0x12,1,false,0,8},{"funds",0x16,2,false,0,65535},{"exp",0x1A,1,false,0,255}};
inline constexpr Field pilot_fields[]={
    {"melee",0x01,1,false,0,255},{"ranged",0x02,1,false,0,255},{"evade",0x03,1,false,0,255},
    {"hit",0x04,1,false,0,255},{"reaction",0x05,1,false,0,255},{"skill",0x06,1,false,0,255},
    {"sp",0x0C,1,false,0,255},{"double_move",0x0E,1,false,0,99},
    // The のりかえ class (runtime +0x32): 800A7F8C copies it when a character is registered,
    // levels up or a save is read; 0 never changes machines. 8 as the units' above.
    {"swap_class",0x0B,1,false,0,8}};
// ammo -1 is none (0xFF).
inline constexpr Field weapon_fields[]={
    {"power",0x01,1,false,0,25500,100},{"range_min",0x02,1,false,0,30},{"range_max",0x03,1,false,1,30},
    {"hit",0x04,1,true,-128,127},{"ammo",0x05,1,true,-1,99},{"en",0x06,1,false,0,255},
    {"morale",0x07,1,false,0,255},{"critical",0x0D,1,true,-128,127},{"upgrade_type",0x0E,1,false,0,4}};
// A weapon's +0 is its kind (runtime record +4): 0x80 melee (格: the pilot's melee stat, else
// 射 and ranged), 0x40 its terrain rank judged on the attacker's own square (801F5628 /
// 80203418 pass the attacker's handle to 801F3FA8, else the target's), 0x08 can be parried
// (切り払い, 801F6D10), 0x04 usable after moving (P: 801F8BCC refuses a unit that moved any
// other weapon), 0x02 beam (B: barriers), 0x01 map weapon (its shape sits in 801C55C0's table
// of 51 weapon numbers, so a MOD cannot make one). The menu name's printed 格／射, P and B
// are text, so native_dialogue.cpp rewrites them to match kind_change().
// +8 is a requirement code 801E6214 checks (label text 0xFBA + code): 1 our side only,
// 2／4／16 the pilot's +0x1C bit 30 (明鏡止水, Sモード, V-MAX), 17 bit 31 (V-MAX red),
// 5–11 pilot level ≥ 10／15／20／25／30／35／45, 12／13 NT or 強化人 L1／L4, 14／15 聖戦士
// L1／L3. +E is the upgrade type (0 none, 1–4 the increment rows). +F: 0x04 locked until a
// 3D6F unlock (a story's, or the full-upgrade table's), 0x02 a combination attack, 0x01 a
// sword (the parry result's animation), 0x38 the battle camera's flags: only 0x04 is a field.
inline constexpr uint32_t weapon_kind=0x00, weapon_requires=0x08, weapon_flags=0x0F;
inline constexpr uint8_t weapon_melee=0x80, weapon_terrain_self=0x40, weapon_parryable=0x08, weapon_post_move=0x04, weapon_beam=0x02, weapon_map=0x01;
inline constexpr uint8_t weapon_locked=0x04, weapon_printed=weapon_melee|weapon_post_move|weapon_beam;
struct Requirement { std::string_view key; uint8_t code; };
inline constexpr Requirement requirements[]={
    {"ally",1},{"hyper_mode",2},{"super_mode",4},{"level_10",5},{"level_15",6},{"level_20",7},{"level_25",8},
    {"level_30",9},{"level_35",10},{"level_45",11},{"newtype",12},{"newtype_4",13},{"holy_warrior",14},
    {"holy_warrior_3",15},{"v_max",16},{"v_max_red",17}};
inline std::span<const Field> fields(Kind kind) {
    switch(kind) {
    case Kind::units: return unit_fields;
    case Kind::pilots: return pilot_fields;
    default: return weapon_fields;
    }
}

// "A".."D" and "-" as the records keep them: 4..1 and 0.
inline std::optional<uint8_t> rank(const json& value) {
    static constexpr std::string_view letters="-DCBA";
    if(!value.is_string() || value.get<std::string>().size()!=1)return std::nullopt;
    const auto at=letters.find(value.get<std::string>()[0]);
    return at==std::string_view::npos?std::nullopt:std::optional<uint8_t>(uint8_t(at));
}
// A character's ability record: a new character's is its own number (table_window.hpp).
inline int pilot_record(std::span<const uint8_t> rom,unsigned pilot) {
    if(table_window::new_pilot(pilot))return int(pilot);
    const uint32_t at=pilot_map+pilot*2;
    return at+1<rom.size()?int(int16_t(rom[at]<<8|rom[at+1])):-1;
}
// The other pilots on the same ability record.
inline std::vector<unsigned> sharing(std::span<const uint8_t> rom,unsigned pilot) {
    std::vector<unsigned> out;
    for(unsigned other=0;other<pilot_count;++other)
        if(other!=pilot && pilot_record(rom,other)==pilot_record(rom,pilot))out.push_back(other);
    return out;
}

// Special abilities a unit's +1C may carry, by the ability table's masks (overlay 801DC8FC,
// the status page's names). 変形, 合体 and 分離 stay as the ROM has them: they need forms.
struct Named { std::string_view key; uint32_t mask; };
inline constexpr Named abilities[]={
    {"beam_coat",0x200},{"i_field",0x20},{"planet_defensor",0x800},{"aura_barrier",0x4000},
    {"afterimage",0x10},{"mach_special",0x1000},{"true_mach_special",0x2000},{"god_shadow",0x1},
    {"getter_vision",0x20000},{"shungensoku",0x40000},{"hyper_jammer",0x100000},
    {"hp_regen_10",0x4},{"hp_regen_20",0x8}};
// How it moves, +5's low five bits (the ability page's icons, 801DC8F0: 陸 空 海 宇 地中);
// 800A5254 copies the byte to the instance's +D at every recompute.
inline constexpr std::array<std::string_view,5> move_types={"land","air","sea","space","underground"};
// Size, +4's low five bits (SS..LL); the two high bits are each unit's own state.
inline constexpr std::array<std::string_view,5> sizes={"SS","S","M","L","LL"};
// A pilot's +F skill bits and the row of the threshold table (0x7B1B0, 30 bytes a record,
// three rows of ten, nine read; level L at the L-th smallest nonzero threshold). The first
// five share row 0, and the status page names one of them (800A80F0).
struct Skill { std::string_view key; uint8_t bit; unsigned row; };
inline constexpr Skill skills[]={
    {"potential",0x04,0},{"newtype",0x08,0},{"enhanced",0x10,0},{"holy_warrior",0x20,0},{"esp",0x40,0},
    {"parry",0x01,1},{"shield_defense",0x02,2}};
inline constexpr uint32_t threshold_records=0x7B1B0, threshold_record_size=30;
// Spirits: their own map (D_800CA6F4, s16 per pilot, negative none) to six (level, command)
// pairs at 0x7CFB0; the command is the spirit's bit number (text 969 + it).
inline constexpr uint32_t spirit_map=0x800CA6F4-0x80075610, spirit_records=0x7CFB0, spirit_record_size=12;
inline constexpr std::array<std::string_view,30> spirits={
    "self_destruct","scout","accel","mercy","focus","alert","guts","strike","trust","wall",
    "cover","valor","spirit","great_guts","luck","drain","effort","soul","stealth","awaken",
    "refuel","provoke","encourage","disrupt","friendship","love","reactivate","miracle","prayer","revive"};
inline int spirit_record(std::span<const uint8_t> rom,unsigned pilot) {
    if(table_window::new_pilot(pilot))return int(pilot);
    const uint32_t at=spirit_map+pilot*2;
    return at+1<rom.size()?int(int16_t(rom[at]<<8|rom[at+1])):-1;
}
template<class T,size_t N> std::string keys_of(const T (&list)[N]) {std::string out;for(const auto& x:list)out+=(out.empty()?"":"、")+std::string(x.key);return out;}

// One file into bytes, all or nothing; an error names the entry and the field.
// A unit or weapon is the ROM's number, or a new one by name ("@final", "pkg@final"; the
// numbers mod_units::assign gave): its record is then in the table window.
inline std::string parse(const json& document,Kind kind,std::span<const uint8_t> rom,Bytes& out,
                         const std::string& package={},const mod_units::Numbers& numbers={}) {
    if(!document.is_object())return "顶层要是对象";
    const unsigned count=kind==Kind::units?unit_count:kind==Kind::pilots?pilot_count:weapon_count;
    Bytes bytes;
    std::map<std::string,uint32_t> records;   // key -> its record, for the checks after
    for(const auto& [key,entry]:document.items()) {
        unsigned id;
        if(key.find('@')!=std::string::npos) {
            const auto of=kind==Kind::units?mod_units::Of::unit:kind==Kind::weapons?mod_units::Of::weapon:mod_units::Of::pilot;
            const auto found=mod_units::resolve(json(key),package,numbers,of);
            if(!found)return key+"：没有这个新"+(kind==Kind::units?"机体":kind==Kind::weapons?"武器":"人物")+"（data/new_units.json 里的名字）";
            id=*found;
        } else {
            if(key.empty() || key.size()>4 || !std::all_of(key.begin(),key.end(),[](char c){return c>='0' && c<='9';}) || (key.size()>1 && key[0]=='0'))
                return key+"：编号要是十进制整数，或新机体、新武器的 @名字";
            id=unsigned(std::stoul(key));
            if(id>=count)return key+"：编号超出范围（0–"+std::to_string(count-1)+"）";
        }
        if(!entry.is_object())return key+"：要是对象";
        uint32_t record=table_window::row(table_window::units,id),terrain=unit_terrain;
        if(kind==Kind::pilots) {
            const int r=pilot_record(rom,id);
            if(r<0 || (r>=int(pilot_record_count) && !table_window::new_pilot(unsigned(r))))return key+"：这个人物没有能力记录";
            record=table_window::row(table_window::pilot_records,unsigned(r));terrain=pilot_terrain;
        } else if(kind==Kind::weapons) {
            record=table_window::row(table_window::weapons,id);terrain=weapon_terrain;
        }
        records[key]=record;
        for(const auto& [name,value]:entry.items()) {
            if(name=="terrain") {
                if(!value.is_object())return key+".terrain：要是对象";
                for(const auto& [channel,letter]:value.items()) {
                    const auto found=std::find(channels.begin(),channels.end(),channel);
                    if(found==channels.end())return key+".terrain：未知地形 "+channel+"（air、land、sea、space）";
                    const auto r=rank(letter);
                    if(!r)return key+".terrain."+channel+"：要是 A、B、C、D 或 -";
                    bytes[record+terrain+uint32_t(found-channels.begin())]=*r;
                }
                continue;
            }
            // This file's bytes, then earlier files' and packages' (a new unit's copied record),
            // then the ROM's.
            const auto here=[&](uint32_t at){
                if(const auto f=bytes.find(at);f!=bytes.end())return f->second;
                return mod_units::read_byte(out,rom,at);
            };
            const std::string where=key+"."+name;
            if(kind==Kind::units && name=="abilities") {
                if(!value.is_array())return where+"：要是数组";
                uint32_t set=0;
                for(const auto& item:value) {
                    const auto found=std::find_if(std::begin(abilities),std::end(abilities),[&](const Named& n){return item.is_string() && n.key==item.get<std::string>();});
                    if(found==std::end(abilities))return where+"：未知特殊能力 "+item.dump()+"（"+keys_of(abilities)+"）";
                    set|=found->mask;
                }
                uint32_t editable=0;for(const auto& n:abilities)editable|=n.mask;
                const uint32_t at=record+0x1C;
                const uint32_t old=uint32_t(here(at))<<24|uint32_t(here(at+1))<<16|uint32_t(here(at+2))<<8|here(at+3);
                const uint32_t bits=(old&~editable)|set;
                for(unsigned k=0;k<4;++k)bytes[at+k]=uint8_t(bits>>(24-8*k));
                continue;
            }
            if(kind==Kind::units && (name=="shield" || name=="sword")) {
                if(!value.is_boolean())return where+"：要是 true 或 false";
                const uint8_t bit=name=="sword"?1:2;
                bytes[record+0x18]=uint8_t(value.get<bool>()?here(record+0x18)|bit:here(record+0x18)&~bit);
                continue;
            }
            if(kind==Kind::units && name=="move_types") {
                if(!value.is_array() || value.empty())return where+"：要是非空数组（land、air、sea、space、underground）";
                uint8_t bits=0;
                for(const auto& item:value) {
                    const auto found=item.is_string()?std::find(move_types.begin(),move_types.end(),item.get<std::string>()):move_types.end();
                    if(found==move_types.end())return where+"：未知移动类型 "+item.dump();
                    bits|=uint8_t(1u<<(found-move_types.begin()));
                }
                bytes[record+0x05]=uint8_t((here(record+0x05)&0xE0)|bits);
                continue;
            }
            if(kind==Kind::units && name=="size") {
                const auto found=value.is_string()?std::find(sizes.begin(),sizes.end(),value.get<std::string>()):sizes.end();
                if(found==sizes.end())return where+"：要是 SS、S、M、L 或 LL";
                bytes[record+0x04]=uint8_t((here(record+0x04)&0xC0)|(1u<<(found-sizes.begin())));
                continue;
            }
            if(kind==Kind::weapons && (name=="post_move" || name=="beam" || name=="parryable" || name=="locked")) {
                if(!value.is_boolean())return where+"：要是 true 或 false";
                const uint32_t at=record+(name=="locked"?weapon_flags:weapon_kind);
                const uint8_t bit=name=="post_move"?weapon_post_move:name=="beam"?weapon_beam:name=="parryable"?weapon_parryable:weapon_locked;
                bytes[at]=uint8_t(value.get<bool>()?here(at)|bit:here(at)&~bit);
                continue;
            }
            if(kind==Kind::weapons && (name=="attack" || name=="terrain_at")) {
                const bool attack=name=="attack";
                const char* yes=attack?"melee":"attacker";const char* no=attack?"ranged":"target";
                if(!value.is_string() || (value.get<std::string>()!=yes && value.get<std::string>()!=no))return where+"：要是 "+yes+" 或 "+no;
                const uint8_t bit=attack?weapon_melee:weapon_terrain_self,old=here(record+weapon_kind);
                bytes[record+weapon_kind]=uint8_t(value.get<std::string>()==yes?old|bit:old&~bit);
                continue;
            }
            if(kind==Kind::weapons && name=="requires") {
                if(value.is_null()){bytes[record+weapon_requires]=0;continue;}
                const auto found=std::find_if(std::begin(requirements),std::end(requirements),[&v=value](const Requirement& r){return v.is_string() && r.key==v.get<std::string>();});
                if(found==std::end(requirements))return where+"：未知条件 "+value.dump()+"（null、"+keys_of(requirements)+"）";
                bytes[record+weapon_requires]=found->code;
                continue;
            }
            if(kind==Kind::units && name=="parts_slots") {
                if(!value.is_number_integer() || value.get<int64_t>()<0 || value.get<int64_t>()>4)return where+"：要是 0–4 的整数";
                bytes[record+0x19]=uint8_t(value.get<int>());
                continue;
            }
            if(kind==Kind::pilots && name=="skills") {
                if(!value.is_object())return where+"：要是对象";
                const int r=pilot_record(rom,id);
                const uint32_t rows=table_window::row(table_window::thresholds,unsigned(r));
                uint8_t bits=here(record+0x0F);
                bool first_row=false;
                for(const auto& [skill,levels]:value.items()) {
                    const auto found=std::find_if(std::begin(skills),std::end(skills),[&f=skill](const Skill& k){return k.key==f;});
                    if(found==std::end(skills))return where+"：未知技能 "+skill+"（"+keys_of(skills)+"）";
                    if(found->row==0) {
                        if(first_row && !levels.is_null())return where+"：底力、NT、强化人、圣战士、超能力只能有一个";
                        if(!levels.is_null())first_row=true;
                    }
                    if(levels.is_null()){bits&=uint8_t(~found->bit);continue;}
                    if(!levels.is_array() || levels.empty() || levels.size()>9)return where+"."+skill+"：要是 1–9 个等级的数组，或 null 去掉";
                    int last=0;
                    for(const auto& l:levels) {
                        if(!l.is_number_integer() || l.get<int64_t>()<1 || l.get<int64_t>()>99)return where+"."+skill+"：等级要是 1–99 的整数";
                        if(l.get<int>()<last)return where+"."+skill+"：等级要从小到大";
                        last=l.get<int>();
                    }
                    if(found->row==0)for(const auto& k:skills)if(k.row==0)bits&=uint8_t(~k.bit);
                    bits|=found->bit;
                    for(unsigned n=0;n<10;++n)bytes[rows+found->row*10+n]=uint8_t(n<levels.size()?levels[n].get<int>():0);
                }
                bytes[record+0x0F]=bits;
                continue;
            }
            if(kind==Kind::pilots && name=="spirits") {
                const int r=spirit_record(rom,id);
                if(r<0)return where+"：这个人物没有精神记录";
                if(!value.is_array() || value.size()>6)return where+"：要是最多 6 项的数组，每项 [精神, 等级]";
                const uint32_t at=table_window::row(table_window::spirit_records,unsigned(r));
                for(unsigned n=0;n<6;++n) {
                    // An unused pair: a level no pilot reaches (800A63E0 counts every pair at or
                    // below the pilot's level, a 0 included); the Library skips past 99 too.
                    if(n>=value.size()){bytes[at+n*2]=0xFF;bytes[at+n*2+1]=0;continue;}
                    const auto& item=value[n];
                    if(!item.is_array() || item.size()!=2 || !item[0].is_string() || !item[1].is_number_integer())return where+"：每项要是 [精神, 等级]";
                    const auto found=std::find(spirits.begin(),spirits.end(),item[0].get<std::string>());
                    if(found==spirits.end()){std::string all;for(const auto k:spirits)all+=(all.empty()?"":"、")+std::string(k);return where+"：未知精神 "+item[0].get<std::string>()+"（"+all+"）";}
                    const auto level=item[1].get<int64_t>();
                    if(level<1 || level>99)return where+"：习得等级要是 1–99";
                    if(n && level<value[n-1][1].get<int64_t>())return where+"：习得等级要从小到大";
                    bytes[at+n*2]=uint8_t(level);bytes[at+n*2+1]=uint8_t(found-spirits.begin());
                }
                continue;
            }
            const auto list=fields(kind);
            const auto field=std::find_if(list.begin(),list.end(),[&n=name](const Field& f){return f.name==n;});
            if(field==list.end()) {
                std::string known="terrain";
                for(const auto& f:list)known+="、"+std::string(f.name);
                if(kind==Kind::units)known+="、abilities、shield、sword、size、move_types、parts_slots";
                if(kind==Kind::pilots)known+="、skills、spirits";
                if(kind==Kind::weapons)known+="、attack、beam、post_move、parryable、terrain_at、requires、locked";
                return key+"：未知字段 "+name+"（"+known+"）";
            }
            if(!value.is_number_integer())return key+"."+name+"：要是整数";
            const auto v=value.get<int64_t>();
            if(v<field->low || v>field->high)return key+"."+name+"：超出范围（"+std::to_string(field->low)+"–"+std::to_string(field->high)+"）";
            if(v%field->scale)return key+"."+name+"：要是 "+std::to_string(field->scale)+" 的倍数";
            const auto stored=uint32_t(int32_t(v/field->scale));
            if(field->bytes==2){bytes[record+field->offset]=uint8_t(stored>>8);bytes[record+field->offset+1]=uint8_t(stored);}
            else bytes[record+field->offset]=uint8_t(stored);
        }
    }
    if(kind==Kind::weapons)
        for(const auto& [key,record]:records) {
            const auto at=[&](uint32_t offset){
                if(const auto f=bytes.find(record+offset);f!=bytes.end())return f->second;
                return mod_units::read_byte(out,rom,record+offset);
            };
            if(at(0x02)>at(0x03))return key+"：range_min 不能大于 range_max";
            // A map weapon's flow has its own 31x31 tables; only ordinary weapons go past 15 (move_range.cpp).
            if((at(0x00)&weapon_map) && at(0x03)>15)return key+"：地图武器的 range_max 不能超过 15";
        }
    for(const auto& [a,v]:bytes)out[a]=v;
    return {};
}

struct Collected {
    Bytes bytes;
    std::vector<std::string> problems;   // "<id> data/<file>: why"; such a file is left out
    std::map<unsigned,std::string> units;   // a new unit's number -> the package that adds it
    std::map<unsigned,std::string> pilots;  // a new character's number -> the package that adds it
    std::map<unsigned,unsigned> copies;     // a new unit's number -> the unit it copies
    std::map<unsigned,unsigned> weapon_copies;
    std::map<std::string,json> names;       // those packages' names (mod.json "name")
    mod_units::Numbers numbers;             // every name's number, whether its file parsed or not
    std::map<uint16_t,std::pair<uint32_t,uint32_t>> name_sources;   // private text id -> record copied
    std::map<unsigned,std::vector<uint8_t>> resources;              // the packages' own (mod_sprites.hpp)
    std::map<std::string,mod_units::Sprite> sprites;
    std::map<unsigned,unsigned> predecessors;                       // new unit -> the unit it inherits from
    std::map<unsigned,std::vector<std::pair<unsigned,unsigned>>> carries;
    std::map<unsigned,std::vector<unsigned>> crews;
    std::map<unsigned,mod_units::Taken::Combine> combines;
    std::map<unsigned,int> works;                                   // new unit -> its Library work
    std::map<unsigned,unsigned> pilot_copies,voices;                // new character -> the one it copies / speaks as
    std::map<unsigned,mod_units::Taken::Portrait> portraits;        // new character -> its own portrait
    std::vector<std::pair<unsigned,unsigned>> loves;                // love rows (holder, partner)
    std::map<unsigned,unsigned> combo_as;                           // new character -> who it counts as in combination attacks
};
// The active packages, bottom first as mods::installed() returns them.
inline Collected collect(const std::vector<mods::Package>& packages,std::span<const uint8_t> rom) {
    Collected c;
    mod_units::Taken taken;
    std::error_code error;
    c.numbers=mod_ids::assign(packages);
    for(const auto& p:packages) {
        if(!p.active || !fs::is_directory(p.folder/"data",error))continue;
        for(const auto& item:fs::directory_iterator(p.folder/"data",error)) {
            const auto name=item.path().filename().string();
            if(item.is_regular_file(error) && item.path().extension()==".json" && name!="new_units.json" && std::find(files.begin(),files.end(),name)==files.end())
                c.problems.push_back(p.id()+" data/"+name+"：暂不支持这个文件");
        }
        // New units first: the number files may change the records they copy.
        if(const auto path=p.folder/"data"/"new_units.json";fs::is_regular_file(path,error)) {
            std::ifstream in(path,std::ios::binary);
            const auto document=json::parse(in,nullptr,false);
            const std::string where=p.id()+" data/new_units.json：";
            if(document.is_discarded())c.problems.push_back(where+"JSON 无效");
            else if(auto why=mod_units::parse(document,p.id(),c.numbers,rom,c.bytes,c.bytes,taken,p.folder);!why.empty())c.problems.push_back(where+why);
            else {
                for(const auto& [key,unit]:c.numbers.units)
                    if(key.compare(0,p.id().size()+1,p.id()+"@")==0){c.units[unit]=p.id();c.names[p.id()]=p.manifest?p.manifest->name:json(p.id());}
                for(const auto& [key,pilot]:c.numbers.pilots)
                    if(key.compare(0,p.id().size()+1,p.id()+"@")==0){c.pilots[pilot]=p.id();c.names[p.id()]=p.manifest?p.manifest->name:json(p.id());}
                c.copies=taken.copies;c.weapon_copies=taken.weapon_copies;c.name_sources=taken.names;
                c.resources=taken.resources;c.sprites=taken.sprites;
                c.predecessors=taken.predecessors;c.carries=taken.carries;c.crews=taken.crews;c.combines=taken.combines;c.works=taken.works;
                c.pilot_copies=taken.pilot_copies;c.voices=taken.voices;c.portraits=taken.portraits;c.loves=taken.loves;c.combo_as=taken.combo_as;
            }
        }
        for(const auto file:files) {
            const auto path=p.folder/"data"/std::string(file);
            if(!fs::is_regular_file(path,error))continue;
            std::ifstream in(path,std::ios::binary);
            const auto document=json::parse(in,nullptr,false);
            const std::string where=p.id()+" data/"+std::string(file)+"：";
            if(document.is_discarded()){c.problems.push_back(where+"JSON 无效");continue;}
            const auto kind=file=="units.json"?Kind::units:file=="pilots.json"?Kind::pilots:Kind::weapons;
            if(auto why=parse(document,kind,rom,c.bytes,p.id(),c.numbers);!why.empty())c.problems.push_back(where+why);
        }
    }
    return c;
}

// --- The game's copy patches ------------------------------------------------------

struct State {
    std::span<const uint8_t> rom;
    std::vector<std::pair<uint32_t,uint8_t>> rules;   // the upgrade rules file's, underneath
    fs::path output;
    unsigned revision{};
    mods::Version game{-1,-1,-1};
    std::atomic<bool> kinds_changed{false};   // some weapon's printed kind bits differ from the ROM's
    std::mutex owners_mutex;
    std::map<unsigned,std::string> owners;   // Collected::units of the active packages
    std::map<unsigned,std::string> pilot_owners;   // Collected::pilots
    std::map<std::string,json> names;
    std::map<unsigned,unsigned> copies,weapon_copies;   // Collected's
    mod_units::Numbers numbers;
    std::map<std::string,mod_units::Sprite> sprites;
    std::map<unsigned,unsigned> predecessors;
    std::map<unsigned,std::vector<std::pair<unsigned,unsigned>>> carries;
    std::map<unsigned,std::vector<unsigned>> crews;
    std::map<unsigned,mod_units::Taken::Combine> combines;
    std::map<unsigned,unsigned> pilot_copies,voices;
    std::map<unsigned,mod_units::Taken::Portrait> portraits;
    std::vector<std::pair<unsigned,unsigned>> loves;
    std::map<unsigned,unsigned> combo_as;
    // The host draws a package sprite's HD frames (native_sprite.cpp); set at start-up, called
    // on the game thread whenever the packages change.
    std::function<void(const std::map<std::string,mod_units::Sprite>&)> sprites_changed;
};
inline State& state(){static State value;return value;}
// The new units the active packages add, by number: the package id and its name. Any thread.
struct Owner { std::string package; json name; };
inline std::map<unsigned,Owner> unit_owners() {
    auto& s=state();
    std::lock_guard lock(s.owners_mutex);
    std::map<unsigned,Owner> out;
    for(const auto& [unit,id]:s.owners)out[unit]={id,s.names.count(id)?s.names.at(id):json(id)};
    return out;
}
inline std::map<unsigned,Owner> pilot_owners() {
    auto& s=state();
    std::lock_guard lock(s.owners_mutex);
    std::map<unsigned,Owner> out;
    for(const auto& [pilot,id]:s.pilot_owners)out[pilot]={id,s.names.count(id)?s.names.at(id):json(id)};
    return out;
}
// The unit or weapon a package's new one copies (its pictures, series and crew go with that
// one's). Any thread.
inline std::optional<unsigned> unit_copy(unsigned unit) {
    auto& s=state();
    std::lock_guard lock(s.owners_mutex);
    const auto found=s.copies.find(unit);
    return found==s.copies.end()?std::nullopt:std::optional<unsigned>(found->second);
}
inline std::optional<unsigned> weapon_copy(unsigned weapon) {
    auto& s=state();
    std::lock_guard lock(s.owners_mutex);
    const auto found=s.weapon_copies.find(weapon);
    return found==s.weapon_copies.end()?std::nullopt:std::optional<unsigned>(found->second);
}
// The new units of the active packages, by number. Any thread.
// A package's new unit's upgrade inheritance: the unit it inherits from, and its weapons'
// (new, old) pairs; and the crew it is registered with. Any thread.
inline std::optional<unsigned> predecessor(unsigned unit) {
    auto& s=state();
    std::lock_guard lock(s.owners_mutex);
    const auto found=s.predecessors.find(unit);
    return found==s.predecessors.end()?std::nullopt:std::optional<unsigned>(found->second);
}
inline std::vector<std::pair<unsigned,unsigned>> carries(unsigned unit) {
    auto& s=state();
    std::lock_guard lock(s.owners_mutex);
    const auto found=s.carries.find(unit);
    return found==s.carries.end()?std::vector<std::pair<unsigned,unsigned>>{}:found->second;
}
// The new characters: the original each copies, and the one whose quotes it speaks. Any thread.
inline std::map<unsigned,unsigned> pilot_copies() {
    auto& s=state();
    std::lock_guard lock(s.owners_mutex);
    return s.pilot_copies;
}
inline std::map<unsigned,mod_units::Taken::Portrait> portraits() {
    auto& s=state();
    std::lock_guard lock(s.owners_mutex);
    return s.portraits;
}
inline std::vector<std::pair<unsigned,unsigned>> loves() {
    auto& s=state();
    std::lock_guard lock(s.owners_mutex);
    return s.loves;
}
inline std::map<unsigned,unsigned> combo_as() {
    auto& s=state();
    std::lock_guard lock(s.owners_mutex);
    return s.combo_as;
}
inline std::map<unsigned,unsigned> voices() {
    auto& s=state();
    std::lock_guard lock(s.owners_mutex);
    return s.voices;
}
// The new units combined from parts (mod_combine.hpp). Any thread.
inline std::map<unsigned,mod_units::Taken::Combine> combines() {
    auto& s=state();
    std::lock_guard lock(s.owners_mutex);
    return s.combines;
}
inline std::vector<unsigned> crew(unsigned unit) {
    auto& s=state();
    std::lock_guard lock(s.owners_mutex);
    const auto found=s.crews.find(unit);
    return found==s.crews.end()?std::vector<unsigned>{}:found->second;
}
// A package's sprite by the resource numbers the game draws it from. Any thread.
inline std::optional<mod_units::Sprite> sprite(unsigned scene,unsigned atlas,unsigned palette) {
    auto& s=state();
    std::lock_guard lock(s.owners_mutex);
    for(const auto& [_,sprite]:s.sprites)
        if(sprite.scene==scene && sprite.atlas==atlas && sprite.palette==palette)return sprite;
    return std::nullopt;
}
inline std::vector<unsigned> new_units() {
    auto& s=state();
    std::lock_guard lock(s.owners_mutex);
    std::vector<unsigned> out;
    for(const auto& [unit,_]:s.copies)out.push_back(unit);
    return out;
}
// Whether any weapon's 格／射, P or B differs from the ROM's: the overlay then redraws those
// menu names in Japanese too (ui_text.cpp names_only). Any thread.
inline bool kinds_changed(){return state().kinds_changed.load(std::memory_order_relaxed);}

// A weapon's kind byte as the ROM has it and as the game reads it (a package's or the rules
// file's), when they differ in a printed bit (格／射, P, B); nothing when they agree. Any
// thread, once configured.
inline std::optional<std::pair<uint8_t,uint8_t>> kind_change(unsigned weapon) {
    const auto& s=state();
    if(s.rom.empty() || weapon>=weapon_count)return std::nullopt;
    const uint32_t at=weapon_records+weapon*weapon_record_size+weapon_kind;
    const uint8_t now=upgrades::game_byte(s.rom,at),was=s.rom[at];
    return ((now^was)&weapon_printed)?std::optional<std::pair<uint8_t,uint8_t>>({was,now}):std::nullopt;
}
// Host start-up, after upgrades::configure().
inline void configure(std::span<const uint8_t> rom,const fs::path& output) {
    auto& s=state();
    s.rom=rom;s.rules=upgrades::patches();s.output=output;s.revision=0;s.game={-1,-1,-1};
}
// Before each copy from ROM, on the game thread: again when the packages change, or
// once the front end has told the game its version (until then a package's "game"
// requirement fails). A unit follows at its next recompute, a pilot at the next load.
// The resident section (ROM 0x1000, 1 MiB at 80076610) is copied once at start: the tables
// the game reads from RAM there (the inheritance tables D_800CA3A0 and D_800CB5E8, a new
// unit's pieces) take the packages' bytes in RDRAM too, and the ROM's back when a package
// stops giving them.
inline void sync_resident(uint8_t* ram,const std::vector<std::pair<uint32_t,uint8_t>>& before,
                          const std::vector<std::pair<uint32_t,uint8_t>>& after,std::span<const uint8_t> rom) {
    const auto inside=[](uint32_t at){return at>=upgrades::resident_rom && at<upgrades::resident_rom+upgrades::resident_copy;};
    const auto write=[&](uint32_t at,uint8_t v){upgrades::write8(ram,upgrades::resident_ram+(at-upgrades::resident_rom),v);};
    std::set<uint32_t> now;
    for(const auto& [at,v]:after)if(inside(at)){write(at,v);now.insert(at);}
    for(const auto& [at,v]:before)if(inside(at) && !now.count(at) && at<rom.size())write(at,rom[at]);
}
inline void refresh(uint8_t* ram=nullptr) {
    auto& s=state();
    if(s.rom.empty())return;
    const unsigned now=mods::revision();
    const auto game=mods::game_version();
    if(now==s.revision && game==s.game)return;
    s.revision=now;s.game=game;
    const auto found=collect(mods::installed(game),s.rom);
    Bytes merged(s.rules.begin(),s.rules.end());
    for(const auto& [at,value]:found.bytes)merged[at]=value;
    std::vector<std::pair<uint32_t,uint8_t>> list;
    // The ROM's bytes that differ, and every byte of the table window (the new rows).
    for(const auto& [at,value]:merged)
        if(table_window::inside(at) || (at<s.rom.size() && s.rom[at]!=value))list.emplace_back(at,value);
    if(ram)sync_resident(ram,upgrades::patches(),list,s.rom);
    bool kinds=false;
    for(const auto& [at,value]:list)
        if(at>=weapon_records && at<weapon_records+weapon_count*weapon_record_size && (at-weapon_records)%weapon_record_size==weapon_kind && ((value^s.rom[at])&weapon_printed))kinds=true;
    // A new weapon prints the markers of the weapon it copies: changed when its kind is not that one's.
    for(const auto& [weapon,source]:found.weapon_copies) {
        const auto kind=[&](unsigned w){return mod_units::read_byte(merged,s.rom,table_window::row(table_window::weapons,w)+weapon_kind);};
        if((kind(weapon)^kind(source))&weapon_printed)kinds=true;
    }
    s.kinds_changed.store(kinds,std::memory_order_relaxed);
    {std::lock_guard lock(s.owners_mutex);s.owners=found.units;s.names=found.names;s.copies=found.copies;s.weapon_copies=found.weapon_copies;s.numbers=found.numbers;s.sprites=found.sprites;
     s.predecessors=found.predecessors;s.carries=found.carries;s.crews=found.crews;s.combines=found.combines;
     s.pilot_copies=found.pilot_copies;s.voices=found.voices;s.pilot_owners=found.pilots;s.portraits=found.portraits;s.loves=found.loves;s.combo_as=found.combo_as;}
    mod_tables::publish_resources(found.resources);
    if(s.sprites_changed)s.sprites_changed(found.sprites);
    upgrades::name_sources()=found.name_sources;
    upgrades::patches()=std::move(list);
    upgrades::publish_patches();
    if(ram)mod_tables::fill_map_icons(s.rom,ram);
    for(const auto& problem:found.problems)std::fprintf(stderr,"SRW64_MOD_DATA_PROBLEM %s\n",problem.c_str());
    std::fprintf(stderr,"SRW64_MOD_DATA bytes=%zu problems=%zu\n",found.bytes.size(),found.problems.size());
    if(!s.output.empty()) {
        json bytes=json::object();
        for(const auto& [at,value]:found.bytes)bytes[std::to_string(at)]=value;
        std::ofstream(s.output/"mod-data.json")<<json({{"schema","srw64.mod-data-applied.v1"},{"bytes",bytes},{"problems",found.problems}}).dump(2)<<'\n';
    }
}
}
