#!/usr/bin/env python3
"""Compile a story MOD (docs/design/mod-packages.md §7) into what the host loads.

An author writes story/<name>.json (schema srw64.story.v1): events, each with the
stage it belongs to, when it fires, what must hold, and what it does, in words a
writer reads ("say", "join", "leave", "if") rather than opcodes. This turns it into
srw64.story-image.v1: the trigger and conditions the host checks itself, the event's
instructions for the original script engine (assembled by the layout-locked
assembler), and its new lines, which the host serves as text records 0xC800 up.

    python tools/mods/story.py compile story/master.json -o build/story.json
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from recomp.script_lab.script_debug import SCHEMA as SCRIPT_SCHEMA, assemble, layout  # noqa: E402

SCHEMA = "srw64.story.v1"
IMAGE_SCHEMA = "srw64.story-image.v1"
TEXT = 0xFFFE                  # a line's place in the words until the host numbers it
NUMBER = 0xFFFD                # a new unit's or weapon's place, numbered by the host (mod_ids.hpp)
NAME = re.compile(r"([a-z0-9][a-z0-9._-]*)?@[a-z0-9._-]{1,40}")
SIDES = {"player": 1, "enemy": 2, "third": 3, "any": 4}
LONGEST = 200                  # code points a line may have
LOCALES = {"ja", "zh-Hans", "zh-Hant", "en", "ko"}


class StoryError(ValueError):
    pass


def _int(value, name: str, low: int, high: int) -> int:
    if type(value) is not int or not low <= value <= high:
        raise StoryError(f"{name} 要是 {low}–{high} 的整数")
    return value


def _side(value, name: str, default: str = "any") -> int:
    value = default if value is None else value
    if value not in SIDES:
        raise StoryError(f"{name} 只能是 {', '.join(SIDES)}")
    return SIDES[value]


FLAG = re.compile(r"[a-z0-9_-]{1,32}")


def _named(value) -> bool:
    """A package's new unit or weapon by name: "@final" (its own) or "package@final"."""
    return isinstance(value, str) and NAME.fullmatch(value) is not None


def _unit(value, name: str):
    """A unit: the ROM's number, or a new one by name (numbered by the host)."""
    return value if _named(value) else _int(value, name, 0, 362)


def _pilot(value, name: str, top: int = 360):
    """A character: the ROM's number, or a new one by name (numbered by the host)."""
    return value if _named(value) else _int(value, name, 0, top)


def _weapon(value, name: str):
    return value if _named(value) else _int(value, name, 0, 1328)


def roster(value, name: str) -> dict:
    """{"pilots": [...], "units": [...]}: who must be on (or off) our roster."""
    if not isinstance(value, dict) or not value or set(value) - {"pilots", "units"}:
        raise StoryError(f"{name} 写 {{\"pilots\": [人物号], \"units\": [机体号或 @新机体]}}")
    return {kind: [_int(n, f"{name}.{kind}", 0, 360) if kind == "pilots" else _unit(n, f"{name}.{kind}") for n in value[kind]]
            for kind in ("pilots", "units") if kind in value}


# Lines a story file names ("@key") instead of writing them in place: the package's
# story/<locale>/*.txt, in the shipped dialogue files' style (docs/guide/dialogue-text.md):
# `@key note` opens one, the next line is its text, `#` a comment, a leading `\` escapes.
KEY = re.compile(r"[a-z0-9][a-z0-9_.-]*")
_LINES: dict[str, dict[str, str]] = {}


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


def _text(value, name: str):
    """A text written in place ({locale: text}) or named ("@key", from the lines files)."""
    if isinstance(value, str) and value.startswith("@"):
        found = _LINES.get(value[1:])
        if not found or "ja" not in found:
            raise StoryError(f"{name}：story/ja/*.txt 里没有 {value}")
        return dict(found)
    return value


