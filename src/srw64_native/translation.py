"""Machine-translation plumbing that keeps every translation loadable by compile_locale.

A source record is cut into pages at <STOP>. Inside a page, <BR> is dropped (the native
dialogue renderer wraps and paginates by itself) and every run of special glyphs becomes a
placeholder the model must copy: dynamic-name runs read as 【主角名字】 etc., any other glyph
as ⟦G1⟧. Decoding puts the exact original runs back in their original order, so the
page structure and the glyph signature of the result equal the source by construction;
anything the model broke is reported instead of silently repaired.
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field

from .catalog import signature
from .original_story import NAME_SLOTS

GLYPH_RUN = re.compile(r"(<G:([0-9A-Fa-f]{4})>)(?:\1)*")
NAME_LABEL = {code: label.split("（")[0] for code, label in NAME_SLOTS.items()}
# How a dynamic-name slot reads in the prompt. Chinese-labelled placeholders get "translated" by an
# English model ([Partner's Name]), so English prompts see ASCII names. Decoding accepts either form.
NAME_PLACEHOLDER = {
    "zh-Hans": {code: f"【{label}】" for code, label in NAME_LABEL.items()},
    "en": {0x124: "{HeroNick}", 0x125: "{HeroFull}", 0x126: "{HeroName}", 0x127: "{HeroSurname}",
           0x128: "{PartnerNick}", 0x129: "{PartnerFull}", 0x12A: "{PartnerName}", 0x12B: "{PartnerSurname}",
           0x12C: "{HeroMech}"},
}
CANONICAL = {text: code for table in NAME_PLACEHOLDER.values() for code, text in table.items()}
PLACEHOLDER = re.compile("|".join(re.escape(t) for t in sorted(CANONICAL, key=len, reverse=True)) + r"|⟦G\d+⟧")
KANA = re.compile(r"[ぁ-ゖァ-ヺヽヾゝゞ]")
KATAKANA_WORD = re.compile(r"[ァ-ヺー・]+")


@dataclass
class Encoded:
    """Pages as the model sees them, plus the glyph runs each page must get back."""
    pages: list[str]
    runs: list[list[tuple[str, str]]] = field(default_factory=list)  # per page: (placeholder, original run)
    separator: str = "<STOP>"  # "<BR>" for choice texts, whose options are lines of one record


def join_lines(text: str) -> str:
    """Drop <BR> and the indentation after it; keep a space only between two Latin words."""
    def join(match: re.Match) -> str:
        before = match.string[match.start() - 1:match.start()]
        after = match.string[match.end():match.end() + 1]
        latin = before.isascii() and before.isalnum() and after.isascii() and after.isalnum()
        return " " if latin else ""
    return re.sub(r"(?:<BR>[ 　]*)+", join, text)


def encode(source: str, choice: bool = False, locale: str = "zh-Hans") -> Encoded:
    """Pages split at <STOP>; with choice=True the <BR>-separated options of a 3D44 text instead."""
    labels = NAME_PLACEHOLDER.get(locale, NAME_PLACEHOLDER["en"])
    if not source.endswith("<END>"):
        raise ValueError("source must end with <END>")
    if choice and "<STOP>" in source:
        raise ValueError("choice text with <STOP>")
    result = Encoded(pages=[], separator="<BR>" if choice else "<STOP>")
    for page in source[:-5].split(result.separator):
        runs, other = [], 0

        def slot(match: re.Match) -> str:
            nonlocal other
            code = int(match.group(2), 16)
            if code in labels:
                placeholder = labels[code]
            else:
                other += 1
                placeholder = f"⟦G{other}⟧"
            runs.append((placeholder, match.group(0)))
            return placeholder
        result.pages.append(join_lines(GLYPH_RUN.sub(slot, page)).strip(" 　"))
        result.runs.append(runs)
    return result


class DecodeError(ValueError):
    pass


def decode(encoded: Encoded, pages: list[str]) -> str:
    """Rebuild a catalog string; the model's pages must match the source page count and placeholders."""
    if len(pages) != len(encoded.pages):
        raise DecodeError(f"页数应为 {len(encoded.pages)}，实际给了 {len(pages)}（zh 必须与 ja 逐页对应）")
    out = []
    for index, (page, runs) in enumerate(zip(pages, encoded.runs)):
        if not isinstance(page, str):
            raise DecodeError(f"第 {index + 1} 页不是文本")
        page = page.replace("\r", "").replace("\n", "")
        if "<" in page or ">" in page:
            page = page.replace("<", "＜").replace(">", "＞")
        found = PLACEHOLDER.findall(page)
        expected = [p for p, _ in runs]
        if [CANONICAL.get(p, p) for p in found] != [CANONICAL.get(p, p) for p in expected]:
            raise DecodeError(f"第 {index + 1} 页的占位符应为 {expected}，实际为 {found}")
        pieces = PLACEHOLDER.split(page)
        rebuilt = pieces[0]
        for (_, run), piece in zip(runs, pieces[1:]):
            rebuilt += run + piece
        out.append(rebuilt)
    target = encoded.separator.join(out) + "<END>"
    if signature(target) != signature(encoded_source_signature(encoded)):
        raise DecodeError("还原后的控制符与原文不一致")
    return target


