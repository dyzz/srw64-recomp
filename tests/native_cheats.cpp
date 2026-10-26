#include "cheats.hpp"
#include "overkill.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>
using namespace srw64::cheats;

static std::vector<uint8_t> ram(0x800000);
static void put(uint32_t p,uint32_t v,unsigned n){for(unsigned i=0;i<n;++i)ram[((p&0x1FFFFFFF)+i)^3]=uint8_t(v>>((n-i-1)*8));}
static uint32_t get(uint32_t p,unsigned n){return srw64::guest::read(ram.data(),p,n);}

int main() {
    // Nothing is written while every switch is off.
    put(funds,1234,4);hold(ram.data(),0);assert(get(funds,4)==1234);

    hold(ram.data(),max_funds);assert(get(funds,4)==99999999);

    // Held counts go to 15, the most a save keeps; a fitted count is never left above the held one.
    put(parts+0,2,1);put(parts+1,1,1);put(parts+2*5,17,1);put(parts+2*5+1,17,1);
    hold(ram.data(),max_parts);
    assert(get(parts,1)==15 && get(parts+1,1)==1);
    assert(get(parts+2*5,1)==17 && get(parts+2*5+1,1)==17);
    assert(get(parts+2*part_kinds,2)==0);   // E9B4 on (the drop thresholds) untouched

    // EN: machines in use only, set to their maximum even when above it.
    const uint32_t a=units,b=units+unit_size,empty=units+2*unit_size;
    put(a,1,1);put(a+8,10,2);put(a+0xA,200,2);
    put(b,1,1);put(b+8,230,2);put(b+0xA,200,2);
    put(empty+8,7,2);put(empty+0xA,99,2);
    hold(ram.data(),keep_en);
    assert(get(a+8,2)==200 && get(b+8,2)==200 && get(empty+8,2)==7);

    // Four slots: machines in use only.
    put(a+unit_slot_count,2,1);put(empty+unit_slot_count,1,1);
    hold(ram.data(),four_slots);
    assert(get(a+unit_slot_count,1)==4 && get(empty+unit_slot_count,1)==1);

    // Back to the ROM count: the parts above it return to the inventory, those below stay.
    // a: two own slots, parts 3 and 5 in slots 0 and 2, part 5 again in slot 3, 0xFF in 1.
    put(a+unit_parts,3,1);put(a+unit_parts+1,0xFF,1);put(a+unit_parts+2,5,1);put(a+unit_parts+3,5,1);
    put(a+unit_fitted,3,1);
    put(parts+2*3,15,1);put(parts+2*3+1,4,1);put(parts+2*5,15,1);put(parts+2*5+1,6,1);
    assert(release_slots(ram.data(),a,2));
    assert(get(a+unit_slot_count,1)==2 && get(a+unit_fitted,1)==1);
    assert(get(a+unit_parts,1)==3 && get(a+unit_parts+2,1)==0xFF && get(a+unit_parts+3,1)==0xFF);
    assert(get(parts+2*3+1,1)==4 && get(parts+2*5+1,1)==4 && get(parts+2*5,1)==15);
    // Already at its count, or not in use: nothing to do.
    assert(!release_slots(ram.data(),a,2) && !release_slots(ram.data(),empty,0));
    // A form that took the same slots (800A5924) only has its count put back.
    put(b+unit_slot_count,4,1);put(b+unit_fitted,1,1);
    for(unsigned n=0;n<4;++n)put(b+unit_parts+n,get(a+unit_parts+n,1),1);
    assert(release_slots(ram.data(),b,2));
    assert(get(b+unit_slot_count,1)==2 && get(b+unit_fitted,1)==1 && get(parts+2*5+1,1)==4);
    // After a load: the count is the ROM's again, but the parts above it came back.
    put(b+unit_parts+2,9,1);put(b+unit_fitted,2,1);put(parts+2*9+1,3,1);
    assert(release_slots(ram.data(),b,2));
    assert(get(b+unit_slot_count,1)==2 && get(b+unit_fitted,1)==1 && get(b+unit_parts+2,1)==0xFF && get(parts+2*9+1,1)==2);
    // Counts never wrap below zero.
    put(a+unit_slot_count,4,1);put(a+unit_parts+3,7,1);put(a+unit_fitted,0,1);put(parts+2*7+1,0,1);
    assert(release_slots(ram.data(),a,2) && get(a+unit_fitted,1)==0 && get(parts+2*7+1,1)==0);

    // Level curve: 3 + 0.9 a chapter cleared, at most 50; nothing written while off.
    assert(curve_level(0)==3 && curve_level(1)==3 && curve_level(10)==12 && curve_level(30)==30);
    assert(curve_level(52)==49 && curve_level(53)==50 && curve_level(120)==50);
    put(base_level,41,1);put(chapters_cleared,10,1);
    hold(ram.data(),max_funds);assert(get(base_level,1)==41);
    hold(ram.data(),level_curve);assert(get(base_level,1)==12 && get(chapters_cleared,1)==10);

    // Match party: the top 15 in the party with a machine the party has, no cap.
    {
        std::vector<uint8_t> keep=ram;
        std::fill(ram.begin(),ram.end(),0);
        const uint32_t unit_in=units+10*unit_size,unit_out=units+11*unit_size,getter=units+12*unit_size;
        put(unit_in+0xC,0x80,1);put(getter+unit_number,0xAB,2);
        for(unsigned i=0;i<20;++i) {   // levels 99, 98 ... 80 on the party's machine
            const uint32_t pilot=pilots_table+i*pilot_size;
            put(pilot,1,1);put(pilot+5,99-i,1);put(pilot+0x37,1,1);put(pilot+0x38,unit_in,4);
        }
        const uint32_t away=pilots_table+20*pilot_size,benched=pilots_table+21*pilot_size,getter_pilot=pilots_table+22*pilot_size;
        put(away,0x81,1);put(away+5,1,1);put(away+0x37,1,1);put(away+0x38,unit_in,4);        // left the party
        put(benched,1,1);put(benched+5,1,1);put(benched+0x37,1,1);put(benched+0x38,unit_out,4);   // a machine not counted
        put(getter_pilot,1,1);put(getter_pilot+5,100-1,1);put(getter_pilot+0x37,1,1);put(getter_pilot+0x38,getter,4);
        assert(party_level(ram.data())==(99+99+98+97+96+95+94+93+92+91+90+89+88+87+86)/15);
        put(base_level,41,1);put(chapters_cleared,1,1);
        hold(ram.data(),level_party);assert(get(base_level,1)==party_level(ram.data()));
        hold(ram.data(),level_party|level_curve);assert(get(base_level,1)==3);   // the curve wins if both
        std::fill(ram.begin(),ram.end(),0);
        assert(party_level(ram.data())==1);
        ram=keep;
    }
    assert(!toggle(level_curve) && !toggle(level_party) && toggle(max_parts));

    // Unlimited moves: player handles only (0x42 + slot), and only with the switch on.
    assert(keeps_action(unlimited_moves,0x42) && keeps_action(unlimited_moves,0x42+29));
    assert(!keeps_action(unlimited_moves,0x60) && !keeps_action(unlimited_moves,0x7E + 3) && !keeps_action(unlimited_moves,0x41));
    assert(!keeps_action(max_parts,0x42));

    // SP and morale: records in use; morale skips the 0xC0 pilots.
    const uint32_t p=pilots_table,q=pilots_table+pilot_size,unused=pilots_table+2*pilot_size;
    put(p,1,1);put(p+0x16,3,2);put(p+0x18,80,2);put(p+0x20,100,2);
    put(q,1,1);put(q+4,0x40,1);put(q+0x20,100,2);
    put(unused+0x20,55,2);
    hold(ram.data(),keep_sp|keep_morale);
    assert(get(p+0x16,2)==80 && get(p+0x20,2)==150);
    assert(get(q+0x20,2)==100 && get(unused+0x20,2)==55);
    hold(ram.data(),keep_morale|morale_300);assert(get(p+0x20,2)==300);

    // The morale cap: the player's pilot records only, the rest keep 150; the spirit raise's
    // room stays within a signed byte.
    assert(morale_cap(0)==150 && morale_cap(morale_200)==200 && morale_cap(morale_300)==300 && morale_cap(morale_999)==999);
    assert(morale_cap(morale_999,p)==999 && morale_cap(morale_999,pilots_table+99*pilot_size)==999);
    assert(morale_cap(morale_999,pilots_table+100*pilot_size)==150 && morale_cap(morale_999,p+0x20)==150 && morale_cap(morale_999,0x80190000)==150);
    assert(morale_room(0,p,100)==150 && morale_room(morale_999,p,100)==227 && morale_room(morale_999,p,950)==999);
    assert(morale_room(morale_999,0x80190000,100)==150);

    // Exclusive rows: one bit of each group stays, the lowest; EXP and money are apart.
    assert(one_per_group(exp_x2|exp_x10|max_funds)==(exp_x2|max_funds));
    assert(one_per_group(exp_x3|money_x1_5|money_x5)==(exp_x3|money_x1_5));
    assert(one_per_group(morale_300|morale_999|level_party|level_curve)==(morale_300|level_curve));
    assert(!toggle(exp_x5) && !toggle(money_x1_5) && !toggle(morale_200) && toggle(real_damage) && toggle(keep_morale));

    // EXP and money: each its own multiplier in halves, kept within the u16 the settlement stores.
    assert(exp_halves(0)==2 && exp_halves(exp_x1_5)==3 && exp_halves(exp_x3)==6 && exp_halves(exp_x10)==20 && exp_halves(money_x10)==2);
    assert(money_halves(0)==2 && money_halves(money_x2)==4 && money_halves(money_x5)==10 && money_halves(exp_x5)==2);
    assert(multiplied(2,1234)==1234 && multiplied(3,1235)==1852 && multiplied(10,1234)==6170 && multiplied(20,9000)==0xFFFF);
    assert(multiplied(4,0xFFFF0010)==0x20);   // only the u16 counts

    // Saved ids: the current ones, and the earlier shared multiplier as both rows.
    assert(bits_of("exp1.5")==exp_x1_5 && bits_of("money10")==money_x10 && bits_of("funds")==max_funds && bits_of("nope")==0);
    assert(bits_of("x2")==(exp_x2|money_x2) && bits_of("x5")==(exp_x5|money_x5) && bits_of("x10")==(exp_x10|money_x10));

    // Damage past 65535: kept per slot with the switch on, taken through the defenses.
    {
        namespace ok=srw64::overkill;
        ok::begin();ok::seen(150000.7f);ok::end(0,true);assert(ok::real(0)==150000);
        assert(ok::held(0,0xFFFF)==0xFFFF);
        assert(ok::defended(0,0xFFFF,0xFFFF-5000)==0xFFFF && ok::real(0)==145000);   // a barrier
        assert(ok::defended(0,0xFFFF,ok::halved(0xFFFF))==0xFFFF && ok::real(0)==72500);   // S防御
        assert(ok::defended(0,0xFFFF,ok::halved(0xFFFF))==36250 && ok::real(0)==36250);   // below the cap again
        ok::begin();ok::seen(70000);ok::end(1,false);assert(ok::real(1)==0);   // switch off
        ok::begin();ok::seen(60000);ok::end(1,true);assert(ok::real(1)==0);    // under the cap
        assert(ok::defended(1,30000,15000)==15000);
        ok::begin();ok::seen(80000);ok::end(1,true);ok::spared(1,0xFFFF,0xFFFF);assert(ok::real(1)==80000);
        ok::spared(1,0xFFFF,1234);assert(ok::real(1)==0 && ok::held(1,1234)==1234);
        ok::begin();ok::seen(80000);ok::end(1,true);
        assert(ok::defended(1,0xFFFF,0)==0 && ok::real(1)==0);   // absorbed
        assert(ok::halved(15)==10 && ok::halved(65535)==32760);
    }
    {
        // The figures: the map's queue matched by target and queued value, the battle's dealt damage.
        namespace ok=srw64::overkill;
        const uint32_t unit=units+5*unit_size,ward=units+6*unit_size;
        put(ok::roster+ok::roster_side+3*ok::roster_entry+ok::roster_unit,unit,4);put(unit+2,40,2);   // enemy 0x63
        put(ok::roster+ok::roster_side+4*ok::roster_entry+ok::roster_unit,ward,4);put(ward+2,0xB9,2);  // ガイヤー 0x64
        put(ok::round_count,1,1);put(ok::round_cells,0x63,1);put(ok::round_damage,1200,2);
        ok::queued(ram.data(),0x63,9000,0);
        put(ok::round_count,2,1);put(ok::round_cells+1,0x64,1);put(ok::round_damage+2,790,2);
        ok::queued(ram.data(),0x64,5000,1);
        put(ok::round_index,0,4);assert(ok::map_figure(ram.data(),1200)==9000);
        put(ok::round_index,1,4);assert(ok::map_figure(ram.data(),790)==790);
        put(ok::round_index,0,4);assert(ok::map_figure(ram.data(),1300)==1300);   // no such entry
        ok::queued(ram.data(),0x63,9000,0);assert(ok::figures().empty());           // a new queue, nothing pushed
        put(ok::battle_target,0,2);put(ok::battle_records+ok::battle_record_size+ok::battle_record_damage,30000,2);
        assert(ok::battle_figure(ram.data(),800)==30000 && ok::battle_figure(ram.data(),0)==0);
        put(ok::battle_target,1,2);put(ok::battle_records+ok::battle_record_damage,500,2);
        assert(ok::battle_figure(ram.data(),800)==800);

        // Past 65535: the attacker's kept damage (row 0 attacks the target in row 1).
        put(ok::participants,0x42,2);put(ok::participants+ok::participant_size,0x63,2);
        ok::begin();ok::seen(123456);ok::end(0,true);ok::begin();ok::seen(500);ok::end(1,true);
        put(ok::round_count,1,1);put(ok::round_cells,0x63,1);put(ok::round_damage,3000,2);
        ok::queued(ram.data(),0x63,0xFFFF,0);
        put(ok::round_index,0,4);assert(ok::map_figure(ram.data(),3000)==123456);
        put(ok::field(0),0xFFFF,2);put(ok::field(1),500,2);
        put(ok::battle_target,1,2);put(ok::battle_records+ok::battle_record_damage,0xFFFF,2);
        assert(ok::battle_figure(ram.data(),3000)==123456);
        // Both rows past it: the record row 0 was copied into (D_80178C78) decides.
        ok::begin();ok::seen(99999);ok::end(1,true);put(ok::field(1),0xFFFF,2);
        put(ok::battle_row0_record,0,2);assert(ok::battle_figure(ram.data(),3000)==123456);
        put(ok::battle_row0_record,1,2);assert(ok::battle_figure(ram.data(),3000)==99999);
    }

    // A level and the experience 800AC220 gives it; out-of-range levels are clamped.
    write_level(ram.data(),p,12);assert(get(p+5,1)==12 && get(p+0x12,2)==5500);
    write_level(ram.data(),p,0);assert(get(p+5,1)==1 && get(p+0x12,2)==0);
    write_level(ram.data(),p,150);assert(get(p+5,1)==99 && get(p+0x12,2)==49000);
    std::puts("native cheats: ok");
}
