#pragma once
#include "mod_library.hpp"
#include "upgrade_rules.hpp"
#include <cstdio>
#include <span>

// Number MODs (docs/design/mod-packages.md §5, docs/design/mod-names-art-data.md §2.3):
// data/units.json, data/pilots.json and data/weapons.json of the enabled packages, the
// upper package winning, become bytes of the ROM records.
//
//   data/units.json    { "184": { "hp": 5200, "terrain": { "sea": "A" } } }   by unit number
//   data/pilots.json   { "133": { "melee": 160, "terrain": { "space": "A" } } } by pilot number
//   data/weapons.json  { "731": { "power": 3600, "range_max": 4 } }          by weapon number
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
    {"repair_cost",0x14,2,false,0,65535},{"upgrade_cap",0x20,1,false,0,15}};
inline constexpr Field pilot_fields[]={
    {"melee",0x01,1,false,0,255},{"ranged",0x02,1,false,0,255},{"evade",0x03,1,false,0,255},
    {"hit",0x04,1,false,0,255},{"reaction",0x05,1,false,0,255},{"skill",0x06,1,false,0,255},
    {"sp",0x0C,1,false,0,255},{"double_move",0x0E,1,false,0,99}};
// ammo -1 is none (0xFF).
inline constexpr Field weapon_fields[]={
    {"power",0x01,1,false,0,25500,100},{"range_min",0x02,1,false,0,30},{"range_max",0x03,1,false,1,30},
    {"hit",0x04,1,true,-128,127},{"ammo",0x05,1,true,-1,99},{"en",0x06,1,false,0,255},
    {"morale",0x07,1,false,0,255},{"critical",0x0D,1,true,-128,127}};
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
inline int pilot_record(std::span<const uint8_t> rom,unsigned pilot) {
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

// One file into bytes, all or nothing; an error names the entry and the field.
inline std::string parse(const json& document,Kind kind,std::span<const uint8_t> rom,Bytes& out) {
    if(!document.is_object())return "顶层要是对象";
    const unsigned count=kind==Kind::units?unit_count:kind==Kind::pilots?pilot_count:weapon_count;
    Bytes bytes;
    for(const auto& [key,entry]:document.items()) {
        if(key.empty() || key.size()>4 || !std::all_of(key.begin(),key.end(),[](char c){return c>='0' && c<='9';}) || (key.size()>1 && key[0]=='0'))
            return key+"：编号要是十进制整数";
        const unsigned id=unsigned(std::stoul(key));
        if(id>=count)return key+"：编号超出范围（0–"+std::to_string(count-1)+"）";
        if(!entry.is_object())return key+"：要是对象";
        uint32_t record=unit_records+id*unit_record_size,terrain=unit_terrain;
        if(kind==Kind::pilots) {
            const int r=pilot_record(rom,id);
            if(r<0 || r>=int(pilot_record_count))return key+"：这个人物没有能力记录";
            record=pilot_records+uint32_t(r)*pilot_record_size;terrain=pilot_terrain;
        } else if(kind==Kind::weapons) {
            record=weapon_records+id*weapon_record_size;terrain=weapon_terrain;
        }
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
            const auto list=fields(kind);
            const auto field=std::find_if(list.begin(),list.end(),[&](const Field& f){return f.name==name;});
            if(field==list.end()) {
                std::string known="terrain";
                for(const auto& f:list)known+="、"+std::string(f.name);
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
        for(const auto& [key,entry]:document.items()) {
            const uint32_t record=weapon_records+unsigned(std::stoul(key))*weapon_record_size;
            const auto at=[&](uint32_t offset){const auto f=bytes.find(record+offset);return f!=bytes.end()?f->second:record+offset<rom.size()?rom[record+offset]:uint8_t(0);};
            if(at(0x02)>at(0x03))return key+"：range_min 不能大于 range_max";
        }
    for(const auto& [a,v]:bytes)out[a]=v;
    return {};
}

struct Collected {
    Bytes bytes;
    std::vector<std::string> problems;   // "<id> data/<file>: why"; such a file is left out
};
// The active packages, bottom first as mods::installed() returns them.
inline Collected collect(const std::vector<mods::Package>& packages,std::span<const uint8_t> rom) {
    Collected c;
    std::error_code error;
    for(const auto& p:packages) {
        if(!p.active || !fs::is_directory(p.folder/"data",error))continue;
        for(const auto& item:fs::directory_iterator(p.folder/"data",error)) {
            const auto name=item.path().filename().string();
            if(item.is_regular_file(error) && item.path().extension()==".json" && std::find(files.begin(),files.end(),name)==files.end())
                c.problems.push_back(p.id()+" data/"+name+"：暂不支持这个文件");
        }
        for(const auto file:files) {
            const auto path=p.folder/"data"/std::string(file);
            if(!fs::is_regular_file(path,error))continue;
            std::ifstream in(path,std::ios::binary);
            const auto document=json::parse(in,nullptr,false);
            const std::string where=p.id()+" data/"+std::string(file)+"：";
            if(document.is_discarded()){c.problems.push_back(where+"JSON 无效");continue;}
            const auto kind=file=="units.json"?Kind::units:file=="pilots.json"?Kind::pilots:Kind::weapons;
            if(auto why=parse(document,kind,rom,c.bytes);!why.empty())c.problems.push_back(where+why);
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
};
inline State& state(){static State value;return value;}

// Host start-up, after upgrades::configure().
inline void configure(std::span<const uint8_t> rom,const fs::path& output) {
    auto& s=state();
    s.rom=rom;s.rules=upgrades::patches();s.output=output;s.revision=0;s.game={-1,-1,-1};
}
// Before each copy from ROM, on the game thread: again when the packages change, or
// once the front end has told the game its version (until then a package's "game"
// requirement fails). A unit follows at its next recompute, a pilot at the next load.
inline void refresh() {
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
    for(const auto& [at,value]:merged)if(at<s.rom.size() && s.rom[at]!=value)list.emplace_back(at,value);
    upgrades::patches()=std::move(list);
    for(const auto& problem:found.problems)std::fprintf(stderr,"SRW64_MOD_DATA_PROBLEM %s\n",problem.c_str());
    std::fprintf(stderr,"SRW64_MOD_DATA bytes=%zu problems=%zu\n",found.bytes.size(),found.problems.size());
    if(!s.output.empty()) {
        json bytes=json::object();
        for(const auto& [at,value]:found.bytes)bytes[std::to_string(at)]=value;
        std::ofstream(s.output/"mod-data.json")<<json({{"schema","srw64.mod-data-applied.v1"},{"bytes",bytes},{"problems",found.problems}}).dump(2)<<'\n';
    }
}
}