def encoded_source_signature(encoded: Encoded) -> str:
    return encoded.separator.join("".join(run for _, run in runs) for runs in encoded.runs) + "<END>"


# ---------------------------------------------------------------------- style checks
CJK = re.compile(r"[\u3400-\u9fff\uf900-\ufaff]")
FULLWIDTH_PUNCT = re.compile(r"[、。！？「」『』（）：；]")
# Visible-length ratio target / source outside which a page is probably cut or padded.
RATIO = {"zh-Hans": (0.35, 2.5), "en": (0.6, 10.0)}
KANA_SILENT = set("ーッっァィゥェォャュョヮヵヶぁぃぅぇぉゃゅょゎ")
ROMANIZED_HONORIFIC = re.compile(r"(?:\b[A-Za-z]+|[}\]】])-(?:sama|san|kun|chan|dono|senpai|sensei)\b", re.IGNORECASE)


def style_problems(source_page: str, target_page: str, locale: str = "zh-Hans") -> list[str]:
    """Mechanical checks for one page; every hit is a reason to re-request, not an auto-fix."""
    problems = []
    visible_source = PLACEHOLDER.sub("", source_page).strip(" 　")
    stripped = PLACEHOLDER.sub("", target_page)
    visible_target = stripped.strip(" 　")
    if visible_source and not visible_target:
        problems.append("空译文")
    kana = KANA.findall(stripped)
    if kana:
        problems.append("残留假名：" + "".join(dict.fromkeys(kana)))
    if locale == "zh-Hans":
        for jp in ("「", "」", "『", "』"):
            if jp in target_page:
                problems.append(f"未转换的日文引号 {jp}")
    else:
        cjk = CJK.findall(stripped)
        if cjk:
            problems.append("残留汉字：" + "".join(dict.fromkeys(cjk)))
        punct = FULLWIDTH_PUNCT.findall(stripped)
        if punct:
            problems.append("残留全角标点：" + "".join(dict.fromkeys(punct)))
        honorific = ROMANIZED_HONORIFIC.findall(target_page)  # {HeroNick}-chan counts too
        if honorific:
            problems.append("保留了日式敬称：" + "、".join(dict.fromkeys(honorific)) + "（改用英文称谓或省略）")
    return problems


def letters(text: str) -> int:
    """Length that counts words, not punctuation: ……/... and quotes weigh nothing."""
    return len(re.sub(r"[\W_]", "", PLACEHOLDER.sub("", text)))


