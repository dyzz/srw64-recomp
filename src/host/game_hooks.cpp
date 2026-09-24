#include "game_hooks.hpp"
#include "guest_memory.hpp"
#include "funcs.h"
#include "script_trace.hpp"
#include "script_inject.hpp"
#include "mini_stage.hpp"
#include "script_move_probe.hpp"
#include "state_probe.hpp"
#include "rule_probe.hpp"
#include "battle_ui_probe.hpp"
#include "base_fixes.hpp"
#include "upgrade_rules.hpp"
#include "upgrade_refund.hpp"
#include "parts_carry.hpp"
#include "link_battler.hpp"

SRW64GameHooks srw64_game_hooks;
namespace rules=srw64::rules;
namespace upgrades=srw64::upgrades;
namespace refund=srw64::refund;
namespace parts_carry=srw64::parts_carry;
extern "C" {
void load_000AB160_func_801C8AB4(uint8_t* ram,recomp_context* ctx) {
    if(!srw64_game_hooks.battle_spirit_return || !srw64_game_hooks.battle_spirit_return(ram,ctx))srw64_original_return_to_map(ram,ctx);
}
void load_000AB160_func_801D5064(uint8_t* ram,recomp_context* ctx) {
    if(!srw64_game_hooks.battle_step || !srw64_game_hooks.battle_step(ram,ctx,1))srw64_original_battle_confirm_step(ram,ctx);
}
void load_000AB160_func_801D5294(uint8_t* ram,recomp_context* ctx) {
    if(!srw64_game_hooks.battle_step || !srw64_game_hooks.battle_step(ram,ctx,2))srw64_original_battle_response_step(ram,ctx);
}

void resident_func_80082334(uint8_t* ram,recomp_context* ctx) {
    if(srw64::combat_preview::test_roll>=0)ctx->r2=srw64::combat_preview::test_roll;
    else srw64_original_random_bound(ram,ctx);
}

void resident_func_80085F30(uint8_t* ram,recomp_context* ctx) {
    srw64_original_frame_boundary(ram,ctx);
    if(srw64_game_hooks.presentation_step)srw64_game_hooks.presentation_step(ram);
    if(srw64_game_hooks.intermission_frame)srw64_game_hooks.intermission_frame(ram);
    if(srw64_game_hooks.upgrade_frame)srw64_game_hooks.upgrade_frame(ram);
    if(srw64_game_hooks.parts_frame)srw64_game_hooks.parts_frame(ram);
    if(srw64_game_hooks.ability_frame)srw64_game_hooks.ability_frame(ram);
    if(srw64_game_hooks.swap_frame)srw64_game_hooks.swap_frame(ram);
    if(srw64_game_hooks.save_frame)srw64_game_hooks.save_frame(ram);
    if(srw64::mini_stage::take_direct_entry()) {
        // The title overlay's own exit (801CAA34..801CAA80) with the mode the
        // intermission's next-stage exit selects (801D8D94).
        const auto call=[&](void(*function)(uint8_t*,recomp_context*),uint32_t a0=0,uint32_t a1=0,uint32_t a2=0) {
            auto c=*ctx;c.r29=int32_t(uint32_t(ctx->r29)-0x200);c.r4=int32_t(a0);c.r5=int32_t(a1);c.r6=int32_t(a2);function(ram,&c);
        };
        call(resident_func_800836CC,0);call(resident_func_800A5138);
        srw64::mini_stage::direct_scene(ram);
        call(resident_func_80080188,0xC);call(resident_func_8007F510,0x800801A4,0,1);
    }
    srw64::mini_stage::service(ram);
    srw64::script_inject::service(ram);
    // State captures bracket each injected script when the state probe is on.
    static uint64_t captured_sequence=0;
    {
        auto& inject=srw64::script_inject::state();
        std::lock_guard lock(inject.mutex);
        if(inject.active && inject.active->sequence!=captured_sequence) {
            captured_sequence=inject.active->sequence;
            srw64::state_probe::capture(ram,"script-inject-applied",uint32_t(captured_sequence));
        }
    }
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
void load_001090A0_func_801C50B8(uint8_t* rdram,recomp_context* ctx) {
    // Protagonist selection: the native page replaces browsing and the はい prompt.
    if(!srw64_game_hooks.name_step || !srw64_game_hooks.name_step(rdram,ctx,3))
        srw64_original_name_selection_step(rdram,ctx);
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
    if(srw64_game_hooks.title_frame)srw64_game_hooks.title_frame(rdram);
}
// Title screens behind the ring menu: the native pages take the build and step functions.
#define SRW64_TITLE_BUILD(address, original, screen) \
    void load_0010DA50_func_##address(uint8_t* rdram, recomp_context* ctx) { \
        if (srw64_game_hooks.title_build && srw64_game_hooks.title_build(rdram, ctx, screen)) return; \
        original(rdram, ctx); \
    }
#define SRW64_TITLE_STEP(address, original, screen) \
    void load_0010DA50_func_##address(uint8_t* rdram, recomp_context* ctx) { \
        if (srw64_game_hooks.title_step && srw64_game_hooks.title_step(rdram, ctx, screen, original)) return; \
        original(rdram, ctx); \
    }
