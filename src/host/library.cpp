#include "library.hpp"
#include "native_dialogue.hpp"
#include "upgrade_page.hpp"
#include "localization/catalog.hpp"
#include "librecomp/game.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <span>

extern uint8_t* srw64_rdram;  // host.cpp

namespace srw64::library {
namespace {
using json=nlohmann::json;
// Resident tables: ROM = VRAM - 0x80075610. The ユニット能力 screen's own tables are in its
// overlay (ROM 8F4B0 loaded at 801C4500).
constexpr uint32_t resident(uint32_t vram){return vram-0x80075610;}
constexpr uint32_t overlay(uint32_t vram){return vram-0x801C4500+0x8F4B0;}
// Units: 363 records of 0x24 (800A6E68). +0 HP, +2 EN, +4 size bits, +5 movement bits,
// +6 move, +8 mobility, +A armor, +C limit, +E..+11 terrain, +14 repair cost, +18
// equipment (2 = shield), +1C special abilities.
constexpr uint32_t units_at=0x71B80,unit_size=0x24,unit_count=363;
// A unit's weapon list (800A68BC): base + u32[base + unit*4] leads to 12-byte entries, the
// weapon then the five forms it belongs to (FFFF none); FFFF ends the list.
constexpr uint32_t weapon_lists=0x7E210,weapon_entry=12;
// Weapons: 1329 records of 0x10 (800A6A18): +1 power/100, +2/+3 range, +4 hit, +5 ammo
// (FF none), +6 EN, +7 morale, +8 required skill (a text id when 2 or more), +9..+C
// terrain, +D critical.
constexpr uint32_t weapons_at=0x74E90,weapon_size=0x10,weapon_count=1329;
// Pilots: actor -> s16 base record (800A6398, 16 bytes: +1..+6 melee, ranged, evade, hit,
// reaction, skill; +7..+A terrain; +C SP; +F skill bits) and the same index into the
// skill thresholds (800A6340, three groups of ten, nine read); actor -> s16 spirit record
// (800A63E0, six (level, command) pairs). Negative maps have none. The base record's +E
// is the level of two actions a turn: 800A6238 sets the pilot's +34 to 2 from that level
// on, 1 below it, and the map takes one from +35 per action. 0 on sub-pilots and fairies.
constexpr uint32_t stats_map=resident(0x800CA9C4),spirits_map=resident(0x800CA6F4);
// SP costs: one u8 per command for everyone (D_80217F70, ROM 100AD0; 801E195C takes it
// from the caster's SP).
constexpr uint32_t spirit_costs=0x100AD0;
constexpr uint32_t pilot_stats_at=0x7A1A0,pilot_stats_size=0x10,thresholds_at=0x7B1B0,thresholds_size=30,spirits_at=0x7CFB0,spirits_size=12;
constexpr unsigned actor_count=361,mapped_actors=360;
constexpr uint32_t move_icons=overlay(0x801DC8F0),ability_table=overlay(0x801DC8FC);
constexpr unsigned ability_entries=16;
constexpr uint16_t text_unit_names=527,text_weapon_pure=1370,text_weapon_menu=2699,text_pilot_names=4382,text_pilot_full=4743,text_spirits=969,
    text_sizes=0x446,text_shield_yes=0x38F;
// The protagonists and partners (actors 25-32) take their default names from records
// 487 / 495, not from the name buffers a loaded game fills (docs/native/default-names.md).
constexpr unsigned first_person=25,people=8;
constexpr uint16_t text_people=487,text_people_full=495;
// The skills a pilot's +F bits name (ability_page.cpp, 801C9xxx): the first group's
// one name by priority 04, 08, 10, 20, 40; 切り払い and S防御 count the other two.
// Level L of a skill is the text record base + L.
// The works (作品): the original's unreachable キャラクターリスト / ロボットリスト (title
// overlay load_0010DA50, ROM 10DA50 at 801C4500; 801C8AF8 and 801C930C build them) give
// each listed character {u16 actor, s16 work, u16 flag} and unit {u16 unit, s16 work,
// s16 model}, in the lists' order. The work's title is text 60 + work, a unit's model
// number 110 + model (-1 none). Duplicate records name their main one in the "seen"
// alias tables (80091574 / 80091670: u16 pairs, duplicate then main).
constexpr uint32_t title_overlay(uint32_t vram){return vram-0x801C4500+0x10DA50;}
constexpr uint32_t character_list=title_overlay(0x801CB3A0),robot_list=title_overlay(0x801CB964),actor_aliases=resident(0x800C6A08),unit_aliases=resident(0x800C6A8C);
constexpr unsigned character_rows=246,robot_rows=316,actor_alias_count=33,unit_alias_count=22,work_count=25;
constexpr uint16_t text_works=60,text_models=110;
// Records neither list nor an alias reaches, by their names and places in the ROM's
// blocks. 真・ゲッター1/2/3 (177-179) the list files under オリジナル; they belong with
// ゲッターロボ. Anyone else (AI, ゲリラ, ???...) goes under その他.
constexpr std::pair<uint16_t,int8_t> unit_works[]={{5,8},{29,8},{361,8},{362,8},{211,24},{214,24},{267,10},{347,20},{348,20},{349,20},{350,20},
    {354,13},{355,13},{356,13},{360,13},{357,14},{358,14},{359,14},{177,13},{178,13},{179,13}};
constexpr std::pair<uint16_t,int8_t> actor_works[]={{19,8},{159,24},{211,22},{288,9},{289,9},{305,9},{306,9},{307,9},{308,9},{290,9},{355,9},
    {291,20},{292,4},{309,4},{310,4},{314,4},{293,5},{311,5},{312,5},{294,23},{295,12},{296,22},{297,22},{298,21},{299,8}};
struct Skill {uint8_t bit;uint16_t base;unsigned group;};
constexpr Skill skills[]={{0x04,0x433,0},{0x08,0x40F,0},{0x10,0x418,0},{0x20,0x421,0},{0x40,0x42A,0},{0x01,0x43C,1},{0x02,0x406,2}};

std::span<const uint8_t> rom;
unsigned u8(uint32_t at){return at<rom.size()?rom[at]:0;}
int s8(uint32_t at){return int8_t(u8(at));}
unsigned u16(uint32_t at){return u8(at)<<8|u8(at+1);}
int s16(uint32_t at){return int16_t(u16(at));}
uint32_t u32(uint32_t at){return uint32_t(u16(at))<<16|u16(at+2);}
std::string bytes(uint32_t at,uint32_t size){return at+size<=rom.size()?std::string(reinterpret_cast<const char*>(rom.data()+at),size):std::string();}
std::string text(uint16_t id){return dialogue::ui_text(srw64_rdram,id);}
// 800A6104(1, rank): 1 'D', 2 'C', 3 'B', 4 'A', else '-'.
std::string terrain(uint32_t at) {
    std::string letters;
    for(unsigned n=0;n<4;++n){const unsigned rank=u8(at+n);letters+=rank==1?'D':rank==2?'C':rank==3?'B':rank==4?'A':'-';}
    return letters;
}
// Placeholder characters the script never shows by name ("???", "3"...).
bool placeholder(const std::string& name) {
    return name.empty() || std::all_of(name.begin(),name.end(),[](char c){return c=='?' || c==' ' || (c>='0' && c<='9');});
}
const json& battle_assets() {
    static const json art=[] {
        const char* path=std::getenv("SRW64_DIALOGUE_DATA");
        if(!path)return json::object();
        std::ifstream source(path);
        auto data=json::parse(source,nullptr,false);
        return !data.is_discarded() && data.contains("battle_assets")?data.at("battle_assets"):json::object();
    }();
    return art;
}
void add_art(json& entry,const char* group,unsigned key) {
    const auto& art=battle_assets();
    if(art.contains(group) && art.at(group).contains(std::to_string(key)))entry["art"]=art.at(group).at(std::to_string(key));
}

json weapon(unsigned number) {
    const uint32_t at=weapons_at+number*weapon_size;
    json w={{"number",number},{"name",text(uint16_t(text_weapon_menu+number))},{"power",u8(at+1)*100},{"range_min",u8(at+2)},{"range_max",u8(at+3)},
        {"hit",s8(at+4)},{"en",u8(at+6)},{"morale",u8(at+7)},{"terrain",terrain(at+9)},{"critical",s8(at+0xD)}};
    if(s8(at+5)>=0)w["ammo"]=s8(at+5);
    if(u8(at+8)>=2)w["skill_name"]=text(uint16_t(u8(at+8)));
    upgrade_page::weapon_markers(w,text(uint16_t(text_weapon_pure+number)));
    return w;
}
// The entries of the unit's list that name it among their forms; the list is shared
// by every form of a machine.
std::vector<unsigned> weapon_numbers(unsigned unit) {
    std::vector<unsigned> numbers;
    for(uint32_t at=weapon_lists+u32(weapon_lists+unit*4);at+weapon_entry<=rom.size();at+=weapon_entry) {
        const unsigned number=u16(at);
        if(number==0xFFFF)break;
        bool own=false;
        for(unsigned n=1;n<6;++n)own|=u16(at+n*2)==unit;
        if(own && number<weapon_count)numbers.push_back(number);
    }
    return numbers;
}
json unit(unsigned id,const std::vector<unsigned>& numbers) {
    const uint32_t at=units_at+id*unit_size;
    const unsigned size_bits=u8(at+4),move_bits=u8(at+5),equipment=u8(at+0x18);
    const unsigned size=size_bits&1?0:size_bits&2?1:size_bits&4?2:size_bits&8?3:4;
    json types=json::array();
    for(unsigned bit=0;bit<4;++bit)if(move_bits&(1u<<bit))types.push_back(text(uint16_t(u16(move_icons+bit*2))));
    if(move_bits&0x10)types.push_back(text(uint16_t(u16(move_icons+8))));
    // HP回復 is one text for both rates; 801FA544 restores 10 % for 4 and 20 % for 8.
    json abilities=json::array();
    const uint32_t flags=u32(at+0x1C);
    for(unsigned n=0;n<ability_entries;++n) {
        const uint32_t mask=u32(ability_table+n*8);
        if(!(flags&mask))continue;
        auto name=text(uint16_t(u16(ability_table+n*8+4)));
        if(mask==4)name+=" 10%";
        else if(mask==8)name+=" 20%";
        abilities.push_back(name);
    }
    json weapons=json::array();
    for(const unsigned number:numbers)weapons.push_back(weapon(number));
    json u={{"id",id},{"name",text(uint16_t(text_unit_names+id))},{"hp",u16(at)},{"en",u16(at+2)},{"move",u8(at+6)},{"mobility",u16(at+8)},{"armor",u16(at+0xA)},
        {"limit",u16(at+0xC)},{"repair",u16(at+0x14)},{"size",text(uint16_t(text_sizes+size))},{"types",types},{"terrain",terrain(at+0xE)},
        {"abilities",abilities},{"weapons",weapons}};
    if(equipment&2)u["shield"]=text(text_shield_yes);
    add_art(u,"units",id);
    return u;
}
json pilot(unsigned actor,int stats,int spirits) {
    const bool person=actor>=first_person && actor<first_person+people;
    json p={{"actor",actor},{"name",text(uint16_t(person?text_people+actor-first_person:text_pilot_names+actor))},
        {"full_name",text(uint16_t(person?text_people_full+actor-first_person:text_pilot_full+actor))}};
    add_art(p,"portraits",actor);
    if(stats>=0) {
        const uint32_t at=pilot_stats_at+uint32_t(stats)*pilot_stats_size;
        p["stats"]={{"melee",u8(at+1)},{"ranged",u8(at+2)},{"evade",u8(at+3)},{"hit",u8(at+4)},{"reaction",u8(at+5)},{"skill",u8(at+6)},{"sp",u8(at+0xC)}};
        p["terrain"]=terrain(at+7);
        if(u8(at+0xE))p["double_move"]=u8(at+0xE);
        // Level L of a skill comes with the L-th smallest of its group's nonzero thresholds
        // (800A80F0 counts those at or below the pilot's level). Actor 284's index 256
        // reads past the table into the spirit records, as the game does.
        json learned=json::array();
        const unsigned bits=u8(at+0xF);
        bool first_group=false;
        for(const auto& skill:skills) {
            if(!(bits&skill.bit) || (skill.group==0 && first_group))continue;
            if(skill.group==0)first_group=true;
            std::vector<unsigned> levels;
            for(unsigned n=0;n<9;++n)if(const unsigned level=u8(thresholds_at+uint32_t(stats)*thresholds_size+skill.group*10+n))levels.push_back(level);
            std::sort(levels.begin(),levels.end());
            json steps=json::array();
            // Levels reached at the same pilot level show only the highest of them.
            for(unsigned n=0;n<levels.size();++n)
                if(n+1==levels.size() || levels[n+1]!=levels[n])steps.push_back({{"name",text(uint16_t(skill.base+n+1))},{"level",levels[n]}});
            if(!steps.empty())learned.push_back({{"levels",steps}});
        }
        p["skills"]=learned;
    }
    if(spirits>=0) {
        json list=json::array();
        for(unsigned n=0;n<6;++n) {
            const uint32_t at=spirits_at+uint32_t(spirits)*spirits_size+n*2;
            if(const unsigned level=u8(at);level && level<100 && u8(at+1)<30)list.push_back({{"name",text(uint16_t(text_spirits+u8(at+1)))},{"level",level},{"cost",u8(spirit_costs+u8(at+1))}});
        }
        p["spirits"]=list;
    }
    return p;
}
// Each entry's work, model and place in the original list: the list itself, else its
// alias's main record, else a listed record of the same name, else the table above.
struct Listed {int work=-1;int model=-1;unsigned place=~0u;};
void assign_works(json& list,const char* id_key,uint32_t table,unsigned rows,bool models,uint32_t aliases,unsigned alias_count,
                  std::span<const std::pair<uint16_t,int8_t>> fixed) {
    std::map<unsigned,Listed> listed;
    for(unsigned n=0;n<rows;++n) {
        const uint32_t at=table+n*6;
        listed.emplace(u16(at),Listed{s16(at+2),models?s16(at+4):-1,n});
    }
    std::map<unsigned,unsigned> alias;
    for(unsigned n=0;n<alias_count;++n)alias[u16(aliases+n*4)]=u16(aliases+n*4+2);   // the last pair wins, as in the game
    std::map<std::string,Listed> by_name;
    for(const auto& e:list)if(const auto found=listed.find(e.at(id_key).get<unsigned>());found!=listed.end())by_name.emplace(e.at("name").get<std::string>(),found->second);
    for(auto& e:list) {
        const unsigned id=e.at(id_key).get<unsigned>();
        Listed work;
        if(const auto found=listed.find(id);found!=listed.end())work=found->second;
        else if(const auto main=alias.find(id);main!=alias.end() && listed.contains(main->second))work=listed.at(main->second);
        else if(const auto same=by_name.find(e.at("name").get<std::string>());same!=by_name.end())work=same->second;
        for(const auto& [fixed_id,fixed_work]:fixed)if(fixed_id==id)work.work=fixed_work;
        if(work.work<0 || work.work>=int(work_count))work.work=-1;
        e["work"]=work.work;e["place"]=work.place;
        if(work.work>=0)e["work_name"]=text(uint16_t(text_works+work.work));
        if(work.model>=0)e["model"]=text(uint16_t(text_models+work.model));
    }
    // Works in the order the original list first shows them, その他 last; within a work the
    // list's order, then the ROM's.
    std::map<int,unsigned> rank;
    for(unsigned n=0;n<rows;++n)rank.emplace(s16(table+n*6+2),n);
    std::vector<std::pair<std::pair<unsigned,unsigned>,json>> keyed;
    for(size_t n=0;n<list.size();++n) {
        const int work=list[n].at("work").get<int>();
        unsigned work_rank=work<0?~0u:rank.contains(work)?rank.at(work):rows;
        keyed.push_back({{work_rank,list[n].at("place").get<unsigned>()},std::move(list[n])});
        keyed.back().second.erase("place");
    }
    std::stable_sort(keyed.begin(),keyed.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    list=json::array();
    for(auto& [key,e]:keyed)list.push_back(std::move(e));
}
// Later records equal to an earlier one of the same name (a unit's stats and weapons, a
// pilot's records) are dropped; a name still shared within a work is numbered from its
// second entry (two works' 大作 are two people).
void number_repeats(json& list) {
    std::map<std::string,unsigned> seen,total;
    const auto key=[](const json& e){return std::to_string(e.at("work").get<int>())+'\0'+e.at("name").get<std::string>();};
    for(const auto& e:list)++total[key(e)];
    for(auto& e:list)
        if(total[key(e)]>1)if(const unsigned n=++seen[key(e)];n>1)e["name"]=e.at("name").get<std::string>()+" ("+std::to_string(n)+")";
}
json build() {
    rom=recomp::get_rom();
    json units=json::array(),pilots=json::array();
    std::map<std::string,bool> kept;
    for(unsigned id=0;id<unit_count;++id) {
        const auto numbers=weapon_numbers(id);
        const auto name=text(uint16_t(text_unit_names+id));
        if(placeholder(name))continue;
        std::string key=name+'\0'+bytes(units_at+id*unit_size,unit_size);
        for(const unsigned n:numbers)key+=std::to_string(n)+",";
        if(kept.emplace(key,true).second)units.push_back(unit(id,numbers));
    }
    kept.clear();
    for(unsigned actor=0;actor<actor_count;++actor) {
        const int stats=actor<mapped_actors?s16(stats_map+actor*2):-1,spirits=actor<mapped_actors?s16(spirits_map+actor*2):-1;
        auto p=pilot(actor,stats,spirits);
        if(placeholder(p.at("name").get<std::string>()))continue;
        std::string key=p.at("name").get<std::string>()+'\0'+p.at("full_name").get<std::string>();
        if(stats>=0)key+=bytes(pilot_stats_at+uint32_t(stats)*pilot_stats_size,pilot_stats_size)+bytes(thresholds_at+uint32_t(stats)*thresholds_size,thresholds_size);
        if(spirits>=0)key+=bytes(spirits_at+uint32_t(spirits)*spirits_size,spirits_size);
        if(kept.emplace(key,true).second)pilots.push_back(std::move(p));
    }
    // Grouped first, so a repeated name is numbered in the order the page shows it.
    assign_works(units,"id",robot_list,robot_rows,true,unit_aliases,unit_alias_count,unit_works);
    assign_works(pilots,"actor",character_list,character_rows,false,actor_aliases,actor_alias_count,actor_works);
    number_repeats(units);number_repeats(pilots);
    json labels;
    const std::pair<const char*,uint16_t> keys[]={{"hp",0xFE7},{"en",0xFE8},{"mobility",0xFE9},{"armor",0xFEA},{"limit",0xFEB},{"size",0x1003},
        {"repair",0x1004},{"abilities",0x1005},{"type",0x1006},{"move",0x1007},{"terrain",0xFF6},{"air",0xFF7},{"land",0xFF8},{"sea",0xFF9},{"space",0xFFA},
        {"sp",0x100B},{"melee",0x100C},{"evade",0x100D},{"reaction",0x100E},{"ranged",0x1023},{"hit",0xFF4},{"skill",0x100F},{"spirits",0x1010},{"skills",0x1011},
        {"level",0xFE1}};
    for(const auto& [key,id]:keys)labels[key]=text(id);
    return {{"units",units},{"pilots",pilots},{"labels",labels},{"weapon_labels",upgrade_page::weapon_labels_json(srw64_rdram)}};
}
}

const json& contents() {
    static json cached;
    static std::string locale;
    if(locale!=localization::catalog().locale || cached.is_null()){cached=build();locale=localization::catalog().locale;}
    return cached;
}
}
