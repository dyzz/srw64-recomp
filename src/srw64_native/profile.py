"""Compile an immutable play profile; display choices never select another ROM."""
from __future__ import annotations

import json
from pathlib import Path

from . import rule_settings
from .assets import compile_art, inside, portrait_lookup, unit_lookup
from .catalog import compile_locale, sha, source_catalog

UI_KEYS = {"settings_error", "manual", "auto", "fast", "skip", "font_size", "controls", "history_title", "history_controls",
           "name_title", "name_cancel", "name_review", "name_step_player", "name_step_partner", "name_step_review", "name_review_hint", "name_start",
           "name_step_select", "select_title", "select_hint", "select_super", "select_real", "select_male",
           "select_female", "select_confirm", "select_keyboard_hint", "review_keyboard_hint", "name_unit", "name_pilot"}
# The 选项 menu and settings window label one item per optional rule, so those keys follow the catalog.
UI_KEYS |= {"options_menu", "rules_menu", "rules_original", "rules_all", "rules_note", "rules_defaults",
            "rules_group_corrections", "rules_group_difficulty", "settings_open", "settings_title",
            "settings_language", "settings_language_note",
            "settings_images", "settings_images_original", "settings_images_hd", "settings_images_note",
            "settings_aspect", "settings_aspect_wide", "settings_aspect_original", "settings_aspect_note",
            "settings_window", "settings_window_windowed", "settings_window_fullscreen", "settings_window_note",
            "settings_window_size", "settings_window_size_note",
            "settings_ui_size", "settings_ui_size_standard", "settings_ui_size_large", "settings_ui_size_largest", "settings_ui_size_note",
            "settings_battle_ui", "settings_battle_ui_native", "settings_battle_ui_hd", "settings_battle_ui_original", "settings_battle_ui_note",
            "settings_intermission_ui", "settings_intermission_ui_native", "settings_intermission_ui_original", "settings_intermission_ui_note",
            "settings_name_entry_ui", "settings_name_entry_ui_native", "settings_name_entry_ui_original", "settings_name_entry_ui_note",
            "refund_notice", "dialogue_text_status", "dialogue_reload", "font_credit"}
# The phone's touch controls, one label per function (docs/design/touch-controls.md §5).
UI_KEYS |= {"touch_settings", "touch_ok", "touch_back", "touch_close", "touch_start", "touch_l1", "touch_r1", "touch_l2", "touch_r2", "touch_next_page", "touch_prev_page", "touch_skip", "touch_next_line", "touch_fast", "touch_auto", "touch_select", "touch_info", "touch_prev_unit", "touch_next_unit", "touch_prev_enemy", "touch_next_enemy", "touch_move_here", "touch_cancel", "touch_farthest", "touch_prev_target", "touch_next_target", "touch_skip_battle", "touch_continue", "touch_history", "touch_animation"}
# Custom campaign notices (src/host/link_page.cpp, src/host/mini_stage.hpp).
UI_KEYS |= {"campaign_link_blocked", "campaign_scene_unmapped"}
# The MOD manager and its extra scenarios (DLC) page (src/native/ui/frontend.cpp).
UI_KEYS |= {"dlc_leave", "dlc_start", "dlc_note", "dlc_empty", "dlc_enter", "dlc_stages", "dlc_saves", "dlc_new", "dlc_entered", "dlc_left",
            "mod_open", "mod_title", "mod_page_campaigns", "mod_page_art", "mod_page_dialogue", "mod_page_audio", "mod_hint",
            "mod_playing", "mod_current", "mod_title_only", "mod_art_installed", "mod_art_missing", "mod_art_note",
            "mod_dialogue_reload", "mod_dialogue_reload_note", "mod_dialogue_locale", "mod_dialogue_note", "mod_audio_note",
            "mod_row", "mod_row_note"}
# The title's Library (src/native/ui/frontend.cpp, src/host/library.cpp).
UI_KEYS |= {"library_open", "library_title", "library_tab_units", "library_tab_pilots", "library_hint", "library_hint_pad",
            "library_growth_note", "library_no_stats", "library_no_weapons", "library_work_other", "library_double_move", "library_spirit_note",
            "library_cap", "library_cap_value", "library_slots", "library_full", "library_type", "library_critical", "library_morale", "library_unlock", "library_combo", "library_full_note", "library_type_line", "library_ally", "library_enemy", "library_sub_pilot", "library_fairy", "library_love", "library_love_mutual", "library_love_one_way", "library_spirit_cost", "library_skill_note", "library_row", "library_row_note", "viewer_row", "viewer_row_note"}
