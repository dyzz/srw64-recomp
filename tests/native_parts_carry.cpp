#include "parts_carry.hpp"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
uint64_t srw64_current_vi(){return 0;}
using namespace srw64::parts_carry;
namespace rules=srw64::rules;

static std::vector<uint8_t> ram(0x800000);
static void put(uint32_t p,uint32_t v,unsigned n){for(unsigned i=0;i<n;++i)ram[((p&0x1FFFFFFF)+i)^3]=uint8_t(v>>((n-i-1)*8));}
static uint32_t get(uint32_t p,unsigned n){return read(ram.data(),p,n);}

// A machine in the player's pool with its part slots empty.
static uint32_t machine(unsigned slot,uint16_t number,unsigned slots) {
    const uint32_t unit=unit_at(slot);
    put(unit,1,1);put(unit+unit_number,number,2);put(unit+unit_slot_count,slots,1);put(unit+unit_equipped,0,1);
    for(unsigned s=0;s<max_slots;++s)put(unit+unit_parts+s,0xFF,1);
    return unit;
}
static void pilot(unsigned slot,int16_t actor,uint32_t unit) {
    const uint32_t record=pilots+slot*pilot_size;
    put(record,1,1);put(record+pilot_number,uint16_t(actor),2);
    put(record+pilot_has_unit,unit?1:0,1);put(record+pilot_unit,unit,4);
}
static void equip(uint32_t unit,unsigned slot,uint8_t part) {
    put(unit+unit_parts+slot,part,1);
    put(unit+unit_equipped,get(unit+unit_equipped,1)+1,1);
    put(inventory+uint32_t(part)*2+1,get(inventory+uint32_t(part)*2+1,1)+1,1);
}
// What 800A9D60 does when the original frees the old machine.
static void strip(uint32_t unit) {
    const unsigned slots=get(unit+unit_slot_count,1);
    for(unsigned s=0;s<slots;++s) {
        const int8_t part=int8_t(get(unit+unit_parts+s,1));
        if(part<0)continue;
        put(inventory+uint32_t(part)*2+1,get(inventory+uint32_t(part)*2+1,1)-1,1);
        put(unit+unit_parts+s,0xFF,1);
    }
    put(unit+unit_equipped,0,1);
}
static void own(uint8_t part,unsigned count){put(inventory+uint32_t(part)*2,count,1);put(inventory+uint32_t(part)*2+1,0,1);}
static unsigned equipped_count(uint8_t part){return get(inventory+uint32_t(part)*2+1,1);}

int main(int argc,char** argv) {
    assert(argc==2);
    const std::filesystem::path dir=argv[1];
    std::filesystem::create_directories(dir);
    configure(dir);
    assert(rules::parse("parts-carry-over")==rules::parts_carry_over);

    // ウイングゼロ (2 slots) with two parts, swapping to ウイングゼロカスタム (1 slot).
    own(3,1);own(5,1);
    const uint32_t old_unit=machine(0,119,2), successor=machine(1,121,1);
    pilot(0,95,old_unit);
    equip(old_unit,0,3);equip(old_unit,1,5);
    assert(equipped_count(3)==1 && equipped_count(5)==1);

    {   // Rule off: nothing is captured, and the parts stay in the inventory.
        rules::Override off(0);
        const auto carry=capture(ram.data(),95,121);
        assert(carry.empty());
        strip(old_unit);
        assert(!apply(ram.data(),carry));
        assert(equipped_count(3)==0 && equipped_count(5)==0);
        assert(int8_t(get(successor+unit_parts,1))<0);
    }
    equip(old_unit,0,3);equip(old_unit,1,5);

    rules::Override on(rules::parts_carry_over);
    {   // Rule on: both parts are noted, the first fits, the second stays behind.
        const auto carry=capture(ram.data(),95,121);
        assert(carry.count==2 && carry.parts[0]==3 && carry.parts[1]==5 && carry.equipped==2);
        strip(old_unit);
        const auto carried=apply(ram.data(),carry);
        assert(carried && carried->unit==121);
        assert(carried->parts==std::vector<uint8_t>{3} && carried->left==std::vector<uint8_t>{5});
        assert(get(successor+unit_parts,1)==3 && get(successor+unit_equipped,1)==1);
        assert(equipped_count(3)==1 && equipped_count(5)==0);
        record(carry,*carried);
        std::ifstream log(dir/"parts-carry-events.jsonl");
        std::string line;std::getline(log,line);
        assert(line.find("\"from\":119")!=std::string::npos && line.find("\"to\":121")!=std::string::npos);
    }

    // A successor with as many slots takes everything.
    machine(1,121,2);put(inventory+3*2+1,0,1);
    equip(old_unit,0,3);equip(old_unit,1,5);
    {
        const auto carry=capture(ram.data(),95,121);
        strip(old_unit);
        const auto carried=apply(ram.data(),carry);
        assert(carried && carried->parts.size()==2 && carried->left.empty());
        assert(get(successor+unit_parts,1)==3 && get(successor+unit_parts+1,1)==5);
        assert(equipped_count(3)==1 && equipped_count(5)==1);
    }

    // The original returned early (the pilot already had this machine): the parts
    // are still on the old one, so nothing is equipped twice.
    machine(1,121,2);put(inventory+3*2+1,1,1);put(inventory+5*2+1,1,1);
    {
        const auto carry=capture(ram.data(),95,121);
        const auto carried=apply(ram.data(),carry);
        assert(!carried);
        assert(int8_t(get(successor+unit_parts,1))<0);
        assert(equipped_count(3)==1 && equipped_count(5)==1);
    }

    // Owning fewer than the inventory says are equipped, no successor, no pilot,
    // and a pilot with no machine: all leave memory alone.
    strip(old_unit);equip(old_unit,0,3);
    {
        const auto carry=capture(ram.data(),95,121);
        strip(old_unit);
        own(3,0);
        assert(!apply(ram.data(),carry));
        own(3,1);
        put(unit_at(1),0,1);                      // successor not in use
        assert(!apply(ram.data(),carry));
        put(unit_at(1),1,1);
        assert(apply(ram.data(),carry));
    }
    assert(capture(ram.data(),999,121).empty());   // registration without a pilot
    pilot(0,95,0);
    assert(capture(ram.data(),95,121).empty());
    return 0;
}
