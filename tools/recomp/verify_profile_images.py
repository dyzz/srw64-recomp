#!/usr/bin/env python3
"""Verify original/HD round trips in a live standard-dialogue scene.

Requires a fresh run with dialogue-only.json and SRW64_WINDOW_CONTROL=1.
Only controller input and the same image-mode request used by F6 are used.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

from PIL import Image, ImageChops


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--start-vi", type=int, default=7100, help="minimum VI before seeking the comparison dialogue")
    parser.add_argument("--model-5600", action="store_true", help="also require Original/HD to select the original/waterdrop model")
    args = parser.parse_args()
    output = args.output.resolve()
    evidence = output / "profile-checks"
    evidence.mkdir(exist_ok=False)

    def read(name):
        try:
            return json.loads((output / name).read_text())
        except (FileNotFoundError, json.JSONDecodeError):
            return {}

    def wait(predicate, timeout=20):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            result = predicate()
            if result:
                return result
            if (output / "native-counters.json").exists():
                raise RuntimeError("Host exited during image verification")
            time.sleep(.05)
        raise TimeoutError(str(read("dialogue-state.json")))

    def active():
        return next((b for b in read("dialogue-state.json").get("boxes", []) if b["active"]), {})

    wait(lambda: read("live-state.json").get("vi", 0) > args.start_vi and active(), 160)
    for _ in range(40):
        box = active()
        if box.get("text_id") == 17412 and box.get("segment") == 1:
            break
        if box.get("text_id", 0) > 17412:
            raise RuntimeError("Script passed the target comparison scene")
        subprocess.run([sys.executable, str(Path(__file__).with_name("control_host.py")), str(output),
                        "--buttons", "a"], check=True, stdout=subprocess.DEVNULL)
        vi = read("live-state.json")["vi"]
        wait(lambda: read("live-state.json").get("vi", 0) >= vi + 60)
    else:
        raise RuntimeError("Could not reach comparison dialogue")
    vi = read("live-state.json")["vi"]
    wait(lambda: read("live-state.json").get("vi", 0) >= vi + 180)
    baseline = read("dialogue-state.json")
    captures = []
    for label, mode in (("original", "original"), ("hd", "hd"), ("original-return", "original"), ("hd-return", "hd")):
        request = output / "image-control.pending"
        request.write_text(f"SRWI1 {time.time_ns()} {mode}\n")
        request.replace(output / "image-control.txt")
        wait(lambda: read("image-mode.json").get("mode") == mode)
        minimum = read("live-state.json")["vi"] + 120

        def frame():
            candidates = sorted(output.glob("present-*.json"), key=lambda p: int(p.stem.split("-")[-1]), reverse=True)
            for path in candidates[:2]:
                data = read(path.name)
                if (data.get("native_vi_at_draw", 0) >= minimum and data.get("image_mode") == int(mode == "hd")
                        and path.with_suffix(".png").exists()):
                    return path, data
            return None

        path, gpu = wait(frame)
        applied = read("image-mode.json")
        if args.model_5600 and applied.get("model_5600") != ("waterdrop" if mode == "hd" else "original"):
            raise RuntimeError("5600 model did not follow the image-mode request")
        state = read("dialogue-state.json")
        for key in ("event", "owner", "page", "revealed_utf16", "boxes"):
            if state[key] != baseline[key]:
                raise RuntimeError(f"Image toggle changed dialogue state: {key}")
        raster = read("dialogue-raster.json")
        if not raster.get("blocks"):
            raise RuntimeError("Native text did not reach the raster backend")
        target = evidence / (label + ".png")
        shutil.copyfile(path.with_suffix(".png"), target)
        captures.append({"image": str(target), "GPU": gpu, "state": state, "raster": raster, "applied_mode": applied})
        print(json.dumps({"captured": label, "locale": raster["locale"], "vi": gpu["native_vi_at_draw"]}), flush=True)
    images = [Image.open(c["image"]).convert("RGB") for c in captures]
    if any(im.size != (960, 720) for im in images):
        raise RuntimeError("Comparison ROIs require the documented 960x720 drawable")
    # Static portrait/map crops exclude the animated marker and reading UI.
    regions = {"portrait": (48, 45, 330, 335), "map": (0, 0, 100, 45)}
    comparisons = {}
    for name, box in regions.items():
        crops = [im.crop(box) for im in images]
        changed = sum(any(pixel) for pixel in ImageChops.difference(crops[0], crops[1]).getdata())
        if changed < 100:
            raise RuntimeError(f"HD mode did not change {name} pixels")
        if ImageChops.difference(crops[0], crops[2]).getbbox() or ImageChops.difference(crops[1], crops[3]).getbbox():
            raise RuntimeError(f"Image round trip did not restore {name} pixels exactly")
        comparisons[name] = {"ROI": box, "changed_pixels": changed, "original_return_exact": True, "hd_return_exact": True}
    result = {"schema": "srw64.profile-live-check.v1", "status": "passed", "scope": "actual guest execution and completed GPU frames",
              "model_5600_follows_mode": args.model_5600,
              "locale": captures[0]["raster"]["locale"], "captures": captures, "comparisons": comparisons}
    (evidence / "acceptance.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    subprocess.run([sys.executable, str(Path(__file__).with_name("control_host.py")), str(output), "--quit"], check=True, stdout=subprocess.DEVNULL)


if __name__ == "__main__":
    main()
