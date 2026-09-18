#pragma once
#include "rule_fixes.hpp"
#include <array>
#include <map>
#include <optional>
#include <set>
#include <vector>

// Unit upgrade limits and costs (docs/upgrade-limits.md). Two independent parts:
//
// - An optional rules file (SRW64_UPGRADE_RULES) replaces the per-level increments
//   and prices, per-unit caps and per-weapon upgrade types. The game keeps using
//   its own tables and records; the host rewrites them in memory when they load.
//   Without a file nothing is written.
// - The difficulty rule upgrade-cap-break lets the upgrade screen go up to 15
//   levels, the length of every original table. Only the upgrade screen sees the
//   raised cap: a scope around its routines writes it into unit +0x51 and puts the
//   original back on return, so EW swaps, full-upgrade weapons, 3D6C and the sale
//   screen keep judging by the original cap.
namespace srw64::upgrades {
using guest::read;
using guest::valid;
using guest::write8;
using guest::write16;
using guest::write32;

inline constexpr unsigned levels=15, stat_count=5, weapon_type_count=4, min_cap=5, max_cap=15;
inline constexpr std::array<std::string_view,stat_count> stat_names={"hp","en","mobility","armor","limit"};
inline constexpr uint32_t max_increment=9999, max_increment_sum=30000, min_price=1, max_price=99998;

// ROM records: unit +0x20 is the cap, weapon +0x0E the upgrade type (0 = none).
inline constexpr uint32_t unit_records=0x71B80, unit_record_size=0x24, unit_record_count=363, record_cap=0x20;
inline constexpr uint32_t weapon_records=0x74E90, weapon_record_size=0x10, weapon_record_count=1329, record_type=0x0E;
// Resident tables used by 800A5254. ROM = VRAM - resident_delta.
inline constexpr uint32_t resident_delta=0x80075610;
inline constexpr uint32_t stat_increments=0x800CA4F0, stat_increments_stride=0x20;   // 5 x 16 u16; entry 0 is 0
inline constexpr uint32_t weapon_increments=0x800CA590;                              // 5 rows x 15 u16, row = type
// Upgrade screen overlay: ROM 8F4B0 loaded at 801C4500.
inline constexpr uint32_t overlay_rom=0x8F4B0, overlay_ram=0x801C4500;
inline constexpr uint32_t overlay_rom_of(uint32_t vram){return vram-overlay_ram+overlay_rom;}
inline constexpr uint32_t stat_prices=0x801DC36C, stat_prices_stride=0x40;           // 5 x 16 u32; entry 15 is 99999
inline constexpr uint32_t stat_previews=0x801DC4AC, stat_previews_stride=0x20;       // 5 x 16 u16; entry n is level n+1
inline constexpr uint32_t weapon_prices[weapon_type_count]={0x801DC7BC,0x801DC77C,0x801DC73C,0x801DC6FC};   // types 1..4
inline constexpr uint32_t weapon_previews[weapon_type_count]={0x801DC85C,0x801DC83C,0x801DC81C,0x801DC7FC};
inline constexpr uint32_t gauge_tiers=0x801DC340;   // caps 5..15: pointer to cap+1 text ids, one per level
inline constexpr uint32_t unlock_table=0x801DC87C;  // s16 (unit, upgraded weapon, unlocked weapon); 999 ends
inline constexpr uint32_t screen_unit=0x801DEC5C;   // u32 unit slot shown by the upgrade screen
// Weapon screen list: slot = rows[page_row + 6 * (page - 1)], as 801D0C7C and 801D1100 compute it.
inline constexpr uint32_t weapon_rows=0x801DEC68, weapon_row_count=0x801DECBE, weapon_row=0x801DDA34, weapon_page=0x801DD20C;
// Unit instances (140 x 0x54) and their weapons (0x24 each); the layout shared
// with the weapon inheritance rule comes from rule_fixes.hpp.
using rules::unit_size, rules::unit_weapon_count, rules::unit_weapon_list;
using rules::weapon_size, rules::weapon_number, rules::weapon_level;
inline constexpr uint32_t units=0x8016A210, unit_slots=140;
inline constexpr uint32_t unit_number=0x02, unit_levels=0x4C, unit_cap=0x51, weapon_type=0x15;
// Text table 0: count, then (offset, size) per id; an entry is an 8-byte header,
// u16 glyphs and FFFF. Gauge strings are served from a range past the 32 MiB ROM
// and past the name codec's virtual glyphs (0x02400000).
inline constexpr uint32_t text_table=0x01A34980, text_virtual=0x03000000, text_slot=0x40;
inline constexpr uint16_t glyph_on=0x0123, glyph_off=0x00DB, glyph_extra_on=0x00FD, glyph_extra_off=0x00FC;   // ▶ ▷ ● ☆
// Sale screen overlay (ROM 107BF0 at 801C2600): 12 rows of (s16 unit, u16 base price).
inline constexpr uint32_t sale_rows=0x801C3A40, sale_row_count=12, sale_upgrade_price=20000;

struct Curve {
    std::array<uint16_t,levels> increments{};   // level n+1 adds increments[n]
    std::array<uint32_t,levels> prices{};       // going from level n to n+1 costs prices[n]
    bool operator==(const Curve&) const=default;
};
struct Tables {
    std::array<Curve,stat_count> stats{};
    std::array<Curve,weapon_type_count> weapons{};   // types 1..4
    std::map<uint16_t,uint8_t> caps, types;          // overrides only
};
struct State {
    const uint8_t* rom{};
    size_t rom_size{};
    Tables original, active;
    bool configured{};
    std::string path;
    std::array<std::array<uint16_t,max_cap+1>,max_cap-min_cap+1> gauge_ids{};   // [cap-5][level]
    std::map<uint16_t,std::pair<uint8_t,uint8_t>> gauge_of;                      // text id -> (cap, level)
    std::map<uint32_t,std::vector<uint8_t>> text;                                // composed entries by virtual ROM address
};
inline State& state(){static State value;return value;}

inline uint32_t rom_read(uint32_t at,unsigned size) {
    const auto& s=state();
    if(!s.rom || at+size>s.rom_size)throw std::runtime_error("upgrade rules: ROM read outside the image");
    uint32_t value=0;
    for(unsigned i=0;i<size;++i)value=(value<<8)|s.rom[at+i];
    return value;
}

// Reads the original tables from the pinned ROM and checks that the two copies the
// game keeps (resident for the stat calculation, overlay for the screen preview)
// agree, as they do in the original.
inline void initialize(const uint8_t* rom,size_t size) {
    auto& s=state();
    s.rom=rom;s.rom_size=size;
    for(unsigned stat=0;stat<stat_count;++stat)
        for(unsigned n=0;n<levels;++n) {
            auto& curve=s.original.stats[stat];
            curve.increments[n]=uint16_t(rom_read(stat_increments-resident_delta+stat*stat_increments_stride+(n+1)*2,2));
            curve.prices[n]=rom_read(overlay_rom_of(stat_prices)+stat*stat_prices_stride+n*4,4);
            if(rom_read(overlay_rom_of(stat_previews)+stat*stat_previews_stride+n*2,2)!=curve.increments[n])
                throw std::runtime_error("upgrade rules: stat preview table differs from the resident table");
        }
    for(unsigned type=0;type<weapon_type_count;++type)
        for(unsigned n=0;n<levels;++n) {
            auto& curve=s.original.weapons[type];
            curve.increments[n]=uint16_t(rom_read(weapon_increments-resident_delta+((type+1)*levels+n)*2,2));
            curve.prices[n]=rom_read(overlay_rom_of(weapon_prices[type])+n*4,4);
            if(rom_read(overlay_rom_of(weapon_previews[type])+n*2,2)!=curve.increments[n])
                throw std::runtime_error("upgrade rules: weapon preview table differs from the resident table");
        }
    s.gauge_of.clear();
    for(unsigned cap=min_cap;cap<=max_cap;++cap) {
        const uint32_t ids=rom_read(overlay_rom_of(gauge_tiers)+(cap-min_cap)*4,4);
        for(unsigned level=0;level<=cap;++level) {
            const uint16_t id=uint16_t(rom_read(overlay_rom_of(ids)+level*2,2));
            s.gauge_ids[cap-min_cap][level]=id;
            s.gauge_of[id]={uint8_t(cap),uint8_t(level)};
        }
    }
    s.active=s.original;
}

// --- Rules file ----------------------------------------------------------------

inline void fail(const std::string& text){throw std::runtime_error("升级规则文件："+text);}
inline void only_keys(const nlohmann::json& object,const std::set<std::string>& allowed,const std::string& where) {
    if(!object.is_object())fail(where+" 必须是对象");
    for(const auto& [key,_]:object.items())if(!allowed.count(key))fail(where+" 有未知字段 "+key);
}
inline uint32_t integer(const nlohmann::json& value,uint32_t low,uint32_t high,const std::string& where) {
    if(!value.is_number_integer())fail(where+" 必须是整数");
    const auto number=value.get<int64_t>();
    if(number<int64_t(low) || number>int64_t(high))fail(where+" 超出范围 "+std::to_string(low)+"–"+std::to_string(high));
    return uint32_t(number);
}
inline void read_curve(const nlohmann::json& document,Curve& curve,const std::string& where) {
    only_keys(document,{"increments","prices"},where);
    if(document.contains("increments")) {
        const auto& values=document["increments"];
        if(!values.is_array() || values.size()!=levels)fail(where+".increments 必须是 15 个整数");
        uint32_t sum=0;
        for(unsigned n=0;n<levels;++n)sum+=curve.increments[n]=uint16_t(integer(values[n],0,max_increment,where+".increments"));
        if(sum>max_increment_sum)fail(where+".increments 之和超过 "+std::to_string(max_increment_sum));
    }
    if(document.contains("prices")) {
        const auto& values=document["prices"];
        if(!values.is_array() || values.size()!=levels)fail(where+".prices 必须是 15 个整数");
        for(unsigned n=0;n<levels;++n)curve.prices[n]=integer(values[n],min_price,max_price,where+".prices");
    }
}
inline void read_overrides(const nlohmann::json& list,const char* field,uint32_t count,uint32_t low,uint32_t high,
                           std::map<uint16_t,uint8_t>& out,const std::string& where) {
    if(!list.is_array())fail(where+" 必须是数组");
    for(const auto& row:list) {
        only_keys(row,{"id",field,"name"},where+" 的条目");
        if(!row.contains("id") || !row.contains(field))fail(where+" 的条目缺少 id 或 "+field);
        const auto id=uint16_t(integer(row["id"],0,count-1,where+".id"));
        if(row.contains("name") && !row["name"].is_string())fail(where+".name 必须是字符串");
        if(!out.emplace(id,uint8_t(integer(row[field],low,high,where+"."+field))).second)
            fail(where+" 重复的 id "+std::to_string(id));
    }
}
// Starts from the original tables; every section and field is optional.
inline Tables parse(const nlohmann::json& document,const Tables& original) {
    only_keys(document,{"schema","description","stats","weapon_types","unit_caps","weapon_type_overrides"},"顶层");
    if(document.value("schema","")!="srw64.upgrade-rules.v1")fail("schema 必须是 srw64.upgrade-rules.v1");
    if(document.contains("description") && !document["description"].is_string())fail("description 必须是字符串");
    Tables tables=original;
    tables.caps.clear();tables.types.clear();
    if(document.contains("stats")) {
        only_keys(document["stats"],{stat_names.begin(),stat_names.end()},"stats");
        for(unsigned stat=0;stat<stat_count;++stat) {
            const std::string name(stat_names[stat]);
            if(document["stats"].contains(name))read_curve(document["stats"][name],tables.stats[stat],"stats."+name);
        }
    }
    if(document.contains("weapon_types")) {
        only_keys(document["weapon_types"],{"1","2","3","4"},"weapon_types");
        for(unsigned type=0;type<weapon_type_count;++type) {
            const auto key=std::to_string(type+1);
            if(document["weapon_types"].contains(key))read_curve(document["weapon_types"][key],tables.weapons[type],"weapon_types."+key);
        }
    }
    if(document.contains("unit_caps"))
        read_overrides(document["unit_caps"],"cap",unit_record_count,min_cap,max_cap,tables.caps,"unit_caps");
    if(document.contains("weapon_type_overrides"))
        read_overrides(document["weapon_type_overrides"],"type",weapon_record_count,0,weapon_type_count,tables.types,"weapon_type_overrides");
    return tables;
}

inline nlohmann::json changes(const Tables& active,const Tables& original) {
    nlohmann::json stats=nlohmann::json::array(), types=nlohmann::json::array();
    for(unsigned stat=0;stat<stat_count;++stat)if(!(active.stats[stat]==original.stats[stat]))stats.push_back(stat_names[stat]);
    for(unsigned type=0;type<weapon_type_count;++type)if(!(active.weapons[type]==original.weapons[type]))types.push_back(type+1);
    return {{"stats",stats},{"weapon_types",types},{"unit_caps",active.caps.size()},
            {"weapon_type_overrides",active.types.size()}};
}
// Every modified byte by its ROM address: the resident increment tables, the
// upgrade overlay's prices and preview increments, and the overridden cap and
// type bytes of the unit and weapon records. The game reaches all of them by
// copying ROM (the runtime's boot copy of the resident section, the overlay DMA,
// the record reads of 800A5254/800A6E68/800A642C), so patching each copy keeps
// every reader consistent. The overlay's preview copy is what a confirmed upgrade
// adds on the spot, so it must follow the resident table or a stat would jump
// when 800A5254 next recomputes it.
inline std::vector<std::pair<uint32_t,uint8_t>> build_patches(const Tables& active) {
    std::map<uint32_t,uint8_t> bytes;
    auto put=[&](uint32_t rom,uint32_t value,unsigned size){for(unsigned i=0;i<size;++i)bytes[rom+i]=uint8_t(value>>((size-1-i)*8));};
    for(unsigned stat=0;stat<stat_count;++stat)
        for(unsigned n=0;n<levels;++n) {
            const auto& curve=active.stats[stat];
            put(stat_increments-resident_delta+stat*stat_increments_stride+(n+1)*2,curve.increments[n],2);
            put(overlay_rom_of(stat_previews)+stat*stat_previews_stride+n*2,curve.increments[n],2);
            put(overlay_rom_of(stat_prices)+stat*stat_prices_stride+n*4,curve.prices[n],4);
        }
    for(unsigned type=0;type<weapon_type_count;++type)
        for(unsigned n=0;n<levels;++n) {
            const auto& curve=active.weapons[type];
            put(weapon_increments-resident_delta+((type+1)*levels+n)*2,curve.increments[n],2);
            put(overlay_rom_of(weapon_previews[type])+n*2,curve.increments[n],2);
            put(overlay_rom_of(weapon_prices[type])+n*4,curve.prices[n],4);
        }
    for(const auto& [unit,cap]:active.caps)put(unit_records+unit*unit_record_size+record_cap,cap,1);
    for(const auto& [weapon,type]:active.types)put(weapon_records+weapon*weapon_record_size+record_type,type,1);
    // Only bytes that differ from the ROM; an unmodified file changes nothing.
    std::vector<std::pair<uint32_t,uint8_t>> patches;
    for(const auto& [rom,value]:bytes)if(rom_read(rom,1)!=value)patches.emplace_back(rom,value);
    return patches;
}
inline std::vector<std::pair<uint32_t,uint8_t>>& patches(){static std::vector<std::pair<uint32_t,uint8_t>> value;return value;}
// After a copy of ROM [rom, rom+size) to RDRAM destination.
inline void patch_copy(uint8_t* ram,uint32_t rom,uint32_t destination,uint32_t size) {
    const auto& list=patches();
    if(list.empty() || rom>list.back().first || rom+size<=list.front().first)return;
    auto at=std::lower_bound(list.begin(),list.end(),std::make_pair(rom,uint8_t(0)));
    for(;at!=list.end() && at->first<rom+size;++at)write8(ram,destination+(at->first-rom),at->second);
}
// The runtime copies the resident section (ROM 0x1000, 1 MiB) before on_init.
inline constexpr uint32_t resident_rom=0x1000, resident_ram=0x80076610, resident_copy=0x100000;
inline void patch_resident(uint8_t* ram){patch_copy(ram,resident_rom,resident_ram,resident_copy);}

// Host start-up, after initialize(). A bad file stops the run here.
inline void configure(const std::filesystem::path& output) {
    auto& s=state();
    s.active=s.original;s.configured=false;s.path.clear();patches().clear();
    const char* path=std::getenv("SRW64_UPGRADE_RULES");
    nlohmann::json report={{"schema","srw64.upgrade-rules-applied.v1"},{"path",nullptr}};
    if(path && *path) {
        std::ifstream in(path);
        if(!in)fail(std::string("无法读取 ")+path);
        nlohmann::json document;
        try{document=nlohmann::json::parse(in);}catch(const nlohmann::json::exception& error){fail(std::string("JSON 无效：")+error.what());}
        s.active=parse(document,s.original);
        s.configured=true;s.path=path;
        report["path"]=path;
        report["changed"]=changes(s.active,s.original);
        report["patched_bytes"]=0;
    }
    patches()=s.configured?build_patches(s.active):std::vector<std::pair<uint32_t,uint8_t>>{};
    if(s.configured)report["patched_bytes"]=patches().size();
    if(!output.empty())std::ofstream(output/"upgrade-rules.json")<<report.dump(2)<<'\n';
    std::fprintf(stderr,"SRW64_UPGRADE_RULES %s\n",s.configured?report["changed"].dump().c_str():"none");
}

// --- Caps ----------------------------------------------------------------------

// The cap the game itself would use: the ROM record, or the rules file's override.
inline uint8_t original_cap(uint16_t unit) {
    const auto& s=state();
    if(const auto found=s.active.caps.find(unit);found!=s.active.caps.end())return found->second;
    return unit<unit_record_count?uint8_t(rom_read(unit_records+unit*unit_record_size+record_cap,1)):0;
}
inline uint8_t original_type(uint16_t weapon) {
    const auto& s=state();
    if(const auto found=s.active.types.find(weapon);found!=s.active.types.end())return found->second;
    return weapon<weapon_record_count?uint8_t(rom_read(weapon_records+weapon*weapon_record_size+record_type,1)):0;
}
inline bool breaking(){return rules::enabled(rules::upgrade_cap_break);}
inline uint8_t allowed_cap(uint16_t unit) {
    const uint8_t cap=original_cap(unit);
    return breaking()?std::max<uint8_t>(cap,max_cap):cap;
}

// Which cap the routine running now should see:
//  original   everything outside the upgrade screen, and the EW check inside it;
//  decide     can this be upgraded, what does it cost, the cap figure on screen;
//  stats_view the five gauges, also wide enough for any level already above the
//             cap (from a session with the rule on, or the original's mkⅡ to
//             スーパーガンダム propagation) so the gauge index stays in its table;
//  weapon     the selected weapon on the screen's unit; a weapon already above the
//             cap sees its own level, so the screen reports it as maxed instead of
//             charging for an upgrade 801D1100 would not apply.
enum class Policy {original,decide,stats_view,weapon};
inline std::vector<Policy>& frames(){static std::vector<Policy> value;return value;}
inline uint32_t unit_at(uint32_t slot){return units+slot*unit_size;}
inline uint32_t screen_slot(const uint8_t* ram){const uint32_t slot=read(ram,screen_unit,4);return slot<unit_slots?slot:unit_slots;}
inline uint8_t highest_stat(const uint8_t* ram,uint32_t unit) {
    uint8_t level=0;
    for(unsigned stat=0;stat<stat_count;++stat)level=std::max<uint8_t>(level,uint8_t(read(ram,unit+unit_levels+stat,1)));
    return level;
}
inline std::optional<uint32_t> selected_weapon(const uint8_t* ram,uint32_t unit) {
    const int32_t row=int16_t(uint16_t(read(ram,weapon_row,2)+6*(int16_t(read(ram,weapon_page,2))-1)));
    if(row<0 || row>=int16_t(read(ram,weapon_row_count,2)))return std::nullopt;
    const int32_t slot=int16_t(read(ram,weapon_rows+uint32_t(row)*2,2));
    const uint32_t count=read(ram,unit+unit_weapon_count,1), list=read(ram,unit+unit_weapon_list,4);
    if(slot<0 || uint32_t(slot)>=count || !valid(list,count*weapon_size))return std::nullopt;
    return list+uint32_t(slot)*weapon_size;
}
inline uint8_t cap_for(const uint8_t* ram,Policy policy,uint32_t slot,uint32_t screen) {
    const uint32_t unit=unit_at(slot);
    const uint16_t number=uint16_t(read(ram,unit+unit_number,2));
    if(policy==Policy::original)return original_cap(number);
    const uint8_t allowed=allowed_cap(number);
    if(policy==Policy::stats_view)return std::max(allowed,highest_stat(ram,unit));
    if(policy==Policy::weapon && slot==screen)
        if(const auto weapon=selected_weapon(ram,unit))return std::max<uint8_t>(allowed,uint8_t(read(ram,*weapon+weapon_level,1)));
    return allowed;
}
inline Policy current_policy(){return frames().empty()?Policy::original:frames().back();}
// Every live unit gets the cap of the innermost scope; the outermost exit puts the
// original back. Unchanged bytes are not written, so outside the rule and without
// a unit above its cap this never touches memory.
inline void apply(uint8_t* ram) {
    const Policy policy=current_policy();
    const uint32_t screen=screen_slot(ram);
    for(uint32_t slot=0;slot<unit_slots;++slot) {
        const uint32_t unit=unit_at(slot);
        if(!read(ram,unit,1))continue;
        const uint8_t cap=cap_for(ram,policy,slot,screen);
        if(cap && read(ram,unit+unit_cap,1)!=cap)write8(ram,unit+unit_cap,cap);
    }
}
class Scope {
public:
    Scope(uint8_t* ram,Policy policy):ram(ram){frames().push_back(policy);apply(ram);}
    ~Scope(){frames().pop_back();apply(ram);}
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
private:
    uint8_t* ram;
};

// 800A5254, before the original: refresh weapon types from the rules file, since
// the game copies the type only when it builds a weapon. After it: the original
// has just rewritten +0x51 from the ROM record, so restore the scope's cap.
inline void before_recompute(uint8_t* ram,uint32_t unit) {
    if(!state().configured || !valid(unit,unit_size))return;
    const uint32_t count=read(ram,unit+unit_weapon_count,1), list=read(ram,unit+unit_weapon_list,4);
    if(!count || !valid(list,count*weapon_size))return;
    for(uint32_t i=0;i<count;++i) {
        const uint32_t weapon=list+i*weapon_size;
        const uint8_t type=original_type(uint16_t(read(ram,weapon+weapon_number,2)));
        if(read(ram,weapon+weapon_type,1)!=type)write8(ram,weapon+weapon_type,type);
    }
}
inline void after_recompute(uint8_t* ram,uint32_t unit) {
    if(frames().empty() || unit<units || unit>=units+unit_slots*unit_size || (unit-units)%unit_size)return;
    const uint32_t slot=(unit-units)/unit_size;
    const uint8_t cap=cap_for(ram,current_policy(),slot,screen_slot(ram));
    if(cap)write8(ram,unit+unit_cap,cap);
}

// Full-upgrade weapons (801D0AE4) fire when a weapon's level equals +0x51 right
// after an upgrade. 800A5F84 runs between the increment and that comparison, so
// its wrapper steers the comparison: fire at the original cap, and not again when
// the weapon later reaches the raised cap.
inline bool unlocks(const uint8_t* ram,uint16_t unit,uint16_t weapon) {
    for(uint32_t row=0;row<64;++row) {
        const uint32_t at=unlock_table+row*6;
        const int16_t owner=int16_t(read(ram,at,2));
        if(owner==999)return false;
        if(uint16_t(owner)==unit && uint16_t(read(ram,at+2,2))==weapon)return true;
    }
    return false;
}
inline void steer_unlock(uint8_t* ram,uint32_t unit,uint32_t weapon) {
    if(current_policy()!=Policy::weapon || !valid(unit,unit_size) || !valid(weapon,weapon_size))return;
    const uint16_t number=uint16_t(read(ram,unit+unit_number,2));
    if(!unlocks(ram,number,uint16_t(read(ram,weapon+weapon_number,2))))return;
    const uint8_t level=uint8_t(read(ram,weapon+weapon_level,1)), cap=uint8_t(read(ram,unit+unit_cap,1));
    const uint8_t original=original_cap(number);
    if(level==original && cap!=original)write8(ram,unit+unit_cap,original);
    else if(level==cap && level!=original)write8(ram,unit+unit_cap,uint8_t(level+1));
}

// Sale price (load_00107BF0 801C2600): base + 20000 x upgraded share of the cap,
// returned as s16. Levels above the original cap would push the share past 1.
inline uint32_t clamp_sale_price(const uint8_t* ram,uint32_t row,uint32_t price) {
    if(row>=sale_row_count)return price;
    const uint32_t top=read(ram,sale_rows+row*4+2,2)+sale_upgrade_price;
    return (price&0xFFFF)>top?uint32_t(int32_t(int16_t(uint16_t(top)))):price;
}

// --- Gauge text ----------------------------------------------------------------

// Inside a gauge-drawing scope, when the screen's unit may go past its original cap
// or already has, each gauge string is rebuilt for that unit: cells up to the
// original cap in the original ▶▷, the rest as ●☆. With the rule on the gauge
// shows every cell up to the raised cap; with it off only as far as the level
// already reached, so nothing suggests an upgrade the screen will refuse.
inline bool gauge_active(const uint8_t* ram,uint8_t& original) {
    const Policy policy=current_policy();
    if(policy!=Policy::stats_view && policy!=Policy::weapon)return false;
    const uint32_t slot=screen_slot(ram);
    if(slot>=unit_slots || !read(ram,unit_at(slot),1))return false;
    original=original_cap(uint16_t(read(ram,unit_at(slot)+unit_number,2)));
    return original && cap_for(ram,policy,slot,slot)!=original;
}
inline std::vector<uint8_t> gauge_entry(uint16_t id,unsigned cap,unsigned level,unsigned original,bool raised) {
    const uint32_t offset=rom_read(text_table+4+id*8,4);
    std::vector<uint8_t> entry;
    for(unsigned i=0;i<8;++i)entry.push_back(uint8_t(rom_read(text_table+offset+i,1)));
    const unsigned cells=raised?cap:std::max(original,level);
    for(unsigned i=0;i<cells;++i) {
        const bool filled=i<level, own=i<original;
        const uint16_t glyph=own?(filled?glyph_on:glyph_off):(filled?glyph_extra_on:glyph_extra_off);
        entry.push_back(uint8_t(glyph>>8));entry.push_back(uint8_t(glyph));
    }
    entry.push_back(0xFF);entry.push_back(0xFF);
    return entry;
}
// 8008C510 wrapper: table 0 ids of the gauge strings only.
inline bool descriptor(const uint8_t* ram,uint16_t table,uint16_t id,uint32_t& offset,uint32_t& size) {
    auto& s=state();
    if(table || !s.rom)return false;
    const auto found=s.gauge_of.find(id);
    uint8_t original=0;
    if(found==s.gauge_of.end() || !gauge_active(ram,original))return false;
    const uint32_t address=text_virtual+uint32_t(id)*text_slot;
    auto entry=gauge_entry(id,found->second.first,found->second.second,original,breaking());
    size=uint32_t(entry.size());
    offset=address-text_table;
    s.text[address]=std::move(entry);
    return true;
}
// 8007F704 wrapper: serves what descriptor() handed out.
inline bool read_text(uint32_t rom,uint8_t* ram,uint32_t destination,uint32_t size) {
    auto& s=state();
    if(rom<text_virtual || rom>=text_virtual+0x10000*text_slot)return false;
    const auto found=s.text.find(rom);
    if(found==s.text.end() || size!=found->second.size() || !valid(destination|0x80000000u,size))
        throw std::runtime_error("upgrade rules: invalid virtual gauge text read");
    for(uint32_t i=0;i<size;++i)write8(ram,destination+i,found->second[i]);
    return true;
}
}
