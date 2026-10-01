#!/usr/bin/env python3
"""Single-source blending against RT64's dual-source path, on the Mac (docs/design/android-port.md).

Runs two prebuilt hosts, one built with -DRT64_SINGLE_SOURCE_BLEND=ON, through the
same boot: original images, Japanese, a 4:3 picture, no input, so the attract battle
plays. At each checkpoint VI it takes a burst of consecutive screenshots into
<out>/<name>/. The runs drift by a few VIs, so each single-source frame is matched with
the closest dual-source frame of the same burst: animation then cancels out and what
remains is the blending difference (summary.json; the worst pairs side by side).

  check_single_source_blend.py dual=<binary> single=<binary> --out DIR
"""
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, run_keys

args = sys.argv[1:]
out = Path(args[args.index("--out") + 1])
binaries = dict(a.split("=", 1) for a in args if "=" in a)
checkpoints = list(range(300, 2701, 150))
BURST = 10
for name, binary in binaries.items():
    folder = out / name
    folder.mkdir(parents=True, exist_ok=True)
    s = Session.launch(language="ja", images="original", binary=binary, env={"SRW64_ASPECT": "4:3"})
    print("RUN", name, s.run, flush=True)
    s.client.call("window", width=960, height=720)
    for vi in checkpoints:
        s.wait(vi=vi, timeout=300, poll=0.01)
        for shot in range(BURST):
            s.client.call("screenshot", path=str(folder / f"vi{vi:05d}-{shot:02d}.png"))
    s.quit()

from PIL import Image, ImageChops


def changed(a, b) -> tuple[int, int]:
    histogram = ImageChops.difference(a, b).convert("L").histogram()
    return sum(histogram[8:]), max(i for i, n in enumerate(histogram) if n)


names = list(binaries)
rows = []
for vi in checkpoints:
    first = [Image.open(p).convert("RGB") for p in sorted((out / names[0]).glob(f"vi{vi:05d}-*.png"))]
    for path in sorted((out / names[1]).glob(f"vi{vi:05d}-*.png")):
        image = Image.open(path).convert("RGB")
        best = min((changed(image, other) + (index,) for index, other in enumerate(first)), key=lambda r: r[0])
        rows.append({"shot": path.name, "match": best[2], "changed_pixels": best[0], "max": best[1],
                     "share": round(best[0] / (image.size[0] * image.size[1]), 5)})
(out / "summary.json").write_text(json.dumps(rows, indent=2) + "\n")
per_vi = {}
for row in rows:
    vi = row["shot"][:7]
    per_vi[vi] = min(per_vi.get(vi, 1 << 30), row["changed_pixels"])
for vi, best in per_vi.items():
    print(vi, "closest pair differs in", best, "pixels", flush=True)
