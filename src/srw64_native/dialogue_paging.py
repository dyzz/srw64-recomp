"""Page layout of story dialogue: the Python reference of the game's rules.

The game (src/native/text/portable_text.cpp, FontSet::Builder::paginate, and
src/host/dialogue_scene.cpp) reads a story record whole and pages it to fit the
text area; docs/design/dialogue-typesetting.md gives the rules. This module
repeats the rules that do not depend on a font: joining a record's original
pages, the size and spacing, and the choice of page ends. Line widths and legal
breaks come from the game's shaping; tests/data/dialogue-paging-cases.json
records them per case, and tests/test_dialogue_paging.py checks that this code
picks the same pages and lines from them.

Offsets are UTF-16 code units, as in the game.
"""
from __future__ import annotations

import math
from bisect import bisect_right
from dataclasses import dataclass, field
from typing import Callable, Sequence

# docs/design/dialogue-typesetting.md §6; portable_text.hpp lists the same.
SENTENCE_END = "。！？…!?.」』）”\""
COMMA = "，、；：—,;:"
HALVABLE = "。，、；：」』）》】〕"
OPENING = "“「『（《【〔\""  # with the two above: not counted in a short last line
BODY_WIDTH, BODY_HEIGHT = 177.0, 43.0
MAX_SPACING = 1.22
HARD_BREAKS = "\n\r\u0085  "


@dataclass(frozen=True)
class PageStyle:
    height: float = BODY_HEIGHT
    min_spacing: float = 1.08
    max_spacing: float = MAX_SPACING
    rank_breaks: bool = True
    halve_line_end: bool = False

    def lines_per_page(self, size: float) -> int:
        # The tolerance keeps 3.0008 lines at three.
        return max(1, math.floor(self.height / (size * self.min_spacing) + 1e-6))

    def pitch(self, size: float) -> float:
        return max(size * self.min_spacing, min(size * self.max_spacing, self.height / self.lines_per_page(size)))


def body_style(locale: str) -> PageStyle:
    return PageStyle(min_spacing=1.15 if locale == "en" else 1.08, halve_line_end=locale == "zh-Hans")


def sentence_ends(locale: str, stops: Sequence[int]) -> list[int]:
    """The original's page breaks end a sentence only in Japanese: a translation marks its
    own sentence ends, so a bare original page break there is a sentence carried over."""
    return sorted(stops) if locale == "ja" else []


def body_size(setting: float, locale: str) -> float:
    """The text size for the reader's size setting; English takes 0.85 of it."""
    return setting * 0.85 if locale == "en" else float(setting)


def utf16(text: str) -> list[str]:
    """The text as UTF-16 code units, one string each (surrogates stay separate)."""
    data = text.encode("utf-16-le", "surrogatepass")
    return [chr(int.from_bytes(data[i:i + 2], "little")) for i in range(0, len(data), 2)]


def joined_record(pages: Sequence[str], locale: str) -> tuple[str, list[int]]:
    """One record's original pages as one text, and where the later pages start.

    Chinese and Japanese pages join directly; English ones with a space unless
    either side is already blank."""
    blank = " \n　"
    text, stops = "", []
    for i, part in enumerate(pages):
        if i:
            if locale == "en" and text and part and text[-1] not in blank and part[0] not in blank:
                text += " "
            stops.append(len(utf16(text)))
        text += part
    return text, stops


@dataclass
class Line:
    start: int
    end: int
    width: float = 0.0
    halved: bool = False
    emergency: bool = False


@dataclass(order=True)
class Rank:
    pages: int = 0
    middle: int = 0
    comma: int = 0
    orphans: int = 0


def end_class(units: Sequence[str], end: int, sentence_ends: Sequence[int]) -> str:
    n = len(units)
    if end >= n or end in sentence_ends:
        return "sentence"
    p = end
    while p > 0 and units[p - 1] in (" ", "\n", "　"):
        p -= 1
    if not p or p in sentence_ends:
        return "sentence"
    c = units[p - 1]
    if c in SENTENCE_END:
        return "sentence"
    return "comma" if c in COMMA else "middle"


