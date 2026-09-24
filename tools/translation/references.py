"""Glossary and character references for the translation prompts.

Binding names come from this project's term table (content/locales/terms/<locale>.json,
maintained with tools/content/apply_terms.py) and, for proper nouns that only dialogue
uses, from content/translation/story-terms.json (drafted by draft_terms.py). The SRW Z
Chinese project, when present, contributes non-binding Chinese hints and its character
library (work, gender, profile). content/translation/zh-Hans/roster.json adds notes on
SRW64's own characters that neither source knows; the notes serve every locale.
"""
from __future__ import annotations

import json
import re
from pathlib import Path

from srw64_native.translation import Term

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SRWZ = Path.home() / "Super-Robot-Wars-Z"
STORY_TERMS = ROOT / "content/translation/story-terms.json"
LOCALE_FIELD = {"zh-Hans": "zh", "en": "en"}
# Term-table sections whose strings are names a line of dialogue can mention.
NAME_SECTIONS = {"pilots": True, "pilot_full_names": True, "character_list": True, "default_names": True,
                 "units": True, "weapons": True, "series": True, "spirits": False, "abilities": False,
                 "parts": False}
# Short katakana names that are also ordinary words; they stay hints (ボス, マスター).
COMMON_WORDS = {"ボス", "マスター", "キャプテン", "ドクター", "ジョーカー", "ゲリラ", "レディ"}
# SRW Z glossary categories that name things; its system/phrase entries are house style for another game.
HINT_CATEGORIES = {"weapon", "people", "unit", "organization", "place", "location", "faction", "species",
                   "technology", "event", "era", "work", "item", "concept", "group", "civilization", "energy"}
KANJI_ONLY = re.compile(r"[一-龯々]+")
HIRAGANA_ONLY = re.compile(r"[ぁ-ゖー]+")


def _load(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def binding_name(ja: str) -> bool:
    """1–2 character names (コウ, 大作, 忍) and hiragana names (ひかる) are also ordinary words:
    they stay hints, and the speaker cards still carry the right name for who is talking."""
    return len(ja) >= 3 and not HIRAGANA_ONLY.fullmatch(ja) and ja not in COMMON_WORDS


def project_terms(locale: str = "zh-Hans") -> list[Term]:
    path = ROOT / f"content/locales/terms/{locale}.json"
    if not path.exists():
        return []
    terms = []
    for section, table in _load(path)["sections"].items():
        if section not in NAME_SECTIONS:
            continue
        for ja, target in table.items():
            if "<" in ja or not target or ja == target:
                continue
            if section == "weapons" and HIRAGANA_ONLY.fullmatch(ja):
                continue  # した、くちばし: grammar and ordinary words, never a useful match
            terms.append(Term(ja, target, f"terms:{section}", NAME_SECTIONS[section] and binding_name(ja)))
    return terms


def story_terms(locale: str = "zh-Hans") -> list[Term]:
    """Dialogue-only proper nouns (organizations, places, ships…) from story-terms.json."""
    if not STORY_TERMS.exists():
        return []
    field = LOCALE_FIELD[locale]
    terms = []
    for ja, entry in _load(STORY_TERMS)["terms"].items():
        target = entry.get(field)
        if entry.get("kind") != "proper" or not target or entry.get("status") == "rejected":
            continue
        terms.append(Term(ja, target, "story-terms", binding_name(ja)))
    return terms


def srwz_terms(root: Path = DEFAULT_SRWZ) -> list[Term]:
    folder = root / "srwz-zh/corpus/glossary"
    if not folder.exists():
        return []
    terms = []
    for path in sorted(folder.glob("*.json")):
        doc = _load(path)
        for entry in doc.get("terms") or doc.get("entries") or []:
            if entry.get("registry_match") == "explicit_only" or not entry.get("translation") \
                    or entry.get("category") not in HINT_CATEGORIES or entry["translation"].isascii():
                continue  # a Latin-only rendering (オレンジ→Orange) is that game's choice, not a Chinese name
            for ja in entry.get("source_terms", []):
                # two-kanji names (元気, 大介) are too often ordinary words to hint blindly
                if ja and len(ja) >= 2 and not ja.isascii() and not (KANJI_ONLY.fullmatch(ja) and len(ja) <= 2):
                    terms.append(Term(ja, entry["translation"], f"srwz:{entry['id']}", False))
    return terms


def glossary(locale: str = "zh-Hans", srwz_root: Path = DEFAULT_SRWZ) -> list[Term]:
    """Project terms win, then dialogue terms; SRW Z hints only for Chinese and only where undecided."""
    project = project_terms(locale)
    decided = {t.ja for t in project}
    story = [t for t in story_terms(locale) if t.ja not in decided]
    decided |= {t.ja for t in story}
    hints, seen = [], set()
    if locale == "zh-Hans":
        for term in srwz_terms(srwz_root):
            if term.ja in decided or term.ja in seen:
                continue
            seen.add(term.ja)
            hints.append(term)
    return project + story + hints


class Characters:
    """Speaker cards: the name in the target locale, gender and a short profile, from whichever source knows them."""

    def __init__(self, locale: str = "zh-Hans", srwz_root: Path = DEFAULT_SRWZ):
        self.locale = locale
        terms_path = ROOT / f"content/locales/terms/{locale}.json"
        sections = _load(terms_path)["sections"] if terms_path.exists() else {}
        self.names = {**sections.get("pilot_full_names", {}), **sections.get("pilots", {})}
        roster_path = ROOT / "content/translation/zh-Hans/roster.json"
        self.roster = _load(roster_path)["characters"] if roster_path.exists() else {}
        self.library: dict[str, dict] = {}
        library = srwz_root / "srwz-community-web/public/data/library/character.json"
        if library.exists():
            for entry in _load(library)["entries"]:
                fields = {f["tag"]: f for f in entry["fields"]}
                profile = (fields.get("DSCR") or {}).get("translation") or ""
                info = {"zh": entry.get("title"), "work": entry.get("workTitle"), "profile": profile[:120]}
                he, she = profile.count("他"), profile.count("她")
                if she > he:
                    info["gender"] = "女"
                elif he > she:
                    info["gender"] = "男"
                for tag in ("CHNN", "CHFN"):
                    source = (fields.get(tag) or {}).get("sourceText")
                    if source:
                        self.library.setdefault(source, info)

    @staticmethod
    def base_name(label: str | None) -> str | None:
        return re.sub(r"（.*?）$", "", label).strip() if label else None

    def card(self, name: str) -> dict:
        card: dict = {"ja": name}
        roster = self.roster.get(name) or {}
        library = self.library.get(name) or {}
        if self.names.get(name):
            card["name"] = self.names[name]
        elif self.locale == "zh-Hans" and library.get("zh"):
            card["name"] = library["zh"] + "（参考机战Z）"
        for key in ("gender", "work"):
            value = roster.get(key) or library.get(key)
            if value:
                card[key] = value
        note = roster.get("note") or library.get("profile")
        if note:
            card["note"] = note
        return card
