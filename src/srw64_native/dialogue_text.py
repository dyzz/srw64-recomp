"""Plain-text dialogue files: one editable file set per locale.

Story dialogue, choices and battle quotes live in content/dialogue/<locale>/**/*.txt
instead of the locale JSON, and players can override single entries from their
user directory. The format is described in docs/guide/dialogue-text.md; the game
reads it with src/native/localization/dialogue_text.cpp, which follows the same
rules. An entry becomes the usual catalog form: <BR> between lines, <STOP> between
pages, <G:XXXX> for a placeholder, <END> at the end."""
from __future__ import annotations

import re
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

# The nine dynamic-name glyphs the dialogue expands from the game's name banks.
NAMES = {0x124: "HeroNick", 0x125: "HeroFull", 0x126: "HeroName", 0x127: "HeroSurname",
         0x128: "PartnerNick", 0x129: "PartnerFull", 0x12A: "PartnerName", 0x12B: "PartnerSurname",
         0x12C: "HeroMech"}
CODES = {name: code for code, name in NAMES.items()}
# Records before this one are names, labels and messages owned by the term tables.
FIRST_DIALOGUE_RECORD = 5644
TOKEN = re.compile(r"<(BR|STOP|END|G:[0-9A-Fa-f]{1,4})>")
PLACEHOLDER = re.compile(r"\{([A-Za-z]+|G:[0-9A-Fa-f]{4})\}")
HEADER = re.compile(r"@(?:(\d{1,5})|intro:(\d{1,5}))(?:[ \t]+(.*))?$")
SPECIAL = (">", "*", "#", "@", "\\")


class DialogueTextError(ValueError):
    pass


@dataclass
class Page:
    source: list[str] = field(default_factory=list)
    target: list[str] = field(default_factory=list)
    source_options: bool = False
    target_options: list[bool] = field(default_factory=list)


@dataclass
class Entry:
    key: str
    note: str
    path: str
    line: int
    pages: list[Page] = field(default_factory=lambda: [Page()])


@dataclass
class Problem:
    path: str
    line: int
    key: str
    message: str

    def __str__(self) -> str:
        return f"{self.path}:{self.line}: {self.key or '-'}: {self.message}"


def key_of(record: int) -> str:
    return f"base:t00_{record:05d}"


def pages_of(text: str) -> list[list[str]]:
    """Catalog form -> pages of lines, a run of one name glyph as one placeholder."""
    if not text.endswith("<END>"):
        raise DialogueTextError("Message must end with <END>")
    pages, lines, line, last = [], [], "", None
    position, body = 0, text[:-5]
    for match in TOKEN.finditer(body):
        literal = body[position:match.start()]
        if literal:
            line += literal
            last = None
        position = match.end()
        token = match.group(1)
        if token == "BR":
            lines.append(line)
            line, last = "", None
        elif token == "STOP":
            lines.append(line)
            pages.append(lines)
            lines, line, last = [], "", None
        elif token == "END":
            raise DialogueTextError("Message contains an early <END>")
        else:
            code = int(token[2:], 16)
            if code in NAMES:
                if last != code:
                    line += "{" + NAMES[code] + "}"
                last = code
            else:
                line += "{G:%04X}" % code
                last = None
    tail = body[position:]
    if tail:
        line += tail
    lines.append(line)
    pages.append(lines)
    return pages


def placeholders(line: str, where: str) -> list[int]:
    codes = []
    for match in PLACEHOLDER.finditer(line):
        name = match.group(1)
        if name.startswith("G:"):
            codes.append(int(name[2:], 16))
        elif name in CODES:
            codes.append(CODES[name])
        else:
            raise DialogueTextError(f"{where}: unknown placeholder {{{name}}}")
    return codes


def catalog_line(line: str) -> str:
    if "<" in line:
        raise DialogueTextError("translation contains a half-width '<'; use '＜'")
    def replace(match):
        name = match.group(1)
        if not name.startswith("G:") and name not in CODES:
            raise DialogueTextError(f"unknown placeholder {{{name}}}")
        return "<G:%04X>" % (int(name[2:], 16) if name.startswith("G:") else CODES[name])
    return PLACEHOLDER.sub(replace, line)


def parse(text: str, path: str = "") -> tuple[list[Entry], list[Problem]]:
    """Syntax only; compile() checks an entry against its original text."""
    entries, problems = [], []
    entry = None
    if text.startswith("﻿"):
        text = text[1:]
    for number, raw in enumerate(text.split("\n"), 1):
        line = raw.rstrip("\r").rstrip(" \t")
        if not line or line.startswith("#"):
            continue
        if line.startswith("@"):
            match = HEADER.match(line)
            if not match:
                problems.append(Problem(path, number, "", "malformed entry header"))
                entry = None
                continue
            record, intro, note = match.groups()
            key = key_of(int(record)) if record else f"intro:{int(intro)}"
            entry = Entry(key, note or "", path, number)
            entries.append(entry)
            continue
        if entry is None:
            problems.append(Problem(path, number, "", "text outside an entry"))
            continue
        page = entry.pages[-1]
        if line == "---":
            entry.pages.append(Page())
        elif line.startswith(">"):
            content = line[1:]
            content = content[1:] if content.startswith(" ") else content
            if content.startswith("* ") or content == "*":
                page.source_options = True
                content = content[2:]
            page.source.append(content)
        elif line.startswith("\\"):
            page.target.append(line[1:])
            page.target_options.append(False)
        elif line.startswith("*"):
            content = line[1:]
            page.target.append(content[1:] if content.startswith(" ") else content)
            page.target_options.append(True)
        else:
            page.target.append(line)
            page.target_options.append(False)
    return entries, problems


