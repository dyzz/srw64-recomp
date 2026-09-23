#pragma once
#include "recomp.h"

// Installed before starting guest threads. Empty callbacks preserve the original
// routines in the CPU-only and fixed-frame diagnostic hosts.
struct SRW64GameHooks {
    bool (*battle_step)(uint8_t*, recomp_context*, unsigned){};
    bool (*battle_spirit_return)(uint8_t*, recomp_context*){};
    void (*presentation_step)(uint8_t*){};
    bool (*dialogue_step)(uint8_t*, recomp_context*){};
    void (*text_loaded)(uint8_t*, unsigned){};
    void (*text_drawn)(uint8_t*, uint32_t, uint32_t){};
    void (*reset)(uint8_t*){};
    bool (*script_before)(uint8_t*, uint32_t){};
    void (*script_after)(uint8_t*, uint32_t){};
    void (*choice)(uint8_t*){};
    void (*intro_step)(uint8_t*, recomp_context*){};
    void (*name_begin)(uint8_t*, unsigned){};
    bool (*name_step)(uint8_t*, recomp_context*, unsigned){};
    // The upgrade-refund rule just paid `amount` back for the player's machine `unit`.
    void (*refund)(uint8_t*, uint16_t unit, uint32_t amount){};
    // The リンク screen opens / steps. True: the native series page owns it, and the
    // original set-up or step does not run this time.
    bool (*link_begin)(uint8_t*){};
    bool (*link_step)(uint8_t*, recomp_context*){};
    // The インターミッション main menu builds / steps. True: the native page did it.
    // The frame callback runs at every frame boundary.
    // The ユニット改造 screens: machine list build / step (weapons: the 武器改造 list),
    // five-stat build / step, and whether the native page replaces the stat drawing.
    bool (*upgrade_list_build)(uint8_t*, recomp_context*, bool weapons){};
    bool (*upgrade_list_step)(uint8_t*, recomp_context*, void (*step)(uint8_t*, recomp_context*)){};
    bool (*upgrade_stats_build)(uint8_t*, recomp_context*){};
    bool (*upgrade_stats_step)(uint8_t*, recomp_context*){};
    bool (*upgrade_stats_view)(uint8_t*){};
    // 武器改造: the weapon list build / step and the confirm screen build / step.
    bool (*weapon_list_build)(uint8_t*, recomp_context*){};
    bool (*weapon_list_step)(uint8_t*, recomp_context*){};
    bool (*weapon_confirm_build)(uint8_t*, recomp_context*){};
    bool (*weapon_confirm_step)(uint8_t*, recomp_context*){};
    void (*upgrade_frame)(uint8_t*){};
    // 強化パーツ: screens 7 (machine list), 18 (slots and inventory), 19 (owned copies).
    bool (*parts_build)(uint8_t*, recomp_context*, unsigned screen){};
    bool (*parts_step)(uint8_t*, recomp_context*, unsigned screen, void (*step)(uint8_t*, recomp_context*)){};
    void (*parts_frame)(uint8_t*){};
    // ユニット能力／パイロット能力: screens 4 (units), 13 (unit page), 14 (weapons), 5 (pilots), 15 (pilot page).
    bool (*ability_build)(uint8_t*, recomp_context*, unsigned screen){};
    bool (*ability_step)(uint8_t*, recomp_context*, unsigned screen, void (*step)(uint8_t*, recomp_context*)){};
    void (*ability_frame)(uint8_t*){};
    // のりかえ: screens 6 (pilots), 16 (machines), 17 (confirm), 20 (fairies), 21 (fairy targets).
    bool (*swap_build)(uint8_t*, recomp_context*, unsigned screen){};
    bool (*swap_step)(uint8_t*, recomp_context*, unsigned screen, void (*step)(uint8_t*, recomp_context*)){};
    void (*swap_frame)(uint8_t*){};
    bool (*intermission_build)(uint8_t*, recomp_context*){};
    bool (*intermission_step)(uint8_t*, recomp_context*){};
    void (*intermission_frame)(uint8_t*){};
};
extern SRW64GameHooks srw64_game_hooks;
extern "C" void srw64_original_dialogue_step(uint8_t*, recomp_context*);
