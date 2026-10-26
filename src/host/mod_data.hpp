#pragma once
#include "mod_library.hpp"
#include "upgrade_rules.hpp"
#include <cstdio>
#include <span>

// Number MODs (docs/design/mod-packages.md §5), so far the terrain ranks of units and
// pilots: data/units.json and data/pilots.json of the enabled packages, the upper package
// winning, become bytes of the ROM records.
//
//   data/units.json   { "184": { "terrain": { "sea": "A" } } }      by unit number
//   data/pilots.json  { "133": { "terrain": { "space": "A" } } }    by pilot number
//
// The game copies both records from ROM whenever it uses them: a unit's on every stat
// recompute (800A5254 writes runtime +0x16..+0x19 from record +0x0E..+0x11), a pilot's in
// the full rebuild 800A7F8C (runtime +0x2E..+0x31 from record +0x07..+0x0A), which a
// loaded save runs for every pilot (800A8E0C). So the bytes join the upgrade rules' copy
// patches (upgrade_rules.hpp) and a saved game follows them when loaded. A pilot's record
// may be shared (800A6398 maps pilot -> record); changing one pilot changes all of them.
namespace srw64::mod_data {
namespace fs=std::filesystem;
using json=nlohmann::json;
using Bytes=std::map<uint32_t,uint8_t>;

inline constexpr uint32_t unit_records=0x71B80, unit_record_size=0x24, unit_count=363, unit_terrain=0x0E;
inline constexpr uint32_t pilot_map=0x800CA9C4-0x80075610, pilot_count=360;   // s16 per pilot, -1 none
inline constexpr uint32_t pilot_records=0x7A1A0, pilot_record_size=0x10, pilot_record_count=257, pilot_terrain=0x07;
inline constexpr std::array<std::string_view,4> channels={"air","land","sea","space"};
inline constexpr std::array<std::string_view,2> files={"units.json","pilots.json"};

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
inline std::string parse(const json& document,bool pilots,std::span<const uint8_t> rom,Bytes& out) {
    if(!document.is_object())return "顶层要是对象";
    const unsigned count=pilots?pilot_count:unit_count;
    Bytes bytes;
    for(const auto& [key,entry]:document.items()) {
        if(key.empty() || key.size()>3 || !std::all_of(key.begin(),key.end(),[](char c){return c>='0' && c<='9';}) || (key.size()>1 && key[0]=='0'))
            return key+"：编号要是十进制整数";
        const unsigned id=unsigned(std::stoul(key));
        if(id>=count)return key+"：编号超出范围（0–"+std::to_string(count-1)+"）";
        if(!entry.is_object())return key+"：要是对象";
        uint32_t at=unit_records+id*unit_record_size+unit_terrain;
        if(pilots) {
            const int record=pilot_record(rom,id);
            if(record<0 || record>=int(pilot_record_count))return key+"：这个人物没有能力记录";
            at=pilot_records+uint32_t(record)*pilot_record_size+pilot_terrain;
        }
        for(const auto& [field,value]:entry.items()) {
            if(field!="terrain")return key+"：未知字段 "+field;
            if(!value.is_object())return key+".terrain：要是对象";
            for(const auto& [channel,letter]:value.items()) {
                const auto found=std::find(channels.begin(),channels.end(),channel);
                if(found==channels.end())return key+".terrain：未知地形 "+channel+"（air、land、sea、space）";
                const auto r=rank(letter);
                if(!r)return key+".terrain."+channel+"：要是 A、B、C、D 或 -";
                bytes[at+uint32_t(found-channels.begin())]=*r;
            }
        }
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
            if(auto why=parse(document,file=="pilots.json",rom,c.bytes);!why.empty())c.problems.push_back(where+why);
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
