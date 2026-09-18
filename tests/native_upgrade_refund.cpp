#include "upgrade_refund.hpp"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
uint64_t srw64_current_vi(){return 0;}
using namespace srw64::refund;
namespace rules=srw64::rules;
namespace upgrades=srw64::upgrades;

static std::vector<uint8_t> ram(0x800000);
static void put(uint32_t p,uint32_t v,unsigned n){for(unsigned i=0;i<n;++i)ram[((p&0x1FFFFFFF)+i)^3]=uint8_t(v>>((n-i-1)*8));}
static uint32_t get(uint32_t p,unsigned n){return read(ram.data(),p,n);}
static uint32_t slot(unsigned n){return units+n*unit_size;}
// A machine in the player's pool: number, five stat levels, and weapons as
// (type, level), placed in the weapon pool.
static void machine(unsigned n,uint16_t number,std::vector<unsigned> stats,std::vector<std::pair<unsigned,unsigned>> weapons,
                    unsigned cap=9) {
    const uint32_t unit=slot(n), list=0x80178F80+n*0x24*8;
    put(unit,1,1);put(unit+unit_number,number,2);put(unit+unit_cap,cap,1);
    for(unsigned s=0;s<5;++s)put(unit+unit_levels+s,stats[s],1);
    put(unit+unit_weapon_count,uint32_t(weapons.size()),1);put(unit+unit_weapon_list,list,4);
    for(size_t i=0;i<weapons.size();++i) {
        put(list+uint32_t(i)*weapon_size+weapon_type,weapons[i].first,1);
        put(list+uint32_t(i)*weapon_size+weapon_level,weapons[i].second,1);
    }
}
static size_t log_lines(const std::filesystem::path& dir) {
    std::ifstream in(dir/"upgrade-refund-events.jsonl");
    size_t count=0;
    for(std::string line;std::getline(in,line);)++count;
    return count;
}