# The battle viewer on the title (docs/design/battle-viewer.md).
UI_KEYS |= {"viewer_attacker", "viewer_back", "viewer_badge_default", "viewer_badge_now", "viewer_cat_air", "viewer_cat_all", "viewer_cat_ground", "viewer_cat_space", "viewer_choose_counter_weapon", "viewer_choose_pilot_crew", "viewer_choose_result_att", "viewer_choose_result_def", "viewer_choose_scene", "viewer_choose_song", "viewer_choose_unit", "viewer_choose_weapon", "viewer_confirm", "viewer_counter_off", "viewer_counter_on", "viewer_counter_weapon", "viewer_default", "viewer_defender", "viewer_destroy", "viewer_dmg_big", "viewer_dmg_left", "viewer_dmg_mid", "viewer_dmg_small", "viewer_dmg_title", "viewer_gate_no_barrier", "viewer_gate_no_bunshin", "viewer_gate_no_shield", "viewer_gate_no_skill", "viewer_gate_no_sword", "viewer_gate_not_beam", "viewer_gate_short_barrier", "viewer_gate_short_parry", "viewer_gate_short_shield", "viewer_gate_unparryable", "viewer_help_pilot", "viewer_help_song", "viewer_help_unit_default", "viewer_help_weapon", "viewer_help_weapon_parry", "viewer_hint", "viewer_hint_pad", "viewer_keep_pilot", "viewer_listen", "viewer_listen_note", "viewer_listen_stop", "viewer_note_barrier", "viewer_note_bunshin", "viewer_note_dodge", "viewer_note_hit", "viewer_note_parry", "viewer_note_shield", "viewer_open", "viewer_pilot", "viewer_pop_hint", "viewer_pop_hint_pad", "viewer_pop_hint_page", "viewer_pop_hint_page_pad", "viewer_previewing", "viewer_res_barrier", "viewer_res_bunshin", "viewer_res_dodge", "viewer_res_hit", "viewer_res_parry", "viewer_res_shield", "viewer_res_title", "viewer_rest", "viewer_row", "viewer_row_counter_hit", "viewer_row_hit", "viewer_row_note", "viewer_row_scene", "viewer_scene_base", "viewer_scene_bridge", "viewer_scene_cave", "viewer_scene_city", "viewer_scene_desert", "viewer_scene_forest", "viewer_scene_fortress", "viewer_scene_harbor", "viewer_scene_moon", "viewer_scene_mountains", "viewer_scene_note", "viewer_scene_other", "viewer_scene_otherworld", "viewer_scene_plains", "viewer_scene_rocks", "viewer_scene_ruins", "viewer_scene_sea", "viewer_scene_sky", "viewer_scene_snow", "viewer_scene_space", "viewer_scene_village", "viewer_scene_wasteland", "viewer_song_attacker", "viewer_song_hint", "viewer_song_hint_pad", "viewer_song_keep", "viewer_start", "viewer_sum_damage", "viewer_sum_rest", "viewer_sum_unhurt", "viewer_swap", "viewer_title", "viewer_unit", "viewer_weapon"}
# The About page's links and the update check (frontend.cpp, src/host/update_check.hpp).
UI_KEYS |= {"about_link_issues", "about_link_site", "about_link_source", "menu_about", "menu_check_updates", "settings_about_links", "settings_about_links_note", "settings_about_tagline", "settings_update", "settings_update_auto", "settings_update_auto_note", "settings_update_auto_off", "settings_update_auto_on", "update_ask_note", "update_ask_off", "update_ask_on", "update_ask_text", "update_check", "update_download", "update_notes", "update_open_failed", "update_status_available", "update_status_checking", "update_status_current", "update_status_failed", "update_status_unchecked", "update_title_hint",
            "settings_update_hd", "update_hd_installed", "update_hd_unknown", "update_hd_missing", "update_hd_available",
            "update_hd_download", "update_title_hint_hd"}
