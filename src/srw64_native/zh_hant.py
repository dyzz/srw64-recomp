"""Traditional Chinese for Taiwan (zh-Hant), made from the Simplified Chinese.

Nothing is translated twice: every zh-Hant text is the zh-Hans text converted.
The wording goes through OpenCC s2twp (Taiwan characters and Taiwan words); the
names of units, people, weapons and works do not, because a converted mainland
name is not what Taiwanese players call them. content/translation/zh-Hant/names.json
gives the Taiwan name of each Japanese name (official Traditional Chinese Super
Robot Wars, the work's Taiwan release, then common use); a name without one keeps
the mainland name in Taiwan characters (s2tw, characters only).

A name is replaced only in a text whose Japanese contains that name, as
content/translation/renames.json does, so a common word that happens to be a
short name is left alone. Quotation marks become Taiwan's 「」 and 『』."""
from __future__ import annotations

import json
import re
from functools import cache
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NAMES = "content/translation/zh-Hant/names.json"
TERMS = "content/locales/terms/zh-Hans.json"
# Term sections whose strings are names: names.json, else the mainland name in Taiwan characters.
NAME_SECTIONS = ("series", "units", "pilots", "pilot_full_names", "character_list", "weapons", "default_names")
# Game terms: the official Traditional Chinese Super Robot Wars term from names.json, else
# converted like any wording.
SYSTEM_SECTIONS = ("abilities", "parts", "skills", "required_skills", "terrain", "spirits")
QUOTES = str.maketrans({"“": "「", "”": "」", "‘": "『", "’": "』"})
# Supplementary Private Use Area: never in the text, and kept whole by OpenCC.
HOLD = 0xF0000
HELD = re.compile("[\U000F0000-\U000FFFFD]")


@cache
def _opencc(config: str):
    import opencc  # requirements.lock; only the tools that write zh-Hant need it
    return opencc.OpenCC(config)


def wording(text: str) -> str:
    """Taiwan characters and words, with Taiwan quotation marks."""
    return _opencc("s2twp").convert(text).translate(QUOTES)


def characters(text: str) -> str:
    """Taiwan characters only: for names, whose words are not words."""
    return _opencc("s2tw").convert(text)


def japanese_of(text: str) -> str:
    """Japanese to look names up in: the lines joined, without line breaks or indents."""
    return "".join(line.strip(" 　") for line in text.replace("<BR>", "\n").splitlines())


class Converter:
    def __init__(self, root: Path = ROOT):
        document = json.loads((root / NAMES).read_text())
        if document.get("schema") != "srw64.zh-hant-names.v1":
            raise ValueError("Unsupported zh-Hant names schema")
        taiwan = {row["ja"]: row["zh_hant"] for row in document["entries"] if row.get("zh_hant")}
        sections = json.loads((root / TERMS).read_text())["sections"]
        # Words that are not terms but name the same things (高达 in narration), and words
        # s2twp gets wrong here, kept to their characters (的士兵 is not a taxi's soldiers).
        words = dict(document.get("words", {}))
        for word in document.get("keep", []):
            words.setdefault(word, characters(word))
        self.words = sorted(words.items(), key=lambda row: (-len(row[0]), row[0]))
        terms = [(section in NAME_SECTIONS, ja.strip(), hans.strip()) for section in NAME_SECTIONS + SYSTEM_SECTIONS
                 for ja, hans in sections.get(section, {}).items() if hans.strip()]
        # A game term is wording in a sentence unless Taiwan names it differently.
        holds = lambda rows: {(ja, hans): hant for ja, hans, hant in rows if len(ja) > 1 and len(hans) > 1}
        self.pairs = self._sorted(holds((ja, hans, taiwan[ja]) for _, ja, hans in terms if ja in taiwan))
        # A name without a Taiwan name still takes the Taiwan names in it (an unresearched
        # weapon named after its unit), the rest in Taiwan characters only.
        self.names: dict[str, str] = {}  # Japanese term -> zh-Hant, for every name and game term
        for proper, ja, hans in terms:
            if ja not in self.names:
                self.names[ja] = taiwan.get(ja) or self.convert(hans, ja, characters if proper else wording)
        self.pairs = self._sorted(holds((ja, hans, self.names[ja]) for proper, ja, hans in terms if proper or ja in taiwan))

    @staticmethod
    def _sorted(pairs: dict) -> list[tuple[str, str, str]]:
        # The longest Simplified name first, so a full name wins over the given name in it.
        return sorted(((ja, hans, hant) for (ja, hans), hant in pairs.items()), key=lambda row: (-len(row[1]), row[1], row[0]))

    def term(self, section: str, ja: str, hans: str) -> str:
        """One string of a term table, its leading and trailing spaces kept."""
        if section not in NAME_SECTIONS + SYSTEM_SECTIONS or ja.strip() not in self.names:
            return self.convert(hans, ja)
        core = hans.strip()
        return hans[:len(hans) - len(hans.lstrip())] + self.names[ja.strip()] + hans[len(hans.rstrip()):] if core else hans

    def convert(self, hans: str, ja: str | None = None, rest=wording) -> str:
        """zh-Hans text in zh-Hant; names are replaced where JA (its Japanese) has them."""
        if not hans:
            return hans
        held: list[str] = []

        def hold(value: str) -> str:
            held.append(value)
            return chr(HOLD + len(held) - 1)

        text = hans
        if ja:
            source = japanese_of(ja)
            for name_ja, name_hans, hant in self.pairs:
                if name_hans in text and name_ja in source:
                    text = text.replace(name_hans, hold(hant))
        for word_hans, hant in self.words:
            if word_hans in text:
                text = text.replace(word_hans, hold(hant))
        text = rest(text)
        if rest is characters:
            text = text.translate(QUOTES)
        return HELD.sub(lambda match: held[ord(match.group()) - HOLD], text)

    def add_to(self, value):
        """Every {"zh-Hans": text, "ja": text, ...} inside VALUE gains "zh-Hant" after "zh-Hans"."""
        if isinstance(value, list):
            return [self.add_to(item) for item in value]
        if not isinstance(value, dict):
            return value
        result = {}
        for key, item in value.items():
            if key == "zh-Hant":
                continue
            result[key] = self.add_to(item)
            if key == "zh-Hans" and isinstance(item, str):
                ja = value.get("ja") if isinstance(value.get("ja"), str) else None
                result["zh-Hant"] = self.convert(item, ja)
        return result
