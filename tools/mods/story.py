#!/usr/bin/env python3
"""Compile a story MOD (docs/design/mod-packages.md §7) into what the host loads.

An author writes story/<name>.json (schema srw64.story.v1): events, each with the
stage it belongs to, when it fires, what must hold, and what it does, in words a
writer reads ("say", "join", "leave", "if") rather than opcodes. This turns it into
srw64.story-image.v1: the trigger and conditions the host checks itself, the event's
instructions for the original script engine, and its new lines, which the host serves as
text records 0xC800 up.

The compiler is the level editor's TypeScript (web/src/lib/compiler/story.ts), run with
node (web_compiler.py); this file only hands it the source.

    python tools/mods/story.py compile story/master.json -o build/story.json
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import web_compiler  # noqa: E402

SCHEMA = "srw64.story.v1"
IMAGE_SCHEMA = "srw64.story-image.v1"
TEXT = 0xFFFE                  # a line's place in the words until the host numbers it
NUMBER = 0xFFFD                # a new unit's or weapon's place, numbered by the host (mod_ids.hpp)
CHECK = 0xFFFC                 # a step's condition the host decides, 0 or 1
FLAG = re.compile(r"[a-z0-9_-]{1,32}")
LOCALES = ("ja", "zh-Hans", "zh-Hant", "en", "ko")   # story.ts LOCALES


class StoryError(ValueError):
    pass


# Lines a story file names ("@key") instead of writing them in place: the package's
# story/<locale>/*.txt, in the shipped dialogue files' style (docs/guide/dialogue-text.md):
# `@key note` opens one, the next line is its text, `#` a comment, a leading `\` escapes.
KEY = re.compile(r"[a-z0-9][a-z0-9_.-]*")


def load_lines(folder: Path) -> dict[str, dict[str, str]]:
    """Every story/<locale>/*.txt under `folder` (the package's story/): key -> {locale: text}."""
    out: dict[str, dict[str, str]] = {}
    if not folder.is_dir():
        return out
    for directory in sorted(d for d in folder.iterdir() if d.is_dir()):
        if directory.name not in LOCALES:
            raise StoryError(f"story/{directory.name}/：不认识的语言（{'、'.join(sorted(LOCALES))}）")
        for path in sorted(directory.glob("*.txt")):
            where = f"story/{directory.name}/{path.name}"
            key, texts = None, []

            def close():
                if key is None:
                    return
                if len(texts) != 1 or not texts[0].strip():
                    raise StoryError(f"{where}：@{key} 要有且只有一行台词（阅读器自己分页、折行）")
                out.setdefault(key, {})[directory.name] = texts[0]
            for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                line = raw.rstrip("\r")
                if not line.strip() or line.startswith("#"):
                    continue
                if line.startswith("@"):
                    close()
                    key = line[1:].split(maxsplit=1)[0] if line[1:].strip() else ""
                    if not KEY.fullmatch(key):
                        raise StoryError(f"{where}:{number}：@ 后面写这一条的名字（小写字母、数字、- _ .）")
                    if directory.name in out.get(key, {}):
                        raise StoryError(f"{where}:{number}：@{key} 在 {directory.name} 里写了两次")
                    texts = []
                    continue
                if key is None:
                    raise StoryError(f"{where}:{number}：台词前要先有一行 @名字")
                texts.append(line[1:] if line.startswith("\\") else line)
            close()
    return out


def compile_story(document: dict, lines: dict | None = None) -> dict:
    """`lines`: the package's lines files (load_lines), for texts written "@key"."""
    with tempfile.TemporaryDirectory() as temp:
        path = Path(temp) / "story.json"
        path.write_text(json.dumps(document, ensure_ascii=False), encoding="utf-8")
        named = Path(temp) / "lines.json"
        named.write_text(json.dumps(lines or {}, ensure_ascii=False), encoding="utf-8")
        try:
            return web_compiler.run("compile-story.ts", path, named)
        except web_compiler.CompileError as error:
            raise StoryError(str(error)) from None


def compile_file(source: Path, out: Path) -> dict:
    image = compile_story(json.loads(source.read_text(encoding="utf-8")), lines=load_lines(source.parent))
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(image, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    return image


def is_source(path: Path) -> bool:
    try:
        return json.loads(path.read_text(encoding="utf-8")).get("schema") == SCHEMA
    except (OSError, ValueError, AttributeError):
        return False


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    c = sub.add_parser("compile")
    c.add_argument("source", type=Path)
    c.add_argument("-o", "--out", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        image = compile_file(args.source, args.out)
    except StoryError as error:
        print(f"{args.source}: {error}", file=sys.stderr)
        return 1
    print(f"{args.out}: {len(image['events'])} 个事件, {len(image['texts'])} 句台词")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