def _by_locale(value, name: str) -> dict:
    """A text by locale for the stage information page, Japanese required."""
    value = _text(value, name)
    if not isinstance(value, dict) or "ja" not in value:
        raise StoryError(f"{name} 写成 {{\"ja\": …, \"zh-Hans\": …, \"en\": …}}，至少有 ja")
    for locale, words in value.items():
        if locale not in LOCALES:
            raise StoryError(f"{name}：不认识的语言 {locale}")
        if not isinstance(words, str) or not words.strip() or len(words) > LONGEST:
            raise StoryError(f"{name}.{locale}：为空或超过 {LONGEST} 字")
    return dict(value)


# What a hidden element's grant in a later playthrough may do (src/host/legacy.cpp runs it
# in the intermission, as the original's grants): joins and machines, no variables.
INHERIT_ACTIONS = ("join", "replace", "unlock", "crew", "build")


def inherit(value, where: str) -> dict:
    """A stage information entry's grant in a later playthrough (docs/design/new-game-plus.md
    §8): in the intermission after a stage of `after` (or before one of `before`), when the
    record has the element and our roster lacks its `done`, these actions run."""
    if not isinstance(value, dict) or set(value) - {"after", "before", "do"} or "do" not in value:
        raise StoryError(f"{where} 写 {{\"after\": [关], \"before\": [关], \"do\": [动作]}}")
    out: dict = {}
    for key in ("after", "before"):
        if key in value:
            if not isinstance(value[key], list) or not value[key]:
                raise StoryError(f"{where}.{key} 是场景号的数组")
            out[key] = [_int(s, f"{where}.{key}", 0, 255) for s in value[key]]
    if not out:
        raise StoryError(f"{where}：after 或 before 至少写一个")
    rows = value["do"]
    if not isinstance(rows, list) or not rows:
        raise StoryError(f"{where}.do 至少一个动作")
    builder = Builder([])
    for row in rows:
        if not isinstance(row, dict) or len(row) != 1 or next(iter(row)) not in INHERIT_ACTIONS:
            raise StoryError(f"{where}.do 只能用 {'、'.join(INHERIT_ACTIONS)}（不改剧情变量）")
        builder.action(row)
    # The grants' form (status.json "grants"): ["3D5A", pilot, level, unit, mode]; a new
    # number stays its name and a level "like" a pilot {"like": n}, both settled by the host.
    commands = []
    for k, command in enumerate(builder.commands):
        args = list(command["args"])
        for at, operand, name, _kind in builder.named:
            if at == k:
                args[operand] = name
        for at, pilot in builder.like:
            if at == k:
                args[1] = {"like": pilot}
        if command["op"] == 0x3D5A and isinstance(args[1], int) and args[1] >= 0x8000:
            args[1] -= 0x10000          # the level offset, signed
        commands.append([f"{command['op']:04X}", *args])
    out["commands"] = commands
    if builder.crew:
        out["crew"] = builder.crew
    if builder.builds:
        out["builds"] = builder.builds
    return out


def stage_info(rows) -> list[dict]:
    """Hidden elements the package adds to scenes' stage information pages (src/host/
    stage_info.cpp): scene, title, text, and optionally `done`, a roster that marks it done;
    with an `id` the record across playthroughs keeps it, and `inherit` grants it again."""
    if not isinstance(rows, list):
        raise StoryError("stage_info 要是数组")
    out, ids = [], set()
    for n, row in enumerate(rows):
        where = f"stage_info[{n}]"
        if not isinstance(row, dict) or set(row) - {"id", "scene", "title", "text", "done", "inherit"}:
            raise StoryError(f"{where} 只能写 id、scene、title、text、done、inherit")
        entry = {"scene": _int(row.get("scene"), f"{where}.scene", 0, 255),
                 "title": _by_locale(row.get("title"), f"{where}.title"), "text": _by_locale(row.get("text"), f"{where}.text")}
        if "id" in row:
            if not isinstance(row["id"], str) or not KEY.fullmatch(row["id"]) or len(row["id"]) > 40:
                raise StoryError(f"{where}.id 只用小写字母、数字、- _ .，最多 40 个")
            if row["id"] in ids:
                raise StoryError(f"{where}.id {row['id']} 重复")
            ids.add(row["id"])
            entry["id"] = row["id"]
        if "done" in row:
            entry["done"] = roster(row["done"], f"{where}.done")
        if "inherit" in row:
            if "id" not in entry or "done" not in entry:
                raise StoryError(f"{where}：写 inherit 要同时写 id 和 done（done 成立就不再补）")
            entry["inherit"] = inherit(row["inherit"], f"{where}.inherit")
        out.append(entry)
    return out