# The View menu and the Windows menu bar (src/native/ui/app_menu.hpp).
UI_KEYS |= {"menu_view", "menu_fullscreen", "menu_window_scale", "menu_game", "menu_exit"}
# The frame-rate readout (settings show_fps).
UI_KEYS |= {"settings_fps", "settings_fps_off", "settings_fps_on", "settings_fps_note"}
# The dialogue controls bar: auto-hidden or always shown (settings dialogue_hints).
UI_KEYS |= {"settings_dialogue_hints", "settings_dialogue_hints_auto", "settings_dialogue_hints_always", "settings_dialogue_hints_note"}
# The debug interface switch on the About page (settings debug_interface).
UI_KEYS |= {"settings_debug", "settings_debug_on", "settings_debug_off", "settings_debug_note", "settings_debug_note_android", "settings_debug_forced",
            "settings_debug_status", "settings_debug_run", "settings_debug_copy", "debug_interface_notice"}
# The Feedback page (frontend.cpp feedback_page, bug_report.hpp).
UI_KEYS |= {"settings_report", "settings_report_note", "settings_report_export", "settings_report_issue",
            "settings_report_open", "settings_report_copy", "settings_report_done", "settings_report_failed",
            "settings_info", "settings_info_note", "settings_info_copy", "settings_info_copied",
            "settings_feedback_links", "settings_feedback_links_note", "settings_feedback_text"}
# The 部隊名 each language shows while the stored name is the original マーチウィンド
# (docs/native/fixed-unit-name.md; the name cannot be changed).
UI_KEYS |= {"unit_default_name"}
# The Link Battler series page in front of the リンク screen.
UI_KEYS |= {"link_back", "link_confirm", "link_crew_f91", "link_crew_goshogun", "link_crew_label",
            "link_crew_zambot", "link_hint", "link_joined", "link_keyboard_hint", "link_lead_f91",
            "link_lead_goshogun", "link_lead_zambot", "link_scheduled", "link_series_f91",
            "link_series_goshogun", "link_series_zambot", "link_ticked", "link_title", "link_units_f91",
            "link_units_goshogun", "link_units_zambot"}
UI_KEYS |= {"rule_" + fix.replace("-", "_") for fix in rule_settings.RULE_FIXES}

UI_KEYS |= {'battle_effect_ready', 'battle_effect_uncuttable', 'battle_skill_parry', 'battle_effect_full', 'battle_effects_title', 'battle_skill_holy', 'battle_skill_jammer', 'battle_effect_hit', 'battle_effect_crit', 'battle_effect_remaining', 'battle_effect_inactive', 'battle_effect_strength', 'battle_skill_clone', 'battle_effect_enemy_crit', 'battle_effect_none', 'battle_skill_getter_vision', 'battle_skill_beam_coat', 'battle_skill_mach', 'battle_skill_planet', 'battle_skill_esp', 'battle_skill_enhanced', 'battle_effect_aura_first', 'battle_effect_morale_low', 'battle_skill_newtype', 'battle_skill_dummy', 'battle_effect_threshold', 'battle_effect_evade', 'battle_skill_shield', 'battle_skill_god_shadow', 'battle_effect_sure_hit', 'battle_skill_shungeki', 'battle_skill_aura_barrier', 'battle_skill_fixed_damage', 'battle_effect_active', 'battle_effect_no_attack', 'battle_skill_potential', 'battle_skill_true_mach', 'battle_effects_note', 'battle_effect_heat', 'battle_effect_barrier_first', 'battle_effect_no_skill', 'battle_skill_i_field', 'battle_effect_no_equipment'}

UI_KEYS |= {"mini_enter", "mini_entering", "battle_shield_damage"}
UI_KEYS |= {"intermission_episode", "intermission_hint", "intermission_swap_hint", "intermission_swap_refused"}
# Title ring menu and PRESS START BUTTON, drawn natively (native_sprite.cpp / sprite_text.cpp).
UI_KEYS |= {"title_press_start", "title_start", "title_load", "title_continue", "title_option"}
# The title menu pages (title_page.cpp) and their original/native switch.
UI_KEYS |= {"settings_title_ui", "settings_title_ui_native", "settings_title_ui_original", "settings_title_ui_note",
            "title_load_hint", "title_options_hint", "title_sound_hint", "title_karaoke_hint"}
