#!/usr/bin/env python3
"""Generate CPU code with audited library names and reviewed ROM identities."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tomllib

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))  # tools/, home of the recomp package
from recomp.toolchain.analyze_layout import ROOT, analyze


NATIVE_HOOKS = {
    # Battle-animation main loop (overlay load_00121560). Wrapped read-only so a
    # probe can record the 25-state machine at D_80250000 before any skip or
    # abort feature depends on it.
    "load_00121560_func_801C9710": "srw64_original_battle_animation_step",
    "load_000AB160_func_801DFBD0": "srw64_original_map_dispatch",
    "load_000AB160_func_801C8AB4": "srw64_original_return_to_map",
    "load_000AB160_func_801D5064": "srw64_original_battle_confirm_step",
    "load_000AB160_func_801D5294": "srw64_original_battle_response_step",
    # The animation-off presentation's round queue (src/host/map_miss.hpp): misses the
    # original drops are queued and shown.
    "load_000AB160_func_801FCA78": "srw64_original_map_rounds",
    # Every end of a unit's action spends one (cheats.hpp unlimited_moves).
    "load_000AB160_func_801C29DC": "srw64_original_action_spend",
    # A battle's EXP and kill money (cheats.hpp reward modes), and the steps after the
    # damage: special defenses and てかげん (overkill.hpp).
    "load_000AB160_func_801F60D4": "srw64_original_battle_exp",
    "load_000AB160_func_801F6588": "srw64_original_battle_funds",
    "load_000AB160_func_801F7204": "srw64_original_battle_defense",
    "load_000AB160_func_801F5B78": "srw64_original_battle_spare",
    "load_000AB160_func_801FB8B4": "srw64_original_map_round_push",
    # The map's pilot page: an enemy's learned spirits and SP drawn over its ?????
    # (src/host/enemy_pilot_page.hpp).
    "load_000AB160_func_801E6C5C": "srw64_original_pilot_page",
    # The map's weapon page lists the unit's combination attacks whatever their partners
    # are up to, and its page reset drops the host's lines (src/host/map_combo.hpp).
    "load_000AB160_func_801E6410": "srw64_original_weapon_page",
    "load_000AB160_func_801E61BC": "srw64_original_page_reset",
    "load_000AB160_func_801E7430": "srw64_original_status_pages",
    "resident_func_80082334": "srw64_original_random_bound",
    "resident_func_80085F30": "srw64_original_frame_boundary",
    # The scheduler's retrace handler, for the 120 Hz experiment (src/host/sched_rate.hpp).
    "resident_func_80080A14": "srw64_original_retrace",
    "resident_func_800821B0": "srw64_original_rng_seed",
    "resident_func_800822D8": "srw64_original_rng_next",
    # Every SRAM transfer, for extended slots and autosaves (save_store.cpp).
    "resident_func_80090E5C": "srw64_original_sram_transfer",
    "resident_func_800924D8": "srw64_original_intermission_serialize",
    "resident_func_80093278": "srw64_original_tactical_serialize",
    "resident_func_800927A4": "srw64_original_intermission_restore",
    # The base level new enemies and recruits start from (cheats.hpp level_curve).
    "resident_func_800A4BE0": "srw64_original_base_level",
    "resident_func_800936A0": "srw64_original_tactical_restore",
    # 3D5E opens the 部隊名 page; the name stays the default (docs/native/fixed-unit-name.md).
    "load_000A7EC0_func_801C517C": "srw64_original_unit_name_command",
    "load_001090A0_func_801C5004": "srw64_original_name_selection_init",
    "load_001090A0_func_801C50B8": "srw64_original_name_selection_step",
    "load_001090A0_func_801C5DAC": "srw64_original_name_review_init",
    "load_001090A0_func_801C5E88": "srw64_original_name_review_step",
    "load_001090A0_func_801C5494": "srw64_original_name_player_init",
    "load_001090A0_func_801C5920": "srw64_original_name_partner_init",
    "load_001090A0_func_801C5644": "srw64_original_name_player_step",
    "load_001090A0_func_801C5AD0": "srw64_original_name_partner_step",
    "load_0010DA50_func_801CA9CC": "srw64_original_intro_main",
    "load_0010DA50_func_801C6F3C": "srw64_original_title_medium_build",
    "load_0010DA50_func_801C709C": "srw64_original_title_medium_step",
    "load_0010DA50_func_801C7328": "srw64_original_title_slots_build",
    "load_0010DA50_func_801C783C": "srw64_original_title_slots_step",
    "load_0010DA50_func_801C7A48": "srw64_original_title_confirm_step",
    "load_0010DA50_func_801C7C90": "srw64_original_title_message_build",
    "load_0010DA50_func_801C8074": "srw64_original_title_message_step",
    "load_0010DA50_func_801C697C": "srw64_original_title_options_build",
    "load_0010DA50_func_801C6B14": "srw64_original_title_options_step",
    "load_0010DA50_func_801C86A8": "srw64_original_title_sound_build",
    "load_0010DA50_func_801C9888": "srw64_original_title_karaoke_build",
    "load_0010DA50_func_801C83D0": "srw64_original_title_list_draw",
    "load_0010DA50_func_801C8724": "srw64_original_title_sound_step",
    "load_0010DA50_func_801C89E4": "srw64_original_title_sound_exit_step",
    "load_0010DA50_func_801C9904": "srw64_original_title_karaoke_step",
    "load_0010DA50_func_801C9ADC": "srw64_original_title_karaoke_exit_step",
    "resident_func_8008D748": "srw64_original_dialogue_step",
    "resident_func_8008C9C0": "srw64_original_dialogue_load",
    "resident_func_8008DC40": "srw64_original_dialogue_draw",
    "resident_func_8008EB5C": "srw64_original_text_front_draw",
    "load_000AB160_func_80209900": "srw64_original_map_damage_draw",
    "load_000AB160_func_801CBB04": "srw64_original_move_select",
    "load_000AB160_func_801C8B04": "srw64_original_map_idle",
    # The map's blank-tile menu (state 0x16): its open, its labels (8009D684 builds a window
    # table entry's) and its A dispatch, for the 返回标题画面 row before 中断 (map_menu.hpp).
    "load_000AB160_func_801D13C0": "srw64_original_map_menu_open",
    "resident_func_8009D684": "srw64_original_window_labels",
    "load_000AB160_func_801D17AC": "srw64_original_map_menu_choose",
    "load_000AB160_func_801E4760": "srw64_original_move_range_draw",
    # A move range wider than the original's 31x31 tables (move_range.hpp, BUG10).
    "load_000AB160_func_801C3E50": "srw64_original_move_fill",
    "load_000AB160_func_801C3D34": "srw64_original_move_clear",
    "load_000AB160_func_801E0B94": "srw64_original_move_query",
    "load_000AB160_func_801E0C68": "srw64_original_move_path",
    "load_000AB160_func_801C5810": "srw64_original_move_step",
    "load_000AB160_func_80202848": "srw64_original_ai_near_unit",
    "load_000AB160_func_80202A84": "srw64_original_ai_near_target",
    "load_000AB160_func_801F3268": "srw64_original_unit_on_range",
    "load_000AB160_func_801C4560": "srw64_original_range_paint",
    "load_000AB160_func_801E49A4": "srw64_original_range_draw",
    "resident_func_800945D4": "srw64_original_map_draw",
    # Sprite mode 10: an ability banner and the number beside it (ui_text.cpp).
    "resident_func_8009504C": "srw64_original_banner_number_draw",
    "load_000AB160_func_801E2D54": "srw64_original_terrain_panel_draw",
    "resident_func_800964E4": "srw64_original_portrait_draw",
    "resident_func_80095974": "srw64_original_background_draw",
    "resident_func_80096CD8": "srw64_original_scene_rect_draw",
    "resident_func_8009761C": "srw64_original_scene_quad_draw",
    # The sprites' matrices, once per frame: the battle's are spread across a picture wider
    # than 4:3 for the length of the call (battle_spread.hpp).
    "resident_func_8008261C": "srw64_original_sprite_matrices",
    # The title demo battle's participant records (8009C2DC); the battle viewer rewrites
    # them after the original fills its defaults (battle_viewer.hpp).
    "resident_func_8009C2DC": "srw64_original_demo_battle_fill",
    # A battle quote's pick and a combination attack's pair (battle overlay): in the battle
    # viewer's demo battle the pick skips its after-a-map checks and the pair comes from the
    # viewer's choice (battle_viewer.hpp; the checks are rewritten below).
    "load_00121560_func_8022245C": "srw64_original_battle_quote_pick",
    "load_00121560_func_80221ACC": "srw64_original_combo_quote_pair",
    # The generic quote draw (80222B14): quote.force writes its record there (quote_force.hpp).
    "load_00121560_func_80222B14": "srw64_original_generic_quote_draw",
    # The unit under the map cursor (801C2BFC) and the two-dimensional list over a unit's
    # record (801F9238): a roster entry whose unit record has gone (a unit the event just
    # withdrew, Erlich persuaded in S56) is no unit (game_hooks.cpp).
    "load_000AB160_func_801C2BFC": "srw64_original_unit_under_cursor",
    "load_000AB160_func_801F9238": "srw64_original_list_2d_input",
    "resident_func_8008F5C8": "srw64_original_dialogue_reset",
    "resident_func_8009EFDC": "srw64_original_script_step",
    # The short skip (script_skip.hpp): a skipped dialogue page reads as finished, and
    # sound effects are silent while it runs.
    "resident_func_8008FED4": "srw64_original_dialogue_show",
    "resident_func_8007E8A8": "srw64_original_play_sound",
    "resident_func_8009FA94": "srw64_original_dialogue_choice",
    "resident_func_8009DE7C": "srw64_original_script_register",
    # Story MODs (story_mod.hpp): the poll's scan of a phase's event types, the persuasion
    # check and the opening/set-up/end event start get the host's events after the original's.
    "resident_func_8009E3B0": "srw64_original_event_scan",
    "resident_func_800A4634": "srw64_original_persuade_check",
    "resident_func_8009EDB8": "srw64_original_special_event",
    "resident_func_800A4790": "srw64_original_variables_reset",   # a new game: story MOD flags cleared
    "load_000AB160_func_80209D6C": "srw64_original_stage_map_select",
    "load_000AB160_func_801E1F08": "srw64_original_seisenshi_bonus",
    "load_000AB160_func_801E1F10": "srw64_original_esp_bonus",
    "load_000AB160_func_801E1D64": "srw64_original_potential_bonus",
    "load_000AB160_func_801F4384": "srw64_original_battle_hit_rate",
    "load_000AB160_func_80204254": "srw64_original_hit_estimate",
    "load_000AB160_func_801F5628": "srw64_original_battle_damage",
    "load_000AB160_func_80203418": "srw64_original_damage_estimate",
    "resident_func_800A5054": "srw64_original_wing_kill_restore",
    "resident_func_800A158C": "srw64_original_party_pair_flag",  # BUG06: S55's missing real-type join
    "resident_func_800AA8F4": "srw64_original_weapon_inherit",
    "resident_func_800AAD28": "srw64_original_unit_register",
    # MOD music (music.cpp): the one way a song starts, its fade, and whether it still plays.
    "resident_func_8007E810": "srw64_original_play_song",
    "resident_func_8007E87C": "srw64_original_fade_song",
    "resident_func_80077AD0": "srw64_original_song_playing",
    # A fight's song from the pilot or unit table (music.json "battle").
    "load_000AB160_func_801E07C4": "srw64_original_battle_song",
    "resident_func_800AA464": "srw64_original_unit_remove",
    "resident_func_800AB808": "srw64_original_unit_merge",
    "resident_func_800AA3C4": "srw64_original_unit_delete",
    "load_000AB160_func_8020ABB4": "srw64_original_deploy_record",
    "resident_func_800A5254": "srw64_original_unit_stats",
    "resident_func_800A5F84": "srw64_original_weapon_twin_sync",
    "load_0008F4B0_func_801CF388": "srw64_original_upgrade_list_open",
    "load_0008F4B0_func_801CF564": "srw64_original_upgrade_list_step",
    "load_0008F4B0_func_801D03D0": "srw64_original_weapon_list_open",
    "load_0008F4B0_func_801D04A4": "srw64_original_weapon_list_step",
    "load_0008F4B0_func_801D0600": "srw64_original_weapon_screen_open",
    "load_0008F4B0_func_801D087C": "srw64_original_weapon_screen_step",
    "load_0008F4B0_func_801CF680": "srw64_original_upgrade_open",
    "load_0008F4B0_func_801C80E0": "srw64_original_upgrade_stats_view",
    "load_0008F4B0_func_801CF988": "srw64_original_upgrade_stats_step",
    "load_0008F4B0_func_801CF85C": "srw64_original_upgrade_ew_check",
    "load_0008F4B0_func_801D0C7C": "srw64_original_upgrade_weapon_view",
    "load_0008F4B0_func_801D1100": "srw64_original_upgrade_weapon_step",
    "load_00107BF0_func_801C2600": "srw64_original_sale_price",
    # Link Battler: the Game Boy pak driver becomes a virtual cartridge, and the
    # リンク screen waits for the native series page (link_page.cpp).
    "resident_func_80090F44": "srw64_original_gbpak_open",
    "resident_func_80090FA0": "srw64_original_gbpak_status",
    "resident_func_80090FC4": "srw64_original_gbpak_power",
    "resident_func_800910C4": "srw64_original_gbpak_check_title",
    "resident_func_80091120": "srw64_original_gbpak_enable_ram",
    "resident_func_80091284": "srw64_original_gbpak_transfer",
    # インターミッション main menu: the build draws nothing and the step takes the
    # native page's answers (intermission_page.cpp).
    "load_0008F4B0_func_801CDFB0": "srw64_original_intermission_menu_build",
    "load_0008F4B0_func_801CE19C": "srw64_original_intermission_menu_step",
    # Entering the intermission (autosave.cpp).
    "load_0008F4B0_func_801D8F74": "srw64_original_intermission_enter",
    # 強化パーツ screens 7 / 18 / 19 (parts_page.cpp).
    "load_0008F4B0_func_801D4A00": "srw64_original_parts_list_open",
    "load_0008F4B0_func_801D4A98": "srw64_original_parts_list_step",
    "load_0008F4B0_func_801D4BEC": "srw64_original_parts_slots_open",
    "load_0008F4B0_func_801D4C94": "srw64_original_parts_slots_step",
    "load_0008F4B0_func_801D5168": "srw64_original_parts_holders_open",
    "load_0008F4B0_func_801D51EC": "srw64_original_parts_holders_step",
    # ユニット能力／パイロット能力 screens 4 / 13 / 14 / 5 / 15 (ability_page.cpp).
    "load_0008F4B0_func_801D14BC": "srw64_original_ability_unit_list_open",
    "load_0008F4B0_func_801D1554": "srw64_original_ability_unit_list_step",
    "load_0008F4B0_func_801D16D8": "srw64_original_ability_unit_open",
    "load_0008F4B0_func_801D2030": "srw64_original_ability_unit_step",
    "load_0008F4B0_func_801D2144": "srw64_original_ability_weapons_open",
    "load_0008F4B0_func_801D21F8": "srw64_original_ability_weapons_step",
    "load_0008F4B0_func_801D22E0": "srw64_original_ability_pilot_list_open",
    "load_0008F4B0_func_801D2378": "srw64_original_ability_pilot_list_step",
    "load_0008F4B0_func_801D2480": "srw64_original_ability_pilot_open",
    "load_0008F4B0_func_801D24C8": "srw64_original_ability_pilot_step",
    # データセーブ screens 1 / 9 (save_page.cpp).
    "load_0008F4B0_func_801CEA30": "srw64_original_save_choice_open",
    "load_0008F4B0_func_801CEABC": "srw64_original_save_choice_step",
    "load_0008F4B0_func_801CECE8": "srw64_original_save_slots_open",
    "load_0008F4B0_func_801CEEF8": "srw64_original_save_slots_step",
    # のりかえ screens 6 / 16 / 17 / 20 / 21 (swap_page.cpp).
    "load_0008F4B0_func_801D25A4": "srw64_original_swap_pilots_open",
    "load_0008F4B0_func_801D263C": "srw64_original_swap_pilots_step",
    "load_0008F4B0_func_801D2758": "srw64_original_swap_targets_open",
    "load_0008F4B0_func_801D2A24": "srw64_original_swap_targets_step",
    "load_0008F4B0_func_801D2B64": "srw64_original_swap_confirm_open",
    "load_0008F4B0_func_801D3A90": "srw64_original_swap_confirm_step",
    "load_0008F4B0_func_801D4164": "srw64_original_swap_fairies_open",
    "load_0008F4B0_func_801D41FC": "srw64_original_swap_fairies_step",
    "load_0008F4B0_func_801D42FC": "srw64_original_swap_fairy_targets_open",
    "load_0008F4B0_func_801D4578": "srw64_original_swap_fairy_targets_step",
    # Controller Pak presence (osPfsIsPlug caller): the runtime aborts in osPfsIsPlug, so
    # the host answers "no pak on any port" itself (game_hooks.cpp).
    "resident_func_80090778": "srw64_original_pfs_plug_check",
    # A resource's descriptor (decoded size, offset from the table): a package's own resources
    # past the ROM's 6,436 come from the host (mod_tables.hpp, docs/design/new-unit-ids.md §11).
    "resident_func_80089BBC": "srw64_original_resource_descriptor",
    # The predecessor row of a unit (D_800CA3A0): a package's new unit inherits through the host.
    "resident_func_800AA814": "srw64_original_predecessor_row",
    "resident_func_80085CD4": "srw64_original_slot_headers",
    "load_000AB160_func_801E977C": "srw64_original_combine_check",
    "load_000AB160_func_8020178C": "srw64_original_combo_locks",
    # EN and ammo after an attack: a package's combination attack lists its participants first (mod_combos.hpp).
    "load_000AB160_func_801FC83C": "srw64_original_battle_spend",
    # A battle quote shown, and the dialogue portrait: a new character speaking with a copied
    # voice part shows its own name and face (battle_speaker.hpp).
    "load_00121560_func_801C6F14": "srw64_original_battle_quote_show",
    "resident_func_8008F970": "srw64_original_dialogue_portrait",
    "resident_func_800A74D4": "srw64_original_combine_parts",
    # Registering a unit looks up the recipes of its parts (800A79EC: share the weapons of a part
    # already there; 800A794C: build the combined unit). A package's recipe is for the map's 合体
    # only (mod_combine.hpp recipe_parts), so its parts register on their own. 800AB5C0 is 3D5D.
    "resident_func_800A79EC": "srw64_original_recipe_partner",
    # A unit's upgrades or 強化パーツ changed (a0, a1 1 for the parts): the original copies them to
    # its form family and its combine parts; a package's combine of units that are whole on
    # their own (no `over`) keeps the parts' own (mod_combine.hpp keep_parts).
    "resident_func_800A5924": "srw64_original_upgrade_spread",
    "resident_func_800A794C": "srw64_original_recipe_build",
    "resident_func_800AB5C0": "srw64_original_deploy_form",
    "load_0008F4B0_func_801D6FF4": "srw64_original_link_open",
    "load_0008F4B0_func_801D70FC": "srw64_original_link_step",
}


# The tactical map's screen-width constants (docs/design/deck-16x10.md §5, src/host/wide_map.hpp).
# Keyed by instruction address: the literal in the statement after that address's comment
# becomes srw64_map_x(literal, halves), the original plus halves/2 of the widened view's
# extra width (0 at 4:3, so the original's behaviour is unchanged there): 2 for a right
# edge or a width, 1 for a centre, negative when the literal is subtracted. A float loaded
# by lui is written as its full bits and becomes srw64_map_x_float(bits, halves); a string
# in place of halves is a whole expression for the literal.
MAP_WIDTH_CONSTANTS = {
    # Overlays share addresses, so each is keyed by its function's generated name too.
    ("srw64_original_map_draw", 0x80094A94): ("0X149", 2),             # 800945D4 draws a cell while its x + 16 < 329
    ("load_000AB160_func_801FFCB0", 0x801FFCB8): ("0X98", 1),          # centre a point: scroll = 152 - x
    ("load_000AB160_func_801FFCB0", 0x801FFCEC): ("0X140", 2),         # ...keeping the map's right edge in view
    ("load_000AB160_func_801FFADC", 0x801FFBA0): ("S32(0X4388 << 16)", 2),  # follow the cursor past x = 272.0
    ("load_000AB160_func_801FFADC", 0x801FFBD0): ("0X110", 2),         # ...scrolling to put it at 272
    ("load_000AB160_func_801FFE34", 0x801FFE44): ("0X101", 2),         # a point is in view while 32 <= x <= 288
    ("load_000AB160_func_801FFE78", 0x801FFE84): ("-0X60", -1),        # the centre box starts at x = 96...
    ("load_000AB160_func_801E44BC", 0x801E45EC): ("0X140", 2),         # the grid's vertical lines up to x = 320
    ("load_000AB160_func_801E44BC", 0x801E46F4): ("0X140", 2),         # ...its horizontal lines end at 320
    ("load_000AB160_func_801E44BC", 0x801E4700): ("0X140", 2),
    # Windows that open beside a unit stay on the centred 320 while units move in the wider
    # view: their anchor is the unit's x less half the extra width, so they still open next to it.
    ("load_000AB160_func_801CABAC", 0x801CAC6C): ("0XBD", 1),          # terrain panel: which side of x = 189
    ("load_000AB160_func_801CABAC", 0x801CAC74): ("-0X22", -1),        # ...then 34 to the left
    ("load_000AB160_func_801C9D08", 0x801C9D44): ("-0X20", -1),        # a unit's window: 32 to its left
    # The map battle's HP window (801E3AE4, f2 the unit's x; its gauge and damage figure follow
    # it on the centred 320): the anchor never goes below 0, which 801E3BEC's unsigned compare
    # (x < 257) took for past the right edge, moving the window to 240 (wide_map.cpp).
    ("load_000AB160_func_801E3AE4", 0x801E3B1C): ("MEM_W(ctx->r1, -0XA2C)", "srw64_map_anchor_scroll(MEM_W(ctx->r1, -0XA2C), ctx->f2.fl)"),
    # The figure the window's place sets (80101DAC = anchor + 17; the map battle's damage, map_miss)
    # is drawn by 80209900 in the widened view, like a spirit's +10 (801D92E4: unit x + the raw
    # scroll), so it takes the half back that the anchor gave up: the three mode paths.
    ("load_000AB160_func_801E3AE4", 0x801E3C30): ("0X11", 1),
    ("load_000AB160_func_801E3AE4", 0x801E3C40): ("0X11", 1),
    ("load_000AB160_func_801E3AE4", 0x801E3CFC): ("0X11", 1),
}

# The cheats ride on the same rewrite. Morale stops at 150 in eight places, all reading
# the pilot record (cheats.hpp morale modes; srw64_morale_cap(pilot) is 150 for anyone
# outside the player's table): a spirit's raise (801E1380, which sign-extends cap - morale
# to a byte, so the cap is kept within 127 of the morale), the 精神 menu's "morale is full"
# tests (pilot = unit + 0x38 + 4 * a2), and the battle's three raises (801FAC24: the
# side's, the other side's and the damaged pilots'), each a test against 151 and a store
# of 150. 801F5ACC caps the damage at 65535.0; the uncapped value in $f22 goes to
# overkill.hpp first, which also gives the figures on screen the damage uncut.
SPIRIT_PILOT = "MEM_W(0X38, ADD32(S32(ctx->r6 << 2), ctx->r3))"
MAP_WIDTH_CONSTANTS.update({
    ("load_000AB160_func_801E1380", 0x801E1460): ("0X96", "srw64_morale_room(ctx->r4, ctx->r3)"),
    ("load_000AB160_func_801EFDE4", 0x801EFEA4): ("0X96", f"srw64_morale_cap({SPIRIT_PILOT})"),
    ("load_000AB160_func_801EFDE4", 0x801F0018): ("0X96", f"srw64_morale_cap({SPIRIT_PILOT})"),
    ("load_000AB160_func_801F1094", 0x801F112C): ("0X96", f"srw64_morale_cap({SPIRIT_PILOT})"),
    ("load_000AB160_func_801FAC24", 0x801FAD5C): ("0X97", "(srw64_morale_cap(ctx->r3) + 1)"),
    ("load_000AB160_func_801FAC24", 0x801FAD64): ("ctx->r13", "srw64_morale_cap(ctx->r3)"),
    ("load_000AB160_func_801FAC24", 0x801FAEA4): ("0X97", "(srw64_morale_cap(ctx->r4) + 1)"),
    ("load_000AB160_func_801FAC24", 0x801FAEB4): ("ctx->r14", "srw64_morale_cap(ctx->r4)"),
    ("load_000AB160_func_801FAC24", 0x801FAFA8): ("0X97", "(srw64_morale_cap(ctx->r3) + 1)"),
    ("load_000AB160_func_801FAC24", 0x801FAFB0): ("ctx->r8", "srw64_morale_cap(ctx->r3)"),
    ("srw64_original_battle_damage", 0x801F5ACC): ("S32(0X477F << 16)", "srw64_damage_real(ctx->f22.fl, S32(0X477F << 16))"),
    # The damage figures, cut to the HP left; each read here only draws the digits (the HP
    # drains from copies taken earlier): the map's float (sub-state 5 of 0x4B, 801FCC6C) and
    # the battle animation's number (8009B1EC(0x17, ...)).
    ("load_000AB160_func_801D8BB0", 0x801D8CB0): ("MEM_HU(ctx->r1, 0X7862)", "srw64_map_figure(rdram, MEM_HU(ctx->r1, 0X7862))"),
    ("load_00121560_func_801C8E1C", 0x801C8F20): ("MEM_HU(ctx->r16, 0X1C)", "srw64_battle_figure(rdram, MEM_HU(ctx->r16, 0X1C))"),
    ("load_00121560_func_801C8F4C", 0x801C9184): ("MEM_HU(ctx->r16, 0X1C)", "srw64_battle_figure(rdram, MEM_HU(ctx->r16, 0X1C))"),
    # The intermission menu's 返回标题画面 row before its last (intermission_row.hpp): the
    # cursor (801C4A20's last index) one row further in the nine-item and the two-item menu,
    # and the nine-item menu's tenth row read as its ninth for the next screen (cursor + 1).
    ("srw64_original_intermission_menu_step", 0x801CE1D0): ("0X8", "0X9"),
    ("srw64_original_intermission_menu_step", 0x801CE234): ("0X1", "0X2"),
    ("srw64_original_intermission_menu_step", 0x801CE1E8): ("MEM_H(ctx->r2, -0X2AB8)", "(MEM_H(ctx->r2, -0X2AB8) == 9 ? 8 : MEM_H(ctx->r2, -0X2AB8))"),
    # The battle quote pick's codes 5000-9999 (a female opponent, the combination attacks) are
    # tested only after a map (the previous mode D_8015DD60 is 3), and the female test reads
    # the opponent's unit -> pilot -> flags, a stand-in in a demo battle. In the battle
    # viewer's demo battle (srw64_quote_demo) the test runs and finds no female opponent.
    ("srw64_original_battle_quote_pick", 0x80222564): ("MEM_B(ctx->r3, -0X22A0)", "(srw64_quote_demo(rdram) ? 3 : MEM_B(ctx->r3, -0X22A0))"),
    ("srw64_original_battle_quote_pick", 0x802225D8): ("MEM_W(ctx->r2, 0X38)", "(srw64_quote_demo(rdram) ? 0 : MEM_W(ctx->r2, 0X38))"),
    ("srw64_original_battle_quote_pick", 0x802225DC): ("MEM_BU(ctx->r2, 0X4)", "(srw64_quote_demo(rdram) ? 0 : MEM_BU(ctx->r2, 0X4))"),
})

# New unit and weapon numbers (docs/design/new-unit-ids.md §3): the tables indexed by unit or
# weapon number are read at a virtual ROM window the host serves (mod_tables.hpp), the ROM's
# rows followed by the packages' new ones. Each reader builds the table's address with lui +
# addiu; the addiu's literal moves to the window. The layout is mod_tables.hpp's, which
# tests/test_table_window.py keeps in step with this one.
TABLE_WINDOW, TABLE_BLOCK = 0x08000000, 0x00100000
TABLE_BLOCKS = {   # key: (block, ROM address)
    "units": (0, 0x71B80), "weapons": (1, 0x74E90), "status_offsets": (2, 0x7D980), "unit_songs": (3, 0x7DF30),
    "weapon_lists": (4, 0x7E210), "poses": (5, 0x84E40), "battle_units": (6, 0x118610),
    "battle_weapons": (7, 0x119970), "scenes": (8, 0x11E3D0), "anim_offsets": (9, 0x11FC80),
    # New characters (docs/design/unit-mod-kit.md §5.4).
    "pilot_records": (13, 0x7A1A0), "thresholds": (14, 0x7B1B0), "spirit_records": (15, 0x7CFB0),
    "portraits": (16, 0x84220), "pilot_songs": (17, 0x7D6A0),
    # Battle quotes by voice part (unit-mod-kit.md §7): a new character's own part reads its
    # generic segments and the offset of its conditional list (mod_quotes.hpp).
    "quote_segments": (18, 0x1161C0), "quote_index": (19, 0x121150),
}
# The map icons (ROM 0x100D78) live in the tactical map overlay at 80218218: their 23 readers
# load from a copy the host keeps in free RDRAM.
MAP_ICONS_RAM, MAP_ICONS_COPY = 0x80218218, 0x807D0000
TABLE_READERS = {   # (function, instruction): (table, the addiu's literal, lui's value)
    ("srw64_original_demo_battle_fill", 0x8009C44C): ("units", 0x1B80, 0x70000),
    ("srw64_original_unit_stats", 0x800A52B0): ("units", 0x1B80, 0x70000),
    ("resident_func_800A6E68", 0x800A6F90): ("units", 0x1B80, 0x70000),
    ("resident_func_800A8CE8", 0x800A8D2C): ("units", 0x1B80, 0x70000),
    ("resident_func_800A642C", 0x800A6440): ("weapons", 0x4E90, 0x70000),
    ("resident_func_8009C6E4", 0x8009C71C): ("status_offsets", -0x2680, 0x80000),
    ("load_000AB160_func_801E085C", 0x801E0B70): ("unit_songs", -0x20D0, 0x80000),
    ("resident_func_800A67C8", 0x800A680C): ("weapon_lists", -0x1DF0, 0x80000),
    ("resident_func_8009C864", 0x8009C8A8): ("poses", 0x4E40, 0x80000),
    ("load_00121560_func_801C30D0", 0x801C30F4): ("battle_units", -0x79F0, 0x120000),
    ("load_00216730_func_801C2634", 0x801C2658): ("battle_units", -0x79F0, 0x120000),
    ("load_00121560_func_801C3278", 0x801C329C): ("battle_weapons", -0x6690, 0x120000),
    ("load_00216730_func_801C27DC", 0x801C2800): ("battle_weapons", -0x6690, 0x120000),
    ("load_00121560_func_801C3170", 0x801C3190): ("scenes", -0x1C30, 0x120000),
    ("load_00216730_func_801C26D4", 0x801C26F4): ("scenes", -0x1C30, 0x120000),
    ("load_00121560_func_801C3128", 0x801C3138): ("anim_offsets", -0x380, 0x120000),
    ("load_00216730_func_801C268C", 0x801C269C): ("anim_offsets", -0x380, 0x120000),
    ("resident_func_800A6398", 0x800A63C4): ("pilot_records", -0x5E60, 0x80000),
    ("resident_func_800A6340", 0x800A637C): ("thresholds", -0x4E50, 0x80000),
    ("resident_func_800A63E0", 0x800A6410): ("spirit_records", -0x3050, 0x80000),
    ("resident_func_8009C768", 0x8009C7A0): ("portraits", 0x4220, 0x80000),
    ("load_000AB160_func_801E085C", 0x801E08B4): ("pilot_songs", -0x2960, 0x80000),
    ("load_000AB160_func_801E085C", 0x801E0AAC): ("pilot_songs", -0x2960, 0x80000),
    ("srw64_original_generic_quote_draw", 0x80222B98): ("quote_segments", 0x61C0, 0x110000),
    ("load_00121560_func_80222CAC", 0x80222CBC): ("quote_index", 0x1150, 0x120000),
}
# The character -> record maps (D_800CA9C4 ability, D_800CA6F4 spirits; s16 × 360 in resident
# RAM, built as 0x800D0000 plus a literal) are read at copies in free RDRAM with room for new
# characters (table_window.hpp; mod_pilots.hpp fills them). The ability map has two kinds of
# readers: the stats', and the battle quotes' (its value is the voice slot), which read a copy
# where a new character has the voice of the character it borrows.
PILOT_STATS_COPY, PILOT_VOICE_COPY, PILOT_SPIRIT_COPY = 0x807D2000, 0x807D2800, 0x807D3000
PILOT_MAP_READERS = {   # (function, instruction): (literal, copy)
    ("srw64_original_demo_battle_fill", 0x8009C414): ("-0X563C", PILOT_STATS_COPY),
    ("resident_func_800A6340", 0x800A6358): ("-0X563C", PILOT_STATS_COPY),
    ("resident_func_800A6398", 0x800A63B0): ("-0X563C", PILOT_STATS_COPY),
    ("load_000AB160_func_801F3578", 0x801F3664): ("-0X563C", PILOT_VOICE_COPY),
    ("load_000AB160_func_801F3578", 0x801F3AB8): ("-0X563C", PILOT_VOICE_COPY),
    ("srw64_original_battle_quote_show", 0x801C6F90): ("-0X563C", PILOT_VOICE_COPY),
    ("load_00121560_func_80222050", 0x80222318): ("-0X563C", PILOT_VOICE_COPY),
    ("srw64_original_generic_quote_draw", 0x80222B7C): ("-0X563C", PILOT_VOICE_COPY),
    ("load_00216730_func_801C2CAC", 0x801C2D18): ("-0X563C", PILOT_VOICE_COPY),
    ("resident_func_800A2C18", 0x800A2C50): ("-0X590C", PILOT_SPIRIT_COPY),
    ("resident_func_800A2C18", 0x800A2DB4): ("-0X590C", PILOT_SPIRIT_COPY),
    ("resident_func_800A63E0", 0x800A63F8): ("-0X590C", PILOT_SPIRIT_COPY),
    ("resident_func_800A7F8C", 0x800A8084): ("-0X590C", PILOT_SPIRIT_COPY),
}
MAP_ICON_READERS = [   # (function, instruction, the statement's literal)
    ("load_000AB160_func_801C5DCC", 0x801C5F1C), ("load_000AB160_func_801C60A4", 0x801C60C0),
    ("load_000AB160_func_801C835C", 0x801C84FC), ("load_000AB160_func_801CCAC4", 0x801CCD40),
    ("load_000AB160_func_801CE510", 0x801CE820), ("load_000AB160_func_801CECF0", 0x801CEDB4),
    ("load_000AB160_func_801CF9A0", 0x801CFD10), ("load_000AB160_func_801CFD9C", 0x801CFEA8),
    ("load_000AB160_func_801D07B4", 0x801D0AC8), ("load_000AB160_func_801D4794", 0x801D4B04),
    ("load_000AB160_func_801DC0AC", 0x801DC244), ("load_000AB160_func_801DCBD8", 0x801DCDC8),
    ("load_000AB160_func_801DD9E8", 0x801DE044), ("load_000AB160_func_801ED864", 0x801EDA08),
    ("load_000AB160_func_801FD99C", 0x801FDAF4), ("load_000AB160_func_801FD99C", 0x801FDC80),
    ("load_000AB160_func_8020B324", 0x8020BAF8), ("load_000AB160_func_8020BE34", 0x8020C308),
    ("load_000AB160_func_8020E860", 0x8020EA48), ("load_000AB160_func_80210758", 0x80210BEC),
    ("load_000AB160_func_80210758", 0x8021136C), ("load_000AB160_func_802132C0", 0x802133D0),
    ("load_000AB160_func_80213AAC", 0x80213C5C),
]


def literal(value: int) -> str:
    return f"-0X{-value:X}" if value < 0 else f"0X{value:X}"


for (function, address), (table, low, high) in TABLE_READERS.items():
    block, rom = TABLE_BLOCKS[table]
    MAP_WIDTH_CONSTANTS[(function, address)] = (literal(low), literal(low + TABLE_WINDOW + block * TABLE_BLOCK - rom))
for function, address in MAP_ICON_READERS:
    MAP_WIDTH_CONSTANTS[(function, address)] = ("-0X7DE8", literal(-0x7DE8 + MAP_ICONS_COPY - MAP_ICONS_RAM))
for (function, address), (low, copy) in PILOT_MAP_READERS.items():
    MAP_WIDTH_CONSTANTS[(function, address)] = (low, literal(copy - 0x800D0000))
# The love / friendship pairs (D_80218754 in the tactical map's overlay, 47 rows of 8 bytes:
# +2 holder, +4/+6 partners) are read at a copy in free RDRAM with room for the packages'
# rows (mod_pilots.hpp fills it): the reads move there, the row loops run over its capacity
# (rows past the ROM's 47 are -1 holders, which no pilot matches).
LOVE_RAM, LOVE_COPY, LOVE_ROWS = 0x80218754, 0x807D3800, 128
LOVE_READERS = [   # (function, instruction, literal): every read is lui 0x8022 plus the literal
    ("load_000AB160_func_801F4908", 0x801F4BE4, -0x78AA), ("load_000AB160_func_801F4908", 0x801F4C6C, -0x78A8),
    ("load_000AB160_func_801F4908", 0x801F4C80, -0x78A6),
    ("load_000AB160_func_801F4DF0", 0x801F4E08, -0x78AA), ("load_000AB160_func_801F4DF0", 0x801F4E1C, -0x78A8),
    ("load_000AB160_func_801F4DF0", 0x801F4E30, -0x78A6),
    ("load_000AB160_func_801F4E64", 0x801F51AC, -0x78A8), ("load_000AB160_func_801F4E64", 0x801F52E0, -0x78AA),
    ("load_000AB160_func_801F4E64", 0x801F533C, -0x78A8), ("load_000AB160_func_801F4E64", 0x801F5358, -0x78A6),
    ("load_000AB160_func_801F4E64", 0x801F5384, -0x78AA),
]
for function, address, low in LOVE_READERS:
    MAP_WIDTH_CONSTANTS[(function, address)] = (literal(low), literal(low + LOVE_COPY - LOVE_RAM))
for function, address, old in (("load_000AB160_func_801F4908", 0x801F4DAC, 0x2F), ("load_000AB160_func_801F4E64", 0x801F543C, 0x2F),
                               ("load_000AB160_func_801F4DF0", 0x801F4E4C, 0x178)):
    MAP_WIDTH_CONSTANTS[(function, address)] = (literal(old), literal(LOVE_ROWS if old == 0x2F else LOVE_ROWS * 8))
# A unit's and a weapon's name are text records at 527 + unit, 1370 + weapon and 2699 +
# weapon (its menu name); a new number takes a record of the host's (mod_tables.hpp
# name_record), so the literal becomes srw64_name_record(kind, the number's register).
NAME_RECORDS = {   # (function, instruction): (U unit, W weapon, M weapon menu; the number's register)
    ("load_0008F4B0_func_801C6C78", 0x801C6DE4): ("U", "r5"),
    ("load_0008F4B0_func_801C75C0", 0x801C77B4): ("M", "r5"),
    ("load_0008F4B0_func_801C8854", 0x801C89B4): ("U", "r5"),
    ("load_0008F4B0_func_801C8ED8", 0x801C90CC): ("U", "r5"),
    ("load_0008F4B0_func_801CA8AC", 0x801CAA1C): ("U", "r5"),
    ("load_0008F4B0_func_801CAC84", 0x801CAE60): ("U", "r5"),
    ("load_0008F4B0_func_801CB230", 0x801CB404): ("U", "r5"),
    ("load_0008F4B0_func_801CB5A0", 0x801CB70C): ("U", "r5"),
    ("load_0008F4B0_func_801CBFAC", 0x801CC040): ("U", "r5"),
    ("load_0008F4B0_func_801CCA80", 0x801CCC50): ("U", "r5"),
    ("srw64_original_upgrade_open", 0x801CF78C): ("U", "r5"),
    ("srw64_original_weapon_screen_open", 0x801D0668): ("M", "r5"),
    ("srw64_original_weapon_screen_open", 0x801D06D0): ("M", "r5"),
    ("srw64_original_upgrade_weapon_view", 0x801D0FE4): ("M", "r5"),
    ("srw64_original_ability_unit_open", 0x801D17B8): ("U", "r5"),
    ("srw64_original_swap_confirm_open", 0x801D2EFC): ("U", "r17"),
    ("load_0008F4B0_func_801DBAD8", 0x801DBC04): ("U", "r5"),
    ("load_000AB160_func_801CF2EC", 0x801CF5F8): ("U", "r5"),
    ("load_000AB160_func_801E5C30", 0x801E5CFC): ("U", "r23"),
    ("load_000AB160_func_801E5C30", 0x801E5E74): ("U", "r23"),
    ("srw64_original_weapon_page", 0x801E66BC): ("M", "r5"),
    ("srw64_original_status_pages", 0x801E753C): ("U", "r5"),
    ("srw64_original_status_pages", 0x801E7778): ("U", "r5"),
    ("load_000AB160_func_801E7A28", 0x801E7AD4): ("U", "r5"),
    ("load_000AB160_func_801E7A28", 0x801E7B58): ("W", "r5"),
    ("load_000AB160_func_801EC3A0", 0x801EC8F0): ("U", "r5"),
    ("load_000AB160_func_801ECE88", 0x801ED304): ("U", "r5"),
    ("load_000AB160_func_8020D754", 0x8020D884): ("U", "r5"),
    ("load_00107BF0_func_801C2B18", 0x801C2EF8): ("U", "r5"),
    ("load_0010DA50_func_801C9458", 0x801C9524): ("U", "r16"),
    ("load_00216730_func_801C29A4", 0x801C2A30): ("W", "r5"),
    ("load_00216730_func_801C3028", 0x801C31AC): ("U", "r5"),
    ("load_00216730_func_801C3028", 0x801C32A0): ("W", "r5"),
    # A character's name (P, 4382 + n) and full name (F, 4743 + n).
    ("load_0008F4B0_func_801C6F80", 0x801C7230): ("P", "r5"),
    ("load_0008F4B0_func_801C8854", 0x801C8A70): ("P", "r5"),
    ("load_0008F4B0_func_801C8BAC", 0x801C8C84): ("P", "r5"),
    ("load_0008F4B0_func_801C8ED8", 0x801C9040): ("P", "r5"),
    ("load_0008F4B0_func_801C936C", 0x801C94C8): ("F", "r5"),
    ("load_0008F4B0_func_801CA524", 0x801CA604): ("P", "r5"),
    ("load_0008F4B0_func_801CA8AC", 0x801CAAC8): ("P", "r5"),
    ("load_0008F4B0_func_801CAC84", 0x801CADE4): ("P", "r5"),
    ("load_0008F4B0_func_801CAFE0", 0x801CB098): ("P", "r5"),
    ("load_0008F4B0_func_801CB230", 0x801CB398): ("P", "r5"),
    ("load_0008F4B0_func_801CB5A0", 0x801CB7D4): ("P", "r5"),
    ("load_0008F4B0_func_801CCA80", 0x801CCC90): ("P", "r5"),
    ("srw64_original_swap_targets_open", 0x801D2820): ("F", "r5"),
    ("srw64_original_swap_targets_open", 0x801D2910): ("P", "r5"),
    ("srw64_original_swap_confirm_open", 0x801D2C50): ("P", "r17"),
    ("srw64_original_swap_confirm_open", 0x801D2CF0): ("P", "r5"),
    ("srw64_original_swap_fairy_targets_open", 0x801D43C0): ("F", "r5"),
    ("srw64_original_swap_fairy_targets_open", 0x801D448C): ("P", "r5"),
    ("load_0008F4B0_func_801D5DE8", 0x801D5F64): ("P", "r5"),
    ("load_0008F4B0_func_801D65B8", 0x801D6940): ("P", "r5"),
    ("srw64_original_link_step", 0x801D7348): ("P", "r5"),
    ("load_0008F4B0_func_801DB240", 0x801DB37C): ("P", "r3"),
    ("load_0008F4B0_func_801DB7D8", 0x801DB8F4): ("P", "r3"),
    ("load_000AB160_func_801E5C30", 0x801E5CC8): ("P", "r18"),
    ("load_000AB160_func_801E5C30", 0x801E5E40): ("P", "r18"),
    ("load_000AB160_func_801E5C30", 0x801E5FB8): ("P", "r5"),
    ("srw64_original_pilot_page", 0x801E6DD4): ("F", "r5"),
    ("srw64_original_status_pages", 0x801E7500): ("P", "r5"),
    ("load_000AB160_func_801E7A28", 0x801E7AF8): ("P", "r5"),
    ("resident_func_80081130", 0x8008116C): ("P", "r16"),
    ("resident_func_80081130", 0x80081170): ("F", "r16"),
    ("load_000AB160_func_801EC3A0", 0x801EC954): ("P", "r5"),
    ("load_000AB160_func_801EC3A0", 0x801ECA08): ("P", "r5"),
    ("load_000AB160_func_801EC3A0", 0x801ECAE4): ("P", "r5"),
    ("load_000AB160_func_801ECE88", 0x801ED29C): ("P", "r5"),
    ("load_000AB160_func_801EEED8", 0x801EF050): ("P", "r5"),
    ("load_000AB160_func_801EF164", 0x801EF33C): ("P", "r5"),
    ("load_000AB160_func_801F9EB4", 0x801FA16C): ("P", "r5"),
    ("load_000AB160_func_8020D4FC", 0x8020D5B0): ("P", "r5"),
    ("load_00107BF0_func_801C2B18", 0x801C2F6C): ("P", "r5"),
    ("load_0010DA50_func_801C8C44", 0x801C8D1C): ("P", "r3"),
    ("load_0010DA50_func_801C8D74", 0x801C8DC8): ("F", "r3"),
    ("load_00216730_func_801C3028", 0x801C3158): ("P", "r5"),
    ("resident_func_8008F648", 0x8008F8E4): ("P", "r5"),
}
# The loader's cap on resource numbers (80089E9C refuses 6,436 up, returning -2): the host
# raises it by exactly the packages' own resources (mod_tables.hpp), so a number none gives is
# refused as before instead of reading a descriptor past the ROM's table.
MAP_WIDTH_CONSTANTS[("resident_func_80089E9C", 0x80089EAC)] = ("0X1924", "srw64_resource_limit()")
# A battle quote's weapon (8022241C, the quote picker's): field 0 of the weapon's battle record,
# or -1 for a weapon number past the ROM's 1,329, so a package's new weapon (1400-2399,
# table_window.hpp; its battle record is in the table window) never had its weapon lines.
MAP_WIDTH_CONSTANTS[("load_00121560_func_8022241C", 0x80222424)] = ("0X531", "0X960")
# The のりかえ candidates (801C5618) count roster units per class in eight u16s on its stack
# (classes 0-7) and read them back by the pilot's class: a class above 7 would run past them.
# The index is masked with 7, so class 8 (mod_data.hpp, the one a package may add) counts in
# class 0's slot, which no unit takes (class 0 is skipped before counting) and no pilot reads.
for address, statement in ((0x801C56BC, "ctx->r3 << 1"), (0x801C57A8, "ctx->r3 << 1"), (0x801C5840, "ctx->r3 << 1")):
    MAP_WIDTH_CONSTANTS[("load_0008F4B0_func_801C5618", address)] = (statement, "(ctx->r3 & 7) << 1")
# The combined-unit table (src/host/mod_combine.hpp): its list of combined units D_800CB40C
# and their record pointers D_800CB5A4 are read at copies in free RDRAM that have room for
# a package's combined units (the originals are followed by other tables). Both are built
# as 0x800D0000 plus a literal; the literal becomes the copy's.
COMBINE_LIST, COMBINE_POINTERS = 0x800CB40C, 0x800CB5A4
COMBINE_LIST_COPY, COMBINE_POINTERS_COPY, COMBINE_CAPACITY = 0x807D1000, 0x807D1080, 0x20
COMBINE_LIST_READERS = [("resident_func_800A8880", 0x800A8888), ("load_000AB160_func_801E7EA8", 0x801E7F34)]
COMBINE_POINTER_READERS = [
    ("load_0008F4B0_func_801D33F4", 0x801D350C), ("load_000AB160_func_801E1688", 0x801E16F4),
    ("load_000AB160_func_801E7EA8", 0x801E7F6C), ("load_000AB160_func_801E8344", 0x801E8548),
    ("srw64_original_upgrade_spread", 0x800A5A80), ("srw64_original_combine_parts", 0x800A750C),
    ("resident_func_800A9DCC", 0x800A9F50), ("resident_func_800AA210", 0x800AA318),
    ("resident_func_800AAB5C", 0x800AABA8), ("resident_func_800AC220", 0x800AC468),
    ("resident_func_800AC50C", 0x800AC598), ("srw64_original_weapon_inherit", 0x800AA958)]
for function, address in COMBINE_LIST_READERS:
    MAP_WIDTH_CONSTANTS[(function, address)] = ("-0X4BF4", literal(COMBINE_LIST_COPY - 0x800D0000))
for function, address in COMBINE_POINTER_READERS:
    MAP_WIDTH_CONSTANTS[(function, address)] = ("-0X4A5C", literal(COMBINE_POINTERS_COPY - 0x800D0000))
# The combine recipes (the map's 合体, mod_combine.hpp fill_recipes): the parts list D_800CAFEC
# has one walk (800A8A90), its record pointers D_800CB390 three readers indexed by its answer.
RECIPE_LIST_COPY, RECIPE_POINTERS_COPY = 0x807D1400, 0x807D1480
RECIPE_LIST_READERS = [("resident_func_800A8A90", 0x800A8A98)]
RECIPE_POINTER_READERS = [("srw64_original_recipe_build", 0x800A79BC), ("srw64_original_recipe_partner", 0x800A7A68),
                          ("srw64_original_combine_check", 0x801E98A8)]
for function, address in RECIPE_LIST_READERS:
    MAP_WIDTH_CONSTANTS[(function, address)] = ("-0X5014", literal(RECIPE_LIST_COPY - 0x800D0000))
for function, address in RECIPE_POINTER_READERS:
    MAP_WIDTH_CONSTANTS[(function, address)] = ("-0X4C70", literal(RECIPE_POINTERS_COPY - 0x800D0000))
# The form families D_800CAC94 (unit numbers, -1) and their record pointers D_800CAEF0 move the
# same way (unit-mod-kit.md §4.6; mod_forms.hpp fills the copies): 800A87FC is the one walk of the
# list, and every user of a family's record indexes the pointers by what it answers. 801E174C
# reads through 801E170C's base, moved with it.
FORMS_LIST_COPY, FORMS_POINTERS_COPY = 0x807D4000, 0x807D4100
FORMS_LIST_READERS = [("resident_func_800A87FC", 0x800A8804)]
FORMS_POINTER_READERS = [
    ("load_0008F4B0_func_801D33F4", 0x801D3470), ("load_000AB160_func_801E1688", 0x801E170C),
    ("load_000AB160_func_801E1688", 0x801E17C8), ("load_000AB160_func_801E8B28", 0x801E8C80),
    ("load_000AB160_func_801E8ED0", 0x801E8F38), ("srw64_original_upgrade_spread", 0x800A597C),
    ("resident_func_800A7330", 0x800A7360), ("resident_func_800A81E0", 0x800A8274),
    ("resident_func_800A9DCC", 0x800A9E4C), ("resident_func_800AA210", 0x800AA268),
    ("resident_func_800AAB5C", 0x800AAC70), ("resident_func_800AC078", 0x800AC0AC),
    ("srw64_original_weapon_inherit", 0x800AA9E4)]
for function, address in FORMS_LIST_READERS:
    MAP_WIDTH_CONSTANTS[(function, address)] = ("-0X536C", literal(FORMS_LIST_COPY - 0x800D0000))
for function, address in FORMS_POINTER_READERS:
    MAP_WIDTH_CONSTANTS[(function, address)] = ("-0X5110", literal(FORMS_POINTERS_COPY - 0x800D0000))
# 801E7EA8 (where a separated unit's parts stand) looks for the unit in the first 11 entries
# only, and takes index 11 for "not there" (two literals: the loop's end and its delay slot).
for address in (0x801E7F44, 0x801E7F4C, 0x801E7F58):
    MAP_WIDTH_CONSTANTS[("load_000AB160_func_801E7EA8", address)] = ("0XB", literal(COMBINE_CAPACITY))
NAME_BASES = {"U": (0, "0X20F"), "W": (1, "0X55A"), "M": (2, "0XA8B"), "P": (3, "0X111E"), "F": (4, "0X1287")}
for (function, address), (kind, register) in NAME_RECORDS.items():
    code, base = NAME_BASES[kind]
    MAP_WIDTH_CONSTANTS[(function, address)] = (
        base, f"(int32_t)(srw64_name_record({code}, (uint32_t)ctx->{register}) - (uint32_t)ctx->{register})")

# 8008AE90 builds each frame's display list by calling every render node's function: sprites
# at 8008B3FC (cursor, slot, sub), callbacks at 8008B3E0 (cursor). Both calls go through the
# host's srw64_render_node (game_hooks.cpp), which may place the draw in the widened map view.
RENDER_NODE_CALLS = {0x8008B3E0: 0, 0x8008B3FC: 1}


def bind_render_nodes(source: str) -> str:
    for address, sprite in RENDER_NODE_CALLS.items():
        pattern = re.compile(rf"(    // 0x{address:08X}: jalr        \$v0\n    // 0x[0-9A-F]{{8}}: [^\n]*\n    [^\n]*\n)    LOOKUP_FUNC\(ctx->r2\)\(rdram, ctx\);")
        source, count = pattern.subn(rf"\1    srw64_render_node(rdram, ctx, (int32_t)ctx->r2, {sprite});", source)
        if count > 1 or (count == 0 and "RECOMP_FUNC void resident_func_8008AE90(" in source):
            raise RuntimeError(f"render node call {address:#x} rewritten {count} times")
    return source


def bind_map_width(source: str, found: dict[tuple[str, int], int]) -> str:
    """Rewrite MAP_WIDTH_CONSTANTS literals; count each constant's rewritten statements."""
    lines = source.split("\n")
    function = ""
    for index, line in enumerate(lines[:-1]):
        if line.startswith("RECOMP_FUNC void "):
            function = line[len("RECOMP_FUNC void "):line.index("(")]
            continue
        match = re.match(r"\s*// 0x([0-9A-F]{8}):", line)
        key = (function, int(match[1], 16)) if match else None
        if key not in MAP_WIDTH_CONSTANTS:
            continue
        address = key[1]
        literal, halves = MAP_WIDTH_CONSTANTS[key]
        statement = lines[index + 1]
        if statement.count(literal) != 1:
            raise RuntimeError(f"map width constant {address:#x}: {literal} not once in {statement.strip()!r}")
        if isinstance(halves, str):
            replacement = halves   # a whole expression in place of the literal
        elif literal.startswith("S32("):
            bits = int(re.match(r"S32\(0X([0-9A-F]+) << 16\)", literal)[1], 16) << 16
            replacement = f"srw64_map_x_float(0X{bits:08X}u, {halves})"
        else:
            replacement = f"srw64_map_x({literal}, {halves})"
        lines[index + 1] = statement.replace(literal, replacement)
        found[key] = found.get(key, 0) + 1
    return "\n".join(lines)


