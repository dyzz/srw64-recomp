"""Traditional Chinese for Hong Kong (zh-Hant-HK), made from the Simplified Chinese.

Hong Kong is the second region of Traditional Chinese: the game shows it as 繁體中文
with 香港 chosen, and what it does not say differently reads as Taiwan's (zh-Hant,
src/srw64_native/zh_hant.py). Its text is made the way Super Robot Wars Z made its
Hong Kong text, with that project's tables (tools/translation/import_srwz_hk.py), and
no OpenCC word tables:

  names       content/translation/zh-Hant-HK/names.json: the Hong Kong name of each
              Japanese name (Hong Kong broadcasts and releases, Super Robot Wars Z's
              table; else the Taiwan name in Hong Kong characters)
  words       regional.json "terms": Hong Kong words for mainland ones (导弹 導彈, 机器人
              機械人, 高达 高達), some only where the record's Japanese has a word
  characters  OpenCC s2tw (characters only), then regional.json "characters": the
              Hong Kong forms of Taiwan characters that Hong Kong writes (說 説, 裡 裏)

Names are replaced only where the Japanese has them, as in zh_hant.py; quotation marks
are 「」 and 『』 as Taiwan's."""
from __future__ import annotations

import json
import re
from pathlib import Path

from .zh_hant import (HELD, HOLD, NAME_SECTIONS, QUOTES, ROOT, SYSTEM_SECTIONS, TERMS, characters as taiwan_characters,
                      japanese_of, longest_first, phrases)

LOCALE = "zh-Hant-HK"
NAMES = "content/translation/zh-Hant-HK/names.json"
REGIONAL = "content/translation/zh-Hant-HK/regional.json"


