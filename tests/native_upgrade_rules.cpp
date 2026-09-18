#include "upgrade_rules.hpp"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
uint64_t srw64_current_vi(){return 0;}
using namespace srw64::upgrades;
namespace rules=srw64::rules;

static std::vector<uint8_t> ram(0x800000);
static void put(uint32_t p,uint32_t v,unsigned n){for(unsigned i=0;i<n;++i)ram[((p&0x1FFFFFFF)+i)^3]=uint8_t(v>>((n-i-1)*8));}
static uint32_t get(uint32_t p,unsigned n){return read(ram.data(),p,n);}
static bool rejects(const nlohmann::json& document) {
    try{parse(document,state().original);}catch(const std::runtime_error&){return true;}
    return false;
}
static nlohmann::json doc(nlohmann::json body){body["schema"]="srw64.upgrade-rules.v1";return body;}
static std::vector<uint32_t> fifteen(uint32_t value){return std::vector<uint32_t>(levels,value);}

int main(int argc,char** argv) {
    assert(argc==3);
    const std::filesystem::path dir=argv[1];
    std::filesystem::create_directories(dir);
    std::ifstream in(argv[2],std::ios::binary);
    const std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    assert(rom.size()==0x2000000);
    initialize(rom.data(),rom.size());
    const auto& original=state().original;

    // Original tables (docs/upgrade-limits.md sections 2 and 3).
    for(unsigned n=0;n<levels;++n) {
        assert(original.stats[0].increments[n]==200 && original.stats[0].prices[n]==2000*(n+1));
        assert(original.stats[1].increments[n]==(n<5?10:20) && original.stats[2].increments[n]==(n<5?5:10));
        assert(original.stats[3].increments[n]==(n<5?100:150) && original.stats[4].increments[n]==(n<5?10:20));
        assert(original.stats[4].prices[n]==original.stats[1].prices[n]);
        for(unsigned type=0;type<weapon_type_count;++type)assert(original.weapons[type].prices[n]==(5-type)*1000*(n+1));
    }
    assert(original.stats[2].prices[0]==5000 && original.stats[2].prices[14]==65000 && original.stats[3].prices[2]==8000);
    assert(original.weapons[1].increments==original.weapons[2].increments);
    assert(original.weapons[0].increments[4]==200 && original.weapons[1].increments[4]==150 && original.weapons[3].increments[14]==300);
    // Gauge strings: caps 5..15, one id per level.
    assert(state().gauge_ids[7-min_cap][3]==0x1053 && state().gauge_ids[15-min_cap][0]==0x1031 && state().gauge_ids[15-min_cap][15]==0x1040);
    assert(state().gauge_of.size()==[]{unsigned n=0;for(unsigned cap=min_cap;cap<=max_cap;++cap)n+=cap+1;return n;}());
    assert(original_cap(124)==6 && original_cap(50)==7 && original_cap(54)==15 && original_cap(263)==13);
    assert(original_type(209)==1 && original_type(19)==0 && original_type(1032)==2);

    // Rules file validation: every field optional, strict names and ranges.
    {
        const auto tables=parse(doc({{"stats",{{"hp",{{"prices",fifteen(1000)}}}}}}),original);
        assert(tables.stats[0].prices[3]==1000 && tables.stats[0].increments==original.stats[0].increments);
        assert(tables.stats[1]==original.stats[1] && tables.weapons==original.weapons && tables.caps.empty());
        nlohmann::json body;
        body["weapon_types"]["4"]["increments"]=fifteen(2000);
        body["unit_caps"]=nlohmann::json::array({{{"id",124},{"cap",7},{"name","ガンダムサンドロック"}}});
        body["weapon_type_overrides"]=nlohmann::json::array({{{"id",19},{"type",2}}});
        const auto more=parse(doc(body),original);
        assert(more.weapons[3].increments[0]==2000 && more.caps.at(124)==7 && more.types.at(19)==2);
    }
    assert(rejects({{"stats",{}}}));                                              // no schema
    assert(rejects(doc({{"extra",1}})));
    assert(rejects(doc({{"stats",{{"hp",{{"prices",std::vector<uint32_t>(14,1000)}}}}}})));
    assert(rejects(doc({{"stats",{{"hp",{{"prices",fifteen(0)}}}}}})));             // 0 and 99999 are UI sentinels
    assert(rejects(doc({{"stats",{{"hp",{{"prices",fifteen(99999)}}}}}})));
    assert(rejects(doc({{"stats",{{"hp",{{"increments",fifteen(2001)}}}}}})));     // sum above 30000
    assert(!rejects(doc({{"stats",{{"hp",{{"increments",fifteen(2000)}}}}}})));
    assert(rejects(doc({{"stats",{{"speed",{{"prices",fifteen(1)}}}}}})));
    assert(rejects(doc({{"weapon_types",{{"0",{{"prices",fifteen(1)}}}}}})));
    assert(rejects(doc({{"unit_caps",{{{"id",124},{"cap",4}}}}})));
    assert(rejects(doc({{"unit_caps",{{{"id",124},{"cap",16}}}}})));
    assert(rejects(doc({{"unit_caps",{{{"id",363},{"cap",7}}}}})));
    assert(rejects(doc({{"unit_caps",{{{"id",124},{"cap",7}},{{"id",124},{"cap",8}}}}})));
    assert(rejects(doc({{"weapon_type_overrides",{{{"id",19},{"type",5}}}}})));
    assert(rejects(doc({{"weapon_type_overrides",{{{"id",19},{"type",1},{"note","x"}}}}})));
    assert(rejects(doc({{"stats",{{"hp",{{"prices",std::vector<double>(levels,1.5)}}}}}})));

    // No file: nothing to patch.
    unsetenv("SRW64_UPGRADE_RULES");
    configure(dir);
    assert(!state().configured && patches().empty());
    assert(nlohmann::json::parse(std::ifstream(dir/"upgrade-rules.json"))["path"].is_null());
    // A file that repeats the original values changes nothing either.
    const auto file=dir/"rules.json";
    {std::ofstream out(file);out<<doc({{"stats",{{"hp",{{"increments",fifteen(200)}}}}}}).dump();}
    setenv("SRW64_UPGRADE_RULES",file.c_str(),1);
    configure(dir);
    assert(state().configured && patches().empty());

    // A real change: resident increments, overlay preview and price, record bytes.
    {
        nlohmann::json body;
        body["stats"]["hp"]["increments"]=fifteen(300);
        body["stats"]["hp"]["prices"]=fifteen(1234);
        body["unit_caps"]=nlohmann::json::array({{{"id",50},{"cap",9}}});
        body["weapon_type_overrides"]=nlohmann::json::array({{{"id",209},{"type",2}}});
        std::ofstream out(file);
        out<<doc(body).dump();
    }
    configure(dir);
    const auto report=nlohmann::json::parse(std::ifstream(dir/"upgrade-rules.json"));
    assert(report["changed"]["stats"]==nlohmann::json::array({"hp"}) && report["changed"]["unit_caps"]==1);
    assert(report["patched_bytes"]==patches().size() && !patches().empty());
    put(stat_increments+2,200,2);put(stat_increments+stat_increments_stride+2,10,2);
    patch_resident(ram.data());
    assert(get(stat_increments,2)==0 && get(stat_increments+2,2)==300 && get(stat_increments+30,2)==300);
    assert(get(stat_increments+stat_increments_stride+2,2)==10);                 // EN untouched
    // Overlay DMA: previews and prices follow; a partial copy only gets its own bytes.
    patch_copy(ram.data(),overlay_rom,overlay_ram,0x18000);
    assert(get(stat_previews,2)==300 && get(stat_previews+28,2)==300 && get(stat_prices,4)==1234 && get(stat_prices+56,4)==1234);
    put(stat_prices,2000,4);
    patch_copy(ram.data(),overlay_rom_of(stat_prices)+4,0x80300000,8);
    assert(get(stat_prices,4)==2000 && get(0x80300000,4)==1234 && get(0x80300004,4)==1234 && get(0x80300008,4)==0);
    // Record reads: 800A5254 reads 36 bytes per unit, 800A642C 16 per weapon.
    patch_copy(ram.data(),unit_records+50*unit_record_size,0x80301000,unit_record_size);
    assert(get(0x80301000+record_cap,1)==9 && original_cap(50)==9);
    patch_copy(ram.data(),weapon_records+209*weapon_record_size,0x80302000,weapon_record_size);
    assert(get(0x80302000+record_type,1)==2 && original_type(209)==2);
    patch_copy(ram.data(),unit_records+51*unit_record_size,0x80303000,unit_record_size);
    assert(get(0x80303000+record_cap,1)==0);                                     // other units untouched
    // Weapon instances keep the type they were built with; 800A5254's wrapper refreshes it.
    const uint32_t unit=unit_at(0), list=0x80178F80;
    put(unit,1,1);put(unit+unit_number,50,2);put(unit+unit_weapon_count,2,1);put(unit+unit_weapon_list,list,4);
    put(list+weapon_number,209,2);put(list+weapon_type,1,1);
    put(list+weapon_size+weapon_number,211,2);put(list+weapon_size+weapon_type,original_type(211),1);
    before_recompute(ram.data(),unit);
    assert(get(list+weapon_type,1)==2 && get(list+weapon_size+weapon_type,1)==original_type(211));

    // Caps. Back to the original tables; slot 0 is νガンダム (cap 7), slot 1 a ガンダム (15).
    unsetenv("SRW64_UPGRADE_RULES");
    configure(dir);
    before_recompute(ram.data(),unit);                                           // no file: no refresh
    assert(get(list+weapon_type,1)==2);
    put(list+weapon_type,1,1);
    const uint32_t other=unit_at(1);
    put(other,1,1);put(other+unit_number,54,2);put(other+unit_cap,15,1);
    put(unit+unit_cap,7,1);
    for(unsigned stat=0;stat<stat_count;++stat)put(unit+unit_levels+stat,3,1);
    put(screen_unit,0,4);
    {
        rules::Override off(0);
        Scope scope(ram.data(),Policy::decide);
        assert(get(unit+unit_cap,1)==7 && get(other+unit_cap,1)==15);
    }
    {
        rules::Override on(rules::upgrade_cap_break);
        {
            Scope scope(ram.data(),Policy::decide);
            assert(get(unit+unit_cap,1)==15 && get(other+unit_cap,1)==15);
            {
                Scope ew(ram.data(),Policy::original);                           // EW check inside the step
                assert(get(unit+unit_cap,1)==7);
            }
            assert(get(unit+unit_cap,1)==15);
            put(unit+unit_cap,7,1);                                              // 800A5254 rewrites from ROM
            after_recompute(ram.data(),unit);
            assert(get(unit+unit_cap,1)==15);
        }
        assert(get(unit+unit_cap,1)==7);
        put(unit+unit_cap,7,1);
        after_recompute(ram.data(),unit);                                        // outside the screen: untouched
        assert(get(unit+unit_cap,1)==7);
    }
    // Rule off, a stat above the cap (left by an earlier session with the rule on):
    // the gauges widen to it, decisions keep the original cap.
    put(unit+unit_levels,12,1);
    {
        rules::Override off(0);
        {Scope scope(ram.data(),Policy::stats_view);assert(get(unit+unit_cap,1)==12);}
        {Scope scope(ram.data(),Policy::decide);assert(get(unit+unit_cap,1)==7);}
    }
    put(unit+unit_levels,3,1);
    // Weapon screen: the selected weapon is rows[row + 6 * (page - 1)].
    put(list+weapon_level,3,1);put(list+weapon_size+weapon_level,9,1);
    put(weapon_rows,0,2);put(weapon_rows+2,1,2);put(weapon_row_count,2,2);put(weapon_page,1,2);
    put(weapon_row,1,2);
    assert(selected_weapon(ram.data(),unit)==list+weapon_size);
    {rules::Override off(0);Scope scope(ram.data(),Policy::weapon);assert(get(unit+unit_cap,1)==9 && get(other+unit_cap,1)==15);}
    {rules::Override on(rules::upgrade_cap_break);Scope scope(ram.data(),Policy::weapon);assert(get(unit+unit_cap,1)==15);}
    put(weapon_row,0,2);
    {rules::Override off(0);Scope scope(ram.data(),Policy::weapon);assert(get(unit+unit_cap,1)==7);}
    put(weapon_row,2,2);assert(!selected_weapon(ram.data(),unit));              // past the list
    put(weapon_row,0,2);put(weapon_page,0,2);assert(!selected_weapon(ram.data(),unit));
    put(weapon_page,1,2);
    assert(get(unit+unit_cap,1)==7 && frames().empty());

    // Full-upgrade weapons: ν フィンファンネル (209) unlocks its MAP version.
    for(uint32_t row=0;row<19;++row)
        for(uint32_t i=0;i<3;++i)put(unlock_table+row*6+i*2,rom_read(overlay_rom_of(unlock_table)+row*6+i*2,2),2);
    assert(unlocks(ram.data(),50,209) && !unlocks(ram.data(),50,211) && !unlocks(ram.data(),54,209));
    {
        rules::Override on(rules::upgrade_cap_break);
        Scope scope(ram.data(),Policy::weapon);
        put(list+weapon_level,7,1);
        steer_unlock(ram.data(),unit,list);
        assert(get(unit+unit_cap,1)==7);                                         // fires at the original cap
        put(unit+unit_cap,15,1);put(list+weapon_level,8,1);
        steer_unlock(ram.data(),unit,list);
        assert(get(unit+unit_cap,1)==15);
        put(list+weapon_level,15,1);
        steer_unlock(ram.data(),unit,list);
        assert(get(unit+unit_cap,1)==16);                                        // not again at the raised cap
        put(unit+unit_cap,15,1);put(list+weapon_size+weapon_level,15,1);
        steer_unlock(ram.data(),unit,list+weapon_size);                          // no full-upgrade weapon
        assert(get(unit+unit_cap,1)==15);
    }
    {
        rules::Override off(0);
        put(list+weapon_level,7,1);put(weapon_row,0,2);
        Scope scope(ram.data(),Policy::weapon);
        assert(get(unit+unit_cap,1)==7);
        steer_unlock(ram.data(),unit,list);
        assert(get(unit+unit_cap,1)==7);                                         // original behaviour
    }
    steer_unlock(ram.data(),unit,list);                                          // outside the screen
    assert(get(unit+unit_cap,1)==7);

    // Sale price: base + 20000 at most, as the s16 the routine returns.
    put(sale_rows+5*4+2,15000,2);
    assert(clamp_sale_price(ram.data(),5,15000+12345)==15000+12345);
    assert(clamp_sale_price(ram.data(),5,uint32_t(int32_t(int16_t(uint16_t(15000+27272)))))==uint32_t(int32_t(int16_t(uint16_t(35000)))));
    assert(clamp_sale_price(ram.data(),12,40000)==40000);

    // Gauge strings: ν (cap 7) with the rule on, level 3 in a 15-cell gauge.
    uint32_t offset=0,size=0;
    put(list+weapon_level,3,1);put(list+weapon_size+weapon_level,3,1);
    {
        rules::Override off(0);
        Scope scope(ram.data(),Policy::stats_view);
        assert(!descriptor(ram.data(),0,0x1053,offset,size));                   // nothing special: original text
    }
    {
        rules::Override on(rules::upgrade_cap_break);
        assert(!descriptor(ram.data(),0,0x1034,offset,size));                   // outside the screen
        Scope scope(ram.data(),Policy::stats_view);
        assert(!descriptor(ram.data(),1,0x1034,offset,size) && !descriptor(ram.data(),0,0x1030,offset,size));
        assert(descriptor(ram.data(),0,0x1034,offset,size));
        assert(size==8+15*2+2 && offset==text_virtual+0x1034*text_slot-text_table);
        assert(read_text(text_table+offset,ram.data(),0x80304000,size));
        assert(get(0x80304000,4)==0x30303034 && get(0x80304004,4)==0x30303030);
        for(unsigned i=0;i<15;++i)
            assert(get(0x80304008+i*2,2)==(i<3?glyph_on:i<7?glyph_off:glyph_extra_off));
        assert(get(0x80304008+30,2)==0xFFFF);
        bool threw=false;
        try{read_text(text_table+offset,ram.data(),0x80304000,size-2);}catch(const std::runtime_error&){threw=true;}
        assert(threw);
        assert(!read_text(0x01A40000,ram.data(),0x80304000,4));
    }
    {
        // Rule off, HP at 12: that row is drawn to 12 with the extra cells filled,
        // a row within the cap keeps the original width.
        rules::Override off(0);
        put(unit+unit_levels,12,1);
        Scope scope(ram.data(),Policy::stats_view);
        assert(descriptor(ram.data(),0,state().gauge_ids[12-min_cap][12],offset,size));
        assert(size==8+12*2+2);
        read_text(text_table+offset,ram.data(),0x80305000,size);
        for(unsigned i=0;i<12;++i)assert(get(0x80305008+i*2,2)==(i<7?glyph_on:glyph_extra_on));
        assert(descriptor(ram.data(),0,state().gauge_ids[12-min_cap][3],offset,size) && size==8+7*2+2);
        put(unit+unit_levels,3,1);
    }
    std::puts("upgrade rules: ok");
    return 0;
}
