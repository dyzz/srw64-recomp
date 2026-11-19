#pragma once
// A new character's own battle quotes (docs/design/unit-mod-kit.md §7): dialogue/
// battle_quotes.json of a package gives a character it adds (or another package's) lines for
// the nine generic situations and conditional ones (a weapon, an opponent). The character
// then speaks with a voice part of its own (table_window::voice_of; mod_pilots.hpp puts it
// in the quote map), whose rows the battle overlay reads through the table window like the
// ROM's (docs/data/battle-quotes.md):
//   generic segments  nine (start, count) pairs, record 5813 + start + a random n < count;
//   conditional list  an offset from ROM 0x20F250 (here into the window's quote_lists block),
//                     (condition code, record - 14227) pairs until code 0xFFFF; the game reads
//                     0x1E0 bytes of it.
// The lines are text records of their own (text_first up), served like a story MOD's lines
// (story_mod.hpp text()) with a battle quote's header, and shown by the native reader. A
// situation the file leaves out keeps the lines of the character it borrows its voice from.
#include "json/json.hpp"
#include "mod_units.hpp"
#include "table_window.hpp"
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace srw64::mod_quotes {
using json=nlohmann::json;
namespace tw=table_window;

inline constexpr const char* schema="srw64.battle-quotes.v1";
// Text records: past the story MODs' (0xC800-0xDFFF), before the new numbers' names (0xE800).
inline constexpr uint16_t text_first=0xE000, text_end=tw::private_first;
inline constexpr int generic_first=5813, special_first=14227;
inline constexpr unsigned list_pairs=119;           // and the end mark: 0x1E0 bytes
inline constexpr size_t longest_ja=42;               // code points: one page, three rows of 14
inline constexpr size_t longest_line=200;
// D_800CA9C4, the character -> voice part map, in the resident section of the ROM.
inline constexpr uint32_t voice_map_rom=0x800CA9C4-0x80076610+0x1000;
inline constexpr uint32_t segment_rom=0x1161C0;

// The nine generic situations, in the segment table's order, and each one's base for the
// conditional codes 10000 + 2500 s + k.
struct Situation { const char* key; unsigned base; };
inline constexpr Situation situations[9]={
    {"attack",20000},{"shot_down",10000},{"heavy",12500},{"medium",15000},{"light",17500},
    {"evade",25000},{"no_damage",22500},{"no_ammo",30000},{"out_of_range",32500}};

struct Line {
    unsigned speaker{};
    std::map<std::string,std::string> lines;   // locale -> text
    std::string package, voice;                // voice: a file in audio/voice/battle/, or none
};
// What the packages' files give, bottom first: the lines (text_first + index) and each
// character's own voice part.
struct Taken {
    std::vector<Line> lines;
    std::map<unsigned,unsigned> voices;        // character -> its voice part
};

inline size_t code_points(const std::string& s) {
    size_t n=0;
    for(const auto c:s)if((uint8_t(c)&0xC0)!=0x80)++n;
    return n;
}
inline int16_t s16(std::span<const uint8_t> rom,uint32_t at){return at+1<rom.size()?int16_t(rom[at]<<8|rom[at+1]):-1;}
// The voice part an original character speaks with (-1: none).
inline int rom_voice(std::span<const uint8_t> rom,unsigned pilot){return pilot<tw::rom_pilot_maps?s16(rom,voice_map_rom+pilot*2):-1;}

// One file of a package. Throws nothing: returns what is wrong (nothing is added then), or "".
// `borrows` is each new character's voice owner (new_units.json "voice", else "copy").
inline std::string parse(const json& document,const std::string& package,const mod_units::Numbers& numbers,
                         std::span<const uint8_t> rom,mod_units::Bytes& bytes,
                         const std::map<unsigned,unsigned>& borrows,Taken& taken) {
    if(!document.is_object() || document.value("schema","")!=schema)return std::string("schema 要写 ")+schema;
    for(const auto& [key,v]:document.items())if(key!="schema" && key!="pilots")return "未知字段 "+key+"（schema、pilots）";
    if(!document.contains("pilots") || !document["pilots"].is_object())return "pilots 要是对象";
    Taken out=taken;
    mod_units::Bytes written;
    const auto add_line=[&](const json& l,const std::string& where,unsigned speaker,int& id)->std::string {
        if(!l.is_object())return where+"：要是对象（各语言的台词，可加 voice）";
        Line line;line.speaker=speaker;line.package=package;
        for(const auto& [key,v]:l.items()) {
            if(!v.is_string())return where+"."+key+"：要是字符串";
            const auto s=v.get<std::string>();
            if(key=="voice") {
                if(s.size()<5 || s.substr(s.size()-4)!=".ogg" || s.find_first_of("/\\")!=std::string::npos || s.find("..")!=std::string::npos)
                    return where+".voice：要写 audio/voice/battle/ 里的 .ogg 文件名";
                line.voice=s;continue;
            }
            if(s.empty() || code_points(s)>longest_line)return where+"."+key+"：台词为空或太长";
            line.lines[key]=s;
        }
        const auto ja=line.lines.find("ja");
        if(ja==line.lines.end())return where+"：每句都要有 ja";
        if(code_points(ja->second)>longest_ja)return where+".ja：战斗台词最多 "+std::to_string(longest_ja)+" 字（一页三行）";
        if(text_first+out.lines.size()>=text_end)return where+"：战斗台词太多（所有包合计 "+std::to_string(text_end-text_first)+" 句）";
        id=int(text_first+out.lines.size());
        out.lines.push_back(std::move(line));
        return {};
    };
    const auto put16=[&](uint32_t at,unsigned v){written[at]=uint8_t(v>>8);written[at+1]=uint8_t(v);};
    for(const auto& [who,entry]:document["pilots"].items()) {
        const std::string where="pilots."+who;
        const auto pilot=mod_units::resolve(json(who),package,numbers,mod_units::Of::pilot);
        if(!pilot || !tw::new_pilot(*pilot))return where+"：要写包里的新人物（@名字）";
        if(!entry.is_object())return where+"：要是对象";
        const auto borrowed=borrows.find(*pilot);
        const int from=borrowed==borrows.end()?-1:rom_voice(rom,borrowed->second);
        const unsigned part=tw::voice_of(*pilot);
        const uint32_t segments=tw::row(tw::quote_segments,part);
        // The situations it leaves out: the borrowed part's segments.
        for(unsigned s=0;s<9;++s)
            for(unsigned i=0;i<4;++i)written[segments+s*4+i]=from<0?0:rom[segment_rom+uint32_t(from)*36+s*4+i];
        std::vector<std::pair<unsigned,int>> pairs;   // (code, text id)
        for(const auto& [key,value]:entry.items()) {
            unsigned s=0;
            while(s<9 && key!=situations[s].key)++s;
            if(s<9) {
                if(!value.is_array() || value.empty() || value.size()>255)return where+"."+key+"：要是 1–255 句的数组";
                int first=-1;
                for(size_t i=0;i<value.size();++i) {
                    int id=-1;
                    if(auto why=add_line(value[i],where+"."+key+"["+std::to_string(i)+"]",*pilot,id);!why.empty())return why;
                    if(first<0)first=id;
                }
                put16(segments+s*4,unsigned(first-generic_first)&0xFFFF);put16(segments+s*4+2,unsigned(value.size()));
                continue;
            }
            if(key!="special")return where+"：未知字段 "+key+"（attack、shot_down、heavy、medium、light、evade、no_damage、no_ammo、out_of_range、special）";
            if(!value.is_array())return where+".special：要是数组";
            for(size_t n=0;n<value.size();++n) {
                const auto& c=value[n];
                const std::string at=where+".special["+std::to_string(n)+"]";
                if(!c.is_object())return at+"：要是对象";
                for(const auto& [k,v]:c.items())
                    if(k!="weapon" && k!="opponent" && k!="situation" && k!="sometimes" && k!="lines")
                        return at+"：未知字段 "+k+"（weapon、opponent、situation、sometimes、lines）";
                unsigned code=0;
                if(c.contains("weapon")==c.contains("opponent"))return at+"：weapon 和 opponent 要写一个";
                if(c.contains("weapon")) {
                    if(c.contains("situation"))return at+".situation：武器台词总是攻击时";
                    const auto weapon=mod_units::resolve(c["weapon"],package,numbers,mod_units::Of::weapon);
                    if(!weapon)return at+".weapon：没有这件武器";
                    // The game compares field 0 of the weapon's battle record (same-named weapons
                    // share it; a new weapon has the one of the weapon it copies).
                    const uint32_t record=tw::row(tw::battle_weapons,*weapon);
                    const int field=int16_t(mod_units::read_byte(bytes,rom,record)<<8|mod_units::read_byte(bytes,rom,record+1));
                    if(c.contains("sometimes") && !c["sometimes"].is_boolean())return at+".sometimes：要是 true 或 false";
                    const bool sometimes=c.value("sometimes",false);
                    if(field<0 || field>=(sometimes?1700:2400))return at+".weapon：这件武器的台词编号超出范围";
                    code=unsigned((sometimes?900:2600)+field);
                } else {
                    if(c.contains("sometimes"))return at+".sometimes：只有武器台词能写";
                    const auto opponent=mod_units::resolve(c["opponent"],package,numbers,mod_units::Of::pilot);
                    if(!opponent || *opponent>=tw::rom_pilot_maps)return at+".opponent：要写原版人物（0–359）";
                    const int v=rom_voice(rom,*opponent);
                    if(v<0 || v>=300)return at+".opponent：这个人物没有台词声部";
                    if(c.contains("situation") && !c["situation"].is_string())return at+".situation：要写九种情境之一";
                    const std::string name=c.value("situation","attack");
                    unsigned s=0;
                    while(s<9 && name!=situations[s].key)++s;
                    if(s==9)return at+".situation：要写九种情境之一";
                    code=situations[s].base+400+unsigned(v);
                }
                if(!c.contains("lines") || !c["lines"].is_array() || c["lines"].empty())return at+".lines：要是非空数组";
                for(size_t i=0;i<c["lines"].size();++i) {
                    int id=-1;
                    if(auto why=add_line(c["lines"][i],at+".lines["+std::to_string(i)+"]",*pilot,id);!why.empty())return why;
                    pairs.emplace_back(code,id);
                }
            }
        }
        if(pairs.size()>list_pairs)return where+".special：条件台词最多 "+std::to_string(list_pairs)+" 句";
        const uint32_t list=tw::base(tw::quote_lists)+(part-tw::first_new_voice)*tw::quote_list;
        const uint32_t offset=list-tw::quote_lists_rom, index=tw::row(tw::quote_index,part);
        for(unsigned i=0;i<4;++i)written[index+i]=uint8_t(offset>>(8*(3-i)));
        for(unsigned i=0;i<tw::quote_list;++i)written[list+i]=0;
        for(size_t i=0;i<pairs.size();++i){put16(list+uint32_t(i)*4,pairs[i].first);put16(list+uint32_t(i)*4+2,unsigned(pairs[i].second-special_first));}
        put16(list+uint32_t(pairs.size())*4,0xFFFF);put16(list+uint32_t(pairs.size())*4+2,0xFFFF);
        out.voices[*pilot]=part;
    }
    for(const auto& [at,v]:written)bytes[at]=v;
    taken=std::move(out);
    return {};
}

// --- What the host serves ---------------------------------------------------------------

struct State {
    std::mutex mutex;
    std::shared_ptr<const std::vector<Line>> lines=std::make_shared<const std::vector<Line>>();
    std::atomic<unsigned> revision{0};
};
inline State& state(){static State value;return value;}
// mod_data.hpp, when the packages change.
inline void publish(std::vector<Line> lines) {
    auto& s=state();
    {std::lock_guard lock(s.mutex);s.lines=std::make_shared<const std::vector<Line>>(std::move(lines));}
    ++s.revision;
}
inline unsigned revision(){return state().revision.load();}
inline std::shared_ptr<const std::vector<Line>> lines() {
    auto& s=state();
    std::lock_guard lock(s.mutex);return s.lines;
}
inline bool ours(uint16_t id){return id>=text_first && id<text_end;}
// A package line by its text record. Any thread.
inline std::optional<Line> line(uint16_t id) {
    if(!ours(id))return std::nullopt;
    const auto all=lines();
    if(size_t(id-text_first)>=all->size())return std::nullopt;
    return (*all)[id-text_first];
}
}