SRW64_TITLE_BUILD(801C6F3C, srw64_original_title_medium_build, srw64_title_medium)
SRW64_TITLE_STEP(801C709C, srw64_original_title_medium_step, srw64_title_medium)
SRW64_TITLE_BUILD(801C7328, srw64_original_title_slots_build, srw64_title_slots)
SRW64_TITLE_STEP(801C783C, srw64_original_title_slots_step, srw64_title_slots)
SRW64_TITLE_STEP(801C7A48, srw64_original_title_confirm_step, srw64_title_confirm)
SRW64_TITLE_BUILD(801C7C90, srw64_original_title_message_build, srw64_title_message)
SRW64_TITLE_STEP(801C8074, srw64_original_title_message_step, srw64_title_message)
SRW64_TITLE_BUILD(801C697C, srw64_original_title_options_build, srw64_title_options)
SRW64_TITLE_STEP(801C6B14, srw64_original_title_options_step, srw64_title_options)
SRW64_TITLE_BUILD(801C86A8, srw64_original_title_sound_build, srw64_title_sound)
SRW64_TITLE_BUILD(801C9888, srw64_original_title_karaoke_build, srw64_title_karaoke)
SRW64_TITLE_BUILD(801C83D0, srw64_original_title_list_draw, srw64_title_list_draw)
SRW64_TITLE_STEP(801C8724, srw64_original_title_sound_step, srw64_title_sound)
SRW64_TITLE_STEP(801C89E4, srw64_original_title_sound_exit_step, srw64_title_sound_exit)
SRW64_TITLE_STEP(801C9904, srw64_original_title_karaoke_step, srw64_title_karaoke)
SRW64_TITLE_STEP(801C9ADC, srw64_original_title_karaoke_exit_step, srw64_title_karaoke_exit)
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
void resident_func_800945D4(uint8_t* rdram, recomp_context* ctx) {
    const int32_t cursor = ctx->r4;
    const uint32_t slot = ctx->r5, sub = ctx->r6;
    const uint32_t begin = MEM_W(0, cursor) & 0x1FFFFFFF;
    srw64_original_map_draw(rdram, ctx);
    const uint32_t end = MEM_W(0, cursor) & 0x1FFFFFFF;
    if (srw64_game_hooks.map_drawn) srw64_game_hooks.map_drawn(rdram, begin, end, slot, sub);
}
void resident_func_800964E4(uint8_t* rdram, recomp_context* ctx) {
    const int32_t cursor = ctx->r4;
    const uint32_t slot = ctx->r5, sub = ctx->r6;
    const uint32_t begin = MEM_W(0, cursor) & 0x1FFFFFFF;
    srw64_original_portrait_draw(rdram, ctx);
    const uint32_t end = MEM_W(0, cursor) & 0x1FFFFFFF;
    if (srw64_game_hooks.portrait_drawn) srw64_game_hooks.portrait_drawn(rdram, begin, end, slot, sub);
}
void resident_func_80095974(uint8_t* rdram, recomp_context* ctx) {
    const int32_t cursor = ctx->r4;
    const uint32_t slot = ctx->r5, sub = ctx->r6;
    const uint32_t begin = MEM_W(0, cursor) & 0x1FFFFFFF;
    srw64_original_background_draw(rdram, ctx);
    const uint32_t end = MEM_W(0, cursor) & 0x1FFFFFFF;
    if (srw64_game_hooks.background_drawn) srw64_game_hooks.background_drawn(rdram, begin, end, slot, sub);
}
void resident_func_80096CD8(uint8_t* rdram, recomp_context* ctx) {
    const int32_t cursor = ctx->r4;
    const uint32_t slot = ctx->r5, sub = ctx->r6;
    const uint32_t begin = MEM_W(0, cursor) & 0x1FFFFFFF;
    srw64_original_scene_rect_draw(rdram, ctx);
    const uint32_t end = MEM_W(0, cursor) & 0x1FFFFFFF;
    if (srw64_game_hooks.scene_drawn) srw64_game_hooks.scene_drawn(rdram, begin, end, slot, sub, false);
}
void resident_func_8009761C(uint8_t* rdram, recomp_context* ctx) {
    const int32_t cursor = ctx->r4;
    const uint32_t slot = ctx->r5, sub = ctx->r6;
    const uint32_t begin = MEM_W(0, cursor) & 0x1FFFFFFF;
    srw64_original_scene_quad_draw(rdram, ctx);
    const uint32_t end = MEM_W(0, cursor) & 0x1FFFFFFF;
    if (srw64_game_hooks.scene_drawn) srw64_game_hooks.scene_drawn(rdram, begin, end, slot, sub, true);
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
    {
        static uint64_t completed=0;
        srw64::script_inject::observe(rdram);
        auto& inject=srw64::script_inject::state();
        uint64_t now,sequence=0;
        {std::lock_guard lock(inject.mutex);now=inject.completed;sequence=inject.completed_sequence;}
        if(now!=completed){completed=now;srw64::state_probe::capture(rdram,"script-inject-complete",uint32_t(sequence));}
    }
    srw64::mini_stage::poll_hook(rdram,owner);
    srw64::rule_probe::poll(rdram,ctx,owner);
    srw64::battle_ui_probe::poll(rdram,ctx,owner);
    move_probe.after(rdram,owner);
    if (srw64_game_hooks.script_after) srw64_game_hooks.script_after(rdram, owner);
}
void resident_func_8009DE7C(uint8_t* rdram, recomp_context* ctx) {
    // Scene registration: a mini stage image replaces the pointer table and buffers first.
    srw64::mini_stage::register_hook(rdram, ctx);
    srw64_original_script_register(rdram, ctx);
}
void load_000AB160_func_80209D6C(uint8_t* rdram, recomp_context* ctx) {
    srw64_original_stage_map_select(rdram, ctx);
    srw64::mini_stage::map_hook(rdram);
}
// Optional rule fixes (rule_fixes.hpp). Unless the run enables a fix, each
// wrapper only calls the original routine.
void load_000AB160_func_801E1F08(uint8_t* rdram, recomp_context* ctx) {
    // 聖戦士 evade bonus. The original body is empty, so callers read the flag bit
    // still in v0 (0x20) as a fixed 32 at every level.
    if (rules::enabled(rules::seisenshi_level)) ctx->r2 = rules::level_bonus(rdram, uint32_t(ctx->r5));
    else srw64_original_seisenshi_bonus(rdram, ctx);
}
void load_000AB160_func_801E1F10(uint8_t* rdram, recomp_context* ctx) {
    // 超能力 hit and evade bonus; same empty body, so a fixed 64 (flag 0x40).
    if (rules::enabled(rules::esp_level)) ctx->r2 = rules::level_bonus(rdram, uint32_t(ctx->r5));
    else srw64_original_esp_bonus(rdram, ctx);
}
void load_000AB160_func_801E1D64(uint8_t* rdram, recomp_context* ctx) {
    // 底力 bonus. a0 is 2 for attacker hit, 1 for defender evade and 0 for the
    // critical rate; the original ignores it, so all three get the same value.
    const uint32_t kind = uint32_t(ctx->r4);
    if (rules::enabled(rules::potential_bands))
        ctx->r2 = rules::potential_bonus(rdram, uint32_t(ctx->r5), uint32_t(ctx->r6), true);
    else srw64_original_potential_bonus(rdram, ctx);
    if (rules::enabled(rules::potential_half) && kind != 0) ctx->r2 = uint32_t(ctx->r2) / 2;
}
void load_000AB160_func_801F4384(uint8_t* rdram, recomp_context* ctx) {
    // Battle hit rate from participant slot a0 against slot a1.
    const auto attacker = rules::participant(rdram, uint32_t(ctx->r4));
    const auto defender = rules::participant(rdram, uint32_t(ctx->r5));
    const bool cap = rules::enabled(rules::limit_cap);
    {
        rules::StatCap hit(rdram, attacker.pilot, attacker.unit, rules::pilot_hit, cap);
        rules::StatCap evade(rdram, defender.pilot, defender.unit, rules::pilot_evade, cap);
        srw64_original_battle_hit_rate(rdram, ctx);
    }
    srw64::rule_probe::observe(rdram, "battle", attacker, defender, int16_t(ctx->r2));
}
void load_000AB160_func_801F5628(uint8_t* rdram, recomp_context* ctx) {
    // Battle damage from participant slot a0 against slot a1: +0x08 weapon,
    // +0x0C pilot. 聖戦士 adds nothing here in the original.
    const uint32_t base = rules::participants + (uint32_t(ctx->r4) & 0xFF) * rules::participant_size;
    const uint32_t weapon = rules::read(rdram, base + 0x08, 4), pilot = rules::read(rdram, base + 0x0C, 4);
    const uint32_t bonus = rules::enabled(rules::aura_slash_power) ? rules::aura_slash_bonus(rdram, pilot, weapon) : 0;
    rules::WeaponPower power(rdram, weapon, bonus);
    srw64_original_battle_damage(rdram, ctx);
}
void load_000AB160_func_80203418(uint8_t* rdram, recomp_context* ctx) {
    // Damage estimate for AI target and weapon choice: a1 weapon, a2 attacker pilot.
    const uint32_t weapon = uint32_t(ctx->r5), pilot = uint32_t(ctx->r6);
    const uint32_t bonus = rules::enabled(rules::aura_slash_power) ? rules::aura_slash_bonus(rdram, pilot, weapon) : 0;
    rules::WeaponPower power(rdram, weapon, bonus);
    srw64_original_damage_estimate(rdram, ctx);
}
void load_000AB160_func_80204254(uint8_t* rdram, recomp_context* ctx) {
    // Hit estimate for target and weapon choice: a2/a3 attacker pilot/unit,
    // stack +0x14/+0x18 defender pilot/unit.
    const uint32_t sp = uint32_t(ctx->r29);
    const rules::Combatant attacker{uint32_t(ctx->r6), uint32_t(ctx->r7)};
    const rules::Combatant defender{rules::read(rdram, sp + 0x14, 4), rules::read(rdram, sp + 0x18, 4)};
    const bool cap = rules::enabled(rules::limit_cap);
    {
        rules::StatCap hit(rdram, attacker.pilot, attacker.unit, rules::pilot_hit, cap);
        rules::StatCap evade(rdram, defender.pilot, defender.unit, rules::pilot_evade, cap);
        srw64_original_hit_estimate(rdram, ctx);
    }
    srw64::rule_probe::observe(rdram, "estimate", attacker, defender, int16_t(ctx->r2));
}
void load_000AB160_func_8020ABB4(uint8_t* rdram, recomp_context* ctx) {
    // One deployment record (a0) becomes a unit on the map. With a boss dummy rule
    // on, the record's dummy count is scaled for this call only.
    rules::DummyScale dummies(rdram, uint32_t(ctx->r4));
    srw64_original_deploy_record(rdram, ctx);
}
void resident_func_800A5054(uint8_t* rdram, recomp_context* ctx) {
    // Wing pilot kill restore into the new pilot record in a0. Always on (BUG05):
    // on a non-player record the original writes the pilot's kills into the field
    // the game reads as the dummy count.
    if (!srw64::base_fixes::player_pilot(uint32_t(ctx->r4))) return;
    srw64_original_wing_kill_restore(rdram, ctx);
}
void resident_func_800AA8F4(uint8_t* rdram, recomp_context* ctx) {
    // Upgrade inheritance on a machine swap: a0 the new machine, a1 the saved
    // weapons of the predecessor, a2 their count. The original moves a weapon's
    // level only when the ROM map lists it; with the rule on, three missing pairs
    // are written afterwards, before the caller's 800AAB5C recomputes power.
    const uint32_t unit = uint32_t(ctx->r4), saved = uint32_t(ctx->r5);
    const int count = int16_t(ctx->r6);
    srw64_original_weapon_inherit(rdram, ctx);
    rules::inherit_missing_weapons(rdram, unit, saved, count);
}
// Upgrade limits (upgrade_rules.hpp, docs/gameplay/upgrade-limits.md). With the cap-break
// rule off and no unit above its cap, every scope below leaves memory unchanged.
void resident_func_800A5254(uint8_t* rdram, recomp_context* ctx) {
    // Stat and weapon power recompute for the unit in a0. The original rewrites the
    // cap (+0x51) from the ROM record, so an upgrade-screen scope's cap is put back.
    const uint32_t unit = uint32_t(ctx->r4);
    upgrades::before_recompute(rdram, unit);
    srw64_original_unit_stats(rdram, ctx);
    upgrades::after_recompute(rdram, unit);
}
void resident_func_800A5F84(uint8_t* rdram, recomp_context* ctx) {
    // Twin-weapon sync (a0 unit, a1 weapon), called by 801D1100 between the level
    // increment and its "level == cap" test for full-upgrade weapons.
    const uint32_t unit = uint32_t(ctx->r4), weapon = uint32_t(ctx->r5);
    srw64_original_weapon_twin_sync(rdram, ctx);
    upgrades::steer_unlock(rdram, unit, weapon);
}
// The machine lists of ユニット改造 / 武器改造 and the five-stat screen: the native
// page (upgrade_page.cpp) may build them without drawing and answer their steps.
void load_0008F4B0_func_801CF388(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.upgrade_list_build && srw64_game_hooks.upgrade_list_build(rdram, ctx, false)) return;
    srw64_original_upgrade_list_open(rdram, ctx);
}
void load_0008F4B0_func_801CF564(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.upgrade_list_step && srw64_game_hooks.upgrade_list_step(rdram, ctx, srw64_original_upgrade_list_step)) return;
    srw64_original_upgrade_list_step(rdram, ctx);
}
void load_0008F4B0_func_801D03D0(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.upgrade_list_build && srw64_game_hooks.upgrade_list_build(rdram, ctx, true)) return;
    srw64_original_weapon_list_open(rdram, ctx);
}
void load_0008F4B0_func_801D04A4(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.upgrade_list_step && srw64_game_hooks.upgrade_list_step(rdram, ctx, srw64_original_weapon_list_step)) return;
    srw64_original_weapon_list_step(rdram, ctx);
}
void load_0008F4B0_func_801CF680(uint8_t* rdram, recomp_context* ctx) {
    // Upgrade screen set-up; prints the unit's cap.
    upgrades::Scope scope(rdram, upgrades::Policy::decide);
    if (srw64_game_hooks.upgrade_stats_build && srw64_game_hooks.upgrade_stats_build(rdram, ctx)) return;
    srw64_original_upgrade_open(rdram, ctx);
}
void load_0008F4B0_func_801C80E0(uint8_t* rdram, recomp_context* ctx) {
    // Five stats: values, next-level preview and gauges.
    if (srw64_game_hooks.upgrade_stats_view && srw64_game_hooks.upgrade_stats_view(rdram)) return;
    upgrades::Scope scope(rdram, upgrades::Policy::stats_view);
    srw64_original_upgrade_stats_view(rdram, ctx);
}
void load_0008F4B0_func_801CF988(uint8_t* rdram, recomp_context* ctx) {
    // Five stats: cursor, price, "can upgrade" and the confirmed upgrade.
    upgrades::Scope scope(rdram, upgrades::Policy::decide);
    if (srw64_game_hooks.upgrade_stats_step && srw64_game_hooks.upgrade_stats_step(rdram, ctx)) return;
    srw64_original_upgrade_stats_step(rdram, ctx);
}
void load_0008F4B0_func_801CF85C(uint8_t* rdram, recomp_context* ctx) {
    // EW swap once all five stats reach the cap: always the original cap.
    upgrades::Scope scope(rdram, upgrades::Policy::original);
    srw64_original_upgrade_ew_check(rdram, ctx);
}
void load_0008F4B0_func_801D0600(uint8_t* rdram, recomp_context* ctx) {
    // 武器改造 weapon list build, with the full-upgrade bonus message.
    if (srw64_game_hooks.weapon_list_build && srw64_game_hooks.weapon_list_build(rdram, ctx)) return;
    srw64_original_weapon_screen_open(rdram, ctx);
}
void load_0008F4B0_func_801D087C(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.weapon_list_step && srw64_game_hooks.weapon_list_step(rdram, ctx)) return;
    srw64_original_weapon_screen_step(rdram, ctx);
}
void load_0008F4B0_func_801D0C7C(uint8_t* rdram, recomp_context* ctx) {
    // Selected weapon: price, power preview, gauge, "cannot upgrade further".
    upgrades::Scope scope(rdram, upgrades::Policy::weapon);
    if (srw64_game_hooks.weapon_confirm_build && srw64_game_hooks.weapon_confirm_build(rdram, ctx)) return;
    srw64_original_upgrade_weapon_view(rdram, ctx);
}
void load_0008F4B0_func_801D1100(uint8_t* rdram, recomp_context* ctx) {
    // Selected weapon: the confirmed upgrade and full-upgrade weapons.
    upgrades::Scope scope(rdram, upgrades::Policy::weapon);
    if (srw64_game_hooks.weapon_confirm_step && srw64_game_hooks.weapon_confirm_step(rdram, ctx)) return;
    srw64_original_upgrade_weapon_step(rdram, ctx);
}
// 強化パーツ: the machine list, the slots/inventory screen and the owned-copies
// screen; the native page (parts_page.cpp) builds them without drawing and
// answers their steps.
void load_0008F4B0_func_801D4A00(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.parts_build && srw64_game_hooks.parts_build(rdram, ctx, 7)) return;
    srw64_original_parts_list_open(rdram, ctx);
}
void load_0008F4B0_func_801D4A98(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.parts_step && srw64_game_hooks.parts_step(rdram, ctx, 7, srw64_original_parts_list_step)) return;
    srw64_original_parts_list_step(rdram, ctx);
}
void load_0008F4B0_func_801D4BEC(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.parts_build && srw64_game_hooks.parts_build(rdram, ctx, 18)) return;
    srw64_original_parts_slots_open(rdram, ctx);
}
void load_0008F4B0_func_801D4C94(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.parts_step && srw64_game_hooks.parts_step(rdram, ctx, 18, srw64_original_parts_slots_step)) return;
    srw64_original_parts_slots_step(rdram, ctx);
}
void load_0008F4B0_func_801D5168(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.parts_build && srw64_game_hooks.parts_build(rdram, ctx, 19)) return;
    srw64_original_parts_holders_open(rdram, ctx);
}
void load_0008F4B0_func_801D51EC(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.parts_step && srw64_game_hooks.parts_step(rdram, ctx, 19, srw64_original_parts_holders_step)) return;
    srw64_original_parts_holders_step(rdram, ctx);
}
// ユニット能力／パイロット能力 (ability_page.cpp): the two lists, the unit page, its
// weapon list and the pilot page, built without drawing and stepped by the page.
void load_0008F4B0_func_801D14BC(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.ability_build && srw64_game_hooks.ability_build(rdram, ctx, 4)) return;
    srw64_original_ability_unit_list_open(rdram, ctx);
}
void load_0008F4B0_func_801D1554(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.ability_step && srw64_game_hooks.ability_step(rdram, ctx, 4, srw64_original_ability_unit_list_step)) return;
    srw64_original_ability_unit_list_step(rdram, ctx);
}
void load_0008F4B0_func_801D16D8(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.ability_build && srw64_game_hooks.ability_build(rdram, ctx, 13)) return;
    srw64_original_ability_unit_open(rdram, ctx);
}
void load_0008F4B0_func_801D2030(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.ability_step && srw64_game_hooks.ability_step(rdram, ctx, 13, srw64_original_ability_unit_step)) return;
    srw64_original_ability_unit_step(rdram, ctx);
}
void load_0008F4B0_func_801D2144(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.ability_build && srw64_game_hooks.ability_build(rdram, ctx, 14)) return;
    srw64_original_ability_weapons_open(rdram, ctx);
}
void load_0008F4B0_func_801D21F8(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.ability_step && srw64_game_hooks.ability_step(rdram, ctx, 14, srw64_original_ability_weapons_step)) return;
    srw64_original_ability_weapons_step(rdram, ctx);
}
void load_0008F4B0_func_801D22E0(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.ability_build && srw64_game_hooks.ability_build(rdram, ctx, 5)) return;
    srw64_original_ability_pilot_list_open(rdram, ctx);
}
void load_0008F4B0_func_801D2378(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.ability_step && srw64_game_hooks.ability_step(rdram, ctx, 5, srw64_original_ability_pilot_list_step)) return;
    srw64_original_ability_pilot_list_step(rdram, ctx);
}
void load_0008F4B0_func_801D2480(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.ability_build && srw64_game_hooks.ability_build(rdram, ctx, 15)) return;
    srw64_original_ability_pilot_open(rdram, ctx);
}
void load_0008F4B0_func_801D24C8(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.ability_step && srw64_game_hooks.ability_step(rdram, ctx, 15, srw64_original_ability_pilot_step)) return;
    srw64_original_ability_pilot_step(rdram, ctx);
}
// データセーブ (save_page.cpp): the medium choice and the slot page, built without
// drawing; the page runs the state machine and calls the original write routines.
void load_0008F4B0_func_801CEA30(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.save_build && srw64_game_hooks.save_build(rdram, ctx, 1)) return;
    srw64_original_save_choice_open(rdram, ctx);
}
void load_0008F4B0_func_801CEABC(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.save_step && srw64_game_hooks.save_step(rdram, ctx, 1, srw64_original_save_choice_step)) return;
    srw64_original_save_choice_step(rdram, ctx);
}
void load_0008F4B0_func_801CECE8(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.save_build && srw64_game_hooks.save_build(rdram, ctx, 9)) return;
    srw64_original_save_slots_open(rdram, ctx);
}
void load_0008F4B0_func_801CEEF8(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.save_step && srw64_game_hooks.save_step(rdram, ctx, 9, srw64_original_save_slots_step)) return;
    srw64_original_save_slots_step(rdram, ctx);
}
// のりかえ (swap_page.cpp): the pilot / fairy lists, the target lists and the confirm
// page, built without drawing and stepped by the page; the swap is the original's.
void load_0008F4B0_func_801D25A4(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.swap_build && srw64_game_hooks.swap_build(rdram, ctx, 6)) return;
    srw64_original_swap_pilots_open(rdram, ctx);
}
void load_0008F4B0_func_801D263C(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.swap_step && srw64_game_hooks.swap_step(rdram, ctx, 6, srw64_original_swap_pilots_step)) return;
    srw64_original_swap_pilots_step(rdram, ctx);
}
void load_0008F4B0_func_801D2758(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.swap_build && srw64_game_hooks.swap_build(rdram, ctx, 16)) return;
    srw64_original_swap_targets_open(rdram, ctx);
}
void load_0008F4B0_func_801D2A24(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.swap_step && srw64_game_hooks.swap_step(rdram, ctx, 16, srw64_original_swap_targets_step)) return;
    srw64_original_swap_targets_step(rdram, ctx);
}
void load_0008F4B0_func_801D2B64(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.swap_build && srw64_game_hooks.swap_build(rdram, ctx, 17)) return;
    srw64_original_swap_confirm_open(rdram, ctx);
}
void load_0008F4B0_func_801D3A90(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.swap_step && srw64_game_hooks.swap_step(rdram, ctx, 17, srw64_original_swap_confirm_step)) return;
    srw64_original_swap_confirm_step(rdram, ctx);
}
void load_0008F4B0_func_801D4164(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.swap_build && srw64_game_hooks.swap_build(rdram, ctx, 20)) return;
    srw64_original_swap_fairies_open(rdram, ctx);
}
void load_0008F4B0_func_801D41FC(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.swap_step && srw64_game_hooks.swap_step(rdram, ctx, 20, srw64_original_swap_fairies_step)) return;
    srw64_original_swap_fairies_step(rdram, ctx);
}
void load_0008F4B0_func_801D42FC(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.swap_build && srw64_game_hooks.swap_build(rdram, ctx, 21)) return;
    srw64_original_swap_fairy_targets_open(rdram, ctx);
}
void load_0008F4B0_func_801D4578(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.swap_step && srw64_game_hooks.swap_step(rdram, ctx, 21, srw64_original_swap_fairy_targets_step)) return;
    srw64_original_swap_fairy_targets_step(rdram, ctx);
}
// 80090778(status): osPfsIsPlug into a port bit mask, then per port +0x74 plugged and
// +0x78 absent (stride 0x7C); an error marks every port both. The recomp runtime's
// osPfsIsPlug aborts (the データセーブ slot screen dies in it), so answer the success
// case with no Controller Pak anywhere: the game then keeps to its SRAM path.
void resident_func_80090778(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t status = uint32_t(ctx->r4);
    for (unsigned port = 0; port < 4; ++port) {
        srw64::guest::write32(rdram, status + 0x74 + port * 0x7C, 0);
        srw64::guest::write32(rdram, status + 0x78 + port * 0x7C, 1);
    }
    ctx->r2 = 0;
}
// Upgrade refund (upgrade_refund.hpp): the story routines that delete a player's
// machine open a scope, and the deletion inside it pays the upgrades back.
void resident_func_800AAD28(uint8_t* rdram, recomp_context* ctx) {
    // Registration (a1 the new machine): its predecessor's levels move to it, so
    // deleting that instance here is not a loss.
    refund::Registration registration(rdram, int16_t(ctx->r5));
    // Freeing the old machine unequips its parts (parts_carry.hpp); with the carry
    // rule on they go back on the successor once it exists.
    const auto carry = parts_carry::capture(rdram, int16_t(ctx->r4), int16_t(ctx->r5));
    srw64_original_unit_register(rdram, ctx);
    if (const auto carried = parts_carry::apply(rdram, carry)) {
        // The same propagation the parts screen runs after a slot changes.
        auto c = *ctx;
        c.r29 = int32_t(uint32_t(ctx->r29) - 0x200);
        c.r4 = int32_t(parts_carry::instance_of(rdram, carried->unit));
        c.r5 = 1;
        resident_func_800A5924(rdram, &c);
        parts_carry::record(carry, *carried);
    }
}
void resident_func_800AA464(uint8_t* rdram, recomp_context* ctx) {
    // Pilot a0 leaves machine a1; the instance numbered a2 is deleted (3D5A modes
    // 3000/4000, or a registration's old machine).
    refund::Removal removal(refund::Source::removal);
    srw64_original_unit_remove(rdram, ctx);
}
void resident_func_800AB808(uint8_t* rdram, recomp_context* ctx) {
    // 3D6A mode 3, the ゴッドマーズ merge: ガイヤー is deleted without passing anything on.
    refund::Removal removal(refund::Source::merge);
    srw64_original_unit_merge(rdram, ctx);
}
void resident_func_800AA3C4(uint8_t* rdram, recomp_context* ctx) {
    // Frees the machine instance in a0, its weapons and pilot links. Sales and map
    // removals call this outside the scopes above and are never refunded.
    const auto paid = refund::before_delete(rdram, uint32_t(ctx->r4));
    srw64_original_unit_delete(rdram, ctx);
    if (paid && srw64_game_hooks.refund) srw64_game_hooks.refund(rdram, paid->unit, paid->cost.total());
}
void load_00107BF0_func_801C2600(uint8_t* rdram, recomp_context* ctx) {
    // Sale price of the unit in a0, a1 its row in the sale table.
    const uint32_t row = uint32_t(int8_t(ctx->r5));
    srw64_original_sale_price(rdram, ctx);
    ctx->r2 = int32_t(upgrades::clamp_sale_price(rdram, row, uint32_t(ctx->r2)));
}
// Link Battler (link_battler.hpp, docs/gameplay/link-battler.md). The host has no
// Transfer Pak, so the resident Game Boy pak driver answers as a Link Battler cartridge
// that is always inserted, and its SRAM is the block link::prepare made. Every routine
// reports success (0) in v0.
void resident_func_80090F44(uint8_t* rdram, recomp_context* ctx) {ctx->r2 = 0;}   // init + read header
void resident_func_80090FA0(uint8_t* rdram, recomp_context* ctx) {ctx->r2 = 0;}   // status
void resident_func_80090FC4(uint8_t* rdram, recomp_context* ctx) {ctx->r2 = 0;}   // power a0
void resident_func_800910C4(uint8_t* rdram, recomp_context* ctx) {ctx->r2 = 0;}   // title is S ROBOT LB
void resident_func_80091120(uint8_t* rdram, recomp_context* ctx) {ctx->r2 = 0;}   // enable cartridge RAM
void resident_func_80091284(uint8_t* rdram, recomp_context* ctx) {
    // a0 write, a1 linear SRAM address, a2 buffer, a3 length.
    srw64::link::transfer(rdram, uint32_t(ctx->r4) != 0, uint16_t(ctx->r5), uint32_t(ctx->r6), uint16_t(ctx->r7));
    ctx->r2 = 0;
}
void load_0008F4B0_func_801D6FF4(uint8_t* rdram, recomp_context* ctx) {
    // リンク screen set-up; it reads the cartridge. With the native series page up, the
    // set-up waits until the player has chosen what to link (link_page.cpp).
    if (srw64_game_hooks.link_begin && srw64_game_hooks.link_begin(rdram)) return;
    srw64::link::prepare(rdram, 0);
    srw64_original_link_open(rdram, ctx);
}
void load_0008F4B0_func_801CDFB0(uint8_t* rdram, recomp_context* ctx) {
    // インターミッション menu build: panels, numbers, cursor. The native page shows them.
    if (srw64_game_hooks.intermission_build && srw64_game_hooks.intermission_build(rdram, ctx)) return;
    srw64_original_intermission_menu_build(rdram, ctx);
}
void load_0008F4B0_func_801CE19C(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.intermission_step && srw64_game_hooks.intermission_step(rdram, ctx)) return;
    srw64_original_intermission_menu_step(rdram, ctx);
}
void load_0008F4B0_func_801D70FC(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.link_step && srw64_game_hooks.link_step(rdram, ctx)) return;
    srw64_original_link_step(rdram, ctx);
}
void resident_func_8009FA94(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.choice) srw64_game_hooks.choice(rdram);
    srw64::state_probe::capture(rdram,"choice");
    srw64_original_dialogue_choice(rdram, ctx);
}
}
