#!/usr/bin/env python3
"""Compare OS text rendering in one identity-locked SRW64 RT64 task replay.

This probe uses explicitly selected dialogue segments, not a live interpreter.
It never runs game logic or reads/writes game saves.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import platform
import struct
import subprocess

from PIL import Image

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "assets/hd-ai/dialogue-type/live-medium-4x"
PACK = ROOT / "assets/hd-ai/dialogue-type/medium-pack/pack"
FONT = ROOT / "assets/hd-ai/dialogue-polish/fonts/HarmonyOS_Sans_SC_Medium.ttf"
IDENTITY = {
    "latest-gfx-rdram.bin": "237f1b494dd2a35e7a54348ab98928323905f582bafbf4b7446477d1d632ceb5",
    "latest-gfx-task.bin": "98149c0ff084c347b25cf559f7537268a76db032ef5a0deb469e5653ee8ce37b",
}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n")


def prepare_source(output: Path) -> dict:
    for name, expected in IDENTITY.items():
        if digest(SOURCE / name) != expected:
            raise RuntimeError(f"Selected scene identity changed: {name}")
    original = (SOURCE / "latest-gfx-rdram.bin").read_bytes()
    task = (SOURCE / "latest-gfx-task.bin").read_bytes()
    fields = struct.unpack("<16I", task)
    start, size = fields[12] & 0x1FFFFFFF, fields[13]
    if len(original) != 0x800000 or start + size > len(original) or size % 8:
        raise RuntimeError("Invalid captured display list")
    if original[0x1C2600:0x1C2620].hex() != "3c05801194a5f5c03c06801194c6f5c227bdffc8afb400280080a021afb60030":
        raise RuntimeError("Captured scene is not the reviewed opening dialogue overlay")
    modified = bytearray(original)
    font = False
    removed = []
    rows: dict[int, list[int]] = {}
    for at in range(start, start + size, 8):
        a, b = struct.unpack_from(">II", original, at)
        if a >> 24 == 0xFD:
            texture = b & 0x1FFFFFFF
            font = (a == 0xFD4800FB and 8 <= texture < len(original)
                    and original[texture - 8:texture] == bytes.fromhex("000501f801f80000"))
        if a >> 24 != 0xE4 or not font:
            continue
        if at + 24 > start + size or original[at+8] != 0xE1 or original[at+16] != 0xF1:
            raise RuntimeError("Unexpected texture rectangle command sequence")
        x, y = (b >> 12 & 4095) // 4, (b & 4095) // 4
        rows.setdefault(y, []).append(x)
        # F3DEX2 G_SPNOOP, with no extended-op magic. Replace the rectangle
        # and its two parameter commands; all other task and resource bytes stay identical.
        modified[at:at+24] = bytes.fromhex("e000000000000000") * 3
        removed.append({"offset": at, "original_hex": original[at:at+24].hex(), "x": x, "y": y})
    expected_rows = {183: list(range(19, 146, 14)), 199: list(range(19, 118, 14)),
                     37: list(range(115, 200, 14)), 53: list(range(115, 228, 14)),
                     168: [22, 36, 50], 22: [118, 132, 146]}
    if len(removed) != 40 or rows != expected_rows:
        raise RuntimeError("Glyph rectangles do not match the reviewed dialogue")
    allowed = set(i for item in removed for i in range(item["offset"], item["offset"]+24))
    changes = [i for i, (a,b) in enumerate(zip(original, modified)) if a != b]
    if not changes or not set(changes) <= allowed:
        raise RuntimeError("Unexpected RDRAM changes")
    target = output / "textless-source"
    target.mkdir()
    (target / "latest-gfx-rdram.bin").write_bytes(modified)
    (target / "latest-gfx-task.bin").write_bytes(task)
    return {"source": str(SOURCE), "input_sha256": IDENTITY, "removed_glyphs": removed,
            "changed_bytes": len(changes), "changed_only_reviewed_commands": True,
            "output_sha256": {name: digest(target/name) for name in IDENTITY}}


def blocks(grey: float, reflow: bool) -> list[dict]:
    data = json.loads((ROOT / "content/locales/zh-Hans.json").read_text())
    rows = {row["key"].removeprefix("base:"): row["target"] for row in data["entries"]}
    top = rows["t00_17412"].split("<STOP>")[1]
    bottom = rows["t00_17411"].split("<STOP>")[-1].removesuffix("<END>")
    if top != "同样至关重要。<BR>别人拿不到的情报，" or bottom != "还有人在痛苦和怨恨中<BR>不断死去啊！？”":
        raise RuntimeError("Translation no longer matches the selected captured scene")
    separator = "" if reflow else "\n"
    return [
        {"id": "lawrence-name", "text": "劳伦斯", "bounds": [366, 75, 528, 56], "grey": 1.0},
        {"id": "t00_17412-stop-1", "text": top.replace("<BR>", separator), "bounds": [357, 121, 531, 108], "grey": 1.0},
        {"id": "manami-name", "text": "玛娜米", "bounds": [78, 513, 522, 56], "grey": grey},
        {"id": "t00_17411-stop-3", "text": bottom.replace("<BR>", separator), "bounds": [69, 559, 531, 105], "grey": grey},
    ]


def render(output: Path, name: str, source: Path, settings: dict | None) -> dict:
    directory = output / name
    environment = {k:v for k,v in os.environ.items() if not k.startswith("SRW64_")}
    environment.update(SRW64_BACKGROUND="1", SRW64_NATIVE_RESOLUTION="1",
                       SRW64_RESOLUTION_SCALE="4", SRW64_FONT_PACK=str(PACK))
    target = "srw64-frame-host"
    if settings:
        config_path = output / (name + ".json")
        write_json(config_path, settings)
        environment["SRW64_CORETEXT_CONFIG"] = str(config_path)
        target = "srw64-coretext-frame-host"
    binary = ROOT / "build/recomp/gfx-build" / target
    command = [str(binary), str(source), str(directory)]
    with (output / (name + ".log")).open("x") as log:
        result = subprocess.run(command, env=environment, stdout=log, stderr=subprocess.STDOUT, timeout=60)
    if result.returncode or not (directory/"present-60.png").exists():
        raise RuntimeError(f"{name} failed with {result.returncode}; see its log")
    metadata = json.loads((directory/"present-60.json").read_text())
    if metadata.get("GPU_completion") != "completed" or "replay_iteration" not in metadata:
        raise RuntimeError("Missing completed renderer-replay evidence")
    report = {"command": command, "binary_sha256": digest(binary), "settings": settings,
              "vi_replay": json.loads((directory/"frame-replay.json").read_text()),
              "frame_sha256": digest(directory/"present-60.png"), "metadata": metadata,
              "log_sha256": digest(output/(name+".log")), "exit_code": result.returncode}
    if settings:
        raster = json.loads((directory/"coretext-raster.json").read_text())
        report["raster"] = raster
        report["font_file_sha256"] = digest(Path(raster["font_file"]))
    write_json(directory/"report.json", report)
    print(json.dumps({"rendered": name, "size": [metadata["width"], metadata["height"]]}), flush=True)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    if digest(FONT) != "6ed1553edccddc48eb27ff25d134a4a715cf54211238d4840b3038576cba1944":
        raise RuntimeError("HarmonyOS font differs from the reviewed font")
    source = prepare_source(output)
    write_json(output/"source.json", source)
    with (output/"build.log").open("x") as log:
        subprocess.run(["cmake", "--build", str(ROOT/"build/recomp/gfx-build"), "--target",
                        "srw64-coretext-frame-host", "srw64-frame-host", "-j", "6"],
                       check=True, stdout=log, stderr=subprocess.STDOUT)
    reports = {}
    reports["baseline"] = render(output, "baseline", SOURCE, None)
    reports["textless"] = render(output, "textless", output/"textless-source", None)
    with Image.open(output/"baseline/present-60.png") as frame:
        region = frame.convert("RGB").crop((70,560,570,652))
        grey_byte = Counter(r for r,g,b in region.get_flattened_data()
                            if r == g == b and r > 70).most_common(1)[0][0]
    for name, font, size, reflow in [
        ("harmony-39", "HarmonyOS_Sans_SC_Medium", 39, False),
        ("system-39", "PingFangSC-Medium", 39, False),
        ("system-33-wrap", "PingFangSC-Medium", 33, True),
        ("system-45-wrap", "PingFangSC-Medium", 45, True),
    ]:
        settings = {"schema": "srw64.coretext-probe.v1", "font_name": font,
                    "font_size_px": size, "line_height_px": 51 if size > 39 else 48,
                    "blocks": blocks(grey_byte/255, reflow), "reflow": reflow}
        if name.startswith("harmony"):
            settings["font_file"] = str(FONT)
        reports[name] = render(output, name, output/"textless-source", settings)
    with Image.open(output/"baseline/present-60.png") as image:
        dimensions = image.size
        baseline = list(image.convert("RGBA").get_flattened_data())
    with Image.open(output/"textless/present-60.png") as image:
        textless = list(image.convert("RGBA").get_flattened_data())
    checks = {}
    for name in reports:
        with Image.open(output/name/"present-60.png") as image:
            if image.size != dimensions:
                raise RuntimeError("Probe frame dimensions changed")
            frame = list(image.convert("RGBA").get_flattened_data())
        changed = [i for i,(a,b) in enumerate(zip(frame,baseline)) if a != b]
        def in_text(i: int) -> bool:
            x,y = i % dimensions[0], i // dimensions[0]
            return (346 <= x < 895 and 68 <= y < 234) or (62 <= x < 605 and 504 <= y < 666)
        outside = sum(not in_text(i) for i in changed)
        checks[name] = {"different_pixels": len(changed), "outside_text_regions": outside}
        if outside:
            raise RuntimeError(f"{name}: {outside} non-text pixels changed")
        if name not in ("baseline", "textless"):
            bgra = (output/name/"coretext-overlay.bgra").read_bytes()
            if len(bgra) != len(frame)*4:
                raise RuntimeError("Core Text overlay byte size mismatch")
            maximum_error = 0
            for i,(background,rendered) in enumerate(zip(textless,frame)):
                offset=i*4
                factor=1-bgra[offset+3]/255
                for channel,source_channel in enumerate((2,1,0)):
                    expected=round(bgra[offset+source_channel]+background[channel]*factor)
                    maximum_error=max(maximum_error,abs(rendered[channel]-expected))
            checks[name]["max_premultiplied_blend_error"] = maximum_error
            if maximum_error > 2:
                raise RuntimeError(f"{name}: GPU composition differs from premultiplied Alpha expectation")
    for name, expected in IDENTITY.items():
        if digest(SOURCE/name) != expected:
            raise RuntimeError("Original captured scene was modified")
    report = {"schema": "srw64.coretext-probe-acceptance.v1", "status": "passed",
              "evidence_scope": "same captured task; renderer replay only; manually bound visible dialogue segments",
              "source": source, "platform": platform.platform(), "inactive_foreground_byte": grey_byte,
              "pack_manifest_sha256": digest(PACK/"rt64.json"),
              "source_sha256": {str(p.relative_to(ROOT)): digest(p) for p in [Path(__file__).resolve(),
                   ROOT/"src/host/coretext_probe.cpp", ROOT/"src/host/coretext_probe.hpp",
                   ROOT/"src/host/graphics.cpp", ROOT/"src/host/frame.cpp",
                   ROOT/"src/host/replay_vi.hpp",
                   ROOT/"src/host/CMakeLists.txt"]},
              "variants": reports, "checks": checks}
    write_json(output/"acceptance.json", report)
    print(json.dumps({"status": "passed", "output": str(output), "checks": checks}), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
