#!/usr/bin/env python3
"""Export the story pages' data for the website (docs/design/website.md §5.6).

    .venv/bin/python web/scripts/export_story.py

Reads the story catalog that `make recomp-data` extracts from the ROM
(assets/original-data/story/), the dialogue text files (content/dialogue/<locale>/
story, Japanese source lines included), the term tables for names and titles, and
the guide's progression cards for episode numbers and routes. Writes, under web/.data
(git-ignored; regenerate on a machine with the ROM-derived assets):

  story/index.json            scenes in reading order, titles and episodes in ja/zh/en
  story/scenes/NNNN.json      one scene: events and lines, Japanese and both translations
  story/search.json           one row per line, for the API's full-text search
and web/public/gen/portraits/<image>.webp, the HD portraits (assets/hd-ai/portrait-batch/whole-v1)
at 160 px.

The site shows only what a reader needs: dialogue, choices and route markers. Script
statements, conditions and BGM notes stay in the catalog.

Each line also carries what changed since the latest game release (the tag in
web/src/data/release.json, or --baseline TAG): `was` holds the release's translation of
each language whose text differs ([] if the release had none). The index records the
baseline, the last commit that changed the translations and whether content/ has
uncommitted edits, which the site should not publish (deploy.sh checks).
"""
from __future__ import annotations

import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "src"))
from srw64_native.dialogue_text import parse  # noqa: E402
import argparse  # noqa: E402
import subprocess  # noqa: E402
import tarfile  # noqa: E402
import tempfile  # noqa: E402
import io  # noqa: E402

CATALOG = ROOT / "assets/original-data/story"
HD_PORTRAITS = ROOT / "assets/hd-ai/portrait-batch/whole-v1"
OUT = ROOT / "web/.data"
GEN = ROOT / "web/public/gen"   # generated images the site serves (git-ignored)
LOCALES = {"zh": "zh-Hans", "en": "en"}
TITLE_TEXT = 281          # scene title = text 281 + scene index
PILOT_TEXT = 4382         # pilot short name = text 4382 + actor id
PORTRAIT_SIZE = 160

# Route markers 3DD0–3DDB (src/srw64_native/original_story.py).
SECTIONS = {
    "3DD0": {"ja": "共通", "zh": "共通", "en": "Common"},
    "3DD1": {"ja": "アーク", "zh": "阿克", "en": "Ark", "route": "ark"},
    "3DD2": {"ja": "セレイン", "zh": "塞蕾茵", "en": "Selain", "route": "selain"},
    "3DD3": {"ja": "ブラッド", "zh": "布拉德", "en": "Brad", "route": "brad"},
    "3DD4": {"ja": "マナミ", "zh": "玛娜米", "en": "Manami", "route": "manami"},
    "3DD5": {"ja": "リアル系", "zh": "真实系", "en": "Real type", "routes": ["ark", "selain"]},
    "3DD6": {"ja": "スーパー系", "zh": "超级系", "en": "Super type", "routes": ["brad", "manami"]},
    "3DD7": {"ja": "男性主人公", "zh": "男性主角", "en": "Male hero", "routes": ["ark", "brad"]},
    "3DD8": {"ja": "女性主人公", "zh": "女性主角", "en": "Female hero", "routes": ["selain", "manami"]},
    "3DD9": {"ja": "選択肢 1", "zh": "选择肢 1", "en": "Choice 1"},
    "3DDA": {"ja": "選択肢 2", "zh": "选择肢 2", "en": "Choice 2"},
    "3DDB": {"ja": "選択肢 3", "zh": "选择肢 3", "en": "Choice 3"},
}
# Actors 25–28 are the four heroes in route order.
HERO_ROUTE = {25: "ark", 26: "selain", 27: "brad", 28: "manami"}
ROLES = {"主角": {"ja": "主人公", "zh": "主角", "en": "Hero"},
         "对手": {"ja": "パートナー", "zh": "搭档", "en": "Partner"}}
PHASES = {"opening": {"ja": "オープニング", "zh": "开场", "en": "Opening"},
          "deployment": {"ja": "初期配置", "zh": "初期配置", "en": "Deployment"},
          "map": {"ja": "戦闘マップ", "zh": "战场", "en": "Battle map"},
          "ending": {"ja": "エンディング", "zh": "结束", "en": "Ending"}}


