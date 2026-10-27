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

// Special abilities a unit's +1C may carry, by the ability table's masks (overlay 801DC8FC,
// the status page's names). 変形, 合体 and 分離 stay as the ROM has them: they need forms.
struct Named { std::string_view key; uint32_t mask; };
inline constexpr Named abilities[]={
    {"beam_coat",0x200},{"i_field",0x20},{"planet_defensor",0x800},{"aura_barrier",0x4000},
    {"afterimage",0x10},{"mach_special",0x1000},{"true_mach_special",0x2000},{"god_shadow",0x1},
    {"getter_vision",0x20000},{"shungensoku",0x40000},{"hyper_jammer",0x100000},
    {"hp_regen_10",0x4},{"hp_regen_20",0x8}};
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
    const uint32_t at=spirit_map+pilot*2;
    return at+1<rom.size()?int(int16_t(rom[at]<<8|rom[at+1])):-1;
}
template<class T,size_t N> std::string keys_of(const T (&list)[N]) {std::string out;for(const auto& x:list)out+=(out.empty()?"":"、")+std::string(x.key);return out;}

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
            const auto here=[&](uint32_t at){const auto f=bytes.find(at);return f!=bytes.end()?f->second:at<rom.size()?rom[at]:uint8_t(0);};
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
            if(kind==Kind::units && name=="size") {
                const auto found=value.is_string()?std::find(sizes.begin(),sizes.end(),value.get<std::string>()):sizes.end();
                if(found==sizes.end())return where+"：要是 SS、S、M、L 或 LL";
                bytes[record+0x04]=uint8_t((here(record+0x04)&0xC0)|(1u<<(found-sizes.begin())));
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
                const uint32_t rows=threshold_records+uint32_t(r)*threshold_record_size;
                uint8_t bits=here(record+0x0F);
                bool first_row=false;
                for(const auto& [skill,levels]:value.items()) {
                    const auto found=std::find_if(std::begin(skills),std::end(skills),[&](const Skill& k){return k.key==skill;});
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
                const uint32_t at=spirit_records+uint32_t(r)*spirit_record_size;
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
            const auto field=std::find_if(list.begin(),list.end(),[&](const Field& f){return f.name==name;});
            if(field==list.end()) {
                std::string known="terrain";
                for(const auto& f:list)known+="、"+std::string(f.name);
                if(kind==Kind::units)known+="、abilities、shield、sword、size、parts_slots";
                if(kind==Kind::pilots)known+="、skills、spirits";
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
    upgrades::publish_patches();
    for(const auto& problem:found.problems)std::fprintf(stderr,"SRW64_MOD_DATA_PROBLEM %s\n",problem.c_str());
    std::fprintf(stderr,"SRW64_MOD_DATA bytes=%zu problems=%zu\n",found.bytes.size(),found.problems.size());
    if(!s.output.empty()) {
        json bytes=json::object();
        for(const auto& [at,value]:found.bytes)bytes[std::to_string(at)]=value;
        std::ofstream(s.output/"mod-data.json")<<json({{"schema","srw64.mod-data-applied.v1"},{"bytes",bytes},{"problems",found.problems}}).dump(2)<<'\n';
    }
}
}
