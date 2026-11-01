#!/usr/bin/env python3
"""Read-only BUG06 audit of the pinned original ROM; never changes the game.

PYTHONPATH=src python tools/recomp/gameplay/check_daitarn_oz.py --machine-code
The optional machine-code checks require Unicorn and execute unmodified MIPS
handlers in synthetic RAM, not a whole-game replay.
--repair-checks also compiles the production C++ repair policy and executes its
registration/party callbacks with the original ROM routines (g++ and Unicorn).
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "src"))
from srw64_native.catalog import source_catalog, text_headers
from srw64_native.original_data import check_layout, extract_gameplay
from srw64_native.original_scripts import attach_script_catalog


def load(rom_path: Path):
    rom = rom_path.read_bytes()
    layout = json.loads((ROOT / "config/data/original-jp-v1.json").read_text())
    check_layout(rom, layout)
    sources, _, _ = source_catalog(ROOT, rom_path)
    data = extract_gameplay(rom, layout, sources)
    attach_script_catalog(data, rom, layout, sources, text_headers(ROOT, rom_path))
    return rom, data


def reach(start, graph):
    seen, todo = {start}, [start]
    while todo:
        for nxt in graph.get(todo.pop(), ()):
            if nxt not in seen:
                seen.add(nxt)
                todo.append(nxt)
    return seen


def audit(rom, data):
    events = {r["key"]: r["script"] for r in data["stage_events"]}
    ending = events["base:stage_events:001c586c"]
    cp = events["base:stage_events:001b8640"]
    command = next(i for i in ending["instructions"] if i["offset"] == 696)
    assert (command["opcode"], command["operands"], command["section"]) == (0x3D6B, [165, 216, 0], "3DD0")
    assert not any(i["opcode"] == 0x3D5A and i["operands"][0] == 165 for i in ending["instructions"])
    assert any(i["opcode"] == 0x3D5A and i["operands"] == [165, 0, 216, 500] and i["section"] == "3DD5" for i in cp["instructions"])
    assert any(i["opcode"] == 0x3D6B and i["operands"] == [165, 216, 0] and i["section"] == "3DD6" for i in cp["instructions"])

    deps = {r["key"]: r for r in data["stage_deployments"]}
    player_deployments = defaultdict(set)
    for block in data["stage_auxiliary"]:
        scenes = [int(l["relation"].split()[-1]) for l in block["links"] if l["relation"].startswith("场景索引")]
        for link in block["links"]:
            if link["key"] not in deps:
                continue
            row = deps[link["key"]]
            fields = {f.get("role", f["name"]): f for f in row["fields"]}
            value = lambda k: fields[k]["encoded_value"]
            if value("actor") == 165 and value("unit") == 216 and value("deploy_side") in (0, 3):
                for scene in scenes:
                    player_deployments[scene].add(value("group"))
    assert player_deployments.get(55, set()) == set()
    assert player_deployments[102] == {12}
    recovery = events["base:stage_events:001dd030"]
    assert any(i["opcode"] == 0x3D45 and i["operands"] == [12] for i in recovery["instructions"])

    route_results = {}
    for hero, start, sections in [("ark", 2, {"3DD0", "3DD1", "3DD5"}),
                                  ("serein", 3, {"3DD0", "3DD2", "3DD5"})]:
        graph, reverse = defaultdict(set), defaultdict(set)
        providers = set(player_deployments)
        for event in events.values():
            for i in event["instructions"]:
                if i["section"] not in sections:
                    continue
                if i["opcode"] == 0x3D4B and i["operands"][0] != 500:
                    for scene in event["scenes"]:
                        graph[scene].add(i["operands"][0])
                        reverse[i["operands"][0]].add(scene)
                if i["opcode"] == 0x3D5A and i["operands"][0] == 165 and i["operands"][2] == 216 and i["operands"][3] < 2000:
                    providers.update(event["scenes"])
        # Over-approximation: variable guards are ignored. Even counting every
        # deployment record as executed, no provider can precede S55 on this route.
        before = reach(start, graph) & reach(55, reverse)
        assert not (before & providers)
        earth = reach(103, graph)
        assert not (earth & providers)
        route_results[hero] = {"possible_scenes_before_s55": sorted(before),
                               "join_providers_before_s55": [],
                               "earth_finale_scenes": sorted(earth),
                               "join_providers_after_earth_choice": []}
    return {"rom_sha256": hashlib.sha256(rom).hexdigest(),
            "fault": {"scene": 55, "event_rom": "0x001c586c", "event_offset": 696,
                      "command_rom": hex(command["rom_offset"]), "opcode": "3D6B", "operands": [165, 216, 0]},
            "normal_cp": {"scene": 69, "event_rom": "0x001b8640", "real_register_offset": 100,
                          "super_unhide_offset": 112},
            "space_recovery": {"scene": 102, "event_rom": "0x001dd030", "deploy_group": 12},
            "routes": route_results,
            "scope": "Original-ROM static audit; graph ignores variable guards, Link returns and user MODs."}


def machine_code_checks(rom):
    from unicorn import Uc, UC_ARCH_MIPS, UC_MODE_MIPS32, UC_MODE_BIG_ENDIAN
    from unicorn.mips_const import (UC_MIPS_REG_A0, UC_MIPS_REG_SP, UC_MIPS_REG_RA, UC_MIPS_REG_PC)
    results = []
    for name, present in [("real_oz_missing_pair", False), ("super_oz_existing_hidden_pair", True)]:
        u = Uc(UC_ARCH_MIPS, UC_MODE_MIPS32 | UC_MODE_BIG_ENDIAN)
        u.mem_map(0, 0x800000)
        u.mem_write(0x76610, rom[0x1000:0x101000])
        u.mem_write(0x16A210, bytes(140 * 0x54))
        u.mem_write(0x172F40, bytes(100 * 0x4C))
        u.mem_write(0x10F5F3, b"\x01")  # valid team level; no level-up required
        if present:
            u.mem_write(0x172F40, bytes([0x81, 0, 0, 165, 0, 99]))
            u.mem_write(0x16A210, bytes([1, 0, 0, 216]))
            u.mem_write(0x16A21C, b"\xc0")
            u.mem_write(0x16A244, b"\x01")
            u.mem_write(0x16A248, struct.pack(">I", 0x80172F40))
        before = bytes(u.mem_read(0x16A210, 140 * 0x54) + u.mem_read(0x172F40, 100 * 0x4C))
        owner, operands = 0x802F0000, 0x802F1000
        u.mem_write(owner & 0x1FFFFFFF, bytes(0x100))
        u.mem_write((owner & 0x1FFFFFFF) + 0x1C, struct.pack(">I", operands))
        u.mem_write(operands & 0x1FFFFFFF, struct.pack(">3H", 165, 216, 0))
        u.reg_write(UC_MIPS_REG_A0, owner)
        u.reg_write(UC_MIPS_REG_SP, 0x807FF000)
        u.reg_write(UC_MIPS_REG_RA, 0x80001000)
        # Unmodified 3D6B handler, including 800AC6C4 and all its callees.
        # No game functions, OS services or memory accesses are stubbed.
        u.emu_start(0x800A158C, 0x80001000, count=100000)
        assert u.reg_read(UC_MIPS_REG_PC) == 0x80001000
        assert struct.unpack(">I", u.mem_read((owner & 0x1FFFFFFF) + 0x1C, 4))[0] == operands + 6
        after = bytes(u.mem_read(0x16A210, 140 * 0x54) + u.mem_read(0x172F40, 100 * 0x4C))
        pilot_flags, unit_flags = u.mem_read(0x172F40, 1)[0], u.mem_read(0x16A21C, 1)[0]
        if present:
            assert (pilot_flags, unit_flags) == (1, 0x80)
        else:
            assert after == before
        results.append({"case": name, "pilot_flags": pilot_flags, "unit_flags": unit_flags,
                        "rosters_unchanged": after == before, "handler_returned": True})
    return results


def repair_checks(rom):
    import array
    import ctypes
    import subprocess
    from unicorn import Uc, UC_ARCH_MIPS, UC_MODE_MIPS32, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE
    from unicorn.mips_const import (UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2,
                                   UC_MIPS_REG_A3, UC_MIPS_REG_SP, UC_MIPS_REG_RA,
                                   UC_MIPS_REG_PC, UC_MIPS_REG_V0)
    build = ROOT / "build/recomp/base-fixes-test"
    build.mkdir(parents=True, exist_ok=True)
    bridge = build / "daitarn-bridge.cpp"
    bridge.write_text('''#include "base_fixes.hpp"
using Callback=void(*)(void*);
extern "C" bool event(uint8_t* ram,uint32_t owner,Callback reg,Callback hide,void* data) {
 return srw64::base_fixes::repair_daitarn(ram,
  {srw64::base_fixes::daitarn_join_event(ram,owner),false},[&]{reg(data);},[&]{hide(data);});
}
extern "C" bool old_save(uint8_t* ram,Callback reg,Callback hide,void* data) {
 return srw64::base_fixes::repair_daitarn(ram,
  srw64::base_fixes::daitarn_old_save(ram),[&]{reg(data);},[&]{hide(data);});
}
''')
    library = build / "daitarn-bridge.so"
    subprocess.run(["g++", "-std=c++20", "-shared", "-fPIC", "-I", str(ROOT / "src/host"),
                    str(bridge), "-o", str(library)], check=True)
    native = ctypes.CDLL(str(library))
    callback_type = ctypes.CFUNCTYPE(None, ctypes.c_void_p)
    native.event.argtypes = [ctypes.c_void_p, ctypes.c_uint32, callback_type, callback_type, ctypes.c_void_p]
    native.old_save.argtypes = [ctypes.c_void_p, callback_type, callback_type, ctypes.c_void_p]
    native.event.restype = native.old_save.restype = ctypes.c_bool

    class Machine:
        def __init__(self, hero=0x3DD1, scene=55):
            self.u = Uc(UC_ARCH_MIPS, UC_MODE_MIPS32 | UC_MODE_BIG_ENDIAN)
            self.u.mem_map(0, 0x800000)
            self.u.mem_write(0x76610, rom[0x1000:0x101000])
            # All three pilot/weapon/instance pools are BSS at game startup.
            self.u.mem_write(0x16A210, bytes(3 * 140 * 0x54))
            self.u.mem_write(0x172F40, bytes(3 * 100 * 0x4C))
            self.u.mem_write(0x10F5F3, b"\x01")
            self.u.mem_write(0x1602EE, struct.pack(">H", hero))
            self.u.mem_write(0x10F5F0, bytes([scene]))
            self.u.mem_write(0x15E81A, struct.pack(">H", 1 << 4))
            self.u.mem_write(0x15E9C5, b"\xff")
            self.owner, self.pc = 0x802F0000, 0x8019C000
            self.u.mem_write(0x2F001C, struct.pack(">I", self.pc + 2))
            self.u.mem_write(0x19C000, rom[0x1C5B24:0x1C5B24+34])
            self.calls, self.errors = [], []
            self.u.hook_add(UC_HOOK_CODE, self.dma, begin=0x8007F704, end=0x8007F704)

        def dma(self, u, address, size, data):
            # Only the synchronous ROM-read I/O boundary is substituted. All
            # registration, stats, forms, weapons and party logic runs in MIPS.
            offset, dest, count = (u.reg_read(r) for r in (UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2))
            assert 0 <= offset <= len(rom) - count
            u.mem_write(dest & 0x1FFFFFFF, rom[offset:offset+count])
            u.reg_write(UC_MIPS_REG_V0, 0)
            u.reg_write(UC_MIPS_REG_PC, u.reg_read(UC_MIPS_REG_RA))

        def call(self, function, *args):
            self.u.reg_write(UC_MIPS_REG_SP, 0x807FF000)
            self.u.reg_write(UC_MIPS_REG_RA, 0x80001000)
            for reg, value in zip((UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2, UC_MIPS_REG_A3), args):
                self.u.reg_write(reg, value)
            self.u.emu_start(function, 0x80001000, count=1000000)
            assert self.u.reg_read(UC_MIPS_REG_PC) == 0x80001000

        @staticmethod
        def swap(data):
            words = array.array("I", data)
            words.byteswap()
            return words.tobytes()

        def repair(self, old=False):
            buffer = ctypes.create_string_buffer(self.swap(bytes(self.u.mem_read(0, 0x800000))))
            def invoke(function, *args):
                try:
                    self.u.mem_write(0, self.swap(buffer.raw[:0x800000]))
                    self.call(function, *args)
                    ctypes.memmove(buffer, self.swap(bytes(self.u.mem_read(0, 0x800000))), 0x800000)
                    self.calls.append(function)
                except Exception as error:
                    self.errors.append(error)
            reg = callback_type(lambda _: invoke(0x800AAD28, 165, 216, 0, 500))
            hide = callback_type(lambda _: invoke(0x800AC6C4, 165, 216, 1))
            if old:
                result = native.old_save(buffer, reg, hide, None)
            else:
                result = native.event(buffer, self.owner, reg, hide, None)
            if self.errors:
                raise self.errors[0]
            self.u.mem_write(0, self.swap(buffer.raw[:0x800000]))
            return result

        def records(self, base, count, stride):
            return [int.from_bytes(self.u.mem_read(base+i*stride+2, 2), "big")
                    for i in range(count) if self.u.mem_read(base+i*stride, 1)[0]]

    results = []
    for hero in (0x3DD1, 0x3DD2):
        m = Machine(hero)
        assert m.repair()
        assert m.records(0x172F40, 100, 0x4C) == [165]
        assert set(m.records(0x16A210, 140, 0x54)) == {216, 217, 218}
        m.call(0x800A158C, m.owner)  # original handler still runs after the repair
        assert int.from_bytes(m.u.mem_read(0x2F001C, 4), "big") == m.pc + 8
        assert m.u.mem_read(0x16A21C, 1)[0] & 0x80
        assert not (m.u.mem_read(0x172F40, 1)[0] & 0x80)
        results.append({"case": f"real_{hero:04x}_event", "pilots": [165], "forms": [216, 217, 218]})
    for scene in (56, 103, 104, 105):
        m = Machine(scene=scene)
        assert m.repair(old=True)
        assert bool(m.u.mem_read(0x172F40, 1)[0] & 0x80) == (scene in (103, 104))
        m.u.mem_write(0x172F45, b"\x2a")  # existing level and part must survive another poll
        m.u.mem_write(0x16A233, b"\x07")
        before = bytes(m.u.mem_read(0, 0x800000))
        assert not m.repair(old=True)
        assert bytes(m.u.mem_read(0, 0x800000)) == before
        if scene == 104:
            m.call(0x800AD990, 0, 2)  # original 3D74 reunion implementation
            assert not (m.u.mem_read(0x172F40, 1)[0] & 0x80)
            assert not (m.u.mem_read(0x16A21C, 1)[0] & 0x40)
        results.append({"case": f"old_save_next_{scene}", "repeat_preserves_all_ram": True})
    return results


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, default=ROOT / "rom.z64")
    parser.add_argument("--machine-code", action="store_true")
    parser.add_argument("--repair-checks", action="store_true")
    args = parser.parse_args()
    rom, data = load(args.rom.resolve())
    report = audit(rom, data)
    if args.machine_code:
        report["machine_code_checks"] = machine_code_checks(rom)
    if args.repair_checks:
        report["repair_checks"] = repair_checks(rom)
    print(json.dumps(report, ensure_ascii=False, indent=2))
