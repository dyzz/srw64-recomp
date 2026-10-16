#!/usr/bin/env python3
"""The game's use of the N64 graphics pipeline, from the counts src/host/gbi_stats.cpp writes
(tools/recomp/debug/check_gbi_usage.py runs the scenes): combiner settings, other modes,
texture formats and draw counts decoded and tabulated for docs/data/gbi-usage.md.

  gbi_usage.py [DIR] [--markdown FILE] [--json FILE]

DIR holds one <scene>.json per scene (build/recomp/gbi-usage). The Markdown goes to stdout
unless --markdown names a file; --json writes the merged, decoded counts.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

CC_A = {0: "COMBINED", 1: "TEXEL0", 2: "TEXEL1", 3: "PRIMITIVE", 4: "SHADE", 5: "ENVIRONMENT", 6: "1", 7: "NOISE"}
CC_B = {0: "COMBINED", 1: "TEXEL0", 2: "TEXEL1", 3: "PRIMITIVE", 4: "SHADE", 5: "ENVIRONMENT", 6: "CENTER", 7: "K4"}
CC_C = {0: "COMBINED", 1: "TEXEL0", 2: "TEXEL1", 3: "PRIMITIVE", 4: "SHADE", 5: "ENVIRONMENT", 6: "SCALE",
        7: "COMBINED_ALPHA", 8: "TEXEL0_ALPHA", 9: "TEXEL1_ALPHA", 10: "PRIMITIVE_ALPHA", 11: "SHADE_ALPHA",
        12: "ENV_ALPHA", 13: "LOD_FRACTION", 14: "PRIM_LOD_FRAC", 15: "K5"}
CC_D = {0: "COMBINED", 1: "TEXEL0", 2: "TEXEL1", 3: "PRIMITIVE", 4: "SHADE", 5: "ENVIRONMENT", 6: "1", 7: "0"}
AC_ABD = {0: "COMBINED", 1: "TEXEL0", 2: "TEXEL1", 3: "PRIMITIVE", 4: "SHADE", 5: "ENVIRONMENT", 6: "1", 7: "0"}
AC_C = {0: "LOD_FRACTION", 1: "TEXEL0", 2: "TEXEL1", 3: "PRIMITIVE", 4: "SHADE", 5: "ENVIRONMENT", 6: "PRIM_LOD_FRAC", 7: "0"}
FORMATS = {0: "RGBA", 1: "YUV", 2: "CI", 3: "IA", 4: "I"}
SIZES = {0: "4", 1: "8", 2: "16", 3: "32"}
CYCLE = {0: "1CYCLE", 1: "2CYCLE", 2: "COPY", 3: "FILL"}
FILTER = {0: "POINT", 2: "BILERP", 3: "AVERAGE"}
LUT = {0: "NONE", 2: "RGBA16", 3: "IA16"}
ZMODE = {0: "OPA", 1: "INTER", 2: "XLU", 3: "DEC"}
CVG_DST = {0: "CLAMP", 1: "WRAP", 2: "FULL", 3: "SAVE"}
ALPHA_COMPARE = {0: "NONE", 1: "THRESHOLD", 3: "DITHER"}
BL_P = {0: "CLR_IN", 1: "CLR_MEM", 2: "CLR_BL", 3: "CLR_FOG"}
BL_A = {0: "A_IN", 1: "A_FOG", 2: "A_SHADE", 3: "0"}
BL_B = {0: "1MA", 1: "A_MEM", 2: "1", 3: "0"}
GEOMETRY = {0x1: "ZBUFFER", 0x4: "SHADE", 0x200: "CULL_FRONT", 0x400: "CULL_BACK", 0x10000: "FOG", 0x20000: "LIGHTING",
            0x40000: "TEXTURE_GEN", 0x80000: "TEXTURE_GEN_LINEAR", 0x200000: "SHADING_SMOOTH", 0x800000: "CLIPPING"}
MOVEWORD = {0: "MATRIX", 2: "NUMLIGHT", 4: "CLIP", 6: "SEGMENT", 8: "FOG", 10: "LIGHTCOL", 12: "FORCEMTX", 14: "PERSPNORM"}
MOVEMEM = {8: "VIEWPORT", 10: "LIGHT", 14: "MATRIX"}
EXTENDED = {0: "NOOP", 1: "PRINT", 2: "TEXRECT", 3: "FILLRECT", 4: "SETVIEWPORT", 5: "SETSCISSOR", 6: "SETRECTALIGN",
            7: "SETVIEWPORTALIGN", 8: "SETSCISSORALIGN", 9: "SETREFRESHRATE", 0x17: "PUSHSCISSOR", 0x18: "POPSCISSOR",
            0x33: "SETRECTASPECT"}


def decode_combine(key: str) -> dict:
    w0, w1 = (int(part, 16) for part in key.split())
    a0, c0, aa0, ac0, a1, c1 = (w0 >> 20) & 0xF, (w0 >> 15) & 0x1F, (w0 >> 12) & 7, (w0 >> 9) & 7, (w0 >> 5) & 0xF, w0 & 0x1F
    b0, b1, aa1, ac1, d0, ab0, ad0, d1, ab1, ad1 = ((w1 >> 28) & 0xF, (w1 >> 24) & 0xF, (w1 >> 21) & 7, (w1 >> 18) & 7, (w1 >> 15) & 7,
                                                    (w1 >> 12) & 7, (w1 >> 9) & 7, (w1 >> 6) & 7, (w1 >> 3) & 7, w1 & 7)
    def cc(a, b, c, d):
        return f"({CC_A.get(a, '0')} - {CC_B.get(b, '0')}) * {CC_C.get(c, '0')} + {CC_D.get(d, '0')}"
    def ac(a, b, c, d):
        return f"({AC_ABD[a]} - {AC_ABD[b]}) * {AC_C[c]} + {AC_ABD[d]}"
    return {"cycle1": {"color": cc(a0, b0, c0, d0), "alpha": ac(aa0, ab0, ac0, ad0)},
            "cycle2": {"color": cc(a1, b1, c1, d1), "alpha": ac(aa1, ab1, ac1, ad1)}}


def decode_othermode_h(h: int) -> dict:
    return {"cycle": CYCLE[(h >> 20) & 3], "filter": FILTER.get((h >> 12) & 3, str((h >> 12) & 3)),
            "lut": LUT.get((h >> 14) & 3, str((h >> 14) & 3)), "persp": bool(h & (1 << 19)),
            "lod": bool(h & (1 << 16)), "detail": (h >> 17) & 3, "rgb_dither": (h >> 6) & 3, "alpha_dither": (h >> 4) & 3,
            "key": bool(h & (1 << 8)), "convert": (h >> 9) & 7}


def decode_othermode_l(l: int) -> dict:
    flags = []
    for bit, name in ((0x8, "AA_EN"), (0x10, "Z_CMP"), (0x20, "Z_UPD"), (0x40, "IM_RD"), (0x80, "CLR_ON_CVG"),
                      (0x1000, "CVG_X_ALPHA"), (0x2000, "ALPHA_CVG_SEL"), (0x4000, "FORCE_BL")):
        if l & bit:
            flags.append(name)
    blender = []
    for cycle, (p, a, m, b) in enumerate(((30, 26, 22, 18), (28, 24, 20, 16))):
        blender.append(f"{BL_P[(l >> p) & 3]}*{BL_A[(l >> a) & 3]} + {BL_P[(l >> m) & 3]}*{BL_B[(l >> b) & 3]}")
    return {"alpha_compare": ALPHA_COMPARE.get(l & 3, str(l & 3)), "z_source": "PRIM" if l & 4 else "PIXEL",
            "flags": flags, "cvg_dst": CVG_DST[(l >> 8) & 3], "zmode": ZMODE[(l >> 10) & 3], "blender": blender}


def decode_geometry(bits: int) -> list:
    return [name for bit, name in GEOMETRY.items() if bits & bit] + ([f"other {bits & ~sum(GEOMETRY):#x}"] if bits & ~sum(GEOMETRY) else [])


def merge(scenes: dict) -> dict:
    """Counts summed over the scenes, each key also remembering which scenes used it."""
    total = {"lists": 0, "commands": 0, "draws": {}, "opcodes": {}, "extended": {}, "combine": {}, "othermode": {},
             "draw_states": {}, "image_formats": {}, "tile_formats": {}, "color_images": {}, "tile_sizes": {},
             "tlut_sizes": {}, "moveword": {}, "movemem": {}, "mtx_params": {}, "texture_tiles": {}, "vtx_counts": {},
             "geometry_set": 0, "geometry_cleared": 0, "branch_z": 0, "ucode_loads": 0, "truncated_lists": 0,
             "unknown_opcodes": 0, "unknown_extended": 0, "deepest_call": 0, "used_by": {}}
    for scene, data in scenes.items():
        for name in ("lists", "commands", "branch_z", "ucode_loads", "truncated_lists", "unknown_opcodes", "unknown_extended"):
            total[name] += data.get(name, 0)
        total["deepest_call"] = max(total["deepest_call"], data.get("deepest_call", 0))
        for name, count in data.get("draws", {}).items():
            total["draws"][name] = total["draws"].get(name, 0) + count
        for table in ("opcodes", "extended", "combine", "othermode", "draw_states", "image_formats", "tile_formats",
                      "color_images", "tile_sizes", "tlut_sizes", "moveword", "movemem", "mtx_params", "texture_tiles", "vtx_counts"):
            for key, count in data.get(table, {}).items():
                total[table][key] = total[table].get(key, 0) + count
                total["used_by"].setdefault(table, {}).setdefault(key, []).append(scene)
        total["geometry_set"] |= int(data["geometry_mode"]["set"], 16)
        total["geometry_cleared"] |= int(data["geometry_mode"]["cleared"], 16)
    return total


def markdown(scenes: dict, total: dict) -> str:
    out = []
    def table(headers, rows):
        out.append("| " + " | ".join(headers) + " |")
        out.append("| " + " | ".join("---" for _ in headers) + " |")
        for row in rows:
            out.append("| " + " | ".join(str(cell) for cell in row) + " |")
        out.append("")
    def scenes_of(table_name, key):
        return ", ".join(total["used_by"].get(table_name, {}).get(key, []))
    out.append("### 各场景的显示列表")
    out.append("")
    rows = []
    for scene, data in scenes.items():
        per = data["per_list"]
        draws = data["draws"]
        rows.append([scene, data["lists"], data["commands"], f"{per['draws']['mean']:.0f} / {per['draws']['p90']} / {per['draws']['max']}",
                     f"{per['triangles']['mean']:.0f} / {per['triangles']['max']}", draws["triangles"], draws["texture_rectangles"],
                     draws["fill_rectangles"], draws["vertices"], data["deepest_call"]])
    table(["场景", "显示列表数", "指令数", "每帧绘制调用 均值 / p90 / 最大", "每帧三角形 均值 / 最大", "三角形", "纹理矩形", "填充矩形", "顶点", "G_DL 最深"], rows)
    out.append("### 指令")
    out.append("")
    rows = sorted(total["opcodes"].items(), key=lambda item: -item[1])
    table(["指令", "次数", "场景"], [[name, count, scenes_of("opcodes", name)] for name, count in rows])
    if total["extended"]:
        out.append("宿主插入的 RT64 扩展指令（0x64，宽屏用）：")
        out.append("")
        table(["扩展指令", "次数", "场景"], [[EXTENDED.get(int(key, 16), key), count, scenes_of("extended", key)]
                                           for key, count in sorted(total["extended"].items(), key=lambda item: -item[1])])
    out.append("### 组合器")
    out.append("")
    out.append(f"G_SETCOMBINE 的不同取值：{len(total['combine'])} 种；绘制时 (othermode_h, othermode_l, combine, geometry) 的不同组合：{len(total['draw_states'])} 种。")
    out.append("")
    rows = []
    for key, count in sorted(total["combine"].items(), key=lambda item: -item[1]):
        d = decode_combine(key)
        rows.append([key, count, scenes_of("combine", key), d["cycle1"]["color"], d["cycle1"]["alpha"], d["cycle2"]["color"], d["cycle2"]["alpha"]])
    table(["w0 w1", "次数", "场景", "周期 1 颜色", "周期 1 透明", "周期 2 颜色", "周期 2 透明"], rows)
    out.append("### RDP 模式（绘制时的 othermode）")
    out.append("")
    rows = []
    for key, count in sorted(total["othermode"].items(), key=lambda item: -item[1]):
        h, l = (int(part, 16) for part in key.split())
        dh, dl = decode_othermode_h(h), decode_othermode_l(l)
        rows.append([key, count, scenes_of("othermode", key), dh["cycle"], dh["filter"], dh["lut"], "是" if dh["persp"] else "否",
                     dl["alpha_compare"], " ".join(dl["flags"]) or "-", dl["zmode"], dl["cvg_dst"], "; ".join(dl["blender"])])
    table(["h l", "次数", "场景", "周期", "滤波", "调色板", "透视", "Alpha 比较", "渲染模式标志", "Z 模式", "覆盖目标", "混合器 周期1; 周期2"], rows)
    out.append("### 贴图")
    out.append("")
    def fmt(key):
        f, s = (int(p) for p in key.split()[:2])
        return f"{FORMATS.get(f, f)}{SIZES.get(s, s)}"
    table(["G_SETTIMG 格式", "次数", "场景"], [[fmt(k), c, scenes_of("image_formats", k)] for k, c in sorted(total["image_formats"].items(), key=lambda i: -i[1])])
    table(["G_SETTILE 格式", "次数", "场景"], [[fmt(k), c, scenes_of("tile_formats", k)] for k, c in sorted(total["tile_formats"].items(), key=lambda i: -i[1])])
    table(["G_SETCIMG 格式 宽", "次数", "场景"], [[f"{fmt(k)} {k.split()[2]}", c, scenes_of("color_images", k)] for k, c in sorted(total["color_images"].items(), key=lambda i: -i[1])])
    table(["G_LOADTLUT 颜色数", "次数"], [[k, c] for k, c in sorted(total["tlut_sizes"].items(), key=lambda i: -i[1])])
    sizes = sorted(total["tile_sizes"].items(), key=lambda i: -i[1])
    table(["G_SETTILESIZE 宽×高（前 20）", "次数"], [[k.replace(" ", "×"), c] for k, c in sizes[:20]])
    widest = max((int(k.split()[0]) for k in total["tile_sizes"]), default=0)
    tallest = max((int(k.split()[1]) for k in total["tile_sizes"]), default=0)
    out.append(f"贴图块尺寸共 {len(sizes)} 种，最宽 {widest}，最高 {tallest}。")
    out.append("")
    out.append("### RSP 状态")
    out.append("")
    table(["几何模式位", "曾置位", "曾清除"], [[name, "是" if total["geometry_set"] & bit else "否", "是" if total["geometry_cleared"] & bit else "否"]
                                           for bit, name in GEOMETRY.items()])
    table(["G_MOVEWORD", "次数"], [[MOVEWORD.get(int(k, 16), k), c] for k, c in sorted(total["moveword"].items(), key=lambda i: -i[1])])
    table(["G_MOVEMEM", "次数"], [[MOVEMEM.get(int(k, 16), k), c] for k, c in sorted(total["movemem"].items(), key=lambda i: -i[1])])
    def mtx(k):
        p = int(k, 16)
        return ("PROJECTION" if p & 4 else "MODELVIEW") + (" LOAD" if p & 2 else " MUL") + (" PUSH" if p & 1 else "")
    table(["G_MTX 参数", "次数"], [[mtx(k), c] for k, c in sorted(total["mtx_params"].items(), key=lambda i: -i[1])])
    table(["G_VTX 顶点数", "次数"], [[int(k, 16), c] for k, c in sorted(total["vtx_counts"].items(), key=lambda i: -i[1])])
    table(["G_TEXTURE 层级", "次数"], [[int(k, 16), c] for k, c in sorted(total["texture_tiles"].items(), key=lambda i: -i[1])])
    out.append(f"G_BRANCH_Z：{total['branch_z']} 次（统计时不跟随分支）；G_LOAD_UCODE：{total['ucode_loads']} 次；"
               f"未识别指令：{total['unknown_opcodes']}；未识别扩展指令：{total['unknown_extended']}；被截断的列表：{total['truncated_lists']}。")
    out.append("")
    return "\n".join(out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("directory", nargs="?", type=Path, default=ROOT / "build/recomp/gbi-usage")
    parser.add_argument("--markdown", type=Path)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    scenes = {}
    for path in sorted(args.directory.glob("*.json")):
        if path.name in ("runs.json", "summary.json"):
            continue
        data = json.loads(path.read_text())
        if data.get("schema") == "srw64.gbi-stats.v1":
            scenes[path.stem] = data
    if not scenes:
        raise SystemExit(f"no gbi-stats files in {args.directory}")
    total = merge(scenes)
    text = markdown(scenes, total)
    if args.markdown:
        args.markdown.write_text(text)
    else:
        print(text)
    if args.json:
        decoded = {"scenes": list(scenes), "total": total,
                   "combine_decoded": {key: decode_combine(key) for key in total["combine"]},
                   "othermode_decoded": {key: {"h": decode_othermode_h(int(key.split()[0], 16)), "l": decode_othermode_l(int(key.split()[1], 16))}
                                         for key in total["othermode"]},
                   "geometry_set": decode_geometry(total["geometry_set"])}
        args.json.write_text(json.dumps(decoded, ensure_ascii=False, indent=1) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
