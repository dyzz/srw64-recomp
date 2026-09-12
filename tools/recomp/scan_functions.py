#!/usr/bin/env python3
"""Disassemble mapped loads independently and export candidate recomp symbols.

Generated symbols remain candidates until boundaries, jump tables and runtime
dispatch have been checked. ROM load identity is part of every overlay name.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess

from analyze_layout import ROOT, analyze


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, default=ROOT / "rom.z64")
    parser.add_argument("--output", type=Path, default=ROOT / "build/recomp/cpu-scan")
    args = parser.parse_args()
    rom_path = args.rom.resolve()
    rom = rom_path.read_bytes()
    layout = analyze(rom, None)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    python = ROOT / "build/recomp/venv/bin/python"
    sections = [{"name": "resident", "rom_start": 0x1000, "rom_end": 0x5BC30,
                 "text_end": 0x4DEA0, "vram": 0x80076610, "size": 0x5AC30},
                *[segment for segment in layout["segments"] if segment["size"]]]
    boundaries = json.loads((ROOT / "config/recomp/code-sections.json").read_text())
    if boundaries["schema"] != "srw64.recomp-code-sections.v1" or boundaries["rom_sha256"] != layout["rom_sha256"]:
        raise RuntimeError("code boundary metadata differs from baseline")
    by_name = {section["name"]: section for section in boundaries["sections"]}
    for section in sections:
        boundary = by_name[section["name"]]
        if any(boundary[key] != section[key] for key in ("rom_start", "rom_end", "vram")):
            raise RuntimeError("code boundaries differ from loader arguments")
        if hashlib.sha256(rom[section["rom_start"]:section["rom_end"]]).hexdigest() != boundary["sha256"]:
            raise RuntimeError("code boundary bytes differ")
        if not section["rom_start"] <= boundary["text_end"] <= section["rom_end"] or boundary["text_end"] & 3:
            raise RuntimeError("invalid CPU text boundary")
        section["text_end"] = boundary["text_end"]
    symbols: list[str] = []
    report: dict = {"schema": "srw64.recomp-cpu-scan.v1", "status": "candidate-symbols",
                    "rom_sha256": layout["rom_sha256"], "sections": [], "function_count": 0}
    for section in sections:
        name = section["name"]
        if section["text_end"] == section["rom_start"]:
            report["sections"].append({"name": name, "rom_start": section["rom_start"], "vram": section["vram"],
                                       "size": section["size"], "function_count": 0, "classification": "data-only"})
            continue
        directory = output / name
        directory.mkdir(exist_ok=True)
        command = [str(python), "-m", "spimdisasm", "singleFileDisasm", str(rom_path), str(directory),
                   "--start", hex(section["rom_start"]), "--end", hex(section.get("text_end", section["rom_end"])),
                   "--vram", hex(section["vram"]), "--function-info", str(directory / "functions.csv"),
                   "--save-context", str(directory / "context.csv"), "--quiet"]
        if section["text_end"] < section["rom_end"]:
            command.extend(["--data-start", hex(section["text_end"]), "--data-end", hex(section["rom_end"])])
        with (directory / "disassemble.log").open("w") as log:
            subprocess.run(command, check=True, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        with (directory / "functions.csv").open() as source:
            candidates = list(csv.DictReader(source))
        functions = [candidate for candidate in candidates if candidate["name"].startswith("func_")]
        item = {"name": name, "rom_start": section["rom_start"], "vram": section["vram"],
                "size": section["size"], "function_count": len(functions),
                "non_function_ranges": [candidate for candidate in candidates if candidate not in functions],
                "command": command}
        report["sections"].append(item)
        if functions:
            symbols.extend(["[[section]]", f'name = "{name}"', f'rom = {section["rom_start"]:#x}',
                            f'vram = {section["vram"]:#x}', f'size = {section["size"]:#x}', "functions = ["])
            for function in functions:
                address, size = int(function["address"], 0), int(function["length"], 0)
                if size <= 0 or address < section["vram"] or address + size > section["vram"] + section["size"]:
                    raise RuntimeError(f"candidate function outside its section: {function}")
                function_name = f"{name}_{function['name']}"
                symbols.append(f'  {{ name = "{function_name}", vram = {address:#x}, size = {size:#x} }},')
            symbols.extend(["]", ""])
        report["function_count"] += len(functions)
        print(f"{name}: {len(functions)} candidate functions", flush=True)
    symbol_path = output / "symbols.toml"
    symbol_path.write_text("\n".join(symbols))
    report["symbols_sha256"] = hashlib.sha256(symbol_path.read_bytes()).hexdigest()
    (output / "report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    (output / "recomp.toml").write_text(
        '[input]\nentrypoint = 0x80076610\n'
        f'rom_file_path = {json.dumps(str(rom_path))}\n'
        'symbols_file_path = "symbols.toml"\noutput_func_path = "generated"\n')
    print(f"Total: {report['function_count']} candidate functions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
