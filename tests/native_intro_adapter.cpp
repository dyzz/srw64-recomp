// Exercise the real guest-memory adapter with stubbed guest cleanup/finish.
#include "native_intro.hpp"
#include "game_hooks.hpp"
#include <cassert>
#include <filesystem>
#include <vector>
#include <unistd.h>

SRW64GameHooks srw64_game_hooks;
uint64_t srw64_current_vi() {return 60;}
unsigned cleared{},finished{},slot{};
extern "C" void resident_func_8008B888(uint8_t*,recomp_context* ctx) {
    ++cleared;slot=ctx->r4;ctx->r4=0x1234;
}
extern "C" void load_0010DA50_func_801CA5B4(uint8_t* rdram,recomp_context* ctx) {
    ++finished;ctx->r4=0x5678;
    MEM_B(0,int32_t(0x8015DA02))=12;
}
int main() {
    using namespace srw64::intro;
    const auto out=std::filesystem::temp_directory_path()/("srw64-intro-test-"+std::to_string(getpid()));
    assert(std::filesystem::create_directory(out));
    configure(out);
    std::vector<uint8_t> memory(0x800000);
    auto* rdram=memory.data();
    recomp_context ctx{};ctx.r4=0xABCD;
    auto set=[&](unsigned address,uint8_t value){memory[(address&0x1FFFFFFF)^3]=value;};
    overlay_loaded(0x10DA50,0x801C4500,0x7C50);
    set(0x801CC3A6,13);set(0x801CC398,4);set(0x801CC395,1);
    set(0x801CC3A7,0); // Request while the first fade still owns slot 0.
    srw64_game_hooks.intro_step(rdram,&ctx);
    assert(input(0x1010)==0);
    srw64_game_hooks.intro_step(rdram,&ctx);
    assert(!cleared && !finished);
    set(0x801CC3A7,1);
    srw64_game_hooks.intro_step(rdram,&ctx);
    assert(cleared==1 && finished==1 && slot==3);
    assert(ctx.r4==0xABCD); // Host callback cannot clobber the guest caller.
    assert(MEM_BU(0,int32_t(0x801CC3A7))==2);
    assert(MEM_BU(0,int32_t(0x801CC395))==5);
    srw64_game_hooks.intro_step(rdram,&ctx);
    assert(finished==1); // Original fade runs normally; completion is one-shot.
    overlay_loaded(0x1090A0,0x801C4500,0x49B0);
    assert(input(0x1010)==0); // Held chord stays consumed across the overlay.
    input(0);
    set(0x801CC3A7,1);set(0x801CC395,0); // Stale lookalike state in another overlay.
    srw64_game_hooks.intro_step(rdram,&ctx);
    assert(input(0x1010)==0x1010 && finished==1);
    std::filesystem::remove_all(out);
}