int main(int argc,char** argv) {
    assert(argc==2);
    const std::filesystem::path dir=argv[1];
    std::filesystem::create_directories(dir);
    // The original price curves (docs/gameplay/upgrade-limits.md section 3): HP
    // 2000 x (n+1), weapon type t (1..4) (5-t) x 1000 x (n+1); the others are only
    // needed to differ.
    auto& tables=upgrades::state().active;
    for(unsigned n=0;n<upgrades::levels;++n) {
        for(unsigned s=0;s<5;++s)tables.stats[s].prices[n]=(s+1)*1000*(n+1);
        for(unsigned t=0;t<4;++t)tables.weapons[t].prices[n]=(4-t)*1000*(n+1);
    }
    assert(curve_cost(tables.stats[0],0,3)==1000+2000+3000 && curve_cost(tables.stats[0],2,2)==0);
    assert(curve_cost(tables.stats[0],0,20)==curve_cost(tables.stats[0],0,15));

    // HP 2, EN 1; one type-1 weapon at 2 (4000+8000), one type-0 weapon at 5 (free).
    machine(0,124,{2,1,0,0,0},{{1,2},{0,5}});
    const uint32_t expected_stats=(1000+2000)+2000, expected_weapons=4000+8000;
    const auto cost=assess(ram.data(),slot(0),tables);
    assert(cost.stats==expected_stats && cost.weapons==expected_weapons && cost.free_levels==0);

    // Story grants: 3D6C gave ウイングゼロ (119) three levels. Everything at 3 or
    // more: the first three are free. One item below 3: the grant never happened on
    // this instance, so nothing is free (levels carried over from a predecessor).
    machine(1,119,{3,5,3,3,4},{{2,3},{2,6}});
    const auto granted=assess(ram.data(),slot(1),tables);
    assert(granted.free_levels==3);
    assert(granted.stats==curve_cost(tables.stats[1],3,5)+curve_cost(tables.stats[4],3,4));
    assert(granted.weapons==curve_cost(tables.weapons[1],3,6));
    machine(2,119,{3,5,3,3,4},{{2,1},{2,6}});
    assert(assess(ram.data(),slot(2),tables).free_levels==0);
    machine(2,128,{2,2,2,2,2},{{1,2},{2,2}});   // デスサイズH (grant 3) with levels bought on デスサイズ
    assert(assess(ram.data(),slot(2),tables).free_levels==0 && assess(ram.data(),slot(2),tables).total()>0);
    machine(2,295,{7,7,7,7,7},{{1,7}},7);        // grant 8 above a cap of 7: 3D6C wrote 7
    assert(assess(ram.data(),slot(2),tables).free_levels==7 && assess(ram.data(),slot(2),tables).total()==0);
    assert(story_grant(119)==3 && story_grant(295)==8 && story_grant(0)==0);

    const uint32_t start=500000;
    put(funds,start,4);
    setenv("SRW64_RULE_FIXES","",1);
    rules::configure(dir);
    configure(dir);
    // Rule off: nothing, even inside a removal.
    {
        Removal removal(Source::removal);
        assert(!before_delete(ram.data(),slot(0)));
    }
    assert(get(funds,4)==start && log_lines(dir)==0);

    rules::set_fixes(rules::upgrade_refund);
    // Outside a story routine (a sale, a map removal): nothing.
    assert(!before_delete(ram.data(),slot(0)) && get(funds,4)==start);
    {
        Removal removal(Source::removal);
        const auto paid=before_delete(ram.data(),slot(0));
        assert(paid && paid->unit==124 && paid->cost.total()==expected_stats+expected_weapons);
        assert(paid->funds_before==start && paid->funds_after==start+paid->cost.total());
        assert(get(funds,4)==paid->funds_after);
        // Another pool, or an address inside a slot: not the player's machine.
        assert(!before_delete(ram.data(),units+unit_slots*unit_size) && !before_delete(ram.data(),slot(0)+4));
    }
    assert(context().source==Source::none);
    assert(log_lines(dir)==1);

    // A registration inherits from the first in-use instance of the predecessor
    // (800AA814 table: successor, predecessor); deleting that one pays nothing
    // but is logged. Another instance with the same number is still refunded.
    put(predecessors,126,2);put(predecessors+2,124,2);
    machine(3,124,{1,0,0,0,0},{});
    assert(predecessor_instance(ram.data(),126)==slot(0) && predecessor_instance(ram.data(),125)==0);
    put(funds,start,4);
    {
        Registration registration(ram.data(),126);
        Removal removal(Source::removal);
        assert(!before_delete(ram.data(),slot(0)) && get(funds,4)==start);
        const auto other=before_delete(ram.data(),slot(3));
        assert(other && other->cost.total()==1000 && get(funds,4)==start+1000);
    }
    assert(context().inherited==0 && log_lines(dir)==3);
    {
        std::ifstream in(dir/"upgrade-refund-events.jsonl");
        std::string line;
        std::getline(in,line);std::getline(in,line);
        const auto row=nlohmann::json::parse(line);
        assert(row["schema"]=="srw64.upgrade-refund.v1" && row["inherited"]==true && row["refund"]==0 && row["unit"]==124);
    }
    // A merge and a machine with nothing bought: logged, no payment, no notice.
    machine(4,59,{0,0,0,0,0},{{1,0}});
    {
        Removal merge(Source::merge);
        assert(!before_delete(ram.data(),slot(4)));
    }
    assert(log_lines(dir)==4);
    // Funds saturate instead of wrapping.
    put(funds,0xFFFFFF00,4);
    {
        Removal removal(Source::removal);
        const auto paid=before_delete(ram.data(),slot(1));
        assert(paid && paid->funds_after==0xFFFFFFFF && get(funds,4)==0xFFFFFFFF);
    }

    // Machine names: text table 0 from id 527, 8-byte header, glyphs, FFFF.
    std::vector<uint8_t> rom(0x2000000);
    auto rom_put=[&](uint32_t at,uint32_t v,unsigned n){for(unsigned i=0;i<n;++i)rom[at+i]=uint8_t(v>>((n-i-1)*8));};
    rom_put(upgrades::text_table+4+(unit_name_base+124)*8,0x100,4);
    for(unsigned i=0;i<8;++i)rom_put(upgrades::text_table+0x100+i,0xAA,1);
    rom_put(upgrades::text_table+0x108,0x0123,2);rom_put(upgrades::text_table+0x10A,0x0456,2);rom_put(upgrades::text_table+0x10C,0xFFFF,2);
    assert(unit_name(124).empty());   // no ROM yet
    upgrades::state().rom=rom.data();upgrades::state().rom_size=rom.size();
    assert((unit_name(124)==std::vector<uint16_t>{0x0123,0x0456}) && unit_name(400).empty());
    return 0;
}
