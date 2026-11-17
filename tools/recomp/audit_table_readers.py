#!/usr/bin/env python3
"""List every place the recompiled game reads the unit and weapon tables, and every
compare against their sizes (docs/design/new-unit-ids.md §2).

The generated C carries each MIPS instruction as a comment. A table's address is built
with lui + addiu/ori (ROM addresses passed to the DMA helpers 8007F704 / 800A3DD0) or with
lui + a load's offset (resident and overlay RAM). Per function, in instruction order, the
scan follows the value lui puts in a register through addiu/ori (exact) and addu/subu
(base plus an index) and reports where it lands inside a table: as a materialized
constant, or as the address of a load or store. Branches are not followed, so a register
keeps the last value written before it in the listing; a hit is a lead, read it.

Usage: audit_table_readers.py [--generated build/recomp/cpu-bound/generated] [--json out]
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# (key, description, kind, start, size). kind: rom = ROM address handed to a DMA helper;
# ram = an address in resident RAM (any section); ovl:<section> = RAM of one overlay.
TABLES = [
    ("units",           "机体记录 363×0x24",               "rom", 0x71B80, 363 * 0x24),
    ("weapons",         "武器记录 1329×0x10",              "rom", 0x74E90, 1329 * 0x10),
    ("status_offsets",  "状态画面偏移 363×4",              "rom", 0x7D980, 363 * 4),
    ("unit_songs",      "机体战斗曲 363×2（music.hpp 包 801E07C4）", "rom", 0x7DF30, 363 * 2),
    ("weapon_lists",    "武器列表指针与行",                "rom", 0x7E210, 0x83110 - 0x7E210),
    ("poses",           "战斗站姿 365×6",                  "rom", 0x84E40, 365 * 6),
    ("map_icons_rom",   "地图图标（ROM）363×2",            "rom", 0x100D78, 363 * 2),
    ("battle_units",    "机体战斗记录 354×14",             "rom", 0x118610, 354 * 14),
    ("battle_weapons",  "武器战斗记录 1329×14",            "rom", 0x119970, 1329 * 14),
    ("scenes",          "战斗场景总表 1053×6",             "rom", 0x11E3D0, 1053 * 6),
    ("anim_offsets",    "武器动画偏移 1329×4",             "rom", 0x11FC80, 1329 * 4),
    ("anim_bank",       "武器动画库",                      "rom", 0x184990, 0x19AA40 - 0x184990),
    ("resources",       "资源描述符表",                    "rom", 0xA20BD0, 4 + 6436 * 8),
    ("map_icons_ram",   "地图图标（战术地图 overlay RAM）", "ovl:load_000AB160", 0x80218218, 363 * 2),
    ("predecessors",    "前任表 D_800CA3A0",               "ram", 0x800CA3A0, 30 * 4),
    ("crew",            "乘员表 D_800CA418",               "ram", 0x800CA418, 0x800CA4AC - 0x800CA418),
    # Form families: 60 unit numbers and -1, then 8-byte records; D_800CAEF0 points at them.
    ("forms",           "变形族 D_800CAC94",               "ram", 0x800CAC94, 0x800CAEF0 - 0x800CAC94),
    ("forms_pointers",  "变形族指针 D_800CAEF0 60×4",      "ram", 0x800CAEF0, 60 * 4),
    # Combinations: 11 combined units and -1, then records; D_800CB5A4 points at them.
    ("combine",         "合体表 D_800CB40C",               "ram", 0x800CB40C, 0x800CB5A4 - 0x800CB40C),
    ("combine_pointers", "合体指针 D_800CB5A4",            "ram", 0x800CB5A4, 0x800CB5E8 - 0x800CB5A4),
    ("weapon_map",      "武器继承映射 D_800CB5E8",         "ram", 0x800CB5E8, 115 * 4),
]

# Immediates worth a look in compares and loop bounds: the tables' sizes and the ID caps.
LIMITS = {
    0x16B: "363 机体数", 0x16C: "364", 0x16D: "365 站姿槽", 0x162: "354 机体战斗记录行",
    0x531: "1329 武器数", 0x530: "1328", 0x41D: "1053 场景槽", 0x41B: "1051 场景（编辑器上限）",
    0x1924: "6436 资源数",
}
COMPARE_OPS = {"slti", "sltiu", "addiu"}

FUNC = re.compile(r"^RECOMP_FUNC void (\w+)\(")
INSN = re.compile(r"^\s*// 0x([0-9A-F]{8}): (\w+(?:\.\w+)*)\s*(.*)$")
MEM = re.compile(r"^(-?0x[0-9A-Fa-f]+|-?\d+)\((\$\w+)\)$")
LOADS = {"lb", "lbu", "lh", "lhu", "lw", "lwu", "ld", "lwl", "lwr", "ldl", "ldr", "ll", "lwc1", "ldc1"}
STORES = {"sb", "sh", "sw", "sd", "swl", "swr", "sc", "swc1", "sdc1"}
NO_WRITE = STORES | {"j", "jr", "jal", "jalr", "b", "beq", "bne", "beql", "bnel", "blez", "bgtz", "bltz", "bgez",
                     "blezl", "bgtzl", "bltzl", "bgezl", "bal", "mult", "multu", "div", "divu", "dmult",
                     "dmultu", "ddiv", "ddivu", "mtc1", "dmtc1", "ctc1", "mthi", "mtlo", "nop", "syscall",
                     "break", "sync", "cache", "bc1t", "bc1f", "bc1tl", "bc1fl", "c.eq.s", "c.lt.s", "c.le.s",
                     "c.eq.d", "c.lt.d", "c.le.d", "teq", "tne"}


def imm(text: str) -> int:
    return int(text, 16) if "0x" in text.lower() else int(text)


def section_of(func: str) -> str:
    return func.split("_func_")[0] if "_func_" in func else func


def table_at(address: int, section: str):
    for key, _, kind, start, size in TABLES:
        if not start <= address < start + size:
            continue
        if kind.startswith("ovl:") and kind[4:] != section and not section.startswith("srw64_original"):
            continue
        return key
    return None


def scan(generated: Path):
    hits: list[dict] = []
    limits: list[dict] = []
    for path in sorted(generated.glob("funcs_*.c")):
        func = None
        seen: set[int] = set()
        regs: dict[str, tuple[int, bool]] = {}
        for line in path.read_text(errors="replace").splitlines():
            m = FUNC.match(line)
            if m:
                func, seen, regs = m.group(1), set(), {}
                continue
            m = INSN.match(line)
            if not m or func is None:
                continue
            address = int(m.group(1), 16)
            if address in seen:          # a delay slot is listed twice
                continue
            seen.add(address)
            op, args = m.group(2), [a.strip() for a in m.group(3).split(",")] if m.group(3) else []
            section = section_of(func)

            def report(value: int, how: str):
                key = table_at(value & 0xFFFFFFFF, section)
                if key:
                    hits.append({"table": key, "function": func, "address": f"{address:08X}",
                                 "value": f"{value & 0xFFFFFFFF:08X}", "how": how, "insn": f"{op} {', '.join(args)}"})

            if op in COMPARE_OPS and len(args) == 3 and re.fullmatch(r"-?(0x[0-9A-Fa-f]+|\d+)", args[2]):
                value = imm(args[2])
                if value in LIMITS and (op != "addiu" or args[1] == "$zero"):
                    limits.append({"limit": LIMITS[value], "function": func, "address": f"{address:08X}",
                                   "insn": f"{op} {', '.join(args)}"})

            if op == "lui" and len(args) == 2:
                regs[args[0]] = ((imm(args[1]) << 16) & 0xFFFFFFFF, True)
                continue
            if op in ("addiu", "daddiu", "ori") and len(args) == 3:
                source = regs.get(args[1])
                if source is not None:
                    value = source[0] + imm(args[2]) if op != "ori" else source[0] | imm(args[2])
                    regs[args[0]] = (value & 0xFFFFFFFF, source[1])
                    if source[1]:
                        report(value, "常量")
                else:
                    regs.pop(args[0], None)
                continue
            if op in ("addu", "daddu", "subu") and len(args) == 3:
                known = [regs[a] for a in args[1:] if a in regs]
                if len(known) == 1:
                    regs[args[0]] = (known[0][0], False)
                else:
                    regs.pop(args[0], None)
                continue
            if (op in LOADS or op in STORES) and len(args) == 2:
                mm = MEM.match(args[1])
                if mm and mm.group(2) in regs:
                    report(regs[mm.group(2)][0] + imm(mm.group(1)), "读" if op in LOADS else "写")
                if op in LOADS:
                    regs.pop(args[0], None)
                continue
            if op not in NO_WRITE and args:
                regs.pop(args[0], None)
            if op in ("jal", "jalr"):
                for r in ("$v0", "$v1", "$a0", "$a1", "$a2", "$a3", "$t0", "$t1", "$t2", "$t3", "$t4",
                          "$t5", "$t6", "$t7", "$t8", "$t9", "$at", "$ra"):
                    regs.pop(r, None)
    return hits, limits


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--generated", type=Path, default=ROOT / "build/recomp/cpu-bound/generated")
    parser.add_argument("--json", type=Path)
    options = parser.parse_args()
    if not options.generated.is_dir():
        print(f"没有生成代码：{options.generated}（先跑 generate_cpu.py）", file=sys.stderr)
        return 1
    hits, limits = scan(options.generated)
    by_table: dict[str, list[dict]] = defaultdict(list)
    for h in hits:
        by_table[h["table"]].append(h)
    for key, description, kind, start, size in TABLES:
        rows = by_table.get(key, [])
        functions = sorted({r["function"] for r in rows})
        print(f"\n## {key}  {description}  {kind} {start:#x}+{size:#x}  —  {len(rows)} 处，{len(functions)} 个函数")
        for r in rows:
            print(f"  {r['function']:<48} {r['address']}  {r['how']}  {r['value']}  {r['insn']}")
    print(f"\n## 上限与循环界  —  {len(limits)} 处")
    for r in limits:
        print(f"  {r['function']:<48} {r['address']}  {r['limit']:<18} {r['insn']}")
    if options.json:
        options.json.write_text(json.dumps({"hits": hits, "limits": limits}, ensure_ascii=False, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
