#!/usr/bin/env python3
"""Offline Android touch-layout review. Compiles historical headers, never runs the game.

The HTML ports touch_sync's geometry/font sizing to browser CSS. This is design
evidence, not an RmlUi screenshot or device-input test. No external dependencies
other than a C++20 compiler; optional archived game screenshots remain local.
"""
from __future__ import annotations

import argparse
import copy
import json
import math
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
VERSIONS = [
    ("4dd963d", "10/03 · 初版固定手柄", "原始 N64 名称；L1 可回看，跳过要组合键。", "original"),
    ("89e1a9b", "10/03 · 按场景精简", "对白：自动、快进、跳过、回看都有；其他场景后来暴露缺键。", "scene"),
    ("b332a65", "10/08 · 恢复固定按钮", "完整一套，L1 可回看；自动/快进仍标 L2/R2。", "fixed"),
    ("f9e3ce2", "10/09 · 上排功能按钮", "上排变跳过、自动、快进；圆形 L1 保留，回看仍可达。", "fixed"),
    ("8fab028", "10/09 · 改动前基线", "上排恢复，圆形 L1/R1 变自动/按住快进；L 入口消失。", "current"),
]
PRESETS = {"wide": (61 * 2670 / 1200, 61), "16:9": (61 * 16 / 9, 61), "4:3": (61 * 4 / 3, 61), "small": (55 * 16 / 9, 55)}


def git(ref: str, file: str) -> str:
    if ref == "WORKTREE":
        return (ROOT / file).read_text()
    return subprocess.check_output(["git", "show", f"{ref}:{file}"], cwd=ROOT, text=True)