def flags(value, name: str) -> list[str]:
    if not isinstance(value, list) or not all(isinstance(f, str) and FLAG.fullmatch(f) for f in value):
        raise StoryError(f"{name} 是标记名的数组：小写字母、数字、_、-，最多 32 个")
    return list(value)


def trigger(when: dict) -> tuple[str, list[int]]:
    """The host's trigger name and its parameters (story_mod.hpp, Event::a/b/c)."""
    if not isinstance(when, dict) or len(when) != 1:
        raise StoryError("when 要写且只写一种时机")
    (kind, value), = when.items()
    if kind == "turn":
        if not isinstance(value, dict) or set(value) - {"turn", "side"}:
            raise StoryError("turn 写 {\"turn\": 画面回合, \"side\": player|enemy|third}")
        return "turn", [_int(value.get("turn"), "turn", 1, 99) - 1, _side(value.get("side"), "side", "player")]
    if kind == "defeated":
        return "defeated", [_int(value, "defeated", 0, 360)]
    if kind == "hp_below":
        return "hp_below", [_int(value.get("who"), "who", 0, 360), _int(value.get("percent"), "percent", 1, 100)]
    if kind == "battle":
        between = value.get("between", [])
        if not isinstance(between, list) or len(between) != 2:
            raise StoryError("battle.between 写两个人物号（0 = 任意）")
        moment = value.get("moment", "after")
        if moment not in ("after", "before"):
            raise StoryError("battle.moment 只能是 after 或 before")
        return f"battle_{moment}", [_int(between[0], "between", 0, 360), _int(between[1], "between", 0, 360)]
    if kind == "enemies_gone":
        value = {} if value is True else value
        by = value.get("by_turn")
        return "enemies_gone", [_side(value.get("side"), "side"), 254 if by is None else _int(by, "by_turn", 1, 99) - 1]
    if kind == "remaining":
        side = value.get("side", "enemy")
        if side not in ("enemy", "third"):
            raise StoryError("remaining.side 只能是 enemy 或 third")
        return "remaining", [1 if side == "third" else 0, _int(value.get("at_most"), "at_most", 0, 99), _side(value.get("phase"), "phase")]
    if kind == "persuade":
        return "persuade", [_int(value.get("by"), "by", 0, 360), _int(value.get("target"), "target", 0, 360)]
    if kind == "after":
        if value not in ("opening", "setup", "end"):
            raise StoryError("after 只能是 opening、setup 或 end")
        return f"after_{value}", []
    raise StoryError(f"不认识的时机 {kind}")