def short_line(units: Sequence[str], clusters: Sequence[int], start: int, end: int) -> bool:
    """A last line of one or two characters (marks and quotes not counted), or of a single English word."""
    while end > start and units[end - 1] in (" ", "\n"):
        end -= 1
    while start < end and units[start] == " ":
        start += 1
    if end <= start:
        return False
    body = units[start:end]
    if all(ord(c) < 0x2E80 for c in body):
        return " " not in body
    characters = 0
    for k in range(bisect_right(clusters, start), len(clusters)):
        if clusters[k] > end:
            break
        piece = units[(clusters[k - 1] if k else 0):clusters[k]]
        characters += not (len(piece) == 1 and piece[0] in SENTENCE_END + COMMA + OPENING)
    return characters <= 2


@dataclass
class Paged:
    pages: list[tuple[int, int]] = field(default_factory=list)
    lines: list[Line] = field(default_factory=list)


def paginate(text: str, clusters: Sequence[int], legal: Sequence[int], line_at: Callable[[int], Line],
             per_page: int, forced: Sequence[int] = (), sentence_ends: Sequence[int] = (),
             rank_breaks: bool = True) -> Paged:
    """Page ends as the game picks them, from its greedy lines.

    line_at(start) is the greedy line from a start: as much as fits, ending at a
    legal break. A page covers up to per_page such lines; it may end at the end
    of any of them or at a legal break inside the last, but never past a forced
    page start. Among the ways to page the whole text the fewest pages win, then
    the fewest ends mid-sentence, then at a comma, then the fewest short last lines; on a
    tie the page before an end starts as late as possible, so earlier pages are the fuller."""
    units = utf16(text)
    n = len(units)
    if not n:
        return Paged([(0, 0)], [])
    forced = sorted({f for f in forced if 0 < f < n})
    sentence_ends = sorted(sentence_ends)
    legal = sorted(legal)
    best: dict[int, tuple[Rank, int]] = {0: (Rank(), n)}
    starts = [0]
    si = 0
    while si < len(starts):
        s = starts[si]
        if s < n:
            k = bisect_right(forced, s)
            cap = forced[k] if k < len(forced) else n
            candidates: list[int] = []
            at = s
            for _ in range(per_page):
                if not (at < n and at < cap):
                    break
                line_end = min(line_at(at).end, cap)
                if rank_breaks:
                    candidates += [b for b in legal[bisect_right(legal, at):] if b < line_end]
                candidates.append(line_end)
                at = line_end
            if not rank_breaks:
                candidates = candidates[-1:]
            base = best[s][0]
            for e in sorted(set(candidates)):
                if e <= s:
                    continue
                r = Rank(base.pages + 1, base.middle, base.comma, base.orphans)
                if e < n:
                    kind = end_class(units, e, sentence_ends)
                    r.middle += kind == "middle"
                    r.comma += kind == "comma"
                last = s
                while True:
                    end = line_at(last).end
                    if end >= e or end >= n:
                        break
                    last = end
                r.orphans += short_line(units, clusters, last, e)
                if e not in best or not best[e][0] < r:
                    if e not in best:
                        starts.append(e)
                    best[e] = (r, s)
            starts[si + 1:] = sorted(starts[si + 1:])
        si += 1
    bounds = [n]
    while bounds[-1] > 0:
        bounds.append(best[bounds[-1]][1])
    bounds.reverse()
    result = Paged()
    for s, e in zip(bounds, bounds[1:]):
        result.pages.append((s, e))
        at = s
        while at < e:
            line = line_at(at)
            if line.end > e:
                line = cut_line(units, clusters, at, e)
            result.lines.append(line)
            at = line.end
    return result


def cut_line(units: Sequence[str], clusters: Sequence[int], start: int, limit: int) -> Line:
    """The line from start cut at a page end inside it (the game reshapes it)."""
    body_end = next((i for i in range(start, len(units)) if units[i] in HARD_BREAKS), len(units))
    end = min(limit, body_end)
    if end == body_end and body_end < len(units):
        end = clusters[bisect_right(clusters, body_end)]  # through the newline grapheme
    return Line(start, end)
