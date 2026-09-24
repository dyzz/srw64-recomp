#!/usr/bin/env python3
"""Generate CPU code with audited library names and reviewed ROM identities."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tomllib

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))  # tools/, home of the recomp package
from recomp.toolchain.analyze_layout import ROOT, analyze


NATIVE_HOOKS = {
    "load_000AB160_func_801C8AB4": "srw64_original_return_to_map",
    "load_000AB160_func_801D5064": "srw64_original_battle_confirm_step",
    "load_000AB160_func_801D5294": "srw64_original_battle_response_step",
    "resident_func_80082334": "srw64_original_random_bound",
    "resident_func_80085F30": "srw64_original_frame_boundary",
    "resident_func_800821B0": "srw64_original_rng_seed",
    "resident_func_800822D8": "srw64_original_rng_next",
    "resident_func_800924D8": "srw64_original_intermission_serialize",
    "resident_func_80093278": "srw64_original_tactical_serialize",
    "resident_func_800927A4": "srw64_original_intermission_restore",
    "resident_func_800936A0": "srw64_original_tactical_restore",
    "load_001090A0_func_801C5004": "srw64_original_name_selection_init",
    "load_001090A0_func_801C50B8": "srw64_original_name_selection_step",
    "load_001090A0_func_801C5DAC": "srw64_original_name_review_init",
    "load_001090A0_func_801C5E88": "srw64_original_name_review_step",
    "load_001090A0_func_801C5494": "srw64_original_name_player_init",
    "load_001090A0_func_801C5920": "srw64_original_name_partner_init",
    "load_001090A0_func_801C5644": "srw64_original_name_player_step",
    "load_001090A0_func_801C5AD0": "srw64_original_name_partner_step",
    "load_0010DA50_func_801CA9CC": "srw64_original_intro_main",
    "resident_func_8008D748": "srw64_original_dialogue_step",
    "resident_func_8008C9C0": "srw64_original_dialogue_load",
    "resident_func_8008DC40": "srw64_original_dialogue_draw",
    "resident_func_800964E4": "srw64_original_portrait_draw",
    "resident_func_8008F5C8": "srw64_original_dialogue_reset",
    "resident_func_8009EFDC": "srw64_original_script_step",
    "resident_func_8009FA94": "srw64_original_dialogue_choice",
    "resident_func_8009DE7C": "srw64_original_script_register",
    "load_000AB160_func_80209D6C": "srw64_original_stage_map_select",
    "load_000AB160_func_801E1F08": "srw64_original_seisenshi_bonus",
    "load_000AB160_func_801E1F10": "srw64_original_esp_bonus",
    "load_000AB160_func_801E1D64": "srw64_original_potential_bonus",
    "load_000AB160_func_801F4384": "srw64_original_battle_hit_rate",
    "load_000AB160_func_80204254": "srw64_original_hit_estimate",
    "load_000AB160_func_801F5628": "srw64_original_battle_damage",
    "load_000AB160_func_80203418": "srw64_original_damage_estimate",
    "resident_func_800A5054": "srw64_original_wing_kill_restore",
    "resident_func_800AA8F4": "srw64_original_weapon_inherit",
    "resident_func_800AAD28": "srw64_original_unit_register",
    "resident_func_800AA464": "srw64_original_unit_remove",
    "resident_func_800AB808": "srw64_original_unit_merge",
    "resident_func_800AA3C4": "srw64_original_unit_delete",
    "load_000AB160_func_8020ABB4": "srw64_original_deploy_record",
    "resident_func_800A5254": "srw64_original_unit_stats",
    "resident_func_800A5F84": "srw64_original_weapon_twin_sync",
    "load_0008F4B0_func_801CF388": "srw64_original_upgrade_list_open",
    "load_0008F4B0_func_801CF564": "srw64_original_upgrade_list_step",
    "load_0008F4B0_func_801D03D0": "srw64_original_weapon_list_open",
    "load_0008F4B0_func_801D04A4": "srw64_original_weapon_list_step",
    "load_0008F4B0_func_801D0600": "srw64_original_weapon_screen_open",
    "load_0008F4B0_func_801D087C": "srw64_original_weapon_screen_step",
    "load_0008F4B0_func_801CF680": "srw64_original_upgrade_open",
    "load_0008F4B0_func_801C80E0": "srw64_original_upgrade_stats_view",
    "load_0008F4B0_func_801CF988": "srw64_original_upgrade_stats_step",
    "load_0008F4B0_func_801CF85C": "srw64_original_upgrade_ew_check",
    "load_0008F4B0_func_801D0C7C": "srw64_original_upgrade_weapon_view",
    "load_0008F4B0_func_801D1100": "srw64_original_upgrade_weapon_step",
    "load_00107BF0_func_801C2600": "srw64_original_sale_price",
    # Link Battler: the Game Boy pak driver becomes a virtual cartridge, and the
    # リンク screen waits for the native series page (link_page.cpp).
    "resident_func_80090F44": "srw64_original_gbpak_open",
    "resident_func_80090FA0": "srw64_original_gbpak_status",
    "resident_func_80090FC4": "srw64_original_gbpak_power",
    "resident_func_800910C4": "srw64_original_gbpak_check_title",
    "resident_func_80091120": "srw64_original_gbpak_enable_ram",
    "resident_func_80091284": "srw64_original_gbpak_transfer",
    # インターミッション main menu: the build draws nothing and the step takes the
    # native page's answers (intermission_page.cpp).
    "load_0008F4B0_func_801CDFB0": "srw64_original_intermission_menu_build",
    "load_0008F4B0_func_801CE19C": "srw64_original_intermission_menu_step",
    # 強化パーツ screens 7 / 18 / 19 (parts_page.cpp).
    "load_0008F4B0_func_801D4A00": "srw64_original_parts_list_open",
    "load_0008F4B0_func_801D4A98": "srw64_original_parts_list_step",
    "load_0008F4B0_func_801D4BEC": "srw64_original_parts_slots_open",
    "load_0008F4B0_func_801D4C94": "srw64_original_parts_slots_step",
    "load_0008F4B0_func_801D5168": "srw64_original_parts_holders_open",
    "load_0008F4B0_func_801D51EC": "srw64_original_parts_holders_step",
    # ユニット能力／パイロット能力 screens 4 / 13 / 14 / 5 / 15 (ability_page.cpp).
    "load_0008F4B0_func_801D14BC": "srw64_original_ability_unit_list_open",
    "load_0008F4B0_func_801D1554": "srw64_original_ability_unit_list_step",
    "load_0008F4B0_func_801D16D8": "srw64_original_ability_unit_open",
    "load_0008F4B0_func_801D2030": "srw64_original_ability_unit_step",
    "load_0008F4B0_func_801D2144": "srw64_original_ability_weapons_open",
    "load_0008F4B0_func_801D21F8": "srw64_original_ability_weapons_step",
    "load_0008F4B0_func_801D22E0": "srw64_original_ability_pilot_list_open",
    "load_0008F4B0_func_801D2378": "srw64_original_ability_pilot_list_step",
    "load_0008F4B0_func_801D2480": "srw64_original_ability_pilot_open",
    "load_0008F4B0_func_801D24C8": "srw64_original_ability_pilot_step",
    # データセーブ screens 1 / 9 (save_page.cpp).
    "load_0008F4B0_func_801CEA30": "srw64_original_save_choice_open",
    "load_0008F4B0_func_801CEABC": "srw64_original_save_choice_step",
    "load_0008F4B0_func_801CECE8": "srw64_original_save_slots_open",
    "load_0008F4B0_func_801CEEF8": "srw64_original_save_slots_step",
    # のりかえ screens 6 / 16 / 17 / 20 / 21 (swap_page.cpp).
    "load_0008F4B0_func_801D25A4": "srw64_original_swap_pilots_open",
    "load_0008F4B0_func_801D263C": "srw64_original_swap_pilots_step",
    "load_0008F4B0_func_801D2758": "srw64_original_swap_targets_open",
    "load_0008F4B0_func_801D2A24": "srw64_original_swap_targets_step",
    "load_0008F4B0_func_801D2B64": "srw64_original_swap_confirm_open",
    "load_0008F4B0_func_801D3A90": "srw64_original_swap_confirm_step",
    "load_0008F4B0_func_801D4164": "srw64_original_swap_fairies_open",
    "load_0008F4B0_func_801D41FC": "srw64_original_swap_fairies_step",
    "load_0008F4B0_func_801D42FC": "srw64_original_swap_fairy_targets_open",
    "load_0008F4B0_func_801D4578": "srw64_original_swap_fairy_targets_step",
    # Controller Pak presence (osPfsIsPlug caller): the runtime aborts in osPfsIsPlug, so
    # the host answers "no pak on any port" itself (game_hooks.cpp).
    "resident_func_80090778": "srw64_original_pfs_plug_check",
    "load_0008F4B0_func_801D6FF4": "srw64_original_link_open",
    "load_0008F4B0_func_801D70FC": "srw64_original_link_step",
}


def bind_native_hooks(source: str) -> str:
    """Rename definitions only; direct calls and overlay tables use the bridge."""
    for original, renamed in NATIVE_HOOKS.items():
        source = source.replace(f"RECOMP_FUNC void {original}(", f"RECOMP_FUNC void {renamed}(")
    return source


def bind_overlay_calls(source: str) -> tuple[str, int]:
    """Resolve fixed-address overlays through the currently loaded function map.

    A unique symbol in the current scan does not prove that another overlay has
    no code at that address. Preserve the JAL address, not the guessed ROM owner.
    Calls within an overlay use the same lookup to keep one dispatch contract.
    """
    return re.subn(r"\bload_[0-9A-F]+_func_([0-9A-F]{8})\(rdram, ctx\);",
                   lambda match: f"LOOKUP_FUNC(0x{match[1]})(rdram, ctx);", source)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scan", type=Path, default=ROOT / "build/recomp/cpu-scan")
    parser.add_argument("--output", type=Path, default=ROOT / "build/recomp/cpu-bound")
    args = parser.parse_args()
    rom = (ROOT / "rom.z64").read_bytes()
    layout = analyze(rom, None)
    work = args.output.resolve()
    work.mkdir(parents=True, exist_ok=True)
    audit_path = work / "library-audit.json"
    subprocess.run([sys.executable, str(ROOT / "tools/recomp/toolchain/audit_library_symbols.py"), "--output", str(audit_path)], check=True)
    audit = json.loads(audit_path.read_text())
    names: dict[int, dict] = {}
    for item in audit["symbols"]:
        if item["runtime_action"] == "retain-game-code":
            continue
        # Accept a full normalized signature only when its remaining candidate
        # bytes are at most alignment padding. Never discard unexplained tails.
        for match in item["matches"]:
            size, candidate_size, offset = match["size"], item["candidate_size"], item["rom"]
            if 0 <= candidate_size - size < 16 and not any(rom[offset + size:offset + candidate_size]):
                names[item["vram"]] = {"name": item["name"], "size": candidate_size,
                                      "evidence": "normalized-code-and-padding-match", "signature_line": match["signature_line"],
                                      "sha256": hashlib.sha256(rom[offset:offset + candidate_size]).hexdigest(),
                                      "runtime_action": item["runtime_action"]}
                break
    reviewed = json.loads((ROOT / "config/recomp/library-symbols.json").read_text())
    if reviewed["schema"] != "srw64.recomp-reviewed-library-symbols.v1" or reviewed["rom_sha256"] != layout["rom_sha256"]:
        raise RuntimeError("reviewed library symbols differ from baseline")
    for item in reviewed["functions"]:
        offset, size = item["rom"], item["size"]
        if offset + 0x80075610 != item["vram"] or hashlib.sha256(rom[offset:offset + size]).hexdigest() != item["sha256"]:
            raise RuntimeError(f"reviewed library bytes differ: {item['name']}")
        names[item["vram"]] = {"name": item["name"], "size": size, "evidence": item["identity_evidence"],
                              "basis": item["basis"], "sha256": item["sha256"], "runtime_action": "reviewed-name"}
    scan_report = json.loads((args.scan / "report.json").read_text())
    if scan_report["rom_sha256"] != layout["rom_sha256"]:
        raise RuntimeError("CPU scan targets another ROM")
    original_symbols = (args.scan / "symbols.toml").read_bytes()
    if hashlib.sha256(original_symbols).hexdigest() != scan_report["symbols_sha256"]:
        raise RuntimeError("CPU scan symbols changed since report")
    sections = tomllib.loads(original_symbols.decode())["section"]
    output: list[str] = []
    applied: list[dict] = []
    excluded: list[dict] = []
    for section in sections:
        if section["name"] == "resident":
            for split in reviewed.get("function_splits", []):
                index = next((i for i, function in enumerate(section["functions"]) if function["vram"] == split["vram"]), None)
                if index is None or section["functions"][index]["size"] != split["size"]:
                    raise RuntimeError("reviewed function split differs from scan")
                offset = split["vram"] - 0x80075610
                if hashlib.sha256(rom[offset:offset + split["size"]]).hexdigest() != split["sha256"]:
                    raise RuntimeError("function split byte identity differs")
                replacements = [{"name": names[address]["name"], "vram": address, "size": names[address]["size"]} for address in split["function_starts"]]
                cursor = split["vram"]
                for function in replacements:
                    if function["vram"] != cursor:
                        raise RuntimeError("function split has a gap or overlap")
                    cursor += function["size"]
                if cursor != split["vram"] + split["size"]:
                    raise RuntimeError("function split does not cover the old range")
                section["functions"][index:index + 1] = replacements
        functions: list[dict] = []
        for function in section["functions"]:
            offset = function["vram"] - section["vram"] + section["rom"]
            if not any(rom[offset:offset + function["size"]]):
                excluded.append({**function, "section": section["name"], "reason": "all-zero range without executable return/control flow"})
                continue
            if section["name"] == "resident" and function["vram"] in names:
                entry = names[function["vram"]]
                if entry["size"] != function["size"]:
                    raise RuntimeError(f"library boundary differs for {entry['name']}")
                function["name"] = entry["name"]
                applied.append({"vram": function["vram"], **entry})
            functions.append(function)
        if not functions:
            continue
        output.extend(["[[section]]", f'name = "{section["name"]}"', f'rom = {section["rom"]:#x}',
                       f'vram = {section["vram"]:#x}', f'size = {section["size"]:#x}', "functions = ["])
        output.extend(f'  {{ name = "{f["name"]}", vram = {f["vram"]:#x}, size = {f["size"]:#x} }},' for f in functions)
        output.extend(["]", ""])
    missing = sorted(set(names) - {item["vram"] for item in applied})
    if missing:
        raise RuntimeError(f"reviewed/audited symbols absent from scan: {missing}")
    symbols_path = work / "symbols.toml"
    symbols_path.write_text("\n".join(output))
    config_path = work / "recomp.toml"
    config_path.write_text('[input]\nentrypoint = 0x80076610\n'
                           f'rom_file_path = {json.dumps(str(ROOT / "rom.z64"))}\n'
                           'symbols_file_path = "symbols.toml"\noutput_func_path = "generated"\n')
    with (work / "generate.log").open("w") as log:
        process = subprocess.run([str(ROOT / "build/recomp/tool-build/N64Recomp"), str(config_path)], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    generated_files: list[dict] = []
    unsupported_calls: list[str] = []
    if process.returncode == 0:
        header = work / "generated/funcs.h"
        declarations = set(re.findall(r"void (\w+)\(", header.read_text()))
        called: set[str] = set()
        for path in sorted((work / "generated").glob("funcs_*.c")):
            if path.name == "funcs_unsupported.c":
                continue
            source = path.read_text()
            adapted, count = bind_overlay_calls(source)
            adapted = bind_native_hooks(adapted)
            adapted = adapted.replace("RECOMP_FUNC void resident_func_8007F704(",
                                      "RECOMP_FUNC void srw64_original_rom_read(")
            adapted = adapted.replace("RECOMP_FUNC void resident_func_8008C510(",
                                      "RECOMP_FUNC void srw64_original_text_descriptor(")
            called.update(re.findall(r"^\s+(\w+)\(rdram, ctx\);", adapted, re.MULTILINE))
            path.write_text(adapted)
            generated_files.append({"path": str(path.relative_to(work)), "overlay_lookup_calls": count,
                                    "raw_sha256": hashlib.sha256(source.encode()).hexdigest(),
                                    "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
        # Upstream deliberately omits some internal OS/Pak routines. Retained
        # SDK callers may still refer to them. Make the diagnostic executable
        # fail with the exact routine instead of silently inventing behavior.
        missing_declarations = sorted(called - declarations)
        runtime_exports: set[str] = set()
        for path in (ROOT / "build/recomp/upstream/N64ModernRuntime/librecomp/src").glob("*.cpp"):
            runtime_exports.update(re.findall(r'extern "C" void (\w+)\(', path.read_text()))
        unsupported_calls = sorted(set(missing_declarations) - runtime_exports)
        known_omitted = {item["name"] + "_recomp" for item in audit["symbols"]
                         if item["runtime_action"] == "ignored-internal"}
        if set(unsupported_calls) - known_omitted:
            raise RuntimeError(f"unexplained missing declarations: {set(unsupported_calls) - known_omitted}")
        header.write_text(header.read_text() + '\n#ifdef __cplusplus\nextern "C" {\n#endif\n' + "\n".join(
            f"void {name}(uint8_t* rdram, recomp_context* ctx);" for name in missing_declarations) + "\n")
        with header.open("a") as destination:
            destination.write("\n".join(f"void {name}(uint8_t*, recomp_context*);" for name in NATIVE_HOOKS.values()) + "\n")
            destination.write("void srw64_original_rom_read(uint8_t*, recomp_context*);\nvoid srw64_original_text_descriptor(uint8_t*, recomp_context*);\n#ifdef __cplusplus\n}\n#endif\n")
        trap = work / "generated/funcs_unsupported.c"
        trap.write_text('#include "recomp.h"\n#include <stdio.h>\n#include <stdlib.h>\n' + "\n".join(
            f'void {name}(uint8_t* rdram, recomp_context* ctx) {{ '
            f'fprintf(stderr, "SRW64_UNSUPPORTED {name}\\n"); abort(); }}'
            for name in unsupported_calls) + "\n")
        generated_files.append({"path": str(trap.relative_to(work)), "sha256": hashlib.sha256(trap.read_bytes()).hexdigest(),
                                "role": "explicit-failure-for-unsupported-OS-calls"})
    report = {"schema": "srw64.recomp-cpu-generation.v1", "rom_sha256": layout["rom_sha256"],
              "status": "generated" if process.returncode == 0 else "generation-failed",
              "generator_exit_code": process.returncode, "applied_library_symbols": applied, "excluded_candidates": excluded,
              "generated_files": generated_files,
              "unsupported_call_traps": unsupported_calls,
              "symbols_sha256": hashlib.sha256(symbols_path.read_bytes()).hexdigest(), "runtime_validation": "pending"}
    (work / "report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"status": report["status"], "library_symbols": len(applied), "excluded_candidates": len(excluded)}, indent=2))
    if process.returncode:
        print((work / "generate.log").read_text()[-2200:], file=sys.stderr)
    return process.returncode


if __name__ == "__main__":
    raise SystemExit(main())