def strip_codes(text: str) -> str:
    return re.sub(r"<END>$", "", text or "").strip()


def load_terms() -> dict[str, dict[str, str]]:
    terms = {}
    for code, locale in LOCALES.items():
        data = json.loads((ROOT / f"content/locales/{locale}.json").read_text())
        terms[code] = {e["key"]: strip_codes(e.get("target") or "") for e in data["entries"]}
    return terms


def text_key(n: int) -> str:
    return f"base:t00_{n:05d}"


def load_dialogue(base: Path = ROOT, strict: bool = True) -> dict[str, dict[str, object]]:
    """key -> {lang: Entry}; Japanese comes from the source lines of either file. Not
    strict for an older release's files: a file the parser no longer reads is left out."""
    out: dict[str, dict[str, object]] = defaultdict(dict)
    for code, locale in LOCALES.items():
        for path in sorted((base / f"content/dialogue/{locale}").rglob("*.txt")):
            entries, problems = parse(path.read_text(), str(path))
            if problems:
                if strict:
                    raise SystemExit("\n".join(map(str, problems[:5])))
                print(f"baseline: skipped {path.relative_to(base)} ({len(problems)} problem(s))")
                continue
            for entry in entries:
                out[entry.key][code] = entry
    return out


def git(*args: str) -> str:
    return subprocess.run(["git", "-C", str(ROOT), *args], check=True, capture_output=True, text=True).stdout


def baseline_dialogue(tag: str) -> dict[str, dict[str, object]]:
    """The dialogue files as they were at a release tag."""
    archive = subprocess.run(["git", "-C", str(ROOT), "archive", tag, "content/dialogue"], check=True, capture_output=True).stdout
    with tempfile.TemporaryDirectory() as tmp:
        with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
            tar.extractall(tmp, filter="data")
        return load_dialogue(Path(tmp), strict=False)


def source_version(tag: str) -> dict:
    """What the exported translations are: the baseline release, the last commit that
    changed them, and whether the working tree has edits not committed yet."""
    paths = ["content/dialogue", "content/locales"]
    commit, date = (git("log", "-1", "--format=%h%x09%cI", "--", *paths).strip().split("\t") + [""])[:2]
    return {"baseline": tag, "commit": commit, "date": date, "dirty": bool(git("status", "--porcelain", "--", *paths).strip())}


PLACEHOLDER = re.compile(r"\{([A-Za-z]+)\}")


def pages(entry, side: str) -> list[str]:
    """The pages of one side as text; lines inside a page are joined without breaks
    (the game re-wraps them), choice options keep one per line."""
    result = []
    for page in entry.pages:
        lines = page.source if side == "source" else page.target
        if side == "source" and page.source_options:
            result.append("\n".join(lines))
        elif side == "target" and any(page.target_options):
            result.append("\n".join(lines))
        else:
            joined = ""
            for line in lines:
                line = line.strip()
                # Japanese joins without spaces; Latin text needs one.
                if joined and re.search(r"[A-Za-z0-9,.!?…'\"’”]$", joined) and re.match(r"[A-Za-z0-9“‘\"']", line):
                    joined += " "
                joined += line
            result.append(joined)
    return [p for p in result if p]


def portrait_id(path: str | None) -> int | None:
    if not path:
        return None
    match = re.search(r"portrait/(\d+)-\d+\.png$", path)
    return int(match.group(1)) if match else None


def actor_id(key: str | None) -> int | None:
    return int(key.rsplit(":", 1)[1]) if key else None


def japanese_name(label: str) -> str:
    return re.sub(r"（.*?）$", "", label or "").strip()


