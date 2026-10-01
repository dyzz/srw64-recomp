#!/usr/bin/env python3
"""Every kind of battle background with the widened picture (docs/design/deck-16x10.md §6
item 13).

The background is a record of the table at ROM 0x5BC30 (38 bytes: flags and a tint, up to
three 3D models, a 2D picture and palette), record block * 101 + terrain, the block from
the map's environment through 800C5940. One record per class (each 2D picture and type,
each pictureless tint, each tinted variant) is forced in a battle-ui battle by writing the
two sides' terrain/environment bytes (800F97EA/B, the second side 0x1074 on) just before it
starts (debug memory.write), screenshot at 2.5 and 5 s and cut short with Z+START, three
battles per session. Then sheet-N.png contact sheets and edges.json, which names shots
with an edge entirely black (a background that is all dark reads the same: look).
Usage: check_battle_backgrounds.py OUT_DIR [--window W H]; a rerun keeps finished shots."""
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session
ROOT = Path(__file__).resolve().parents[3]
OUT = Path(sys.argv[1]); OUT.mkdir(parents=True, exist_ok=True)
window = tuple(int(v) for v in sys.argv[sys.argv.index("--window") + 1:][:2]) if "--window" in sys.argv else (1280, 800)
rom = (ROOT / "rom.z64").read_bytes()
BASE = 0x5BC30
ENV_BLOCK = bytes.fromhex("000102030405060000070809000A0B0C0D0E0F1011001213141516000000001" + "7")
block_env = {}
for env, block in enumerate(ENV_BLOCK):
    block_env.setdefault(block, env)
def ents(r): return [r[8 + i * 6:14 + i * 6] for i in range(4)]
def valid(r):
    for e in ents(r):
        if e[0] == 0:
            if any(e): return False
            continue
        if not 5500 <= int.from_bytes(e[2:4], "big") <= 6400: return False
    return r[0] in (0, 1)
# One record per class: sky picture (and its type), or for no picture its header.
chosen, seen = [], set()
for b in range(23):
    for t in range(101):
        r = rom[BASE + (b * 101 + t) * 38:BASE + (b * 101 + t) * 38 + 38]
        if not any(r) or not valid(r): continue
        s = ents(r)[3]
        key = ("sky", s[0], int.from_bytes(s[2:4], "big")) if s[0] else ("plain", r[:8].hex())
        if r[0] == 1: key = key + ("flag", r[1:5].hex())
        if key in seen or b not in block_env: continue
        seen.add(key); chosen.append({"block": b, "terrain": t, "env": block_env[b], "key": list(map(str, key))})
todo = [c for c in chosen if not (OUT / f"bg-{c['block']:02}-{c['terrain']:03}-a.png").exists()]
print(len(chosen), "classes,", len(todo), "to go", flush=True)
(OUT / "chosen.json").write_text(json.dumps(chosen, indent=1))

def session_run(batch):
    s = Session.launch(language="zh-Hans", images="hd", binary=str(ROOT / "build/recomp/gfx-build/srw64-gfx-host"),
                       mini_stage=str(ROOT / "config/recomp/mini-stages/battle-ui.json"))
    st = lambda: s.client.call("status")
    try:
        s.client.call("window", width=window[0], height=window[1])
        s.wait(vi=600, timeout=300)
        s.enter_mini_stage(); time.sleep(4)
        for key in ["up", "z", "z", "z"]:
            s.client.call("keys", press=key, hold_ms=100); time.sleep(0.8)
        for c in batch:
            end = time.monotonic() + 90
            while not st()["battle_page"].get("visible"):
                if time.monotonic() > end: print("no battle page", flush=True); return
                time.sleep(0.2)
            time.sleep(0.8)
            if not st()["battle_page"].get("animation", False):
                s.client.call("ui.click", id="battle-animation"); time.sleep(0.3)
            try: s.client.call("ui.click", id="battle-defend"); time.sleep(0.3)
            except Exception: pass
            for side in (0, 1):
                s.client.call("memory.write", address=0x800F97EA + side * 0x1074, hex=f"{c['terrain']:02X}{c['env']:02X}")
            s.client.call("ui.click", id="battle-confirm")
            name = f"bg-{c['block']:02}-{c['terrain']:03}"
            time.sleep(2.5); s.client.call("screenshot", path=str(OUT / f"{name}-a.png"))
            time.sleep(2.5); s.client.call("screenshot", path=str(OUT / f"{name}-b.png"))
            print("shot", name, c["key"], flush=True)
            s.client.call("keys", down="z"); time.sleep(0.1)
            s.client.call("keys", press="return", hold_ms=150); time.sleep(0.3)
            s.client.call("keys", release_all=True)
    finally:
        s.quit()

tries = {}
while todo:
    for c in todo[:3]: tries[id(c)] = tries.get(id(c), 0) + 1
    session_run(todo[:3])
    todo = [c for c in chosen if not (OUT / f"bg-{c['block']:02}-{c['terrain']:03}-a.png").exists() and tries.get(id(c), 0) < 2]

# The contact sheets.
from PIL import Image, ImageDraw, ImageStat
def edges(im):
    g = im.convert("L"); w, h = g.size; band = max(2, h // 60)
    def dark(box): return ImageStat.Stat(g.crop(box)).mean[0] < 6
    return {"top": dark((0, 0, w, band)), "bottom": dark((0, h - band, w, h)),
            "left": dark((0, 0, band, h)), "right": dark((w - band, 0, w, h))}
report, tiles = [], []
for c in chosen:
    name = f"bg-{c['block']:02}-{c['terrain']:03}"
    shots = [OUT / f"{name}-{k}.png" for k in "ab"]
    if not shots[0].exists(): report.append({"name": name, "missing": True}); continue
    flags = {k: edges(Image.open(p)) for k, p in zip("ab", shots) if p.exists()}
    bad = sorted({side for f in flags.values() for side, v in f.items() if v})
    report.append({"name": name, "key": c["key"], "dark_edges": bad})
    tiles.append((name, " ".join(c["key"]), bad, [Image.open(p) for p in shots if p.exists()]))
(OUT / "edges.json").write_text(json.dumps(report, indent=1))
tw, th = 256, 160; cols = 4; per = 24
for n in range(0, len(tiles), per):
    part = tiles[n:n + per]; rows = (len(part) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (2 * tw + 8), rows * (th + 16)), "white"); dr = ImageDraw.Draw(sheet)
    for i, (name, key, bad, ims) in enumerate(part):
        x, y = (i % cols) * (2 * tw + 8), (i // cols) * (th + 16)
        for j, im in enumerate(ims): sheet.paste(im.convert("RGB").resize((tw, th)), (x + j * tw, y + 16))
        dr.text((x + 2, y + 2), f"{name} {key}" + (f"  DARK {','.join(bad)}" if bad else ""), fill="red" if bad else "black")
    sheet.save(OUT / f"sheet-{n // per + 1}.png")
print(sum(1 for r in report if r.get("dark_edges")), "with dark edges;", sum(1 for r in report if r.get("missing")), "missing;", len(tiles), "shown")
print("DONE", OUT, flush=True)