def source_weight(text: str) -> float:
    """Japanese length as it translates: kanji 1, kana ½, drawn-out vowels and small kana 0
    (ライトニングソォォォォード is a short name, not a long sentence)."""
    weight = 0.0
    for char in re.sub(r"[\W_]", "", PLACEHOLDER.sub("", text)):
        weight += 0 if char in KANA_SILENT else 0.5 if KANA.match(char) else 1
    return weight


def ratio_problems(source_pages: list[str], target_pages: list[str], locale: str = "zh-Hans") -> list[str]:
    """Over the whole record: a sentence that straddles a page break may split at a different point."""
    source, target = sum(map(source_weight, source_pages)), sum(map(letters, target_pages))
    low, high = RATIO.get(locale, RATIO["en"])
    if source >= 8 and target and not low <= target / source <= high:
        return [f"长度比异常 {target / source:.2f}"]
    return []


def record_problems(target_pages: list[str], locale: str = "zh-Hans") -> list[str]:
    """Checks that only make sense over the whole record: a quote opens on one page and closes on another."""
    text = "".join(target_pages)
    problems = []
    if text.count("“") != text.count("”"):
        problems.append("双引号不成对")
    if locale == "zh-Hans" and text.count("‘") != text.count("’"):
        problems.append("单引号不成对")  # English uses ’ as the apostrophe too
    if locale != "zh-Hans" and '"' in text:
        problems.append("用了直引号，应为 “ ”")
    return problems


# ---------------------------------------------------------------------- glossary
@dataclass(frozen=True)
class Term:
    ja: str
    zh: str            # the rendering in the target locale (Chinese or English)
    source: str        # where the decision lives, e.g. "terms:pilots" or "srwz:people/..."
    binding: bool      # binding terms must appear in the translation


def relevant_terms(texts: list[str], terms: list[Term], limit: int = 200) -> list[Term]:
    """Terms whose Japanese occurs in the batch, longest first; katakana terms must stand as a word
    (ガイ must not hit ガイゾック) and a term swallowed by a longer hit is dropped."""
    blob = "\n".join(texts)
    hits: dict[str, Term] = {}
    for term in sorted(terms, key=lambda t: (-len(t.ja), not t.binding)):
        if len(term.ja) < 2 or term.ja not in blob or term.ja in hits:
            continue
        if KATAKANA_WORD.fullmatch(term.ja) and not re.search(
                rf"(?<![ァ-ヺー]){re.escape(term.ja)}(?![ァ-ヺー])", blob):
            continue
        hits[term.ja] = term
    kept, reduced = [], blob
    for term in hits.values():
        if term.ja in reduced:
            kept.append(term)
        reduced = reduced.replace(term.ja, "\0")
    return kept[:limit]


def missing_terms(source_pages: list[str], target_pages: list[str], terms: list[Term]) -> list[str]:
    """Binding terms whose rendering is absent; case-insensitive so a sentence-initial capital still counts."""
    source, target = "".join(source_pages), "".join(target_pages).casefold()
    return [f"{t.ja}→{t.zh}" for t in relevant_terms([source], terms) if t.binding and t.zh.casefold() not in target]


# ---------------------------------------------------------------------- punctuation normalization
PAIRS = {"zh-Hans": {"「": ("“", "”"), "（": ("（", "）")}, "en": {"「": ("“", "”"), "（": ("(", ")")}}
CLOSING = {"「": "」", "（": "）"}