# Battlefield trigger summaries are Chinese strings from the extractor; the common
# ones are rewritten per language, the rest fall back to their event category.
def trigger_text(event: dict, names: dict[str, dict[str, str]]) -> dict[str, str]:
    t = event.get("trigger") or ""
    t = re.sub(r"（变量.*?）", "", t)

    def name(ja: str) -> dict[str, str]:
        return names.get(ja, {"ja": ja, "zh": ja, "en": ja})

    rules = [
        (r"^敌方残存 ≤ (\d+)", lambda m: {"zh": f"敌方剩余 {m[1]} 台以下", "en": f"{m[1]} or fewer enemies left", "ja": f"敵残り {m[1]} 機以下"}),
        (r"^第三方残存 ≤ (\d+)", lambda m: {"zh": f"第三方剩余 {m[1]} 台以下", "en": f"{m[1]} or fewer third-party units left", "ja": f"第三勢力残り {m[1]} 機以下"}),
        (r"^敌方全灭", lambda m: {"zh": "敌方全灭", "en": "All enemies defeated", "ja": "敵全滅"}),
        (r"^第 (\d+) 回合起 · 我方阶段", lambda m: {"zh": f"第 {m[1]} 回合我方阶段起", "en": f"From turn {m[1]}, player phase", "ja": f"{m[1]} ターン目 味方フェイズ以降"}),
        (r"^第 (\d+) 回合起 · 敌方阶段", lambda m: {"zh": f"第 {m[1]} 回合敌方阶段起", "en": f"From turn {m[1]}, enemy phase", "ja": f"{m[1]} ターン目 敵フェイズ以降"}),
        (r"^说服", lambda m: {"zh": "说服", "en": "Persuasion", "ja": "説得"}),
        (r"^由 .*? 启动的延迟计数", lambda m: {"zh": "延时事件", "en": "Delayed event", "ja": "時間差イベント"}),
        (r"^(.+?) 击破／退场", lambda m: {k: v for k, v in zip(("zh", "en", "ja"), (f"{name(m[1])['zh']}被击破或撤退", f"{name(m[1])['en']} defeated or withdrawn", f"{name(m[1])['ja']} 撃墜・撤退"))}),
        (r"^(.+?) 与 (.+?) 交战（战斗前", lambda m: {"zh": f"{name(m[1])['zh']}与{name(m[2])['zh']}交战前", "en": f"Before {name(m[1])['en']} fights {name(m[2])['en']}", "ja": f"{name(m[1])['ja']} と {name(m[2])['ja']} の戦闘前"}),
        (r"^(.+?) 与 (.+?) 交战（战斗后", lambda m: {"zh": f"{name(m[1])['zh']}与{name(m[2])['zh']}交战后", "en": f"After {name(m[1])['en']} fights {name(m[2])['en']}", "ja": f"{name(m[1])['ja']} と {name(m[2])['ja']} の戦闘後"}),
        (r"^(.+?) HP ≤ (\d+)%", lambda m: {"zh": f"{name(m[1])['zh']} HP 降到 {m[2]}% 以下", "en": f"{name(m[1])['en']} at {m[2]}% HP or less", "ja": f"{name(m[1])['ja']} HP {m[2]}% 以下"}),
        (r"^阵营 \d+ 的任意单位 到达区域", lambda m: {"zh": "有单位到达指定区域", "en": "A unit reaches an area", "ja": "ユニットが指定地点に到達"}),
        (r"^(.+?) 到达区域", lambda m: {"zh": f"{name(m[1])['zh']}到达指定区域", "en": f"{name(m[1])['en']} reaches an area", "ja": f"{name(m[1])['ja']} が指定地点に到達"}),
    ]
    for pattern, build in rules:
        m = re.match(pattern, t)
        if m:
            return build(m)
    return {"zh": "战场事件", "en": "Battle-map event", "ja": "戦闘マップのイベント"}


