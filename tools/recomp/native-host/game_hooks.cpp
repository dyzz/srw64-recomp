#include "game_hooks.hpp"
#include "funcs.h"
#include "script_trace.hpp"
#include "script_inject.hpp"
#include "mini_stage.hpp"
#include "script_move_probe.hpp"
#include "state_probe.hpp"
#include "rule_probe.hpp"
#include "base_fixes.hpp"
#include "upgrade_rules.hpp"

SRW64GameHooks srw64_game_hooks;
namespace rules=srw64::rules;
namespace upgrades=srw64::upgrades;
extern "C" {
void resident_func_80085F30(uint8_t* ram,recomp_context* ctx) {
    srw64_original_frame_boundary(ram,ctx);
    if(srw64_game_hooks.presentation_step)srw64_game_hooks.presentation_step(ram);
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
// Upgrade limits (upgrade_rules.hpp, docs/upgrade-limits.md). With the cap-break
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
void load_0008F4B0_func_801CF680(uint8_t* rdram, recomp_context* ctx) {
    // Upgrade screen set-up; prints the unit's cap.
    upgrades::Scope scope(rdram, upgrades::Policy::decide);
    srw64_original_upgrade_open(rdram, ctx);
}
void load_0008F4B0_func_801C80E0(uint8_t* rdram, recomp_context* ctx) {
    // Five stats: values, next-level preview and gauges.
    upgrades::Scope scope(rdram, upgrades::Policy::stats_view);
    srw64_original_upgrade_stats_view(rdram, ctx);
}
void load_0008F4B0_func_801CF988(uint8_t* rdram, recomp_context* ctx) {
    // Five stats: cursor, price, "can upgrade" and the confirmed upgrade.
    upgrades::Scope scope(rdram, upgrades::Policy::decide);
    srw64_original_upgrade_stats_step(rdram, ctx);
}
void load_0008F4B0_func_801CF85C(uint8_t* rdram, recomp_context* ctx) {
    // EW swap once all five stats reach the cap: always the original cap.
    upgrades::Scope scope(rdram, upgrades::Policy::original);
    srw64_original_upgrade_ew_check(rdram, ctx);
}
void load_0008F4B0_func_801D0C7C(uint8_t* rdram, recomp_context* ctx) {
    // Selected weapon: price, power preview, gauge, "cannot upgrade further".
    upgrades::Scope scope(rdram, upgrades::Policy::weapon);
    srw64_original_upgrade_weapon_view(rdram, ctx);
}
void load_0008F4B0_func_801D1100(uint8_t* rdram, recomp_context* ctx) {
    // Selected weapon: the confirmed upgrade and full-upgrade weapons.
    upgrades::Scope scope(rdram, upgrades::Policy::weapon);
    srw64_original_upgrade_weapon_step(rdram, ctx);
}
void load_00107BF0_func_801C2600(uint8_t* rdram, recomp_context* ctx) {
    // Sale price of the unit in a0, a1 its row in the sale table.
    const uint32_t row = uint32_t(int8_t(ctx->r5));
    srw64_original_sale_price(rdram, ctx);
    ctx->r2 = int32_t(upgrades::clamp_sale_price(rdram, row, uint32_t(ctx->r2)));
}
void resident_func_8009FA94(uint8_t* rdram, recomp_context* ctx) {
    if (srw64_game_hooks.choice) srw64_game_hooks.choice(rdram);
    srw64::state_probe::capture(rdram,"choice");
    srw64_original_dialogue_choice(rdram, ctx);
}
}
