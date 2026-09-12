#include "game_hooks.hpp"
#include "funcs.h"
#include "script_trace.hpp"
#include "script_move_probe.hpp"
#include "state_probe.hpp"

SRW64GameHooks srw64_game_hooks;
extern "C" {
void resident_func_80085F30(uint8_t* ram,recomp_context* ctx) {
    srw64_original_frame_boundary(ram,ctx);
    if(srw64_game_hooks.presentation_step)srw64_game_hooks.presentation_step(ram);
}
void resident_func_800821B0(uint8_t* ram,recomp_context* ctx) {
    const uint32_t seed=ctx->r4;srw64_original_rng_seed(ram,ctx);
    srw64::state_probe::rng("seed",seed,ram);
}
void resident_func_800822D8(uint8_t* ram,recomp_context* ctx) {
    srw64_original_rng_next(ram,ctx);srw64::state_probe::rng("next",ctx->r2,ram);
}
void resident_func_800924D8(uint8_t* ram,recomp_context* ctx) {
    srw64_original_intermission_serialize(ram,ctx);srw64::state_probe::save_payload(ram,false,0);
}
void resident_func_80093278(uint8_t* ram,recomp_context* ctx) {
    const unsigned write_sram=uint8_t(ctx->r4);
    srw64_original_tactical_serialize(ram,ctx);srw64::state_probe::save_payload(ram,true,write_sram);
}
void resident_func_800927A4(uint8_t* ram,recomp_context* ctx) {
    srw64_original_intermission_restore(ram,ctx);srw64::state_probe::capture(ram,"intermission-restored");
}
void resident_func_800936A0(uint8_t* ram,recomp_context* ctx) {
    srw64_original_tactical_restore(ram,ctx);srw64::state_probe::capture(ram,"tactical-restored");
}
void load_001090A0_func_801C5004(uint8_t* rdram,recomp_context* ctx) {
    srw64_original_name_selection_init(rdram,ctx);
    if(srw64_game_hooks.name_begin)srw64_game_hooks.name_begin(rdram,3);
}
void load_001090A0_func_801C5DAC(uint8_t* rdram,recomp_context* ctx) {
    srw64_original_name_review_init(rdram,ctx);
    if(srw64_game_hooks.name_begin)srw64_game_hooks.name_begin(rdram,2);
}
void load_001090A0_func_801C5E88(uint8_t* rdram,recomp_context* ctx) {
    if(!srw64_game_hooks.name_step || !srw64_game_hooks.name_step(rdram,ctx,2))
        srw64_original_name_review_step(rdram,ctx);
}
void load_001090A0_func_801C5494(uint8_t* rdram,recomp_context* ctx) {
    srw64_original_name_player_init(rdram,ctx);
    if(srw64_game_hooks.name_begin)srw64_game_hooks.name_begin(rdram,0);
}
void load_001090A0_func_801C5920(uint8_t* rdram,recomp_context* ctx) {
    srw64_original_name_partner_init(rdram,ctx);
    if(srw64_game_hooks.name_begin)srw64_game_hooks.name_begin(rdram,1);
}
void load_001090A0_func_801C5644(uint8_t* rdram,recomp_context* ctx) {
    if(!srw64_game_hooks.name_step || !srw64_game_hooks.name_step(rdram,ctx,0))
        srw64_original_name_player_step(rdram,ctx);
}
void load_001090A0_func_801C5AD0(uint8_t* rdram,recomp_context* ctx) {
    if(!srw64_game_hooks.name_step || !srw64_game_hooks.name_step(rdram,ctx,1))
        srw64_original_name_partner_step(rdram,ctx);
}
void load_0010DA50_func_801CA9CC(uint8_t* rdram, recomp_context* ctx) {
    if(srw64_game_hooks.intro_step)srw64_game_hooks.intro_step(rdram,ctx);
    srw64_original_intro_main(rdram,ctx);
}
void resident_func_8008D748(uint8_t* rdram, recomp_context* ctx) {
    if (!srw64_game_hooks.dialogue_step || !srw64_game_hooks.dialogue_step(rdram, ctx))
        srw64_original_dialogue_step(rdram, ctx);
}
void resident_func_8008C9C0(uint8_t* rdram, recomp_context* ctx) {
    const unsigned slot = uint8_t(ctx->r4);
    srw64_original_dialogue_load(rdram, ctx);
    if (srw64_game_hooks.text_loaded) srw64_game_hooks.text_loaded(rdram, slot);
}
void resident_func_8008DC40(uint8_t* rdram, recomp_context* ctx) {
    const int32_t pointer = ctx->r4;
    const uint32_t begin = MEM_W(0, pointer) & 0x1FFFFFFF;
    srw64_original_dialogue_draw(rdram, ctx);
    const uint32_t end = MEM_W(0, pointer) & 0x1FFFFFFF;
    if (srw64_game_hooks.text_drawn) srw64_game_hooks.text_drawn(rdram, begin, end);
}
void resident_func_8008F5C8(uint8_t* rdram, recomp_context* ctx) {
    srw64_original_dialogue_reset(rdram, ctx);
    if (srw64_game_hooks.reset) srw64_game_hooks.reset(rdram);
}
void resident_func_8009EFDC(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t owner = ctx->r5;
    const uint32_t engine = ctx->r4;
    if (srw64_game_hooks.script_before && srw64_game_hooks.script_before(rdram, owner)) {
        // This is a polling routine: while a reading UI owns the wait, report
        // the unchanged script-engine status using its original return field.
        ctx->r2 = MEM_H(0x97C, ctx->r4);
        return;
    }
    const bool tracing=srw64::script_trace::enabled();
    auto& move_probe=srw64::script_move_probe::instance();
    move_probe.before(rdram,engine,owner);
    srw64::script_trace::Snapshot before;
    if(tracing)before=srw64::script_trace::snapshot(rdram,engine,owner);
    srw64_original_script_step(rdram, ctx);
    if(tracing)srw64::script_trace::record(engine,owner,before,srw64::script_trace::snapshot(rdram,engine,owner));
    move_probe.after(rdram,owner);
    if (srw64_game_hooks.script_after) srw64_game_hooks.script_after(rdram, owner);
}
void resident_func_8009FA94(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.choice) srw64_game_hooks.choice(rdram);
    srw64::state_probe::capture(rdram,"choice");
    srw64_original_dialogue_choice(rdram, ctx);
}
}