# Giant Robo's subtitle under its title in the title demo (sprite_text.cpp kDemoWorks).
UI_KEYS |= {"title_demo_giant_robo_subtitle"}
# The settings window's pages, footer and Controls page (frontend.cpp settings_sync; the
# page ids and the functions are the same lists as settings_pages and control_rows there).
SETTINGS_PAGES = ("general", "interface", "rules", "cheats", "saves", "controls", "feedback", "about")
UI_KEYS |= {f"settings_page_{page}" for page in SETTINGS_PAGES}
# The General page's bezel and filter rows (frontend.cpp look_rows, docs/native/bezels-and-filters.md).
UI_KEYS |= {"settings_bezel", "settings_bezel_note", "settings_filter", "settings_filter_note", "settings_filter_scale",
            "settings_filter_scale_note", "settings_look_off", "settings_look_choose", "settings_bezel_mine", "settings_filter_mine",
            "settings_browse_retroarch", "settings_browse_builtin", "settings_browse_open", "settings_browse_up", "settings_browse_empty", "settings_filter_loading",
            "settings_filter_error", "settings_filter_unavailable", "settings_about_librashader"}
UI_KEYS |= {f"settings_filter_scale_{n}" for n in (1, 2, 4, 0)}
# The Cheats page (frontend.cpp cheats_page; the switch ids are cheats.hpp catalog).
UI_KEYS |= {"cheats_note", "cheat_levels", "cheat_levels_note", "cheat_levels_open", "cheat_levels_close", "cheat_levels_away"}
UI_KEYS |= {f"cheat_{switch}" for switch in ("funds", "parts", "en", "sp", "morale")}
# The Controls page's functions (frontend.cpp control_rows): controls_row_<id>.
CONTROL_ROWS = ("a", "b", "start", "l", "r", "aux_left", "aux_right", "c_left", "c_up", "c_down", "animation", "settings",
                "language", "images", "d_up", "d_down", "d_left", "d_right", "z")
UI_KEYS |= {f"controls_row_{row}" for row in CONTROL_ROWS}
UI_KEYS |= {"settings_close", "settings_hint", "settings_hint_pad", "settings_presets", "settings_about_version",
            "settings_about_font", "settings_about_prompts_title", "settings_about_prompts"}
# The Controls page's remapping (frontend.cpp controls_page).
UI_KEYS |= {"controls_cancel", "controls_capture", "controls_capture_note", "controls_controller", "controls_detected", "controls_fixed", "controls_fixed_list", "controls_keyboard", "controls_keyboard_note", "controls_list_note", "controls_no_pad", "controls_reserved", "controls_reset"}
# Battle HUD banners and response badges drawn natively (sprite_text.cpp).
UI_KEYS |= {"hud_counter", "hud_defend", "hud_evade", "hud_shield_defense", "hud_critical", "hud_dummy"}
UI_KEYS |= {"upgrade_list_hint", "upgrade_list_hint_pages", "upgrade_stats_hint", "upgrade_confirm_hint", "upgrade_weapon_confirm_hint", "upgrade_message_hint", "upgrade_weapons_hint", "upgrade_weapons_hint_pages", "funds_edit_hint", "upgrade_cap_original"}
UI_KEYS |= {"parts_list_hint", "parts_list_hint_pages", "parts_slots_hint", "parts_inventory_hint", "parts_holders_hint", "parts_free", "parts_equipped_count"}
UI_KEYS |= {"ability_list_hint", "ability_unit_hint", "ability_weapons_hint", "ability_pilot_hint"}
UI_KEYS |= {"swap_list_hint", "swap_confirm_hint"}
UI_KEYS |= {"save_choice_hint", "save_slots_hint", "save_confirm_hint", "save_message_hint",
            "save_slots_hint_pages", "title_load_hint_pages", "save_cartridge",
            "save_slots_hint_delete", "title_load_hint_delete",
            "save_auto", "save_auto_turn", "save_turn", "save_delete",
            "settings_autosave", "settings_autosave_intermission", "settings_autosave_intermission_note", "settings_autosave_note", "settings_autosave_off", "settings_autosave_on", "settings_autosave_turn", "settings_autosave_turn_note", "settings_save_export", "settings_save_export_button", "settings_save_export_note", "settings_save_exported", "settings_save_import", "settings_save_import_confirm", "settings_save_import_damaged", "settings_save_import_note", "settings_save_import_refresh", "settings_save_import_slot", "settings_save_import_unknown", "settings_save_import_warning", "settings_save_imported", "settings_saves_off"}