class Builder:
    """The commands of one event and the lines they show."""

    def __init__(self, texts: list[dict]):
        self.commands: list[dict] = []
        self.texts = texts
        self.refs: list[int] = []         # command indexes whose argument is a line
        self.like: list[tuple[int, int]] = []   # (command index, pilot): a join's level, set by the host
        self.crew: list[dict] = []              # a unit's crew, set by the host once the event has run
        self.builds: list = []                  # combined units the host builds when the event starts
        self.named: list[tuple[int, int, str, str]] = []   # (command, operand, name, "unit"|"weapon"|"pilot")

    def op(self, opcode: int, *args) -> None:
        """An instruction; an operand that names a new unit or weapon is numbered by the host."""
        values = []
        for k, a in enumerate(args):
            if isinstance(a, tuple):
                kind, value = a
                if _named(value):
                    self.named.append((len(self.commands), k, value, kind))
                    a = NUMBER
                else:
                    a = value
            values.append(a)
        self.commands.append({"op": opcode, "args": values})

    def line(self, speaker, text) -> int:
        speaker = _pilot(speaker, "say")   # a new character by name: numbered by the host
        text = _text(text, "text")
        if not isinstance(text, dict) or not text:
            raise StoryError("text 写成 {\"ja\": …, \"zh-Hans\": …, \"en\": …}")
        if "ja" not in text:
            raise StoryError("每句台词都要有 ja（原版界面和缺译时用它）")
        for locale, words in text.items():
            if locale not in LOCALES:
                raise StoryError(f"不认识的语言 {locale}")
            if not isinstance(words, str) or not words.strip():
                raise StoryError(f"{locale} 的台词是空的")
            if len(words) > LONGEST:
                raise StoryError(f"{locale} 的台词超过 {LONGEST} 字，拆成几句")
            if "<STOP>" in words or "<END>" in words:
                raise StoryError("台词里不写 <STOP>／<END>：一句一个 say，阅读器自己分页")
        self.texts.append({"speaker": speaker, "lines": dict(text)})
        return len(self.texts) - 1

    def actions(self, rows: list) -> None:
        if not isinstance(rows, list):
            raise StoryError("do／then／else 要是数组")
        for row in rows:
            self.action(row)

    def action(self, row: dict) -> None:
        if not isinstance(row, dict):
            raise StoryError("每个动作是一个对象")
        if "op" in row:                                        # an original command as is
            self.commands.append({"op": row["op"], "args": row.get("args", [])})
            return
        if "say" in row:
            mode = _int(row.get("mode", 0), "mode", 0, 5)
            index = self.line(row["say"], row.get("text"))
            self.refs.append(len(self.commands))
            self.op(0x3D3E + mode, index)
            return
        if "if" in row:
            cond = row["if"]
            if not isinstance(cond, dict) or set(cond) != {"var", "is"}:
                raise StoryError("if 写 {\"var\": 变量号, \"is\": 0–3}")
            # As the original writes it (event 001d38c8): ACC 0, the block of 3E03; with an
            # else, the block ends with ACC 1 and the else block runs while ACC is still 0.
            otherwise = row.get("else") or []
            self.op(0x3E14, 0)
            self.op(0x3E03, _int(cond["var"], "var", 0, 199), _int(cond["is"], "is", 0, 3), 1 if otherwise else 0)
            self.actions(row.get("then", []))
            if otherwise:
                self.op(0x3E14, 1)
            self.op(0x3E1D)
            if otherwise:
                self.op(0x3E08, 0)
                self.actions(otherwise)
                self.op(0x3E1D)
            return
        if len(row) != 1:
            raise StoryError(f"一个动作只写一件事：{sorted(row)}")
        (kind, value), = row.items()
        if kind == "set":
            if not isinstance(value, dict) or not value:
                raise StoryError("set 写 {\"变量号\": 值}")
            for var, x in value.items():
                self.op(0x3E13, _int(int(var), "set 变量", 0, 199), _int(x, "set 值", 0, 3))
        elif kind == "join":
            # The level is the stage's base level (8010F5F3) plus the second operand (800AAD28):
            # {"offset": n} sets n, {"like": pilot} has the host make it that pilot's level.
            level = value.get("level", {"offset": 0})
            if not isinstance(level, dict) or len(level) != 1 or not set(level) <= {"offset", "like"}:
                raise StoryError("join.level 写 {\"offset\": n} 或 {\"like\": 人物号}")
            offset = 0
            if "offset" in level:
                offset = _int(level["offset"], "level.offset", -98, 98)
            else:
                self.like.append((len(self.commands), _int(level["like"], "level.like", 0, 360)))
            self.op(0x3D5A, ("pilot", _pilot(value.get("pilot"), "pilot")), offset & 0xFFFF, ("unit", _unit(value.get("unit"), "unit")), 500)
        elif kind == "remove":
            self.op(0x3D5A, ("pilot", _pilot(value.get("pilot"), "pilot")), 0, ("unit", _unit(value.get("unit"), "unit")), 4000)
        elif kind == "replace":
            # The original's machine change (3D5A pilot,0,new,old, as 224 replacing 223): the old
            # unit goes, upgrades follow D_800CA3A0 / D_800CB5E8 (docs/gameplay/upgrade-inheritance.md).
            if not isinstance(value, dict) or set(value) != {"pilot", "unit", "with"}:
                raise StoryError("replace 写 {\"pilot\": 人物号, \"unit\": 旧机体, \"with\": 新机体}")
            self.op(0x3D5A, ("pilot", _pilot(value["pilot"], "pilot")), 0, ("unit", _unit(value["with"], "with")), ("unit", _unit(value["unit"], "unit")))
        elif kind == "remove_unit":
            # 3D5A pilot,0,unit,3000: the unit leaves the roster, its pilot stays (800A3540).
            if not isinstance(value, dict) or set(value) != {"pilot", "unit"}:
                raise StoryError("remove_unit 写 {\"pilot\": 人物号, \"unit\": 机体号}")
            self.op(0x3D5A, _int(value["pilot"], "pilot", 0, 359), 0, ("unit", _unit(value["unit"], "unit")), 3000)
        elif kind == "unlock":
            self.op(0x3D6F, ("weapon", _weapon(value, "unlock")))   # as 断空光牙剣 874 is unlocked
        elif kind == "crew":
            if not isinstance(value, dict) or set(value) != {"unit", "pilots"} or not isinstance(value["pilots"], list) \
                    or not 1 <= len(value["pilots"]) <= 5:
                raise StoryError("crew 写 {\"unit\": 机体号, \"pilots\": [1–5 个人物号，第一个是主驾驶]}")
            self.crew.append({"unit": _unit(value["unit"], "unit"),
                              "pilots": [_int(x, "crew 人物", 0, 360) for x in value["pilots"]]})
        elif kind == "build":
            # A package's combined unit (data/new_units.json "combine") gets our instance when
            # the event starts, its crew the parts' pilots; the host does it (mod_combine.hpp).
            self.builds.append(_unit(value, "build"))
        elif kind == "leave":
            self.op(0x3D46, _int(value, "leave", 0, 360), 1)
        elif kind == "leave_group":
            self.op(0x3D46, 500 + _int(value, "leave_group", 0, 99), 1)
        elif kind == "deploy":
            self.op(0x3D45, _int(value, "deploy", 0, 99))
        elif kind == "focus":
            self.op(0x3D55, _int(value, "focus", 0, 360), 30)
        elif kind == "bgm":
            self.op(0x3D3A, _int(value, "bgm", 0, 255))
        elif kind == "sound":
            self.op(0x3D39, _int(value, "sound", 0, 0xFFFF))
        elif kind == "fade":
            if value not in ("out", "in"):
                raise StoryError("fade 只能是 out 或 in")
            self.op(0x3D3B, 0 if value == "out" else 1)
        elif kind == "wait":
            self.op(0x3D38, _int(value, "wait", 1, 0x7FFF))
        elif kind == "close":
            self.op(0x3D47 if value != "yield" else 0x3D4E)
        else:
            raise StoryError(f"不认识的动作 {kind}")