def episode_cards() -> dict[str, list[dict]]:
    """title_ja -> cards, each with stage number, lane and section in three languages."""
    by_title: dict[str, list[dict]] = defaultdict(list)
    per_lang = {}
    for code, folder in (("zh", "zh-Hans"), ("en", "en"), ("ja", "ja")):
        per_lang[code] = {c["id"]: c for c in json.loads((ROOT / f"guide/data/{folder}/progression.json").read_text())["cards"]}
    for cid, zh in per_lang["zh"].items():
        card = {"id": cid, "stage": zh.get("stage"), "route": zh.get("route"),
                "lane": {code: per_lang[code].get(cid, zh).get("lane") for code in per_lang},
                "section": {code: per_lang[code].get(cid, zh).get("section") for code in per_lang}}
        by_title[zh["title_ja"]].append(card)
    return by_title


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--baseline", help="release tag to compare with (default: the tag in web/src/data/release.json)")
    args = parser.parse_args()
    if not (CATALOG / "index.json").exists():
        raise SystemExit(f"{CATALOG} is missing: run `make recomp-data` with the ROM first")
    tag = args.baseline or json.loads((ROOT / "web/src/data/release.json").read_text())["tag"]
    terms = load_terms()
    dialogue = load_dialogue()
    before = baseline_dialogue(tag)
    source = source_version(tag)
    index = json.loads((CATALOG / "index.json").read_text())
    cards = episode_cards()

    # Names: zh/en from the pilot texts, Japanese from the catalog's speaker labels.
    ja_names: dict[int, Counter] = defaultdict(Counter)
    scenes_raw = {}
    for s in index["scenes"]:
        data = json.loads((CATALOG / s["file"].split("/", 1)[1]).read_text())
        scenes_raw[s["scene"]] = data
        for event in data["events"]:
            for line in event["lines"]:
                if line["kind"] != "dialogue":
                    continue
                sp = line["speaker"]
                if sp.get("key"):
                    ja_names[actor_id(sp["key"])][japanese_name(sp["label"])] += 1
                for c in sp.get("candidates", []):
                    ja_names[actor_id(c["key"])][japanese_name(c["label"])] += 1

    def actor_names(aid: int) -> dict[str, str]:
        ja = ja_names[aid].most_common(1)[0][0] if ja_names[aid] else ""
        return {"ja": ja,
                "zh": terms["zh"].get(text_key(PILOT_TEXT + aid)) or ja,
                "en": terms["en"].get(text_key(PILOT_TEXT + aid)) or ja}

    by_ja_name = {}
    for aid in ja_names:
        n = actor_names(aid)
        by_ja_name.setdefault(n["ja"], n)

    portraits_used: set[int] = set()
    search_rows = []
    out_scenes = []
    (OUT / "story/scenes").mkdir(parents=True, exist_ok=True)

    for s in index["scenes"]:
        n = s["scene"]
        data = scenes_raw[n]
        title = {"ja": strip_codes(data["title"]),
                 "zh": terms["zh"].get(text_key(TITLE_TEXT + n)) or data["title"],
                 "en": terms["en"].get(text_key(TITLE_TEXT + n)) or data["title"]}
        events = []
        for event in data["events"]:
            lines = []
            for line in event["lines"]:
                kind = line["kind"]
                if kind == "section":
                    meta = SECTIONS.get(line["marker"], {"ja": line["label"], "zh": line["label"], "en": line["label"]})
                    lines.append({"t": "route", "marker": line["marker"], "label": {k: meta[k] for k in ("ja", "zh", "en")},
                                  "routes": meta.get("routes") or ([meta["route"]] if "route" in meta else None)})
                elif kind in ("dialogue", "choice"):
                    key = line["text_key"]
                    entry = dialogue.get(key, {})
                    src = entry.get("zh") or entry.get("en")
                    item = {"t": "say" if kind == "dialogue" else "choice", "id": line["text_id"],
                            "ja": pages(src, "source") if src else [line.get("display", "")],
                            "zh": pages(entry["zh"], "target") if "zh" in entry else [],
                            "en": pages(entry["en"], "target") if "en" in entry else []}
                    # What the release said, for each language that reads differently now.
                    old = before.get(key, {})
                    for code in LOCALES:
                        was = pages(old[code], "target") if code in old else []
                        if item[code] and was != item[code]:
                            item.setdefault("was", {})[code] = was
                    if kind == "dialogue":
                        sp = line["speaker"]
                        item["side"] = line.get("mode", 0)
                        if sp.get("key"):
                            aid = actor_id(sp["key"])
                            item["who"] = actor_names(aid)
                            pid = portrait_id(sp.get("portrait"))
                            item["face"] = pid
                            if pid is not None:
                                portraits_used.add(pid)
                            if aid in HERO_ROUTE and sp.get("status") != "resolved":
                                item["route"] = HERO_ROUTE[aid]
                        else:
                            role = (entry.get("zh").note.split(" · ")[0] if entry.get("zh") else "").strip()
                            item["who"] = dict(ROLES.get(role, {"ja": japanese_name(sp.get("label", "")), "zh": role or "?", "en": role or "?"}))
                            item["cands"] = []
                            for c in sp.get("candidates", []):
                                aid = actor_id(c["key"])
                                pid = portrait_id(c.get("portrait"))
                                if pid is not None:
                                    portraits_used.add(pid)
                                item["cands"].append({"route": HERO_ROUTE.get(aid) or HERO_ROUTE.get(aid - 4), "who": actor_names(aid), "face": pid})
                    lines.append(item)
                    search_rows.append([line["text_id"], n, item["who"]["zh"] if "who" in item else "", item["who"]["en"] if "who" in item else "",
                                        item["who"]["ja"] if "who" in item else "",
                                        "\n".join(item["ja"]), "\n".join(item["zh"]), "\n".join(item["en"])])
            if not any(l["t"] in ("say", "choice") for l in lines):
                continue
            ev = {"phase": event["phase"], "label": PHASES.get(event["phase"], PHASES["map"]), "lines": lines}
            if event["phase"] == "map":
                ev["trigger"] = trigger_text(event, by_ja_name)
            events.append(ev)

        matched = cards.get(title["ja"], [])
        episode = {"stage": matched[0]["stage"], "section": matched[0]["section"],
                   "lanes": [c["lane"] for c in matched], "routes": sorted({c["route"] for c in matched if c.get("route")})} if matched else None
        counts = sum(1 for e in events for l in e["lines"] if l["t"] == "say")
        changed = {code: sum(1 for e in events for l in e["lines"] if code in l.get("was", {})) for code in LOCALES}
        scene = {"scene": n, "title": title, "episode": episode, "events": events, "count": counts, "changed": changed,
                 "next": [x["scene"] for x in data.get("next_scenes", [])],
                 "previous": [x["scene"] if isinstance(x, dict) else x for x in data.get("previous_scenes", [])]}
        (OUT / f"story/scenes/{n:04d}.json").write_text(json.dumps(scene, ensure_ascii=False, separators=(",", ":")))
        out_scenes.append({k: scene[k] for k in ("scene", "title", "episode", "count", "changed", "next", "previous")})

    # Reading order: by episode number (scenes the guide does not list go last, by index).
    def order(s):
        ep = s["episode"]
        try:
            return (0, int(ep["stage"]), s["scene"]) if ep else (1, 0, s["scene"])
        except (TypeError, ValueError):
            return (1, 0, s["scene"])
    out_scenes.sort(key=order)
    (OUT / "story/index.json").write_text(json.dumps({"schema": "srw64.web-story-index.v2", "source": source, "scenes": out_scenes},
                                                       ensure_ascii=False, indent=1))
    (OUT / "story/search.json").write_text(json.dumps({"schema": "srw64.web-story-search.v1",
                                                         "columns": ["id", "scene", "who_zh", "who_en", "who_ja", "ja", "zh", "en"],
                                                         "rows": search_rows}, ensure_ascii=False, separators=(",", ":")))

    # HD portraits, small.
    from PIL import Image
    GEN.joinpath("portraits").mkdir(parents=True, exist_ok=True)
    missing = []
    for pid in sorted(portraits_used):
        src = HD_PORTRAITS / f"portrait-{pid}.png"
        dst = GEN / f"portraits/{pid}.webp"
        if not src.exists():
            missing.append(pid)
            continue
        if dst.exists() and dst.stat().st_mtime >= src.stat().st_mtime:
            continue
        im = Image.open(src).convert("RGBA").resize((PORTRAIT_SIZE, PORTRAIT_SIZE), Image.LANCZOS)
        im.save(dst, "WEBP", quality=82, method=6)
    print(f"scenes {len(out_scenes)}, lines {len(search_rows)}, portraits {len(portraits_used) - len(missing)} (missing HD: {missing})")
    print(f"since {tag}: " + ", ".join(f"{code} {sum(s['changed'][code] for s in out_scenes)} lines changed" for code in LOCALES)
          + f"; translations at {source['commit']}{' + uncommitted edits' if source['dirty'] else ''}")
    unmatched = [s["scene"] for s in out_scenes if not s["episode"]]
    print(f"scenes without an episode number: {unmatched}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
