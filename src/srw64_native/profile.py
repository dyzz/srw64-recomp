"""Compile an immutable play profile; display choices never select another ROM."""
from __future__ import annotations

import json
from pathlib import Path

from .assets import compile_art, inside
from .catalog import compile_locale, sha, source_catalog

UI_KEYS = {"settings_error", "manual", "auto", "fast", "skip", "font_size", "controls", "history_title", "history_controls",
           "name_title", "name_player", "name_partner", "name_given", "name_family", "name_nickname", "name_limit", "name_cancel", "name_default", "name_back", "name_next", "name_confirm", "name_hint", "name_empty", "name_long", "name_unsupported", "name_spaces", "name_invalid", "name_review_keyboard_hint", "name_review", "name_step_player", "name_step_partner", "name_step_review", "name_review_hint", "name_page_hint", "name_preview", "name_keyboard_hint", "name_start", "name_edit", "name_to_partner", "name_to_review"}


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