def compile_story(document: dict, spec: dict | None = None, lines: dict | None = None) -> dict:
    global _LINES
    _LINES = lines or {}
    if not isinstance(document, dict) or document.get("schema") != SCHEMA:
        raise StoryError(f"schema 要是 {SCHEMA}")
    unknown = set(document) - {"schema", "events", "stage_info"}
    if unknown:
        raise StoryError(f"不认识的字段 {sorted(unknown)}")
    spec = spec or layout()
    texts: list[dict] = []
    events, ids = [], set()
    for row in document.get("events", []):
        where = row.get("id", "?") if isinstance(row, dict) else "?"
        try:
            unknown = set(row) - {"id", "scene", "when", "if", "repeat", "set_flags", "do", "note"}
            if unknown:
                raise StoryError(f"不认识的字段 {sorted(unknown)}")
            if not isinstance(row.get("id"), str) or not row["id"]:
                raise StoryError("每个事件要有 id")
            if row["id"] in ids:
                raise StoryError("id 重复")
            ids.add(row["id"])
            name, params = trigger(row.get("when"))
            cond = row.get("if", {})
            known = {"vars", "fired", "not_fired", "kills", "kills_below", "roster", "not_roster", "flags", "not_flags"}
            if not isinstance(cond, dict) or set(cond) - known:
                raise StoryError("if 只能写 " + "、".join(sorted(known)))
            variables = {str(_int(int(k), "vars 变量", 0, 199)): _int(v, "vars 值", 0, 3) for k, v in cond.get("vars", {}).items()}
            kills = {str(_int(int(k), "kills 人物", 0, 360)): _int(v, "kills 击坠数", 1, 9999) for k, v in cond.get("kills", {}).items()}
            kills_below = {str(_int(int(k), "kills_below 人物", 0, 360)): _int(v, "kills_below 击坠数", 1, 9999)
                           for k, v in cond.get("kills_below", {}).items()}
            repeat = row.get("repeat", False)
            if type(repeat) is not bool or (repeat and name.startswith("after_")):
                raise StoryError("repeat 是 true/false，开场／配置／结束之后的事件不能重复")
            builder = Builder(texts)
            builder.actions(row.get("do", []))
            assembled = assemble({"schema": SCRIPT_SCHEMA, "event_type": 12, "header": [0, 0, 0, 0],
                                  "commands": builder.commands}, spec)
            words = assembled["words"][5:]
            offsets = [entry["offset"] // 2 - 5 for entry in assembled["listing"]]
            levels = [[offsets[command] + 2, pilot] for command, pilot in builder.like]
            numbers = []
            for command, operand, value, kind in builder.named:
                at = offsets[command] + 1 + operand
                numbers.append([at, value, kind])
            refs = []
            for command in builder.refs:
                at = offsets[command] + 1
                refs.append([at, words[at]])
                words[at] = TEXT
            events.append({"id": row["id"], "scene": _int(row.get("scene"), "scene", 0, 255), "trigger": name,
                           "params": params, "vars": variables, "kills": kills, "kills_below": kills_below,
                           **{k: roster(cond[k], k) for k in ("roster", "not_roster") if k in cond},
                           **{k: flags(cond[k], k) for k in ("flags", "not_flags") if k in cond},
                           "set_flags": flags(row.get("set_flags", []), "set_flags"), "fired": list(cond.get("fired", [])),
                           "not_fired": list(cond.get("not_fired", [])), "repeat": repeat,
                           "words": words, "texts": refs, "levels": levels, "crew": builder.crew, **({"builds": builder.builds} if builder.builds else {}),
                           **({"numbers": numbers} if numbers else {}),
                           "listing": [f"{e['opcode']} {','.join(map(str, e['operands']))}".rstrip() for e in assembled["listing"]]})
        except (StoryError, ValueError, KeyError, TypeError) as error:
            raise StoryError(f"事件 {where}：{error}") from None
    for event in events:
        for other in event["fired"] + event["not_fired"]:
            if other not in ids:
                raise StoryError(f"事件 {event['id']}：fired／not_fired 引用了不存在的事件 {other}")
    info = stage_info(document["stage_info"]) if "stage_info" in document else []
    return {"schema": IMAGE_SCHEMA, "events": events, "texts": texts, **({"stage_info": info} if info else {})}


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