# Battle confirmation labels share the same immutable locale catalog.
UI_KEYS |= {'battle_cuttable', 'battle_target_barrier', 'battle_barrier_non_beam', 'battle_parry', 'battle_clone', 'battle_clone_morale', 'battle_barrier_absorbed', 'battle_critical_damage', 'battle_damage_note', 'battle_barrier_en_low', 'battle_barrier_broken', 'battle_shield', 'battle_sure_hit', 'battle_defense_note', 'battle_damage', 'battle_uncuttable', 'battle_barrier_reduced', 'battle_barrier_first'}
UI_KEYS |= {
    "battle_spirits",
    "battle_spirit_ready",
    "battle_spirit_sp",
    "battle_spirit_unavailable",
    "battle_spirit_map",
    "battle_spirit_back",
    "battle_spirit_hint", "battle_damage_if_hit", "battle_first", "battle_second",
    "battle_first_player", "battle_first_enemy", "battle_phase_enemy", "battle_phase_player", "battle_barrier",
    "battle_spirits_none",
    "battle_ammo", "battle_animation", "battle_attacker", "battle_back",
    "battle_change_weapon", "battle_confirm", "battle_cost", "battle_counter",
    "battle_crit_mod", "battle_critical", "battle_critical_note", "battle_damage",
    "battle_damage_note", "battle_defend", "battle_defender", "battle_evade", "battle_guard_switch",
    "battle_hint", "battle_hit", "battle_hit_mod", "battle_modifiers",
    "battle_morale", "battle_none", "battle_off", "battle_on",
    "battle_response", "battle_response_hint", "battle_title", "battle_weapon",
}
# Controller versions of the key hints ("_pad"), shown after controller input (Steam Deck).
PAD_HINT_KEYS = {"settings_open", "controls", "history_controls", "select_keyboard_hint", "review_keyboard_hint", "link_keyboard_hint", "battle_hint", "title_load_hint", "title_options_hint", "title_sound_hint", "title_karaoke_hint", "intermission_hint", "intermission_swap_hint", "upgrade_list_hint", "upgrade_list_hint_pages", "upgrade_stats_hint", "upgrade_confirm_hint", "upgrade_weapon_confirm_hint", "upgrade_message_hint", "upgrade_weapons_hint", "upgrade_weapons_hint_pages", "parts_list_hint", "parts_list_hint_pages", "parts_slots_hint", "parts_inventory_hint", "parts_holders_hint", "ability_list_hint", "ability_unit_hint", "ability_weapons_hint", "ability_pilot_hint", "swap_list_hint", "swap_confirm_hint", "save_choice_hint", "save_slots_hint", "save_confirm_hint", "save_message_hint", "funds_edit_hint", "save_slots_hint_pages", "title_load_hint_pages", "save_slots_hint_delete", "title_load_hint_delete"}
UI_KEYS |= {key + "_pad" for key in PAD_HINT_KEYS}


def load_profile(path: Path, *, locale: str | None = None, images: str | None = None) -> dict:
    profile = json.loads(path.read_text())
    if profile.get("schema") != "srw64.play-profile.v1" or profile.get("baseline") != "srw64-jp-rev0":
        raise ValueError("Unsupported play profile/baseline")
    if profile.get("gameplay_mods") != []:
        raise ValueError("Gameplay mods are not enabled by this presentation milestone")
    p = profile["presentation"]
    if locale is not None:
        p["locale"] = locale
    if images is not None:
        p["images"] = images
    if p["images"] not in ("original", "hd") or p["model_5600"] not in ("original", "waterdrop"):
        raise ValueError("Invalid image or model mode")
    if type(p["resolution_scale"]) is not int or not 1 <= p["resolution_scale"] <= 8:
        raise ValueError("Resolution scale must be in 1..8")
    if type(p["font_size"]) is not int or not 10 <= p["font_size"] <= 18:
        raise ValueError("Font size must be in 10..18")
    if p["locale"] not in profile["locales"] or "ja" not in profile["locales"]:
        raise ValueError("Requested locale or Japanese fallback is not registered")
    return profile