def compile_entry(entry: Entry, source: str | None) -> str | None:
    """The catalog target for one entry, None for an untranslated template."""
    if not any(page.target for page in entry.pages):
        return None
    if entry.key.startswith("intro:"):
        return "<STOP>".join("<BR>".join(catalog_line(l) for l in page.target) for page in entry.pages) + "<END>"
    record = int(entry.key.split("_")[1])
    if record < FIRST_DIALOGUE_RECORD:
        raise DialogueTextError(f"record {record} is a name or label; edit the term tables instead")
    if source is None:
        raise DialogueTextError("no such text record")
    original = pages_of(source)
    if len(entry.pages) != len(original):
        raise DialogueTextError(f"{len(entry.pages)} pages, the original has {len(original)}")
    result = []
    for n, (page, lines) in enumerate(zip(entry.pages, original), 1):
        if page.source and page.source != [l.rstrip(" \t") for l in lines]:
            raise DialogueTextError(f"page {n}: the '>' lines are not this record's original text")
        if not page.target:
            raise DialogueTextError(f"page {n} has no translation")
        options = page.target_options
        if any(options) and not all(options):
            raise DialogueTextError(f"page {n} mixes options (*) and plain lines")
        if (page.source_options or any(options)) and (not all(options) or len(page.target) != len(lines)):
            raise DialogueTextError(f"page {n}: {len(page.target)} options, the original has {len(lines)}")
        want = Counter(c for l in lines for c in placeholders(l, f"page {n} original"))
        have = Counter(c for l in page.target for c in placeholders(l, f"page {n}"))
        if want != have:
            missing = sorted((want - have).elements())
            extra = sorted((have - want).elements())
            names = lambda codes: ", ".join("{" + NAMES.get(c, "G:%04X" % c) + "}" for c in codes)
            raise DialogueTextError(f"page {n}: placeholders differ from the original"
                                    + (f"; missing {names(missing)}" if missing else "")
                                    + (f"; extra {names(extra)}" if extra else ""))
        result.append("<BR>".join(catalog_line(l) for l in page.target))
    return "<STOP>".join(result) + "<END>"


def load(roots: list[Path], sources: dict[str, str]) -> tuple[dict[str, str], dict[str, str], list[Problem]]:
    """(targets, intro, problems) for all *.txt under the roots; later roots override earlier ones."""
    targets, intro, problems = {}, {}, []
    for root in roots:
        seen: dict[str, tuple[str, int]] = {}
        if not root.is_dir():
            continue
        for path in sorted(root.rglob("*.txt")):
            name = str(path.relative_to(root))
            try:
                text = path.read_text(encoding="utf-8")
            except (OSError, UnicodeDecodeError) as error:
                problems.append(Problem(name, 0, "", f"unreadable: {error}"))
                continue
            entries, syntax = parse(text, name)
            problems += syntax
            for entry in entries:
                if entry.key in seen:
                    where = seen[entry.key]
                    problems.append(Problem(name, entry.line, entry.key, f"also at {where[0]}:{where[1]}; this one is ignored"))
                    continue
                seen[entry.key] = (name, entry.line)
                try:
                    target = compile_entry(entry, sources.get(entry.key))
                except DialogueTextError as error:
                    problems.append(Problem(name, entry.line, entry.key, str(error)))
                    continue
                if target is not None:
                    (intro if entry.key.startswith("intro:") else targets)[entry.key] = target
    return targets, intro, problems


def escaped(line: str) -> str:
    return "\\" + line if line.startswith(SPECIAL) or line == "---" else line


def format_entry(key: str, source: str, target: str | None = None, note: str = "", options: bool = False) -> str:
    """One entry as text: each page's original lines, then its translation."""
    record = key.split("_")[1].lstrip("0") or "0"
    lines = [f"@{record}" + (f" {note}" if note else "")]
    original = pages_of(source)
    translated = pages_of(target) if target else None
    if translated and len(translated) != len(original):
        raise DialogueTextError(f"{key}: {len(translated)} translated pages, the original has {len(original)}")
    for n, page in enumerate(original):
        if n:
            lines.append("---")
        lines += [("> * " if options else "> ") + line for line in page]
        if translated:
            lines += [("* " + line) if options else escaped(line) for line in translated[n]]
    return "\n".join(lines) + "\n"