def dump(ref: str, kind: str, folder: Path, reading: str = "Active") -> dict:
    folder.mkdir()
    for name in ("touch_pad.hpp", "input_mode.hpp"):
        (folder / name).write_text(git(ref, f"src/host/{name}"))
    start = r'''#include "touch_pad.hpp"
#include <iostream>
#include <iomanip>
#include <cstdlib>
using namespace srw64::touch_pad;
int main(int argc,char**argv){auto l=layout(std::atof(argv[1]),std::atof(argv[2]),1);std::cout<<"{\"buttons\":[";bool first=true;
auto emit=[&](int id,float x,float y,float w,float h,bool round,uint32_t bits,std::string_view key){
if(!first)std::cout<<",";first=false;std::cout<<"{\"slot\":"<<id<<",\"x\":"<<x<<",\"y\":"<<y<<",\"w\":"<<w<<",\"h\":"<<h<<",\"round\":"<<(round?"true":"false")<<",\"bits\":"<<bits<<",\"key\":"<<std::quoted(std::string(key))<<"}";};
'''
    if kind == "original":
        body = r'''for(auto&p:l.shapes)if(p.control!=Control::DPad){float w=p.round?2*p.w:p.w,h=p.round?2*p.w:p.h;emit(int(p.control),p.round?p.x:p.x+w/2,p.round?p.y:p.y+h/2,w,h,p.round,mask(p.control),p.label);}
auto d=l[Control::DPad];std::cout<<"],\"stick\":1,\"cx\":"<<d.x<<",\"cy\":"<<d.y<<",\"radius\":"<<d.w<<"}";}
'''
    else:
        scene = "scene(SceneId::Dialogue)" if kind == "scene" else f"scene(SceneId::FixedDialogue,Reading::{reading})" if ref == "WORKTREE" else "scene(fixed(SceneId::Dialogue))"
        body = f"auto s={scene};" + r'''for(size_t i=0;i<slot_count;i++){auto&a=s.slots[i];auto&p=l.slots[i];if(a.filled())emit(int(i),p.x,p.y,p.w,p.h,p.round,a.bits,a.label);}
std::cout<<"],\"stick\":"<<int(s.stick)<<",\"cx\":"<<l.rest_x<<",\"cy\":"<<l.rest_y<<",\"radius\":"<<l.stick_radius<<"}";}
'''
    (folder / "dump.cpp").write_text(start + body)
    subprocess.run(["c++", "-std=c++20", "dump.cpp", "-o", "dump"], cwd=folder, check=True, capture_output=True)
    return {name: json.loads(subprocess.check_output([str(folder / "dump"), str(w), str(h)], text=True)) for name, (w, h) in PRESETS.items()}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/android/touch-ui-review-20261010")
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    data = {"presets": PRESETS, "versions": [], "head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()}
    with tempfile.TemporaryDirectory(prefix="srw64-touch-review-") as temp:
        for ref, title, note, kind in VERSIONS:
            data["versions"].append({"ref": ref, "title": title, "note": note, "kind": kind,
                "layouts": dump(ref, kind, Path(temp) / ref),
                "locales": {lang: json.loads(git(ref, f"content/locales/{lang}.json"))["ui"] for lang in ("zh-Hans", "ja", "en")}})
        data["adopted"] = []
        for state, title, note in (("Active", "已采用 · 正常对白", "原返回位改为回看；上排和其余圆键保持原样。"),
                                   ("History", "已采用 · 回看打开", "原位置变返回；收起自动、按住快进、跳过及重复触发键。"),
                                   ("Skipping", "已采用 · 正在跳过", "原位置显示停止跳过，发送 B 取消；停止后回到回看。")):
            data["adopted"].append({"ref": "工作区实现", "title": title, "note": note, "kind": "current", "state": state,
                "layouts": dump("WORKTREE", "current", Path(temp) / state, state),
                "locales": {lang: json.loads(git("WORKTREE", f"content/locales/{lang}.json"))["ui"] for lang in ("zh-Hans", "ja", "en")}})
    for name in ("HarmonyOS_Sans_SC.ttf", "HarmonyOS_Sans_Condensed.ttf", "SRW64Symbols.ttf"):
        shutil.copyfile(ROOT / "content/fonts" / name, out / name)
    # Copy originals without editing; their old keyboard hints remain visible.
    for source, target in (("build/portable-source-check/docs/media/02-dialogue-hd.png", "dialogue-archive.png"),
                           ("build/portable-source-check/docs/media/05-history-zh.png", "history-archive.png"),
                           ("build/android/dialogue-touch-qa-20261009/hold-icon-zh-Hans.png", "device-reference.png")):
        if (ROOT / source).is_file():
            shutil.copyfile(ROOT / source, out / target)
    (out / "source-layouts.json").write_text(json.dumps(data, ensure_ascii=False, indent=2))
    template = Path(__file__).with_suffix(".html").read_text()
    (out / "index.html").write_text(template.replace("/*SOURCE_DATA*/", "const DATA=" + json.dumps(data, ensure_ascii=False) + ";"))
    audit = []
    for v in data["versions"]:
        bs = v["layouts"]["wide"]["buttons"]
        counts = {name: sum(b["bits"] == mask for b in bs) for name, mask in {"history": 0x20, "auto": 1 << 21, "hold_fast": 1 << 22, "single_skip": 0x1010}.items()}
        audit.append({"ref": v["ref"], "buttons": len(bs), **counts})
    (out / "coverage.json").write_text(json.dumps(audit, ensure_ascii=False, indent=2))
    circles = [b for b in data["versions"][-1]["layouts"]["wide"]["buttons"] if b["round"]]
    auto = next(b for b in circles if b["key"] == "touch_auto")
    skip = next(b for b in circles if b["key"] == "touch_skip")
    fast = next(b for b in circles if b["key"] == "touch_hold_fast")
    proposed = copy.deepcopy(auto)
    proposed.update(x=auto["x"] - 1, y=auto["y"] - 13.2, key="touch_history")
    naive = copy.deepcopy(auto)
    naive["y"] -= 12.2

    def gap(a: dict, b: dict) -> float:
        return round(math.hypot(a["x"] - b["x"], a["y"] - b["y"]) - (a["w"] + b["w"]) / 2 - 3, 4)

    geometry = {"unit": "mm", "slack_each": 1.5,
        "naive_history_skip_touch_gap": gap(naive, skip),
        "proposed_history_touch_gaps": {b["key"]: gap(proposed, b) for b in circles},
        "existing_auto_fast_touch_gap": gap(auto, fast)}
    (out / "geometry.json").write_text(json.dumps(geometry, ensure_ascii=False, indent=2))
    print(json.dumps({"page": str(out / "index.html"), "coverage": audit}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