class HongKong:
    def __init__(self, root: Path = ROOT):
        document = json.loads((root / NAMES).read_text())
        if document.get("schema") != "srw64.zh-hant-hk-names.v1":
            raise ValueError("Unsupported zh-Hant-HK names schema")
        regional = json.loads((root / REGIONAL).read_text())
        if regional.get("schema") != "srw64.zh-hant-hk-regional.v1":
            raise ValueError("Unsupported zh-Hant-HK regional schema")
        self.regional = regional
        self.map = str.maketrans(regional["characters"])
        # Longest mainland word first; for one word, the rules bound to the Japanese first.
        self.rules = sorted(regional["terms"], key=lambda r: (-len(r["sc"]), r["sc"], not r.get("jp")))
        # Every Hong Kong string ends in Hong Kong characters, names and words too (as Super
        # Robot Wars Z's derivation does): a table's name may carry a Taiwan form (殖民衛星).
        hong_kong = {row["ja"]: row["zh_hant_hk"].translate(self.map) for row in document["entries"] if row.get("zh_hant_hk")}
        self.rules = [{**rule, "hk": rule["hk"].translate(self.map)} for rule in self.rules]
        # Taiwan's phrases.json: the characters OpenCC picks wrong (只能, 干得, 组织里), in Hong Kong form.
        table = phrases(root)
        hk_form = lambda rows: longest_first((sc, tw.translate(self.map)) for sc, tw in rows.items())
        self.phrases = hk_form(table["phrases"])
        self.guard = hk_form(table["guard"])
        self.dialogue_guard = hk_form({**table["dialogue_guard"], **table["guard"]})
        sections = json.loads((root / TERMS).read_text())["sections"]
        terms = [(section in NAME_SECTIONS, ja.strip(), hans.strip()) for section in NAME_SECTIONS + SYSTEM_SECTIONS
                 for ja, hans in sections.get(section, {}).items() if hans.strip()]
        # Every name and game term has its Hong Kong form: the table's, else the mainland
        # name in Hong Kong characters (a game term as any words).
        self.names: dict[str, str] = {}
        for proper, ja, hans in terms:
            if ja not in self.names:
                self.names[ja] = hong_kong.get(ja) or self.convert(hans, ja, proper=proper)
        pairs = {(ja, hans): self.names[ja] for proper, ja, hans in terms
                 if (proper or ja in hong_kong) and len(ja) > 1 and len(hans) > 1}
        spellings = {row["ja"]: row.get("spellings", []) for row in document["entries"]}
        pairs.update({(spelling, hans): hk for (ja, hans), hk in list(pairs.items()) for spelling in spellings.get(ja, [])})
        # The longest Simplified name first, so a full name wins over the given name in it.
        self.pairs = sorted(((ja, hans, hk) for (ja, hans), hk in pairs.items()), key=lambda r: (-len(r[1]), r[1], r[0]))

    def characters(self, text: str) -> str:
        """Hong Kong characters only."""
        return taiwan_characters(text).translate(self.map)

    def _fires(self, rule: dict, source: str | None) -> bool:
        if "jp" in rule and not (source and any(word in source for word in rule["jp"])):
            return False
        return not (source and any(word in source for word in rule.get("jp_not", [])))

    def term(self, section: str, ja: str, hans: str) -> str:
        """One string of a term table, its leading and trailing spaces kept."""
        if section not in NAME_SECTIONS + SYSTEM_SECTIONS or ja.strip() not in self.names:
            return self.convert(hans, ja)
        core = hans.strip()
        return hans[:len(hans) - len(hans.lstrip())] + self.names[ja.strip()] + hans[len(hans.rstrip()):] if core else hans

    def convert(self, hans: str, ja: str | None = None, *, proper: bool = False, dialogue: bool = False) -> str:
        """zh-Hans text in zh-Hant-HK; names are replaced where JA (its Japanese) has them.
        PROPER: a name without a Hong Kong name, whose words are not words (characters only).
        DIALOGUE: a line of a dialogue file, whose narration keeps a few words (phrases.json)."""
        if not hans:
            return hans
        held: list[str] = []

        def hold(value: str) -> str:
            held.append(value)
            return chr(HOLD + len(held) - 1)

        text = hans
        for word, hk in self.dialogue_guard if dialogue else getattr(self, "guard", []):
            if word in text:
                text = text.replace(word, hold(hk))
        source = japanese_of(ja) if ja else None
        if source and hasattr(self, "pairs"):
            for name_ja, name_hans, hk in self.pairs:
                if name_hans in text and name_ja in source:
                    text = text.replace(name_hans, hold(hk))
        # A name takes only the works' names (高达 高達, 机器人 機械人), not the words.
        for rule in self.rules:
            if (not proper or rule.get("group") == "franchise") and rule["sc"] in text and self._fires(rule, source):
                text = text.replace(rule["sc"], hold(rule["hk"]))
        for word, hk in getattr(self, "phrases", []):
            if word in text:
                text = text.replace(word, hold(hk))
        # Simplified 著 is always zhù (显著, 著作): s2tw writes 着 as 著 too, and the Hong Kong
        # form of that 著 is 着, so the Simplified one is kept apart.
        if "著" in text:
            text = text.replace("著", hold("著"))
        text = self.characters(text).translate(QUOTES)
        return HELD.sub(lambda match: held[ord(match.group()) - HOLD], text)

    def name(self, hans: str) -> str:
        """A name with no Japanese beside it (the Library's own names): the Hong Kong names in
        it wherever their mainland names are, then the works' names and Hong Kong characters."""
        held: list[str] = []
        text = hans
        for _, name_hans, hk in self.pairs:
            if name_hans in text:
                held.append(hk)
                text = text.replace(name_hans, chr(HOLD + len(held) - 1))
        for rule in self.rules:
            if rule.get("group") == "franchise" and rule["sc"] in text:
                held.append(rule["hk"])
                text = text.replace(rule["sc"], chr(HOLD + len(held) - 1))
        text = self.characters(text).translate(QUOTES)
        return HELD.sub(lambda match: held[ord(match.group()) - HOLD], text)

    def add_to(self, value):
        """Every {"zh-Hans": text, "ja": text, ...} inside VALUE gains "zh-Hant-HK" after "zh-Hant"
        (after "zh-Hans" when it has no zh-Hant), when it differs from the zh-Hant text."""
        if isinstance(value, list):
            return [self.add_to(item) for item in value]
        if not isinstance(value, dict):
            return value
        hk = None
        if isinstance(value.get("zh-Hans"), str):
            ja = value.get("ja") if isinstance(value.get("ja"), str) else None
            hk = self.convert(value["zh-Hans"], ja)
            if hk == value.get("zh-Hant"):
                hk = None
        after = "zh-Hant" if "zh-Hant" in value else "zh-Hans"
        result = {}
        for key, item in value.items():
            if key == LOCALE:
                continue
            result[key] = self.add_to(item)
            if key == after and hk is not None:
                result[LOCALE] = hk
        return result
