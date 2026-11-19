#pragma once
#include "json/json.hpp"
#include "mod_sprites.hpp"
#include "table_window.hpp"
#include <filesystem>
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <map>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

// New units and weapons (docs/design/new-unit-ids.md): data/new_units.json of a package
// names its new units, weapons and battle scenes; the host gives them numbers of their own,
// past the ROM's (table_window.hpp), and lays their rows out in the table window: the
// records copied from the unit or weapon each copies, its weapon list, its animation, its
// name records. Upgrade inheritance still borrows rows of the resident tables no script
// reaches.
//
//   { "schema": "srw64.new-units.v2",
//     "sprites": { "stand": { "image": "art/battle/stand.png", "origin": [59, 90], "hd": "art/battle/stand-hd.png" } },
//     "scenes":  { "stance": [2324, 1724, 2016], "pose": "@stand" },
//     "weapons": { "cannon": { "copy": 875, "inherits_from": 875, "animation": { … } } },
//     "units":   { "final": { "copy": 224, "pose": [2324, 1724, 2016], "icon": 978,
//                             "weapons": [870, 871, "@cannon"], "inherits_from": 224 } } }
//
// Elsewhere a package names them "@final" (its own) or "other.package@final".
namespace srw64::mod_units {
using json=nlohmann::json;
using Bytes=std::map<uint32_t,uint8_t>;
namespace tw=table_window;

inline constexpr const char* schema="srw64.new-units.v2";
inline constexpr uint32_t resident(uint32_t ram){return ram-0x80076610+0x1000;}
inline constexpr uint32_t unit_record_size=0x24, weapon_record_size=0x10, battle_row=14, list_row=12;
inline constexpr unsigned battle_units_with_rows=354;   // the units whose battle record is their own
inline constexpr uint32_t animation_window=0x3C0;        // an animation record is read as 0x3C0 bytes
// Upgrade inheritance (docs/gameplay/upgrade-inheritance.md): the predecessor table, (new, old)
// u16 pairs, 30 read by 800AA814; row 7 (128 <- 127) is one no script reaches, which the host
// fills for a package's new unit just before it answers the lookup with that row.
inline constexpr uint32_t predecessor_table=0x800CA3A0, predecessor_rows=30, borrowed_predecessor_row=7;
// Text table 0 (8008C510): u32 count, then (offset, size) per record from the table's start.
inline constexpr uint32_t text_table=0x01A34980;

// --- Numbers -----------------------------------------------------------------------------

// A name in new_units.json: lower-case letters, digits, '-', '_' and '.'.
inline bool valid_name(const std::string& name) {
    return !name.empty() && name.size()<=40 && std::all_of(name.begin(),name.end(),[](char c){
        return (c>='a' && c<='z') || (c>='0' && c<='9') || c=='-' || c=='_' || c=='.';});
}
inline uint32_t fnv1a(const std::string& text) {
    uint32_t h=2166136261u;
    for(const unsigned char c:text){h^=c;h*=16777619u;}
    return h;
}
// "package@name" -> number, for the units and the weapons of every active package.
struct Numbers {
    std::map<std::string,unsigned> units,weapons,pilots;
    std::map<unsigned,std::string> unit_keys,weapon_keys,pilot_keys;   // the reverse
};
// Each name's number comes from its key's hash, the next free one on a clash, the keys taken
// in the order of their hashes: the same packages give the same numbers on any machine, in
// any order, and a package added later moves another's only when they clash.
inline void place(const std::vector<std::string>& keys,unsigned first,unsigned last,
                  std::map<std::string,unsigned>& out,std::map<unsigned,std::string>& reverse) {
    std::vector<std::pair<uint32_t,std::string>> order;
    for(const auto& k:keys)order.emplace_back(fnv1a(k),k);
    std::sort(order.begin(),order.end());
    const unsigned span=last-first+1;
    for(const auto& [h,k]:order) {
        if(out.count(k))continue;
        for(unsigned step=0;step<span;++step) {
            const unsigned n=first+(h+step)%span;
            if(reverse.count(n))continue;
            out[k]=n;reverse[n]=k;break;
        }
    }
}
// The packages' new_units.json documents (package id, document), bottom first.
inline Numbers assign(const std::vector<std::pair<std::string,json>>& documents) {
    std::vector<std::string> units,weapons,pilots;
    for(const auto& [id,document]:documents) {
        if(!document.is_object())continue;
        for(const auto& [part,list]:{std::pair<const char*,std::vector<std::string>*>{"units",&units},{"weapons",&weapons},{"pilots",&pilots}}) {
            const auto found=document.find(part);
            if(found==document.end() || !found->is_object())continue;
            for(const auto& [name,_]:found->items())if(valid_name(name))list->push_back(id+"@"+name);
        }
    }
    Numbers n;
    place(units,tw::first_new_unit,tw::last_new_unit,n.units,n.unit_keys);
    place(weapons,tw::first_new_weapon,tw::last_new_weapon,n.weapons,n.weapon_keys);
    place(pilots,tw::first_new_pilot,tw::last_new_pilot,n.pilots,n.pilot_keys);
    return n;
}
// A unit or weapon as a package writes it: the ROM's number, "@name" (its own) or
// "package@name". Nothing when it names no such thing.
enum class Of { unit, weapon, pilot };
inline std::optional<unsigned> resolve(const json& value,const std::string& package,const Numbers& numbers,Of of) {
    const unsigned rom=of==Of::unit?tw::rom_units:of==Of::weapon?tw::rom_weapons:tw::rom_pilots;
    if(value.is_number_integer()) {
        const auto v=value.get<int64_t>();
        return v>=0 && v<int64_t(rom)?std::optional<unsigned>(unsigned(v)):std::nullopt;
    }
    if(!value.is_string())return std::nullopt;
    const auto text=value.get<std::string>();
    const auto at=text.find('@');
    if(at==std::string::npos) {   // a number written as a string, as JSON keys are
        if(text.empty() || text.size()>4 || !std::all_of(text.begin(),text.end(),[](char c){return c>='0' && c<='9';}) || (text.size()>1 && text[0]=='0'))return std::nullopt;
        const unsigned v=unsigned(std::stoul(text));
        return v<rom?std::optional<unsigned>(v):std::nullopt;
    }
    const std::string key=(at==0?package:text.substr(0,at))+"@"+text.substr(at+1);
    const auto& table=of==Of::unit?numbers.units:of==Of::weapon?numbers.weapons:numbers.pilots;
    const auto found=table.find(key);
    return found==table.end()?std::nullopt:std::optional<unsigned>(found->second);
}

// --- The bytes ---------------------------------------------------------------------------

// A byte as the game would read it with `over` (these packages' bytes) on the ROM: the table
// window reads its rows (table_window.hpp).
inline uint8_t read_byte(const Bytes& over,std::span<const uint8_t> rom,uint32_t at) {
    const auto plain=[&](uint32_t a)->uint8_t{
        if(const auto f=over.find(a);f!=over.end())return f->second;
        return a<rom.size()?rom[a]:0;
    };
    if(const auto f=over.find(at);f!=over.end())return f->second;
    if(tw::inside(at))return tw::window_byte(at,plain,[](uint16_t,uint32_t&,uint32_t&){return false;});
    return plain(at);
}

// A package's sprite (mod_sprites.hpp) as the game draws it: its three resource numbers, and
// the files the host draws in its place (HD) or shows on its own pages.
struct Sprite {
    std::string key;                       // "package@name"
    uint16_t scene=0, atlas=0, palette=0;
    unsigned frames=0, width=0, height=0;  // the first frame's size
    std::filesystem::path picture;         // the first frame's PNG
    std::vector<std::filesystem::path> hd; // per frame, empty when none
};
// A sprite as the host's pages show a unit's pose (battle_assets.py's entries): the first
// frame's PNG at its own size, and the HD one when there is.
inline json art(const Sprite& s) {
    json a={{"path",s.picture.string()},{"width",s.width},{"height",s.height},{"facing","left"},{"frame",0}};
    if(!s.hd.empty())a["hd"]=s.hd.front().string();
    return a;
}
// What the packages so far have taken, so a later package or file does not reuse it.
struct Taken {
    unsigned resource_next=tw::rom_resources;
    std::map<unsigned,std::vector<uint8_t>> resources;   // resource number -> its decoded bytes
    std::map<std::string,Sprite> sprites;                // "package@name" -> the sprite
    uint32_t list_next=tw::base(tw::weapon_lists)+tw::lists_copy+(tw::lists_rom_end-tw::layouts[tw::weapon_lists].rom);
    uint32_t animation_next=tw::base(tw::anim_records);
    unsigned scene_next=tw::rom_scenes;
    std::map<unsigned,unsigned> predecessors;                          // a new unit -> the unit it inherits from
    std::map<unsigned,std::vector<std::pair<unsigned,unsigned>>> carries;   // a new unit -> its weapons' (new, old)
    std::map<unsigned,std::vector<unsigned>> crews;                    // a new unit -> its pilots, the main one first
    // A new unit combined from parts (mod_combine.hpp): its parts in order, and the ROM's
    // combined units whose map 合体 it takes over when its other parts join in.
    struct Combine { std::vector<unsigned> parts,over; };
    std::map<unsigned,Combine> combines;
    std::map<unsigned,int> works;            // a new unit -> its work in the Library (text 60 + it)
    // Form families (mod_forms.hpp): the base form -> every form of the family, the base first.
    std::map<unsigned,std::vector<unsigned>> forms;
    // New characters (docs/design/unit-mod-kit.md §5.4): the character each copies (its records,
    // portrait, song, names) and the one whose battle quotes it speaks.
    std::map<unsigned,unsigned> pilot_copies,voices;
    // A new character's own portrait: its two resources (CI8 96x96 image, 64-colour palette),
    // the package's pictures, and the FNV-1a 64 digests of the index and palette data the
    // draw points at, by which the HD picture replaces it (native_portrait.cpp).
    struct Portrait { unsigned image=0, palette=0; std::filesystem::path pixels, hd; uint64_t pixel_digest=0, palette_digest=0; };
    std::map<unsigned,Portrait> portraits;
    // Love / friendship rows a package adds (holder, partner), and the original character a new
    // one counts as in combination attacks' pilot checks (mod_pilots.hpp).
    std::vector<std::pair<unsigned,unsigned>> loves;
    std::map<unsigned,unsigned> combo_as;
    std::map<std::string,unsigned> scenes;   // "package@name" -> battle scene registry number
    std::map<unsigned,unsigned> copies;      // a new unit -> the unit it copies
    std::map<unsigned,unsigned> weapon_copies;
    std::map<uint16_t,std::pair<uint32_t,uint32_t>> names;   // private text id -> source record (ROM address, size)
};

template<class T,size_t N> bool listed(const T (&list)[N],unsigned value) {
    for(const auto x:list)if(x==value)return true;
    return false;
}
inline bool integer(const json& v,int64_t low,int64_t high) {
    return v.is_number_integer() && v.get<int64_t>()>=low && v.get<int64_t>()<=high;
}

// A sprite's file, inside the package's folder.
inline std::optional<std::filesystem::path> inside(const std::filesystem::path& folder,const json& value) {
    if(!value.is_string())return std::nullopt;
    const std::filesystem::path relative=value.get<std::string>();
    if(relative.empty() || relative.is_absolute())return std::nullopt;
    for(const auto& piece:relative)if(piece==".." || piece==".")return std::nullopt;
    return folder/relative;
}
// The "sprites" of one package: PNGs into resources (mod_sprites.hpp build).
inline std::string sprites(const json& part,const std::string& package,const std::filesystem::path& folder,Taken& t) {
    if(!part.is_object())return "sprites 要是对象";
    for(const auto& [name,entry]:part.items()) {
        const std::string where="sprites."+name;
        if(!valid_name(name))return where+"：名字只能用小写字母、数字和 - _ .";
        if(!entry.is_object())return where+"：要是对象";
        for(const auto& [field,_]:entry.items())
            if(field!="image" && field!="frames" && field!="origin" && field!="mirror" && field!="ticks" && field!="loop" && field!="hd")
                return where+"：未知字段 "+field+"（image、frames、origin、mirror、ticks、loop、hd）";
        json files=entry.contains("frames")?entry["frames"]:json::array({entry.value("image",json())});
        if(entry.contains("frames") && entry.contains("image"))return where+"：image 和 frames 只写一个";
        if(!files.is_array() || files.empty() || files.size()>64)return where+"：image 要是一张 PNG，frames 要是 1–64 张";
        std::vector<mod_sprites::Frame> frames;
        Sprite sprite;sprite.key=package+"@"+name;
        for(const auto& f:files) {
            const auto file=inside(folder,f);
            if(!file)return where+"：图片要写包里的相对路径";
            std::string why;
            auto frame=mod_sprites::load(*file,why);
            if(!why.empty())return where+"："+why;
            if(sprite.picture.empty()){sprite.picture=*file;sprite.width=frame.width;sprite.height=frame.height;}
            frames.push_back(std::move(frame));
        }
        const auto& origin=entry.value("origin",json());
        if(!origin.is_array() || origin.size()!=2 || !integer(origin[0],-2048,2048) || !integer(origin[1],-2048,2048))
            return where+".origin：要写 [x, y]：图里哪个像素是场景原点（站姿是脚下中间）";
        const bool mirror=entry.value("mirror",true);
        std::vector<mod_sprites::Step> steps;
        const json ticks=entry.value("ticks",json::array());
        if(!ticks.is_array() || (!ticks.empty() && ticks.size()!=frames.size()))return where+".ticks：要每张图一个拍数";
        for(size_t k=0;k<frames.size();++k) {
            const json tick=ticks.empty()?json(1):ticks[k];
            if(!integer(tick,1,255))return where+".ticks：拍数要是 1–255";
            steps.push_back({unsigned(k),tick.get<unsigned>()});
        }
        const json loop=entry.value("loop",json(0));
        if(!integer(loop,0,int64_t(steps.size())-1))return where+".loop：要是 0 到图数减 1";
        mod_sprites::Built built;
        if(auto why=mod_sprites::build(frames,origin[0].get<int>(),origin[1].get<int>(),mirror,steps,loop.get<unsigned>(),built);!why.empty())
            return where+"："+why;
        if(t.resource_next+3>tw::resource_limit)return where+"：资源号用完了";
        sprite.scene=uint16_t(t.resource_next);sprite.atlas=uint16_t(t.resource_next+1);sprite.palette=uint16_t(t.resource_next+2);
        sprite.frames=built.frames;
        t.resources[sprite.scene]=std::move(built.scene);t.resources[sprite.atlas]=std::move(built.atlas);t.resources[sprite.palette]=std::move(built.palette);
        t.resource_next+=3;
        if(entry.contains("hd")) {
            const json hd=entry["hd"].is_array()?entry["hd"]:json::array({entry["hd"]});
            if(hd.size()!=frames.size())return where+".hd：要每张图一张 HD 图";
            for(const auto& h:hd) {
                const auto file=inside(folder,h);
                if(!file || !std::filesystem::is_regular_file(*file))return where+".hd：找不到 "+h.dump();
                sprite.hd.push_back(*file);
            }
        }
        t.sprites[sprite.key]=std::move(sprite);
    }
    return {};
}

// One package's file into bytes, all or nothing; `below` is what earlier files and packages
// wrote; `folder` is the package's, where its sprites' PNGs are.
inline std::string parse(const json& document,const std::string& package,const Numbers& numbers,
                         std::span<const uint8_t> rom,const Bytes& below,Bytes& out,Taken& taken,
                         const std::filesystem::path& folder={}) {
    if(!document.is_object())return "顶层要是对象";
    if(document.value("schema","")!=schema)return std::string("schema 要是 ")+schema;
    for(const auto& [key,_]:document.items())
        if(key!="schema" && key!="units" && key!="weapons" && key!="scenes" && key!="sprites" && key!="pilots")
            return "未知分区 "+key+"（sprites、units、weapons、scenes、pilots）";
    Bytes bytes;
    Taken t=taken;
    Bytes view=below;   // below, then this file's bytes as they come
    const auto at=[&](uint32_t a){return read_byte(view,rom,a);};
    const auto u16=[&](uint32_t a){return unsigned(at(a))<<8|at(a+1);};
    const auto u32=[&](uint32_t a){return uint32_t(u16(a))<<16|u16(a+2);};
    const auto put8=[&](uint32_t a,unsigned v){bytes[a]=uint8_t(v);view[a]=uint8_t(v);};
    const auto put16=[&](uint32_t a,unsigned v){put8(a,v>>8);put8(a+1,v&255);};
    const auto put32=[&](uint32_t a,uint32_t v){put16(a,v>>16);put16(a+2,v&0xFFFF);};
    const auto copy=[&](uint32_t to,uint32_t from,uint32_t size){
        std::vector<uint8_t> row(size);
        for(uint32_t i=0;i<size;++i)row[i]=at(from+i);
        for(uint32_t i=0;i<size;++i)put8(to+i,row[i]);
    };
    // A text record of table 0: where it is and how long.
    const auto record=[&](uint16_t id){
        const uint32_t entry=text_table+4+uint32_t(id)*8;
        return std::pair<uint32_t,uint32_t>(text_table+u32(entry),u32(entry+4));
    };
    // The weapons a unit's list gives it (rows naming the unit among their forms).
    const auto carried=[&](unsigned unit) {
        std::vector<unsigned> list;
        const uint32_t lists=tw::base(tw::weapon_lists);
        for(uint32_t row=lists+u32(lists+unit*4),k=0;u16(row)!=0xFFFF && k<64;row+=list_row,++k)
            for(unsigned f=1;f<6;++f)if(u16(row+f*2)==unit){list.push_back(u16(row));break;}
        return list;
    };
    const auto own=[&](const std::string& name){return package+"@"+name;};
    if(document.contains("sprites"))
        if(auto why=sprites(document["sprites"],package,folder,t);!why.empty())return why;
    // A picture: [scene, atlas, palette] resource numbers, or "@sprite" (a package's sprite).
    const auto triple=[&](const json& v)->std::optional<std::array<unsigned,3>>{
        if(v.is_array() && v.size()==3 && integer(v[0],1,0xFFFF) && integer(v[1],1,0xFFFF) && integer(v[2],1,0xFFFF))
            return std::array<unsigned,3>{v[0].get<unsigned>(),v[1].get<unsigned>(),v[2].get<unsigned>()};
        if(!v.is_string())return std::nullopt;
        const auto text=v.get<std::string>();
        const auto a=text.find('@');
        if(a==std::string::npos)return std::nullopt;
        const auto found=t.sprites.find((a==0?package:text.substr(0,a))+"@"+text.substr(a+1));
        if(found==t.sprites.end())return std::nullopt;
        return std::array<unsigned,3>{found->second.scene,found->second.atlas,found->second.palette};
    };

    // Scenes first: animations name them.
    const auto& scene_part=document.value("scenes",json::object());
    if(!scene_part.is_object())return "scenes 要是对象";
    for(const auto& [name,value]:scene_part.items()) {
        const std::string where="scenes."+name;
        if(!valid_name(name))return where+"：名字只能用小写字母、数字和 - _ .";
        const auto picture=triple(value);
        if(!picture)return where+"：要是 [场景, 图集, 调色板] 三个资源号，或 @图片名（sprites 里的）";
        if(t.scene_next>=tw::new_scene_limit)return where+"：战斗场景总表的空位用完了";
        const unsigned n=t.scene_next++;
        t.scenes[own(name)]=n;
        for(unsigned k=0;k<3;++k)put16(tw::row(tw::scenes,n)+k*2,(*picture)[k]);
    }
    const auto scene_of=[&](const json& v)->std::optional<unsigned>{
        if(integer(v,0,tw::rom_scenes-1))return v.get<unsigned>();
        if(!v.is_string())return std::nullopt;
        const auto text=v.get<std::string>();
        const auto a=text.find('@');
        if(a==std::string::npos)return std::nullopt;
        const auto found=t.scenes.find((a==0?package:text.substr(0,a))+"@"+text.substr(a+1));
        return found==t.scenes.end()?std::nullopt:std::optional<unsigned>(found->second);
    };

    // Weapons.
    const auto& weapon_part=document.value("weapons",json::object());
    if(!weapon_part.is_object())return "weapons 要是对象";
    std::map<unsigned,unsigned> weapon_inherits;   // new weapon -> the old one its upgrades come from
    for(const auto& [name,entry]:weapon_part.items()) {
        const std::string where="weapons."+name;
        if(!valid_name(name))return where+"：名字只能用小写字母、数字和 - _ .";
        const auto number=numbers.weapons.find(own(name));
        if(number==numbers.weapons.end())return where+"：新武器的编号用完了";
        const unsigned n=number->second;
        if(!entry.is_object())return where+"：要是对象";
        for(const auto& [field,_]:entry.items())
            if(field!="copy" && field!="animation" && field!="inherits_from")return where+"：未知字段 "+field+"（copy、animation、inherits_from）";
        const auto source=resolve(entry.value("copy",json()),package,numbers,Of::weapon);
        if(!source || *source==n)return where+".copy：要写照抄的武器（原版武器号或 @名字）";
        t.weapon_copies[n]=*source;
        copy(tw::row(tw::weapons,n),tw::row(tw::weapons,*source),weapon_record_size);
        copy(tw::row(tw::battle_weapons,n),tw::row(tw::battle_weapons,*source),battle_row);   // [0] keeps the source's quotes
        // The name records copy the source's, which the host shows its own way (mod_names).
        for(const auto kind:{tw::NameKind::weapon,tw::NameKind::weapon_menu}) {
            const uint16_t mine=tw::name_record(kind,n),theirs=tw::name_record(kind,*source);
            if(const auto inherited=t.names.find(theirs);inherited!=t.names.end())t.names[mine]=inherited->second;
            else t.names[mine]=record(theirs);
        }
        if(entry.contains("inherits_from")) {
            const auto from=resolve(entry["inherits_from"],package,numbers,Of::weapon);
            if(!from)return where+".inherits_from：要是武器号或 @名字";
            weapon_inherits[n]=*from;
        }
        const json animation=entry.value("animation",json{{"copy",entry.value("copy",json())}});
        if(!animation.is_object())return where+".animation：要是 {\"copy\": 武器} 或整条记录";
        if(animation.contains("copy")) {
            const auto from=resolve(animation["copy"],package,numbers,Of::weapon);
            if(animation.size()!=1 || !from)return where+".animation.copy：要是武器号或 @名字，且不和别的字段同写";
            put32(tw::row(tw::anim_offsets,n),u32(tw::row(tw::anim_offsets,*from)));
            continue;
        }
        // The record (battle_graphics.py parse_animation): camera, hit, action, defender, sounds,
        // FFFF, actors of 16 bytes, FFFF; at most 8 sounds, 24 actors and the 0x3C0 window.
        static const std::set<std::string> fields{"camera","hit","action","defender","sounds","actors"};
        for(const auto& [field,_]:animation.items())if(!fields.count(field))return where+".animation：未知字段 "+field;
        std::vector<uint8_t> data;
        const auto push16=[&](int v){data.push_back(uint8_t(unsigned(v)>>8));data.push_back(uint8_t(v&255));};
        for(const char* field:{"camera","hit","action","defender"}) {
            if(!animation.contains(field) || !integer(animation[field],0,0x7FFF))return where+".animation."+field+"：要是整数";
            push16(animation[field].get<int>());
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
            const auto scene=scene_of(a.value("scene",json()));
            if(!scene)return here+".scene：要是战斗场景总表的号（0–1052）或 @场景名";
            push16(int(*scene));
            for(const char* f:{"x","y","z"}){if(!integer(a.value(f,json(0)),-32768,32767))return here+"."+f+"：要是整数";push16(a.value(f,0));}
            for(const char* f:{"h4","h5","h6"}){if(!integer(a.value(f,json(0)),0,0xFFFF))return here+"."+f+"：要是 0–65535";push16(a.value(f,0));}
            if(!integer(a.value("behavior",json()),3,382))return here+".behavior：要是行为例程号（3–382）";
            push16(a["behavior"].get<int>());
        }
        push16(-1);
        if(data.size()>animation_window)return where+".animation：记录太长";
        // The game reads 0x3C0 bytes from the record's start: keep each a window apart.
        if(t.animation_next+animation_window>tw::base(tw::anim_records)+tw::block)return where+".animation：动画记录的空位用完了";
        for(size_t i=0;i<data.size();++i)put8(t.animation_next+uint32_t(i),data[i]);
        put32(tw::row(tw::anim_offsets,n),t.animation_next-tw::animation_bank);
        t.animation_next+=animation_window;
    }

    // Units.
    const auto& unit_part=document.value("units",json::object());
    if(!unit_part.is_object())return "units 要是对象";
    std::map<unsigned,std::vector<unsigned>> unit_weapons;   // each new unit's own list, for its family's
    std::vector<std::pair<std::string,std::vector<unsigned>>> families;   // where, the base form first
    for(const auto& [name,entry]:unit_part.items()) {
        const std::string where="units."+name;
        if(!valid_name(name))return where+"：名字只能用小写字母、数字和 - _ .";
        const auto number=numbers.units.find(own(name));
        if(number==numbers.units.end())return where+"：新机体的编号用完了";
        const unsigned n=number->second;
        if(!entry.is_object())return where+"：要是对象";
        for(const auto& [field,_]:entry.items())
            if(field!="copy" && field!="pose" && field!="icon" && field!="weapons" && field!="inherits_from" && field!="crew" && field!="combine" && field!="work" && field!="forms")
                return where+"：未知字段 "+field+"（copy、pose、icon、weapons、inherits_from、crew、combine、work、forms）";
        const auto source=resolve(entry.value("copy",json()),package,numbers,Of::unit);
        if(!source || *source==n || (*source<tw::rom_units && *source>=battle_units_with_rows))
            return where+".copy：要写照抄的机体（原版 0–353 或 @名字）";
        t.copies[n]=*source;
        const uint32_t unit=tw::row(tw::units,n);
        copy(unit,tw::row(tw::units,*source),unit_record_size);
        // 変形, 分離 and 合体 (+1C's 0x2, 0x40, 0x100) need the unit in the form and combination
        // tables, which a new number is in none of: the copy drops them, and the status pages
        // stop naming them (Final Dancouga showed 分離, copied from 224).
        put8(unit+0x1E,at(unit+0x1E)&~0x01);
        put8(unit+0x1F,at(unit+0x1F)&~0x42);
        for(const auto table:{tw::battle_units,tw::status_offsets,tw::unit_songs,tw::poses,tw::map_icons})
            copy(tw::row(table,n),tw::row(table,*source),tw::layouts[table].stride);
        {
            const uint16_t mine=tw::name_record(tw::NameKind::unit,n),theirs=tw::name_record(tw::NameKind::unit,*source);
            if(const auto inherited=t.names.find(theirs);inherited!=t.names.end())t.names[mine]=inherited->second;
            else t.names[mine]=record(theirs);
        }
        if(entry.contains("pose")) {
            const auto picture=triple(entry["pose"]);
            if(!picture)return where+".pose：要是 [场景, 图集, 调色板] 三个资源号，或 @图片名（sprites 里的）";
            for(unsigned k=0;k<3;++k)put16(tw::row(tw::poses,n)+k*2,(*picture)[k]);
        }
        if(entry.contains("icon")) {
            if(!integer(entry["icon"],688,1009))return where+".icon：要是地图图标资源号（688–1009）";
            put16(tw::row(tw::map_icons,n),entry["icon"].get<unsigned>());
        }
        if(!entry.contains("weapons") || !entry["weapons"].is_array() || entry["weapons"].empty() || entry["weapons"].size()>63)
            return where+".weapons：要是 1–63 件武器的数组";
        std::vector<unsigned> weapons;
        for(const auto& w:entry["weapons"]) {
            const auto found=resolve(w,package,numbers,Of::weapon);
            if(!found)return where+".weapons："+w.dump()+" 不是原版武器号，也不是新武器的 @名字";
            weapons.push_back(*found);
        }
        const uint32_t lists=tw::base(tw::weapon_lists);
        const uint32_t size=uint32_t(weapons.size()+1)*list_row;
        if(t.list_next+size>lists+tw::block)return where+".weapons：武器表的空位用完了";
        put32(tw::row(tw::weapon_lists,n),t.list_next-lists);
        for(size_t k=0;k<weapons.size();++k) {
            const uint32_t row=t.list_next+uint32_t(k)*list_row;
            put16(row,weapons[k]);put16(row+2,n);
            for(unsigned f=2;f<6;++f)put16(row+f*2,0xFFFF);
        }
        for(unsigned f=0;f<6;++f)put16(t.list_next+uint32_t(weapons.size())*list_row+f*2,0xFFFF);
        t.list_next+=size;
        unit_weapons[n]=weapons;
        // Other forms it changes into (変形): new units of this file, two at most, as the
        // original's families have (ゲッター, ダイターン3); written once every unit is read.
        if(entry.contains("forms")) {
            const auto& f=entry["forms"];
            if(!f.is_array() || f.empty() || f.size()>2)return where+".forms：要写 1–2 个别的形态（同一文件里的新机体 @名字）";
            std::vector<unsigned> members{n};
            for(const auto& other:f) {
                const auto u=resolve(other,package,numbers,Of::unit);
                if(!u || !tw::new_unit(*u) || std::find(members.begin(),members.end(),*u)!=members.end())
                    return where+".forms：要写同一文件里别的新机体 @名字";
                members.push_back(*u);
            }
            families.emplace_back(where,members);
        }
        // The Library's work (the original's 25, text 60 + n); without it, the copied unit's.
        if(entry.contains("work")) {
            if(!integer(entry["work"],0,24))return where+".work：要是原版作品号 0–24";
            t.works[n]=entry["work"].get<int>();
        }
        if(entry.contains("crew")) {
            const auto& crew=entry["crew"];
            if(!crew.is_array() || crew.empty() || crew.size()>5)return where+".crew：要写 1–5 个人物号，第一个是主驾驶";
            std::vector<unsigned> pilots;
            for(const auto& c:crew){if(!integer(c,0,360))return where+".crew：人物号是 0–360";pilots.push_back(c.get<unsigned>());}
            t.crews[n]=pilots;
        }
        if(entry.contains("combine")) {
            const auto& c=entry["combine"];
            const std::string why=where+".combine：要写 {\"parts\": [2–5 台部件], \"over\": [原版合体机]}";
            if(!c.is_object() || !c.contains("parts") || !c["parts"].is_array() || c["parts"].size()<2 || c["parts"].size()>5)return why;
            for(const auto& [k,_]:c.items())if(k!="parts" && k!="over")return why;
            Taken::Combine combine;
            for(const auto& part:c["parts"]) {
                const auto u=resolve(part,package,numbers,Of::unit);
                if(!u)return where+".combine.parts：要是机体号或 @名字";
                combine.parts.push_back(*u);
            }
            if(c.contains("over")) {
                if(!c["over"].is_array())return why;
                for(const auto& o:c["over"]) {
                    const auto u=resolve(o,package,numbers,Of::unit);
                    if(!u || *u>=tw::rom_units)return where+".combine.over：要是原版合体机的机体号";
                    combine.over.push_back(*u);
                }
            }
            t.combines[n]=combine;
        }
        if(!entry.contains("inherits_from"))continue;
        const auto from=resolve(entry["inherits_from"],package,numbers,Of::unit);
        if(!from)return where+".inherits_from：要是机体号或 @名字";
        // The host answers the predecessor lookup for it (800AA814, game_hooks.cpp) and carries
        // the weapons' levels after the original's (800AA8F4): no rows of the ROM's tables.
        t.predecessors[n]=*from;
        const auto old_ones=carried(*from);
        auto& pairs=t.carries[n];
        for(const unsigned w:weapons) {
            const auto inherits=weapon_inherits.find(w);
            const unsigned old=inherits!=weapon_inherits.end()?inherits->second:w;
            if(std::find(old_ones.begin(),old_ones.end(),old)!=old_ones.end() || weapon_inherits.count(w))pairs.push_back({w,old});
        }
    }
    // Form families: one weapon list the forms share, each row naming the forms that have the
    // weapon (as the original's: ダイターン3's lists name 216, 217, 218 per row), every form's
    // list pointing at it, and 変形 (+1C's 0x2) back on.
    std::set<unsigned> in_family;
    for(const auto& [where,members]:families) {
        for(const unsigned m:members) {
            if(!unit_weapons.count(m))return where+".forms：要写同一文件里别的新机体 @名字";
            if(!in_family.insert(m).second)return where+".forms：一台机体只能在一个变形族里";
        }
        std::vector<unsigned> shared;
        for(const unsigned m:members)
            for(const unsigned w:unit_weapons[m])if(std::find(shared.begin(),shared.end(),w)==shared.end())shared.push_back(w);
        if(shared.size()>63)return where+".forms：各形态的武器合起来最多 63 件";
        const uint32_t lists=tw::base(tw::weapon_lists);
        const uint32_t size=uint32_t(shared.size()+1)*list_row;
        if(t.list_next+size>lists+tw::block)return where+".forms：武器表的空位用完了";
        for(size_t k=0;k<shared.size();++k) {
            const uint32_t row=t.list_next+uint32_t(k)*list_row;
            put16(row,shared[k]);
            unsigned slot=1;
            for(const unsigned m:members)
                if(const auto& own_list=unit_weapons[m];std::find(own_list.begin(),own_list.end(),shared[k])!=own_list.end())put16(row+slot++*2,m);
            for(;slot<6;++slot)put16(row+slot*2,0xFFFF);
        }
        for(unsigned f=0;f<6;++f)put16(t.list_next+uint32_t(shared.size())*list_row+f*2,0xFFFF);
        for(const unsigned m:members) {
            put32(tw::row(tw::weapon_lists,m),t.list_next-lists);
            const uint32_t unit=tw::row(tw::units,m);
            put8(unit+0x1F,at(unit+0x1F)|0x02);
        }
        t.list_next+=size;
        t.forms[members[0]]=members;
    }
    // New characters: a copy of an original character's ability record, thresholds, spirit
    // learning, portrait, battle song and names at the character's own rows (its records are
    // its own number), then the package's changes (data/pilots.json, names.json).
    const json pilots=document.value("pilots",json::object());
    for(const auto& [name,entry]:pilots.items()) {
        const auto where="pilots."+name;
        if(!valid_name(name))return where+"：名字只能用小写字母、数字和 - _ .";
        const auto number=numbers.pilots.find(own(name));
        if(number==numbers.pilots.end())return where+"：新人物的编号用完了";
        const unsigned n=number->second;
        if(!entry.is_object())return where+"：要是对象";
        for(const auto& [field,_]:entry.items())
            if(field!="copy" && field!="voice" && field!="portrait" && field!="love" && field!="combo_as")
                return where+"：未知字段 "+field+"（copy、voice、portrait、love、combo_as）";
        const auto stats_of=[&](unsigned p){return int(int16_t(u16(resident(tw::pilot_map_ram)+p*2)));};
        const auto spirits_of=[&](unsigned p){return int(int16_t(u16(resident(tw::spirit_map_ram)+p*2)));};
        const auto original=[&](const json& v)->std::optional<unsigned>{
            if(!integer(v,0,int64_t(tw::rom_pilot_maps)-1) || stats_of(v.get<unsigned>())<0)return std::nullopt;
            return v.get<unsigned>();
        };
        const auto source=original(entry.value("copy",json()));
        if(!source)return where+".copy：要写照抄的原版人物（0–359，有能力记录的）";
        const auto voice=entry.contains("voice")?original(entry["voice"]):source;
        if(!voice)return where+".voice：要写借台词的原版人物（0–359，有能力记录的）";
        const unsigned stats=unsigned(stats_of(*source));
        copy(tw::row(tw::pilot_records,n),tw::row(tw::pilot_records,stats),tw::layouts[tw::pilot_records].stride);
        copy(tw::row(tw::thresholds,n),tw::row(tw::thresholds,stats),tw::layouts[tw::thresholds].stride);
        if(const int s=spirits_of(*source);s>=0)
            copy(tw::row(tw::spirit_records,n),tw::row(tw::spirit_records,unsigned(s)),tw::layouts[tw::spirit_records].stride);
        else for(uint32_t i=0;i<tw::layouts[tw::spirit_records].stride;++i)put8(tw::row(tw::spirit_records,n)+i,0);
        copy(tw::row(tw::portraits,n),tw::row(tw::portraits,*source),tw::layouts[tw::portraits].stride);
        copy(tw::row(tw::pilot_songs,n),tw::row(tw::pilot_songs,*source),tw::layouts[tw::pilot_songs].stride);
        if(entry.contains("portrait")) {
            const auto& p=entry["portrait"];
            if(p.is_object()) {
                // The package's own: a 96x96 picture of up to 63 colours (RGBA5551, half alpha
                // and under transparent) and an HD one drawn in its place.
                const std::string why_shape=where+".portrait：要写 {\"image\": 96×96 的 PNG, \"hd\": 高清 PNG}";
                for(const auto& [k,_]:p.items())if(k!="image" && k!="hd")return why_shape;
                const auto image=inside(folder,p.value("image",json()));
                if(!image)return why_shape;
                std::string why;
                const auto frame=mod_sprites::load(*image,why);
                if(!why.empty())return where+".portrait.image："+why;
                if(frame.width!=96 || frame.height!=96)return where+".portrait.image：要是 96×96 像素";
                Taken::Portrait own;
                own.pixels=*image;
                if(p.contains("hd")) {
                    const auto hd=inside(folder,p["hd"]);
                    if(!hd || !std::filesystem::is_regular_file(*hd))return where+".portrait.hd：包里没有这张图";
                    own.hd=*hd;
                }
                std::vector<uint16_t> colors{0};
                std::map<uint16_t,uint8_t> index;
                std::vector<uint8_t> pixels(96*96,0);
                for(unsigned i=0;i<96*96;++i) {
                    const uint8_t* px=&frame.rgba[size_t(i)*4];
                    if(px[3]<128)continue;
                    const uint16_t c=mod_sprites::rgba5551(px);
                    auto [at,added]=index.emplace(c,uint8_t(colors.size()));
                    if(added) {
                        if(colors.size()>=64)return where+".portrait.image：颜色太多，最多 63 种不透明颜色（tools/mods/sprite_pixels.py --size 96 96 --colors 63）";
                        colors.push_back(c);
                    }
                    pixels[i]=at->second;
                }
                std::vector<uint8_t> picture={0,15,0,96,0,96,0,0},palette={0,3,0,0x80,0,0,0,0};
                picture.insert(picture.end(),pixels.begin(),pixels.end());
                for(unsigned k=0;k<64;++k){const uint16_t c=k<colors.size()?colors[k]:0;palette.push_back(uint8_t(c>>8));palette.push_back(uint8_t(c));}
                const auto digest=[](const uint8_t* data,size_t size){uint64_t v=0xCBF29CE484222325ull;for(size_t i=0;i<size;++i)v=(v^data[i])*0x100000001B3ull;return v;};
                own.pixel_digest=digest(pixels.data(),pixels.size());
                own.palette_digest=digest(palette.data()+8,128);
                if(t.resource_next+2>tw::resource_limit)return where+".portrait：资源号用完了";
                own.image=t.resource_next;own.palette=t.resource_next+1;t.resource_next+=2;
                t.resources[own.image]=std::move(picture);t.resources[own.palette]=std::move(palette);
                put16(tw::row(tw::portraits,n),own.image);put16(tw::row(tw::portraits,n)+2,own.palette);
                t.portraits[n]=std::move(own);
            } else {
                if(!p.is_array() || p.size()!=2 || !integer(p[0],1,0xFFFF) || !integer(p[1],1,0xFFFF))
                    return where+".portrait：要是头像的两个资源号 [图, 调色板]，或 {\"image\": …, \"hd\": …}";
                put16(tw::row(tw::portraits,n),p[0].get<unsigned>());put16(tw::row(tw::portraits,n)+2,p[1].get<unsigned>());
            }
        }
        for(const auto kind:{tw::NameKind::pilot,tw::NameKind::pilot_full}) {
            const uint16_t mine=tw::name_record(kind,n),theirs=tw::name_record(kind,*source);
            t.names[mine]=record(theirs);
        }
        t.pilot_copies[n]=*source;t.voices[n]=*voice;
        // 愛／友情 (801F4908): the character gains 30 % next to a partner; "mutual" gives the
        // partner the same next to it.
        if(entry.contains("love")) {
            const auto& love=entry["love"];
            const std::string why=where+".love：要写 [{\"with\": 人物号或 @名字, \"mutual\": true/false}]";
            if(!love.is_array() || love.empty())return why;
            for(const auto& l:love) {
                if(!l.is_object() || !l.contains("with"))return why;
                for(const auto& [k,_]:l.items())if(k!="with" && k!="mutual")return why;
                const auto partner=resolve(l["with"],package,numbers,Of::pilot);
                if(!partner || *partner==n)return where+".love.with：要是另一个人物号（0–360）或新人物的 @名字";
                if(l.contains("mutual") && !l["mutual"].is_boolean())return why;
                t.loves.push_back({n,*partner});
                if(l.value("mutual",false))t.loves.push_back({*partner,n});
            }
            if(t.loves.size()>tw::love_rows-tw::love_rom_rows)return where+".love：所有包合计最多 "+std::to_string(tw::love_rows-tw::love_rom_rows)+" 行";
        }
        if(entry.contains("combo_as")) {
            const auto as=original(entry["combo_as"]);
            if(!as)return where+".combo_as：要写原版人物号（0–359）";
            t.combo_as[n]=*as;
        }
    }
    taken=t;
    for(const auto& [a,v]:bytes)out[a]=v;
    return {};
}
}