def prepare_profile(root: Path, profile: dict, rom: Path, output: Path) -> dict:
    sources, hashes, glyphs = source_catalog(root, rom)
    p = profile["presentation"]
    locale_path = inside(root, profile["locales"][p["locale"]])
    ja_path = inside(root, profile["locales"]["ja"])
    language = json.loads(locale_path.read_text())
    japanese = json.loads(ja_path.read_text())
    if language["locale"] != p["locale"] or japanese["locale"] != "ja":
        raise ValueError("Locale registry identity mismatch")
    entries = compile_locale(language, sources, hashes)
    from .coverage import locale_coverage
    coverage = {}
    locale_options = []
    locale_catalogs = {}
    for locale, relative in profile["locales"].items():
        document = json.loads(inside(root, relative).read_text())
        if document["locale"] != locale:
            raise ValueError("Locale registry identity mismatch")
        compiled = entries if locale == p["locale"] else compile_locale(document, sources, hashes)
        labels = {**japanese["ui"], **document["ui"]}
        if set(labels) != UI_KEYS or any(not isinstance(value, str) or not value for value in labels.values()):
            raise ValueError("Missing or invalid native UI strings")
        locale_catalogs[locale] = {"config": {"font": document["font"], "locale": locale,
            "font_size": p["font_size"], "mode": "replace"}, "entries": compiled, "ui": labels,
            "catalog_sha256": sha(inside(root, relative).read_bytes())}
        coverage[locale] = locale_coverage(document, sources, compiled, UI_KEYS)
        locale_options.append({"locale": locale, "label": document.get("display_name", locale),
                               "translated": len(compiled), "source": len(sources),
                               "reviewed": coverage[locale]["reviewed_records"]})
    ui = {**japanese["ui"], **language["ui"]}
    if set(ui) != UI_KEYS or any(not isinstance(value, str) or not value for value in ui.values()):
        raise ValueError("Missing or invalid native UI strings")
    output.mkdir(parents=True, exist_ok=False)
    from .name_assets import prepare_name_assets
    art = None
    art_sha = None
    hd_portrait = None
    hd_unit = None
    unavailable_reason = None
    try:
        art_path = inside(root, profile["art_pack"])
        art_bytes = art_path.read_bytes()
        art = compile_art(root, json.loads(art_bytes), output / "art", maps_in_place=True)
        hd_portrait = portrait_lookup(output / "art", output / "portraits")
        hd_unit = unit_lookup(output / "art")
        art_sha = sha(art_bytes)
        name_assets = prepare_name_assets(rom.read_bytes(), output / "name-entry", hd_portrait=hd_portrait)
    except FileNotFoundError as error:
        if p["images"] != "original":
            raise
        unavailable_reason = f"Missing HD resource: {error.filename}"
        art = None
        name_assets = prepare_name_assets(rom.read_bytes(), output / "name-entry")
    data = {"schema": "srw64.native-dialogue-data.v2", "config": {
        "font": language["font"], "locale": p["locale"], "font_size": p["font_size"], "mode": "replace"},
        "entries": entries, "source_entries": sources, "glyphs": glyphs, "ui": ui,
        "locale_options": locale_options, "locale_catalogs": locale_catalogs,
        "rom_sha256": sha(rom.read_bytes()), "catalog_sha256": sha(locale_path.read_bytes()),
        "sources": {relative: sha(inside(root, relative).read_bytes()) for relative in profile["locales"].values()}}
    from .battle_assets import prepare_battle_assets
    data["battle_assets"] = prepare_battle_assets(root, rom.read_bytes(), output / "battle", hd_portrait, hd_unit)
    data["name_entry_assets"] = name_assets
    (output / "coverage.json").write_text(json.dumps(coverage, ensure_ascii=False, indent=2) + "\n")
    dialogue = output / "dialogue.json"
    dialogue.write_text(json.dumps(data, ensure_ascii=False) + "\n")
    result = {"schema": "srw64.prepared-profile.v1", "profile": profile,
              "rom_sha256": data["rom_sha256"], "dialogue": {"path": str(dialogue), "sha256": sha(dialogue.read_bytes())},
              "art": art, "art_source_sha256": art_sha,
              "hd_available": art is not None, "hd_unavailable_reason": unavailable_reason,
              "coverage": {"path": str(output / "coverage.json"), "sha256": sha((output / "coverage.json").read_bytes())},
              "source_records": len(sources), "translated_records": len(entries)}
    (output / "profile.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    return result