def bind_native_hooks(source: str) -> str:
    """Rename definitions only; direct calls and overlay tables use the bridge."""
    for original, renamed in NATIVE_HOOKS.items():
        source = source.replace(f"RECOMP_FUNC void {original}(", f"RECOMP_FUNC void {renamed}(")
    return source


def bind_overlay_calls(source: str) -> tuple[str, int]:
    """Resolve fixed-address overlays through the currently loaded function map.

    A unique symbol in the current scan does not prove that another overlay has
    no code at that address. Preserve the JAL address, not the guessed ROM owner.
    Calls within an overlay use the same lookup to keep one dispatch contract.
    """
    return re.subn(r"\bload_[0-9A-F]+_func_([0-9A-F]{8})\(rdram, ctx\);",
                   lambda match: f"LOOKUP_FUNC(0x{match[1]})(rdram, ctx);", source)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scan", type=Path, default=ROOT / "build/recomp/cpu-scan")
    parser.add_argument("--output", type=Path, default=ROOT / "build/recomp/cpu-bound")
    args = parser.parse_args()
    rom = (ROOT / "rom.z64").read_bytes()
    layout = analyze(rom, None)
    work = args.output.resolve()
    work.mkdir(parents=True, exist_ok=True)
    audit_path = work / "library-audit.json"
    subprocess.run([sys.executable, str(ROOT / "tools/recomp/toolchain/audit_library_symbols.py"), "--output", str(audit_path)], check=True)
    audit = json.loads(audit_path.read_text())
    names: dict[int, dict] = {}
    for item in audit["symbols"]:
        if item["runtime_action"] == "retain-game-code":
            continue
        # Accept a full normalized signature only when its remaining candidate
        # bytes are at most alignment padding. Never discard unexplained tails.
        for match in item["matches"]:
            size, candidate_size, offset = match["size"], item["candidate_size"], item["rom"]
            if 0 <= candidate_size - size < 16 and not any(rom[offset + size:offset + candidate_size]):
                names[item["vram"]] = {"name": item["name"], "size": candidate_size,
                                      "evidence": "normalized-code-and-padding-match", "signature_line": match["signature_line"],
                                      "sha256": hashlib.sha256(rom[offset:offset + candidate_size]).hexdigest(),
                                      "runtime_action": item["runtime_action"]}
                break
    reviewed = json.loads((ROOT / "config/recomp/library-symbols.json").read_text())
    if reviewed["schema"] != "srw64.recomp-reviewed-library-symbols.v1" or reviewed["rom_sha256"] != layout["rom_sha256"]:
        raise RuntimeError("reviewed library symbols differ from baseline")
    for item in reviewed["functions"]:
        offset, size = item["rom"], item["size"]
        if offset + 0x80075610 != item["vram"] or hashlib.sha256(rom[offset:offset + size]).hexdigest() != item["sha256"]:
            raise RuntimeError(f"reviewed library bytes differ: {item['name']}")
        names[item["vram"]] = {"name": item["name"], "size": size, "evidence": item["identity_evidence"],
                              "basis": item["basis"], "sha256": item["sha256"], "runtime_action": "reviewed-name"}
    scan_report = json.loads((args.scan / "report.json").read_text())
    if scan_report["rom_sha256"] != layout["rom_sha256"]:
        raise RuntimeError("CPU scan targets another ROM")
    original_symbols = (args.scan / "symbols.toml").read_bytes()
    if hashlib.sha256(original_symbols).hexdigest() != scan_report["symbols_sha256"]:
        raise RuntimeError("CPU scan symbols changed since report")
    sections = tomllib.loads(original_symbols.decode())["section"]
    output: list[str] = []
    applied: list[dict] = []
    excluded: list[dict] = []
    for section in sections:
        if section["name"] == "resident":
            for split in reviewed.get("function_splits", []):
                index = next((i for i, function in enumerate(section["functions"]) if function["vram"] == split["vram"]), None)
                if index is None or section["functions"][index]["size"] != split["size"]:
                    raise RuntimeError("reviewed function split differs from scan")
                offset = split["vram"] - 0x80075610
                if hashlib.sha256(rom[offset:offset + split["size"]]).hexdigest() != split["sha256"]:
                    raise RuntimeError("function split byte identity differs")
                replacements = [{"name": names[address]["name"], "vram": address, "size": names[address]["size"]} for address in split["function_starts"]]
                cursor = split["vram"]
                for function in replacements:
                    if function["vram"] != cursor:
                        raise RuntimeError("function split has a gap or overlap")
                    cursor += function["size"]
                if cursor != split["vram"] + split["size"]:
                    raise RuntimeError("function split does not cover the old range")
                section["functions"][index:index + 1] = replacements
        functions: list[dict] = []
        for function in section["functions"]:
            offset = function["vram"] - section["vram"] + section["rom"]
            if not any(rom[offset:offset + function["size"]]):
                excluded.append({**function, "section": section["name"], "reason": "all-zero range without executable return/control flow"})
                continue
            if section["name"] == "resident" and function["vram"] in names:
                entry = names[function["vram"]]
                if entry["size"] != function["size"]:
                    raise RuntimeError(f"library boundary differs for {entry['name']}")
                function["name"] = entry["name"]
                applied.append({"vram": function["vram"], **entry})
            functions.append(function)
        if not functions:
            continue
        output.extend(["[[section]]", f'name = "{section["name"]}"', f'rom = {section["rom"]:#x}',
                       f'vram = {section["vram"]:#x}', f'size = {section["size"]:#x}', "functions = ["])
        output.extend(f'  {{ name = "{f["name"]}", vram = {f["vram"]:#x}, size = {f["size"]:#x} }},' for f in functions)
        output.extend(["]", ""])
    missing = sorted(set(names) - {item["vram"] for item in applied})
    if missing:
        raise RuntimeError(f"reviewed/audited symbols absent from scan: {missing}")
    symbols_path = work / "symbols.toml"
    symbols_path.write_text("\n".join(output))
    config_path = work / "recomp.toml"
    config_path.write_text('[input]\nentrypoint = 0x80076610\n'
                           f'rom_file_path = {json.dumps(str(ROOT / "rom.z64"))}\n'
                           'symbols_file_path = "symbols.toml"\noutput_func_path = "generated"\n')
    with (work / "generate.log").open("w") as log:
        process = subprocess.run([str(ROOT / "build/recomp/tool-build/N64Recomp"), str(config_path)], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    generated_files: list[dict] = []
    unsupported_calls: list[str] = []
    if process.returncode == 0:
        header = work / "generated/funcs.h"
        declarations = set(re.findall(r"void (\w+)\(", header.read_text()))
        called: set[str] = set()
        map_width_found: dict[tuple[str, int], int] = {}
        for path in sorted((work / "generated").glob("funcs_*.c")):
            if path.name == "funcs_unsupported.c":
                continue
            source = path.read_text()
            adapted, count = bind_overlay_calls(source)
            adapted = bind_native_hooks(adapted)
            adapted = bind_map_width(adapted, map_width_found)
            adapted = bind_render_nodes(adapted)
            adapted = adapted.replace("RECOMP_FUNC void resident_func_8007F704(",
                                      "RECOMP_FUNC void srw64_original_rom_read(")
            adapted = adapted.replace("RECOMP_FUNC void resident_func_8008C510(",
                                      "RECOMP_FUNC void srw64_original_text_descriptor(")
            called.update(re.findall(r"^\s+(\w+)\(rdram, ctx\);", adapted, re.MULTILINE))
            path.write_text(adapted)
            generated_files.append({"path": str(path.relative_to(work)), "overlay_lookup_calls": count,
                                    "raw_sha256": hashlib.sha256(source.encode()).hexdigest(),
                                    "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
        if set(map_width_found) != set(MAP_WIDTH_CONSTANTS):
            raise RuntimeError(f"map width constants not found: {sorted(set(MAP_WIDTH_CONSTANTS) - set(map_width_found))}")
        # Upstream deliberately omits some internal OS/Pak routines. Retained
        # SDK callers may still refer to them. Make the diagnostic executable
        # fail with the exact routine instead of silently inventing behavior.
        missing_declarations = sorted(called - declarations)
        runtime_exports: set[str] = set()
        for path in (ROOT / "build/recomp/upstream/N64ModernRuntime/librecomp/src").glob("*.cpp"):
            runtime_exports.update(re.findall(r'extern "C" void (\w+)\(', path.read_text()))
        unsupported_calls = sorted(set(missing_declarations) - runtime_exports)
        known_omitted = {item["name"] + "_recomp" for item in audit["symbols"]
                         if item["runtime_action"] == "ignored-internal"}
        if set(unsupported_calls) - known_omitted:
            raise RuntimeError(f"unexplained missing declarations: {set(unsupported_calls) - known_omitted}")
        header.write_text(header.read_text() + '\n#ifdef __cplusplus\nextern "C" {\n#endif\n' + "\n".join(
            f"void {name}(uint8_t* rdram, recomp_context* ctx);" for name in missing_declarations) + "\n")
        with header.open("a") as destination:
            destination.write("\n".join(f"void {name}(uint8_t*, recomp_context*);" for name in NATIVE_HOOKS.values()) + "\n")
            destination.write("void srw64_original_rom_read(uint8_t*, recomp_context*);\nvoid srw64_original_text_descriptor(uint8_t*, recomp_context*);\n")
            destination.write("int32_t srw64_map_x(int32_t original, int32_t halves);\nint32_t srw64_map_x_float(uint32_t bits, int32_t halves);\n"
                              "int32_t srw64_map_anchor_scroll(int32_t scroll, float unit_x);\n"
                              "uint32_t srw64_morale_cap(uint64_t pilot);\nuint32_t srw64_morale_room(uint64_t pilot, uint64_t morale);\n"
                              "int32_t srw64_damage_real(float damage, int32_t cap_bits);\n"
                              "uint32_t srw64_map_figure(uint8_t* rdram, uint32_t shown);\nuint32_t srw64_battle_figure(uint8_t* rdram, uint32_t shown);\n"
                              "int srw64_quote_demo(uint8_t* rdram);\n"
                              "uint32_t srw64_name_record(uint32_t kind, uint32_t number);\n"
                              "uint32_t srw64_resource_limit(void);\n"
                              "void srw64_render_node(uint8_t* rdram, recomp_context* ctx, int32_t function, int32_t sprite);\n#ifdef __cplusplus\n}\n#endif\n")
        trap = work / "generated/funcs_unsupported.c"
        trap.write_text('#include "recomp.h"\n#include <stdio.h>\n#include <stdlib.h>\n' + "\n".join(
            f'void {name}(uint8_t* rdram, recomp_context* ctx) {{ '
            f'fprintf(stderr, "SRW64_UNSUPPORTED {name}\\n"); abort(); }}'
            for name in unsupported_calls) + "\n")
        generated_files.append({"path": str(trap.relative_to(work)), "sha256": hashlib.sha256(trap.read_bytes()).hexdigest(),
                                "role": "explicit-failure-for-unsupported-OS-calls"})
    report = {"schema": "srw64.recomp-cpu-generation.v1", "rom_sha256": layout["rom_sha256"],
              "status": "generated" if process.returncode == 0 else "generation-failed",
              "generator_exit_code": process.returncode, "applied_library_symbols": applied, "excluded_candidates": excluded,
              "generated_files": generated_files,
              "unsupported_call_traps": unsupported_calls,
              "symbols_sha256": hashlib.sha256(symbols_path.read_bytes()).hexdigest(), "runtime_validation": "pending"}
    (work / "report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"status": report["status"], "library_symbols": len(applied), "excluded_candidates": len(excluded)}, indent=2))
    if process.returncode:
        print((work / "generate.log").read_text()[-2200:], file=sys.stderr)
    return process.returncode


if __name__ == "__main__":
    raise SystemExit(main())
