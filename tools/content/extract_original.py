#!/usr/bin/env python3
"""Generate a local, read-only original-data catalog and its browser assets."""
from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import asdict
import json
from pathlib import Path
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "src"))

from srw64_native.catalog import source_catalog, sha
from srw64_native.original_data import (check_layout, extract_gameplay, raw_record,
                                        snapshot_observation)
from srw64_native.original_profiles import attach_profiles
from srw64_native.original_abilities import attach_ability_catalog
from srw64_native.original_scripts import attach_script_catalog
from srw64_native.original_story import StoryBuilder, story_search_rows
from srw64_native.original_images import attach_images
from srw64_rom.baseline import load_baseline
from srw64_rom.resources import ResourceTable
from srw64_rom.text import parse_entries


def json_bytes(value) -> bytes:
    return (json.dumps(value, ensure_ascii=False, separators=(",", ":")) + "\n").encode()


def build(rom_path: Path, destination: Path, snapshot: Path | None = None) -> dict:
    destination = destination.resolve()
    build_root = (ROOT / "build").resolve()
    if not destination.is_relative_to(build_root) or destination == build_root:
        raise ValueError("Generated catalog must use its own directory under build/")
    if destination.exists():
        old_manifest = destination / "manifest.json"
        if not old_manifest.exists() or json.loads(old_manifest.read_text()).get("schema") != "srw64.original-catalog.v1":
            raise ValueError("Refusing to replace a directory that is not a generated catalog")
    rom = rom_path.read_bytes()
    layout_path = ROOT / "config/data/original-jp-v1.json"
    layout = json.loads(layout_path.read_text())
    check_layout(rom, layout)
    sources, hashes, _ = source_catalog(ROOT, rom_path)
    gameplay = extract_gameplay(rom, layout, sources)
    categories = dict(gameplay)
    categories["texts"] = []
    baseline = load_baseline(ROOT / "config/srw64-jp-rev0.json")
    for entry in parse_entries(rom, baseline.text_layout):
        key = "base:" + entry.key
        row = raw_record(key, sources[key], entry.data, entry.data_offset)
        row["text_ir"] = entry.to_record()
        if hashes[key] != row["source_sha256"] or rom[entry.data_offset:entry.data_offset+entry.byte_size] != entry.data:
            raise ValueError("Text byte roundtrip mismatch")
        categories["texts"].append(row)
    if snapshot:
        folder = snapshot.parent
        report = json.loads((folder / "report.json").read_text())
        scene = json.loads((folder / "scene-evidence.json").read_text())
        if report["rom_sha256"] != layout["rom_sha256"] or scene["scene"] != "female-route-first-tactical-map":
            raise ValueError("Snapshot must have original-ROM female first-map evidence")
        metadata = {"snapshot_path": str(snapshot.relative_to(ROOT)),
                    "report_path": str((folder / "report.json").relative_to(ROOT)),
                    "report_sha256": sha((folder / "report.json").read_bytes()),
                    "scene_evidence_sha256": sha((folder / "scene-evidence.json").read_bytes()),
                    "rom_sha256": report["rom_sha256"], "run_status": report["status"],
                    "run_exit_code": report["exit_code"], "scope": "historical snapshot; not a fresh runtime test"}
        categories["observations"] = [snapshot_observation(snapshot.read_bytes(), gameplay, metadata)]

    attach_profiles(categories, sources)
    ability_coverage = attach_ability_catalog(categories, rom, layout, sources)
    headers = {row["key"]: bytes.fromhex(row["text_ir"]["header_hex"]) for row in categories["texts"]}
    script_coverage = attach_script_catalog(categories, rom, layout, sources, headers)

    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".original-data-", dir=destination.parent) as temporary:
        output = Path(temporary)
        files = {}

        def write(path: str, data: bytes) -> None:
            target = output / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            files[path] = {"sha256": sha(data), "size": len(data)}

        for name, bank in layout["stage_scripts"]["banks"].items():
            start = bank["rom_offset"]
            write(f"raw/stage-{name}-bank.bin", rom[start:start + bank["byte_size"]])

        # Preserve compressed spans and their padding independently of decoded bytes.
        resources = ResourceTable(rom)
        categories["resources"] = []
        span_pack = bytearray()
        for index in range(resources.count):
            entry = resources.entry(index)
            decoded, consumed = resources.extract(index)
            span = rom[entry.data_offset:entry.data_offset+entry.span_size]
            row = raw_record(f"base:resources:{index:04d}", f"资源 {index}", span, entry.data_offset)
            row["descriptor"] = asdict(entry)
            row["raw_hex"] = span[:64].hex()
            row["raw_preview_only"] = True
            row["span_pack_offset"] = len(span_pack)
            row["span_pack_path"] = "raw/resource-spans.bin"
            row["decoded_sha256"] = sha(decoded)
            row["decoded_size"] = len(decoded)
            row["compressed_bytes_consumed"] = consumed
            row["decoded_file"] = f"raw/resources/{index:04d}.bin"
            row["fields"] = [{"name": "解压字节数", "value": len(decoded), "confidence": "structure-confirmed"},
                              {"name": "用途", "value": "尚未统一分类；编号不是资源类型", "confidence": "unknown"}]
            write(row["decoded_file"], decoded)
            span_pack.extend(span)
            categories["resources"].append(row)
        write("raw/resource-spans.bin", bytes(span_pack))
        image_coverage = attach_images(categories, rom, layout["images"],
            lambda rid: (output / f"raw/resources/{rid:04d}.bin").read_bytes(), write)
        # Portraits come from attach_images, so the story projection is built after it.
        story_index, story_documents = StoryBuilder(categories, sources, layout["stage_scripts"]).build()

        evidence = []
        for item in layout["evidence"]:
            row = raw_record("evidence:" + item["id"], item["finding"],
                             rom[item["rom_offset"]:item["rom_offset"]+item["byte_size"]],
                             item["rom_offset"], "code-confirmed")
            row["evidence_source"] = item
            evidence.append(row)
        categories["evidence"] = evidence
        all_keys = {row["key"] for rows in categories.values() for row in rows}
        if len(all_keys) != sum(len(rows) for rows in categories.values()):
            raise ValueError("Duplicate catalog identities")
        for rows in categories.values():
            for row in rows:
                for evidence_id in row.get("evidence", []):
                    row["links"].append({"key": "evidence:" + evidence_id,
                                         "relation": "加载／消费依据", "confidence": "code-confirmed"})
                for reference in row["links"]:
                    if reference["key"] not in all_keys:
                        raise ValueError(f"Dangling catalog link: {reference}")
        catalog_index = {}
        for category, rows in categories.items():
            summaries = []
            for start in range(0, len(rows), 256):
                path = f"details/{category}/{start//256:04d}.json"
                write(path, json_bytes({r["key"]: r for r in rows[start:start+256]}))
                for row in rows[start:start+256]:
                    summaries.append({k: row[k] for k in ("key", "label", "confidence", "rom_offset", "byte_size", "label_confidence", "summary", "search_terms", "has_stats", "thumbnail") if k in row} | {"detail_file": path})
            write(f"indexes/{category}.json", json_bytes(summaries))
            write(f"records/{category}.jsonl", b"".join(json_bytes(row) for row in rows))
            catalog_index[category] = {"count": len(rows), "index": f"indexes/{category}.json"}
        for source in sorted((ROOT / "tools/data_viewer/web").iterdir()):
            if source.is_file():
                write(source.name, source.read_bytes())
        write("story/index.json", json_bytes(story_index))
        write("story/search.json", json_bytes(story_search_rows(story_documents)))
        for document in story_documents:
            write(f"story/{document['scene']:04d}.json", json_bytes(document))
        write("layout.json", json_bytes(layout))
        manifest = {"schema": "srw64.original-catalog.v1", "rom_sha256": sha(rom),
            "producer_sha256": {p: sha((ROOT / p).read_bytes()) for p in [
                "tools/content/extract_original.py", "src/srw64_native/original_data.py", "src/srw64_native/original_profiles.py", "src/srw64_native/original_abilities.py", "src/srw64_native/original_scripts.py",
                "src/srw64_native/original_story.py", "src/srw64_native/original_images.py", "src/srw64_native/weapon_traits.py",
                "src/srw64_native/catalog.py", "src/srw64_rom/text.py", "src/srw64_rom/resources.py",
                "src/srw64_rom/glyphs.py", "config/srw64-jp-rev0.json",
                "config/data/original-glyph-map.json", "reference/original-glyph-map.csv"]},
            "layout_sha256": sha(layout_path.read_bytes()), "categories": catalog_index,
            "counts": {k: len(v) for k, v in categories.items()},
            "ability_coverage": ability_coverage,
            "script_coverage": script_coverage,
            "story_coverage": {"scenes": len(story_documents), "titled_scenes": sum(d["title"] is not None for d in story_documents),
                               "dialogue_lines": sum(d["counts"]["dialogue"] for d in story_documents),
                               "story_lines": sum(d["counts"]["lines"] for d in story_documents),
                               "commands_omitted": sum(d["counts"]["commands_omitted"] for d in story_documents),
                               "entry": "story.html", "scope": "静态剧情视图，未按实际路径筛选"},
            "image_coverage": image_coverage,
            "text_table_counts": dict(sorted(Counter(r["text_ir"]["table_id"] for r in categories["texts"]).items())),
            "validation": {"text_raw_roundtrip": len(categories["texts"]), "resources_decoded": resources.count,
                           "unique_keys": len(all_keys), "all_catalog_links_resolve": True},
            "limits": ["机体、武器和驾驶员的主要数值已有加载与界面依据；显示成长／改造后的数值需要另算运行时修正。",
                       "特殊能力汇总按原始机体／形态与人物身份计数；不等于出击数量。运行时追加能力和技能实际触发另计。",
                       "技能阈值每组占 10 字节，但等级循环只读取前 9 字节；其余字节保留。",
                       "武器形态匹配已确认，但完整可用条件还受运行时标志等控制。",
                       "人物名称区有 361 项，两个映射表各 360 项；末项不越界关联。",
                       "人物 284 映射基础记录 256，但阈值 256 落入精神表；保留异常。",
                       "事件索引 142 槽含共用脚本；章节标题与场景 ID 尚未关联，不据此统计可玩关卡。",
                       "事件脚本已按确认的参数长度读到结束符；条件块、上下文段与触发类型为静态结构，未求值实际执行路径，部分指令效果仍以技术名称保留。",
                       "配套记录按 14 个半字解析到 999；无对齐 999 的块保留原始字节，出击位置的写入链尚未逐项确认。",
                       "场景→地图与地图→资源已关联；场景表末尾全零二字节可能为填充，不据此统计可玩关卡。",
                       "图片为原版头像、我方配色的机体地图图标和静态战场底图；底图未包含机体、事件叠加层和运行时效果。",
                       "武器属性标签来自原始菜单文本的拆分；保留纯名称及菜单字符串，不据此推断数值位或完整可用条件。",
                       "全资源解压成功不代表图像、音频、模型或脚本用途全部确认。"],
            "files": dict(sorted(files.items()))}
        (output / "manifest.json").write_bytes(json_bytes(manifest))
        # Publish only after all parsing and reference checks have succeeded.
        if destination.exists():
            shutil.rmtree(destination)
        shutil.move(str(output), str(destination))
    return {k: manifest[k] for k in ("schema", "counts", "validation", "limits")}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, default=ROOT / "rom.z64")
    parser.add_argument("--output", type=Path, default=ROOT / "assets/original-data")
    parser.add_argument("--snapshot", type=Path, help="Optional historical RDRAM; requires sibling report.json and scene-evidence.json")
    args = parser.parse_args()
    print(json.dumps(build(args.rom.resolve(), args.output, args.snapshot.resolve() if args.snapshot else None), ensure_ascii=False, indent=2))
