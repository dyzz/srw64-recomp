"""Compile an immutable play profile; display choices never select another ROM."""
from __future__ import annotations

import json
from pathlib import Path

from . import rule_settings
from .assets import compile_art, inside
from .catalog import compile_locale, sha, source_catalog

UI_KEYS = {"settings_error", "manual", "auto", "fast", "skip", "font_size", "controls", "history_title", "history_controls",
           "name_title", "name_player", "name_partner", "name_given", "name_family", "name_nickname", "name_limit", "name_cancel", "name_default", "name_back", "name_next", "name_confirm", "name_hint", "name_empty", "name_long", "name_unsupported", "name_spaces", "name_invalid", "name_review_keyboard_hint", "name_review", "name_step_player", "name_step_partner", "name_step_review", "name_review_hint", "name_page_hint", "name_preview", "name_keyboard_hint", "name_start", "name_edit", "name_to_partner", "name_to_review",
           "name_step_select", "select_title", "select_hint", "select_super", "select_real", "select_male",
           "select_female", "select_confirm", "select_keyboard_hint"}
# The 选项 menu and settings window label one item per optional rule, so those keys follow the catalog.
UI_KEYS |= {"options_menu", "rules_menu", "rules_original", "rules_all", "rules_note", "rules_defaults",
            "rules_group_corrections", "rules_group_difficulty", "settings_open", "settings_title",
            "settings_language", "settings_language_note",
            "settings_images", "settings_images_original", "settings_images_hd", "settings_images_note",
            "settings_battle_ui", "settings_battle_ui_native", "settings_battle_ui_original", "settings_battle_ui_note",
            "settings_intermission_ui", "settings_intermission_ui_native", "settings_intermission_ui_original", "settings_intermission_ui_note",
            "refund_notice"}
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
UI_KEYS |= {"upgrade_list_hint", "upgrade_list_hint_pages", "upgrade_stats_hint", "upgrade_confirm_hint", "upgrade_message_hint", "upgrade_weapons_hint", "upgrade_weapons_hint_pages", "funds_edit_hint", "upgrade_cap_original"}
UI_KEYS |= {"parts_list_hint", "parts_list_hint_pages", "parts_slots_hint", "parts_inventory_hint", "parts_holders_hint", "parts_free", "parts_equipped_count"}
UI_KEYS |= {"ability_list_hint", "ability_unit_hint", "ability_weapons_hint", "ability_pilot_hint"}
UI_KEYS |= {"swap_list_hint", "swap_confirm_hint"}

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
    "battle_first_player", "battle_first_enemy", "battle_barrier",
    "battle_spirits_none",
    "battle_ammo", "battle_animation", "battle_attacker", "battle_back",
    "battle_change_weapon", "battle_confirm", "battle_cost", "battle_counter",
    "battle_crit_mod", "battle_critical", "battle_critical_note", "battle_damage",
    "battle_damage_note", "battle_defend", "battle_defender", "battle_evade",
    "battle_hint", "battle_hit", "battle_hit_mod", "battle_modifiers",
    "battle_morale", "battle_none", "battle_off", "battle_on",
    "battle_response", "battle_response_hint", "battle_title", "battle_weapon",
}


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
    unavailable_reason = None
    try:
        art_path = inside(root, profile["art_pack"])
        art_bytes = art_path.read_bytes()
        art = compile_art(root, json.loads(art_bytes), output / "art")
        art_sha = sha(art_bytes)
        name_assets = prepare_name_assets(root, rom.read_bytes(), output / "name-entry")
    except FileNotFoundError as error:
        if p["images"] != "original":
            raise
        unavailable_reason = f"Missing HD resource: {error.filename}"
        art = None
        name_assets = prepare_name_assets(root, rom.read_bytes(), output / "name-entry", include_hd=False)
    data = {"schema": "srw64.native-dialogue-data.v2", "config": {
        "font": language["font"], "locale": p["locale"], "font_size": p["font_size"], "mode": "replace"},
        "entries": entries, "source_entries": sources, "glyphs": glyphs, "ui": ui,
        "locale_options": locale_options, "locale_catalogs": locale_catalogs,
        "rom_sha256": sha(rom.read_bytes()), "catalog_sha256": sha(locale_path.read_bytes()),
        "sources": {relative: sha(inside(root, relative).read_bytes()) for relative in profile["locales"].values()}}
    from .battle_assets import prepare_battle_assets
    data["battle_assets"] = prepare_battle_assets(root, rom.read_bytes(), output / "battle")
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
