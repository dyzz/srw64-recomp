#include "script_skip.hpp"
#include <cassert>
#include <cstring>
#include <vector>
// The short skip's command table (script_skip.hpp): which commands it stops after, and
// how each waiting command is brought to its end in RAM.
int main() {
    using namespace srw64::script_skip;
    std::vector<uint8_t> ram(0x800000);
    auto put=[&](uint32_t p,uint32_t v,unsigned n){for(unsigned i=0;i<n;++i)ram[((p&0x1FFFFFFF)+i)^3]=uint8_t(v>>((n-i-1)*8));};
    auto get=[&](uint32_t p,unsigned n){return read(ram.data(),p,n);};
    constexpr uint32_t vm=0x8015F950+0x948;
    auto command=[&](uint32_t handler){std::memset(ram.data()+((vm+0x20)&0x1FFFFFFF),0,0x20);put(vm+0x30,handler,4);put(vm+0x24,2,2);};

    // Start, end, ownership and the stop handed to the reader.
    assert(!active() && !skipping(vm));
    start(vm);assert(active() && skipping(vm) && !skipping(vm+4) && !skipping(0) && state().fresh);
    state().stop="choice";assert(std::string(take_stop())=="choice" && !take_stop());
    end();assert(!active() && !state().fresh);

    // The six dialogue handlers, 0x1C apart, and nothing between them.
    for(uint32_t h=handler::dialogue_first;h<=handler::dialogue_last;h+=0x1C)assert(dialogue(h));
    assert(!dialogue(handler::dialogue_first+4) && !dialogue(handler::dialogue_last+0x1C) && !dialogue(handler::wait));

    // Stop points: after them the player acts or the story leaves the scene.
    assert(std::string(stop_point(handler::choice))=="choice" && std::string(stop_point(handler::sortie))=="sortie");
    assert(std::string(stop_point(handler::battlefield))=="battlefield" && std::string(stop_point(handler::victory))=="victory");
    assert(stop_point(handler::game_over) && stop_point(handler::ending) && !stop_point(handler::wait) && !stop_point(0));

    // 3D38: its count goes to 0 once the first call has read it, so the next call ends it.
    command(handler::wait);put(vm+0x2E,45,2);
    assert(finish(ram.data(),vm)==Finish::once && get(vm+0x2E,2)==45);  // not started: untouched
    put(vm+0x26,1,2);assert(finish(ram.data(),vm)==Finish::once && get(vm+0x2E,2)==0);
    // 3D47: counts up to 32; the next call reaches it.
    command(handler::close_wait);put(vm+0x26,1,2);put(vm+0x2E,3,2);
    assert(finish(ram.data(),vm)==Finish::once && get(vm+0x2E,2)==0x1F);
    // 3D4E ends on its second call; a dialogue page is answered by 8008FED4.
    command(handler::close_yield);assert(finish(ram.data(),vm)==Finish::once);
    command(handler::dialogue_first+0x1C);assert(finish(ram.data(),vm)==Finish::once);

    // 3D3B: the fade engine's record 1 takes its target alpha and stops running.
    command(handler::fade);put(fade_record+4,1,1);put(fade_record+5,0xFF,1);put(fade_record+0xC,0x30,1);
    assert(finish(ram.data(),vm)==Finish::once && get(fade_record+4,1)==0 && get(fade_record+0xC,1)==0xFF);
    put(fade_record+0xC,0x40,1);assert(finish(ram.data(),vm)==Finish::once && get(fade_record+0xC,1)==0x40);  // idle: untouched

    // 3D36 is stepped on any map; 3D45 and 3D3C call the tactical overlay and are hurried
    // only there (after 3D4D the world map is still in for some frames).
    command(handler::shake);assert(finish(ram.data(),vm)==Finish::steps);
    command(handler::deploy);put(0x8015DA02,1,1);
    assert(finish(ram.data(),vm)==Finish::no && get(vm+0x28,2)==0 && get(vm+0x2E,2)==0);
    command(handler::move);assert(finish(ram.data(),vm)==Finish::no);
    put(0x8015DA02,3,1);
    command(handler::deploy);assert(finish(ram.data(),vm)==Finish::steps && get(vm+0x28,2)==9 && get(vm+0x2E,2)==9);
    command(handler::deploy);put(vm+0x26,2,2);put(vm+0x28,0x1C,2);  // phase 2: +0x28 indexes the records
    assert(finish(ram.data(),vm)==Finish::steps && get(vm+0x28,2)==0x1C && get(vm+0x2E,2)==9);
    put(0x8015DA02,0xB,1);command(handler::move);assert(finish(ram.data(),vm)==Finish::motion);

    // Anything else runs its own time.
    command(0x800A0E64);assert(finish(ram.data(),vm)==Finish::no);  // 3D32
    command(0x800A10F0);assert(finish(ram.data(),vm)==Finish::no);  // 3D49
    return 0;
}