class Renames:
    """Term renames applied to finished translations (content/translation/renames.json).

    A rename replaces a whole name only: at each position the longest known name wins, so
    renaming 阿克 never touches 阿克西斯 as long as 阿克西斯 is itself a known term (the
    protected names). Latin names match whole words only. Name placeholders are left alone.
    A gated rename (a short name) applies only to a text whose Japanese source contains one
    of its Japanese names: 老大 becomes 波士 where the source says ボス, nowhere else. A
    rename whose gate is closed does not shield the shorter names inside it."""

    def __init__(self, pairs: dict[str, str], protected=(), latin: bool = False,
                 gates: dict[str, set[str]] | None = None):
        self.pairs = {old: new for old, new in pairs.items() if old and old != new}
        self.gates = {old: {plain_japanese(j) for j in jas} for old, jas in (gates or {}).items() if jas}
        self.protected = {n for n in protected if n} - set(self.pairs)
        self.names = sorted({*self.pairs, *self.protected}, key=len, reverse=True)  # every known name, longest first
        self.latin = latin
        self._patterns: dict[frozenset, re.Pattern | None] = {}

    def _pattern(self, allowed: frozenset) -> re.Pattern | None:
        if allowed not in self._patterns:
            names = sorted({*allowed, *self.protected}, key=len, reverse=True)
            edge = (r"(?<![A-Za-z0-9])", r"(?![A-Za-z0-9])") if self.latin else ("", "")
            body = "|".join(re.escape(n) for n in names)
            self._patterns[allowed] = re.compile(f"{edge[0]}(?:{body}){edge[1]}") if names else None
        return self._patterns[allowed]

    def apply(self, text: str, source: str | None = None) -> str:
        if not self.pairs:
            return text
        plain = plain_japanese(source) if source is not None else None
        allowed = frozenset(old for old in self.pairs
                            if plain is None or old not in self.gates or any(ja in plain for ja in self.gates[old]))
        pattern = self._pattern(allowed) if allowed else None
        if not pattern:
            return text
        out, at = [], 0
        for m in PLACEHOLDER.finditer(text):
            out.append(pattern.sub(self._swap, text[at:m.start()]))
            out.append(m.group())
            at = m.end()
        out.append(pattern.sub(self._swap, text[at:]))
        return "".join(out)

    def _swap(self, m: re.Match) -> str:
        return self.pairs.get(m.group(), m.group())


def plain_japanese(text: str) -> str:
    """Japanese for name matching: no control tokens, spaces, ・ or ＝ (names break across lines)."""
    return re.sub(r"<[^>]*>|[\s\u3000・＝=]", "", text)


def chinese_marks(pages: list[str], locale: str) -> list[str]:
    """Chinese punctuation as the style rules want it: full-width ！ and ？ (the Japanese source,
    and so some drafts, has ASCII ones) and no full stop after an ellipsis (……。 becomes ……)."""
    if locale != "zh-Hans":
        return pages
    return [re.sub(r"(…+)。", r"\1", p.replace("!", "！").replace("?", "？")) if isinstance(p, str) else p
            for p in pages]


def normalize_quotes(source_pages: list[str], target_pages: list[str], locale: str = "zh-Hans") -> list[str]:
    """When the Japanese is one 「…」 or （…） spanning every page, give the translation exactly one opening
    mark on its first page and one closing mark on its last: models drop the pair or quote each page.
    Other shapes (several quotes, a quote inside narration) are left as the model wrote them."""
    text = "".join(source_pages).strip(" 　")
    if not target_pages or not text:
        return target_pages
    for opener, (left, right) in PAIRS.get(locale, PAIRS["en"]).items():
        closer = CLOSING[opener]
        if not (text.startswith(opener) and text.endswith(closer)
                and text.count(opener) == 1 and text.count(closer) == 1):
            continue
        straight = {left, right, "“", "”", '"'}
        pages = [p.strip(" 　") for p in target_pages]
        last = len(pages) - 1
        for n, page in enumerate(pages):
            while page[:1] in straight | {left} and (n > 0 or page[:1] != left):
                page = page[1:].lstrip(" 　")
            while page[-1:] in straight | {right} and (n < last or page[-1:] != right):
                page = page[:-1].rstrip(" 　")
            pages[n] = page
        if not pages[0].startswith(left):
            pages[0] = left + pages[0]
        if not pages[last].endswith(right):
            pages[last] = pages[last] + right
        return pages
    return target_pages
