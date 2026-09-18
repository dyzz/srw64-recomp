#!/usr/bin/env python3
"""Build/run the native CPU integration probe with strict input provenance.

This host records graphics tasks and consumes audio samples for diagnostics.
With --graphics it renders through RT64/Metal and captures completed GPU frames.
Individual probe success is not a complete playable-game acceptance.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

from analyze_layout import ROOT, analyze
from audit_rom_variant import load_variant


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--reuse-build-from", type=Path, help="reuse an identical existing binary from a prior run after checking source, ROM and ABI fingerprints")
    parser.add_argument("--variant", choices=("jp", "model5600"), default="jp", help="pinned ROM resource variant; code compatibility is verified before building")
    parser.add_argument("--vis", type=int, default=600)
    parser.add_argument("--graphics", action="store_true", help="build/run RT64 Metal and capture GPU output")
    parser.add_argument("--audio", action="store_true", help="play through SDL and record a bounded device-input capture (requires graphics)")
    parser.add_argument("--native-resolution", action="store_true", help="render at the window drawable scale with HiDPI enabled")
    parser.add_argument("--resolution-scale", type=int, choices=range(1, 9), help="fixed internal render scale, 1..8; overrides window scaling without changing game layout")
    parser.add_argument("--dump-textures", action="store_true", help="record RT64 TMEM hashes, load coordinates and original texture bytes")
    parser.add_argument("--font-pack", type=Path, help="load a local RT64 texture replacement directory")
    parser.add_argument("--native-marker", type=Path, help="native GPU model pack for original JP resource 5600")
    parser.add_argument("--profile", type=Path, help="independent locale, art and native model on the original JP ROM")
    parser.add_argument("--presentation-settings", type=Path, help="persistent next-launch locale preferences; requires profile")
    parser.add_argument("--rule-settings", type=Path, help="rule file the in-game 选项 menu and settings window write back to; requires profile")
    parser.add_argument("--original-name-entry", action="store_true", help="keep the original character grid for reference runs and old N64-input fixtures")
    parser.add_argument("--language", help="override the profile locale")
    parser.add_argument("--images", choices=("original", "hd"), help="initial image mode; F6 toggles during play")
    parser.add_argument("--input", type=Path, help="native VI-indexed N64 button script")
    parser.add_argument("--save-from", type=Path, help="copy an existing 32 KiB SRAM into the new isolated run")
    parser.add_argument("--save-sha256", help="require this previously recorded digest for --save-from")
    parser.add_argument("--interactive", action="store_true", help="keyboard play until the window closes; no automatic VI timeout or input script")
    parser.add_argument("--diagnostics", choices=("light", "full"), help="interactive default: light (no periodic GPU/8 MiB RAM captures); bounded probes default: full")
    args = parser.parse_args()
    diagnostics = args.diagnostics or ("light" if args.interactive else "full")
    profile = None
    if args.profile:
        if not args.graphics or args.variant != "jp" or args.font_pack or args.native_marker:
            parser.error("profile requires JP graphics and owns dialogue/art/model configuration")
        from srw64_native.profile import load_profile
        profile = load_profile(args.profile, locale=args.language, images=args.images)
        if args.resolution_scale is not None:
            profile["presentation"]["resolution_scale"] = args.resolution_scale
        args.resolution_scale = profile["presentation"]["resolution_scale"]
    elif args.language or args.images:
        parser.error("language/images overrides require --profile")
    if args.presentation_settings and not profile:
        parser.error("presentation-settings requires profile")
    if args.rule_settings and not profile:
        parser.error("rule-settings requires profile")
    comparison_fixture = None
    if os.environ.get("SRW64_STATE_FIXTURE"):
        if (os.environ.get("SRW64_STATE_PROBE") != "1" or not profile or args.variant != "jp"
                or args.interactive or args.audio or args.save_from):
            parser.error("RNG comparison fixture requires a muted bounded JP profile probe with empty SRAM")
        fixture = Path(os.environ["SRW64_STATE_FIXTURE"]).resolve()
        comparison_fixture = {"path": str(fixture), "sha256": digest(fixture)}
    move_probe = os.environ.get("SRW64_SCRIPT_MOVE_PROBE")
    if move_probe is not None:
        if move_probe not in ("baseline", "target17"):
            parser.error("unknown fixed script movement experiment")
        if (not profile or args.variant != "jp" or args.audio or args.save_from or args.interactive
                or args.images != "original" or args.language != "ja"
                or os.environ.get("SRW64_SCRIPT_TRACE") != "1"):
            parser.error("movement experiment requires original JP profile/images, ja, script trace, muted bounded run and empty SRAM")
    if os.environ.get("SRW64_SCRIPT_INJECT") == "1":
        if (not profile or args.variant != "jp" or args.audio or args.interactive or not args.graphics
                or os.environ.get("SRW64_SCRIPT_TRACE") != "1"):
            parser.error("script injection requires a muted bounded JP profile graphics run with script trace")
    if os.environ.get("SRW64_MINI_STAGE"):
        # Bounded runs stay attributable through the script trace. Audio is allowed
        # so presentation commands that only differ in sound can be told apart; a
        # bounded audio run must name the VI window it captures.
        if (not profile or args.variant != "jp" or not args.graphics
                or (not args.interactive and os.environ.get("SRW64_SCRIPT_TRACE") != "1")):
            parser.error("mini stage substitution requires a JP profile graphics run; bounded runs need script trace")
        if args.audio and not args.interactive and not os.environ.get("SRW64_AUDIO_CAPTURE_TO"):
            parser.error("a bounded mini stage audio run must set SRW64_AUDIO_CAPTURE_FROM/_TO around the commands being listened to")
        if not Path(os.environ["SRW64_MINI_STAGE"]).is_file():
            parser.error("SRW64_MINI_STAGE must name a compiled mini stage image")
    from srw64_native import rule_settings
    try:
        rule_fixes = rule_settings.parse(os.environ.get("SRW64_RULE_FIXES", ""))
    except ValueError as error:
        parser.error(f"SRW64_RULE_FIXES: {error}")
    from srw64_native import upgrade_rules
    upgrade_rules_report = None
    if os.environ.get("SRW64_UPGRADE_RULES"):
        # The host applies the same checks; failing here keeps a bad file from
        # costing a build and a boot.
        try:
            upgrade_rules_report = upgrade_rules.report(Path(os.environ["SRW64_UPGRADE_RULES"]))
        except ValueError as error:
            parser.error(f"SRW64_UPGRADE_RULES: {error}")
    native_marker = None
    if args.native_marker:
        if not args.graphics or args.variant != "jp":
            parser.error("native marker requires original JP ROM and graphics")
        from prepare_native_marker import validate
        native_marker = validate(args.native_marker)
    if args.interactive and (not args.graphics or args.input):
        parser.error("interactive play requires graphics and excludes scripted input")
    if args.audio and not args.graphics:
        parser.error("audio output currently requires the SDL graphics host")
    if (args.native_resolution or args.resolution_scale or args.dump_textures or args.font_pack) and not args.graphics:
        parser.error("graphics options require --graphics")
    if args.font_pack:
        args.font_pack = args.font_pack.resolve()
        if not (args.font_pack / "rt64.json").is_file():
            parser.error("font pack must contain rt64.json")
    if not 1 <= args.vis <= 216000:
        raise RuntimeError("VI limit must be in 1..216000")
    if args.interactive:
        args.vis = 0
    initial_save = None
    if args.save_sha256 and not args.save_from:
        parser.error("save-sha256 requires save-from")
    if args.save_from:
        args.save_from = args.save_from.resolve()
        if args.save_from.stat().st_size != 0x8000:
            raise RuntimeError("initial SRAM must be exactly 32 KiB")
        initial_save = {"path": str(args.save_from), "sha256": digest(args.save_from)}
        if args.save_sha256 is not None and initial_save["sha256"] != args.save_sha256:
            raise RuntimeError("initial SRAM differs from the selected recorded digest")
    output = args.output.resolve()
    if output.exists():
        raise RuntimeError("output directory must be new")
    output.parent.mkdir(parents=True, exist_ok=True)
    rom_path, variant, compatibility = load_variant(args.variant)
    prepared_profile = None
    if profile:
        from srw64_native.profile import prepare_profile
        prepared_profile = prepare_profile(ROOT, profile, rom_path, output.parent / (output.name + ".content"))
        if prepared_profile["hd_available"] and profile["presentation"]["model_5600"] == "waterdrop":
            args.native_marker = ROOT / "build/recomp/native-marker/assets"
            if not args.native_marker.exists():
                subprocess.run([sys.executable, str(ROOT / "tools/recomp/prepare_native_marker.py"), "--output", str(args.native_marker)], check=True)
            from prepare_native_marker import validate
            native_marker = validate(args.native_marker)
        if not prepared_profile["hd_available"]:
            print(f"原始画面模式；HD 不可用：{prepared_profile['hd_unavailable_reason']}", flush=True)
    layout = analyze((ROOT / "rom.z64").read_bytes(), None)
    lock = json.loads((ROOT / "config/recomp/toolchain.json").read_text())
    upstream = ROOT / "build/recomp/upstream"
    for name in ("N64ModernRuntime", "N64Recomp"):
        checkout = upstream / name
        revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=checkout, text=True).strip()
        if revision != lock["sources"][name]["commit"]:
            raise RuntimeError(f"{name} revision differs from lock")
        subprocess.run(["git", "diff", "--quiet", "HEAD"], cwd=checkout, check=True)
    header = upstream / "N64Recomp/include/recomp.h"
    if header.read_bytes() != (upstream / "N64ModernRuntime/N64Recomp/include/recomp.h").read_bytes():
        raise RuntimeError("generator and runtime context/helper ABI headers differ")
    generation = ROOT / "build/recomp/cpu-bound/report.json"
    generated = json.loads(generation.read_text())
    if generated["status"] != "generated" or generated["rom_sha256"] != layout["rom_sha256"]:
        raise RuntimeError("CPU generation is incomplete or targets another ROM")
    for item in generated["generated_files"]:
        if digest(generation.parent / item["path"]) != item["sha256"]:
            raise RuntimeError(f"generated source changed: {item['path']}")
    if args.graphics and not args.reuse_build_from:
        subprocess.run([sys.executable, str(ROOT / "tools/recomp/prepare_rt64.py")], check=True, stdout=subprocess.DEVNULL)
    build = ROOT / ("build/recomp/gfx-build" if args.graphics else "build/recomp/host-build")
    target = "srw64-gfx-host" if args.graphics else "srw64-host"
    build.mkdir(parents=True, exist_ok=True)
    commands = [[str(ROOT / "build/recomp/tool-build/RSPRecomp"), str(ROOT / "config/recomp/audio-probe.toml")],
                ["cmake", "-S", str(ROOT / "tools/recomp/native-host"), "-B", str(build), "-G", "Ninja",
                 "-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++",
                 "-DSRW64_ENABLE_RT64=" + ("ON" if args.graphics else "OFF"), "-DSRW64_METAL_SOURCE_SHADERS=ON"],
                ["cmake", "--build", str(build), "--target", target, "-j", "6"]]
    binary = build / target
    source_hashes = {str(path.relative_to(ROOT)): digest(path)
                     for path in sorted((ROOT / "tools/recomp/native-host").iterdir()) if path.is_file()}
    source_hashes.update({str(path.relative_to(ROOT)): digest(path)
                         for path in sorted((ROOT / "src/native").rglob("*")) if path.is_file()})
    source_hashes.update({str(path.relative_to(ROOT)): digest(path)
                         for path in [ROOT / "tools/recomp/prepare_runtime_lifecycle.py",
                                      *sorted((ROOT / "tools/recomp/runtime-support").glob("*.hpp"))]})
    reused_build = None
    if args.reuse_build_from:
        prior_path = args.reuse_build_from.resolve() / "report.json"
        prior = json.loads(prior_path.read_text())
        if (prior["command"][0] != str(binary) or prior["binary_sha256"] != digest(binary)
                or prior["source_sha256"] != source_hashes
                or prior["rom_sha256"] != variant["sha256"]
                or prior["generation_report_sha256"] != digest(generation)
                or prior["runtime_abi_header_sha256"] != digest(header)
                or prior["variant_lock_sha256"] != digest(ROOT / "config/recomp/rom-variants.json")):
            raise RuntimeError("prior run build fingerprints differ; refusing binary reuse")
        reused_build = {"report": str(prior_path), "report_sha256": digest(prior_path),
                        "binary_sha256": digest(binary)}
        commands = []
    for index, command in enumerate(commands):
        with (build / f"probe-build-{index}.log").open("w") as log:
            subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    lifecycle = json.loads((ROOT / "build/recomp/runtime-lifecycle/manifest.json").read_text())
    command = [str(binary), str(rom_path), str(output), str(args.vis)]
    input_report = None
    if args.input:
        from native_inputs import compile_input
        input_path = output.parent / (output.name + ".inputs.bin")
        input_path.write_bytes(compile_input(json.loads(args.input.read_text()), args.vis))
        command.append(str(input_path))
        input_report = {"script_path": str(args.input.resolve()), "script_sha256": digest(args.input),
                        "compiled_path": str(input_path), "compiled_sha256": digest(input_path), "clock": "native VI ticks"}
    if args.save_from:
        if not args.input:
            command.append("-")
        if digest(args.save_from) != initial_save["sha256"]:
            raise RuntimeError("initial SRAM changed during preparation")
        command.append(str(args.save_from))
    report: dict = {"schema": "srw64.recomp-native-host-probe.v1", "status": "incomplete",
                    "evidence_scope": "native-RT64-Metal-GPU-readback-with-audio-sink" if args.graphics else "native-CPU-integration-with-recording-renderer-and-audio-sink",
                    "rom_sha256": variant["sha256"], "rom_variant": args.variant,
                    "variant_lock_sha256": digest(ROOT / "config/recomp/rom-variants.json"),
                    "code_compatibility": compatibility, "binary_sha256": digest(binary),
                    "generation_report_sha256": digest(generation), "runtime_abi_header_sha256": digest(header),
                    "unsupported_call_traps": generated["unsupported_call_traps"],
                    "source_sha256": source_hashes, "reused_build": reused_build,
                    "build_commands": commands, "command": command, "requested_vis": args.vis}
    report["profile"] = prepared_profile
    report["input"] = input_report
    report["native_marker"] = native_marker
    if prepared_profile:
        report["native_dialogue"] = prepared_profile["dialogue"]
    report["interactive"] = args.interactive
    report["initial_save"] = initial_save
    report["audio_output_enabled"] = args.audio
    if args.audio:
        report["audio_capture_window"] = {"from_vi": os.environ.get("SRW64_AUDIO_CAPTURE_FROM"),
                                          "to_vi": os.environ.get("SRW64_AUDIO_CAPTURE_TO")}
    report["native_name_entry"] = {"enabled": bool(prepared_profile) and not args.original_name_entry,
        "control_enabled": bool(os.environ.get("SRW64_NAME_ENTRY_CONTROL"))}
    report["diagnostics"] = diagnostics
    report["script_trace_enabled"] = os.environ.get("SRW64_SCRIPT_TRACE") == "1"
    report["script_move_probe"] = os.environ.get("SRW64_SCRIPT_MOVE_PROBE")
    report["script_inject_enabled"] = os.environ.get("SRW64_SCRIPT_INJECT") == "1"
    report["mini_stage"] = os.environ.get("SRW64_MINI_STAGE")
    report["state_probe_enabled"] = os.environ.get("SRW64_STATE_PROBE") == "1"
    report["rule_fixes"] = {"rules_version": rule_settings.RULES_VERSION, "enabled": list(rule_fixes)}
    report["rule_probe_enabled"] = os.environ.get("SRW64_RULE_PROBE") == "1"
    report["upgrade_rules"] = upgrade_rules_report
    report["comparison_fixture"] = comparison_fixture
    report["frame_trace"] = {"from_vi": os.environ.get("SRW64_FRAME_TRACE_FROM"), "to_vi": os.environ.get("SRW64_FRAME_TRACE_TO")}
    report["native_resolution"] = args.native_resolution
    report["resolution_scale"] = args.resolution_scale
    report["font_pack"] = {"path": str(args.font_pack), "manifest_sha256": digest(args.font_pack / "rt64.json")} if args.font_pack else None
    if args.audio:
        report["evidence_scope"] = "native-RT64-Metal-GPU-readback-and-SDL-audio-device"
    if args.graphics and diagnostics == "light":
        report["evidence_scope"] = "native-RT64-Metal-execution-without-periodic-GPU-capture"
    log_path = output.parent / (output.name + ".native.log")
    if args.graphics:
        report["graphics_source_patches"] = json.loads((ROOT / "build/recomp/graphics-source-patches.json").read_text())
    started = time.monotonic()
    environment = {**os.environ, "SRW64_AUDIO_OUTPUT": "1" if args.audio else "0",
                   "SRW64_NATIVE_NAME_ENTRY": "0" if args.original_name_entry else "1",
                   "SRW64_DIAGNOSTICS": diagnostics,
                   "SRW64_ROM_VARIANT": args.variant,
                   "SRW64_INTERACTIVE": "1" if args.interactive else "0",
                   "SRW64_NATIVE_RESOLUTION": "1" if args.native_resolution else "0",
                   "SRW64_RULE_FIXES": ",".join(rule_fixes)}
    for name in ("SRW64_TEXTURE_DUMP", "SRW64_FONT_PACK", "SRW64_RESOLUTION_SCALE", "SRW64_DIALOGUE_DATA", "SRW64_NATIVE_MARKER", "SRW64_ART_PACK", "SRW64_IMAGE_MODE", "SRW64_HD_AVAILABLE", "SRW64_PRESENTATION_SETTINGS", "SRW64_RULE_SETTINGS"):
        environment.pop(name, None)
    if native_marker:
        environment["SRW64_NATIVE_MARKER"] = native_marker["path"]
    if prepared_profile:
        environment["SRW64_DIALOGUE_DATA"] = report["native_dialogue"]["path"]
        environment["SRW64_PRESENTATION_SETTINGS"] = str(args.presentation_settings.resolve() if args.presentation_settings else output / "presentation-settings.json")
        report["presentation_settings_path"] = environment["SRW64_PRESENTATION_SETTINGS"]
        if args.rule_settings:
            environment["SRW64_RULE_SETTINGS"] = str(args.rule_settings.resolve())
            report["rule_settings_path"] = environment["SRW64_RULE_SETTINGS"]
    if comparison_fixture:
        environment["SRW64_STATE_FIXTURE"] = comparison_fixture["path"]
        if digest(Path(comparison_fixture["path"])) != comparison_fixture["sha256"]:
            raise RuntimeError("Comparison fixture changed during preparation")
    if prepared_profile:
        if prepared_profile["art"]:
            environment["SRW64_ART_PACK"] = prepared_profile["art"]["path"]
        environment["SRW64_HD_AVAILABLE"] = "1" if prepared_profile["hd_available"] else "0"
        environment["SRW64_IMAGE_MODE"] = profile["presentation"]["images"]
    if args.resolution_scale:
        environment["SRW64_RESOLUTION_SCALE"] = str(args.resolution_scale)
    report["runtime_lifecycle"] = lifecycle
    report["shutdown_trace_enabled"] = bool(environment.get("SRW64_SHUTDOWN_TRACE"))
    if args.dump_textures:
        environment["SRW64_TEXTURE_DUMP"] = str(output / "textures")
    if args.font_pack:
        environment["SRW64_FONT_PACK"] = str(args.font_pack)
    try:
        with log_path.open("x") as log:
            result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                                    env=environment,
                                    timeout=None if args.interactive else args.vis / 60 + (120 if args.graphics else 20))
        report["exit_code"] = result.returncode
        audio_device = output / "audio-device.json"
        if audio_device.exists():
            report["audio_device"] = json.loads(audio_device.read_text())
            report["audio_capture_sha256"] = digest(output / "audio-output.s16")
        save_path = output / "runtime-data/saves" / (variant["game_id"] + ".bin")
        if save_path.exists():
            report["final_save"] = {"path": str(save_path), "size": save_path.stat().st_size,
                                    "sha256": digest(save_path)}
        rule_changes = output / "rule-fixes-events.jsonl"
        if rule_changes.exists():
            report["rule_fix_changes"] = [json.loads(line) for line in rule_changes.read_text().splitlines()]
        control_events = output / "control-events.jsonl"
        if control_events.exists():
            report["live_control"] = {"events_path": str(control_events), "events_sha256": digest(control_events),
                                      "events": [json.loads(line) for line in control_events.read_text().splitlines()]}
        naming_events = output / "name-entry-events.jsonl"
        if naming_events.exists():
            report["native_name_entry"].update({"events_path": str(naming_events), "events_sha256": digest(naming_events)})
        counters = output / "native-counters.json"
        if counters.exists():
            report["counters"] = json.loads(counters.read_text())
        report["status"] = "native-task-submissions-observed" if result.returncode == 0 else "native-run-failed"
        if args.graphics and result.returncode == 0:
            report["frames"] = [{"path": path.name, "sha256": digest(path),
                                 "metadata": json.loads(path.with_suffix(".json").read_text()) if path.with_suffix(".json").exists() else None}
                                for path in sorted(output.glob("present-*.png"))]
            report["status"] = "native-graphics-frames-captured" if report["frames"] else "native-no-GPU-frame-captured"
            if not report["frames"] and diagnostics == "light":
                report["status"] = "native-graphics-run-completed"
        if result.returncode == 0 and report.get("counters", {}).get("vis", 0) < args.vis:
            report["status"] = "native-run-ended-by-control" if report.get("counters", {}).get("control_quit") else "native-run-ended-before-VI-limit"
    except subprocess.TimeoutExpired:
        report["status"] = "native-run-timeout"
    finally:
        output.mkdir(parents=True, exist_ok=True)
        report["native_log_path"] = str(log_path)
        report["native_log_sha256"] = digest(log_path)
        report["elapsed_seconds"] = time.monotonic() - started
        (output / "report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps({key: report[key] for key in ("status", "native_log_path", "requested_vis")}, indent=2))
    print(log_path.read_text()[-2500:])
    return 0 if report["status"] in ("native-task-submissions-observed", "native-graphics-frames-captured", "native-graphics-run-completed", "native-run-ended-by-control") else 1


if __name__ == "__main__":
    raise SystemExit(main())
