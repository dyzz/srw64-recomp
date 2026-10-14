#include "cheats.hpp"
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

    // SP and morale: records in use; morale skips the 0xC0 pilots.
    const uint32_t p=pilots_table,q=pilots_table+pilot_size,unused=pilots_table+2*pilot_size;
    put(p,1,1);put(p+0x16,3,2);put(p+0x18,80,2);put(p+0x20,100,2);
    put(q,1,1);put(q+4,0x40,1);put(q+0x20,100,2);
    put(unused+0x20,55,2);
    hold(ram.data(),keep_sp|morale_150);
    assert(get(p+0x16,2)==80 && get(p+0x20,2)==150);
    assert(get(q+0x20,2)==100 && get(unused+0x20,2)==55);

    // A level and the experience 800AC220 gives it; out-of-range levels are clamped.
    write_level(ram.data(),p,12);assert(get(p+5,1)==12 && get(p+0x12,2)==5500);
    write_level(ram.data(),p,0);assert(get(p+5,1)==1 && get(p+0x12,2)==0);
    write_level(ram.data(),p,150);assert(get(p+5,1)==99 && get(p+0x12,2)==49000);
    std::puts("native cheats: ok");
}
