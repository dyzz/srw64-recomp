"""Classified, context-rich export of every original text record for translation work.

Every record of the 20 ROM text tables gets exactly one category. Ranges whose meaning
comes from a display formula in the game code (names, weapons, titles, spirits, the
script dialogue and choices) are marked ``code``; the remaining system ranges were
delimited by reading their content and are marked ``content``. The export never edits
or reorders source text: ``source`` is the lossless catalog form with <BR>/<STOP>/<END>
and <G:XXXX> tokens, ``display`` only folds dynamic-name runs into placeholders.
"""
from __future__ import annotations

import re
from collections import Counter, defaultdict
from dataclasses import dataclass

from .catalog import TOKEN, text_key
from .original_story import NAME_SLOTS, display_text

EXPORT_SCHEMA = "srw64.text-export.v1"
RECORD_SCHEMA = "srw64.text-export-record.v1"

# Table 0 ids from 17347 on are exactly the ids the stage scripts reference (33,582 dialogue
# lines + 46 choice texts); the export asserts this instead of trusting the boundary.
STORY_FIRST_ID = 17347
BATTLE_FIRST_ID = 5813

# policy: how the text enters translation.
#   mt       machine draft in context batches (tools/translation/run_mt.py), then review
#   terms    the per-section term table (content/locales/terms/, tools/content/apply_terms.py):
#            each distinct string translated once; weapon menu names are derived there
#   manual   strings of the native UI, maintained by hand in the locale's "ui" map
#   copy     no translation (codes, symbols, gauges)
#   decide   needs a product decision first (lyrics, song titles)
#   skip     development/debug text or replaced screens, not shown in normal play
CATEGORIES: dict[str, dict] = {
    "story.dialogue": dict(label="剧情对白", domain="story", policy="mt",
                           consumer="原生对白（标准双框）经台词文本文件 content/dialogue 接入"),
    "story.choice": dict(label="选择肢", domain="story", policy="mt",
                         consumer="原版选择窗口（3D44 → 8008F648），未接入"),
    "story.chapter_title": dict(label="话名", domain="story", policy="terms",
                                consumer="原生场间／存档页已接入；章节标题卡为图片，未接入"),
    "story.intro": dict(label="开场序章文字（纹理图片，人工转写）", domain="story", policy="mt",
                        consumer="原版缩放文字纹理，未接入；需原生重排版"),
    "story.condition": dict(label="胜败条件", domain="story", policy="terms",
                            consumer="原版作战目的窗口，未接入"),
    "battle.quote": dict(label="战斗台词", domain="battle", policy="mt",
                         consumer="战斗 overlay 对白适配器只替换显示（27b221e），2026-09-23 实机验证中英切换"),
    "battle.special": dict(label="战斗特殊台词（表头有说话人）", domain="battle", policy="mt",
                           consumer="消费者未确认"),
    "names.unit": dict(label="机体名", domain="names", policy="terms",
                       consumer="原生能力／改造／零件／换乘／战前页已接入；原版地图与战斗未接入"),
    "names.weapon": dict(label="武器名（纯名称）", domain="names", policy="terms",
                         consumer="原生能力／改造／战前页已接入；原版战斗未接入"),
    "names.weapon_menu": dict(label="武器菜单名（含 格／射／P 标记）", domain="names", policy="terms",
                              consumer="原生页按标记拆分显示；原版武器菜单未接入"),
    "names.actor": dict(label="人物简称", domain="names", policy="terms",
                        consumer="原生页已接入，对白说话人按记录号接入（cb86e82）；原版地图未接入"),
    "names.actor_full": dict(label="人物全名", domain="names", policy="terms",
                             consumer="原生驾驶员能力页已接入"),
    "names.protagonist": dict(label="主角／对手默认名", domain="names", policy="terms",
                              consumer="姓名页默认值；玩家可改名，游戏内保存为 N64 字码"),
    "names.character_list": dict(label="角色名单（推测为特典角色列表）", domain="names", policy="terms",
                                 consumer="消费者未确认"),
    "names.spirit": dict(label="精神指令名", domain="names", policy="terms",
                         consumer="原生能力／战前页已接入；原版精神菜单未接入"),
    "names.spirit_abbr": dict(label="精神指令单字缩写", domain="names", policy="terms",
                              consumer="原版精神检索等窄栏，未接入"),
    "names.ability": dict(label="机体特殊能力名", domain="names", policy="terms",
                          consumer="原生能力页已接入"),
    "names.skill": dict(label="特殊技能名（含等级）", domain="names", policy="terms",
                        consumer="原生驾驶员能力页已接入"),
    "names.part": dict(label="强化零件名", domain="names", policy="terms",
                       consumer="原生零件／能力页已接入"),
    "names.terrain": dict(label="地形名", domain="names", policy="terms",
                          consumer="原版地图信息窗，未接入"),
    "names.series": dict(label="参战作品名", domain="names", policy="terms",
                         consumer="原版特典／列表，未接入"),
    "names.model_number": dict(label="机体型号（词条表只译非型号文字）", domain="names", policy="copy",
                               consumer="原版机体列表（特典），未接入"),
    "names.music": dict(label="曲名", domain="extras", policy="decide",
                        consumer="原版音乐鉴赏，未接入"),
    "extras.menu": dict(label="特典菜单", domain="extras", policy="terms",
                        consumer="原版标题选项，未接入"),
    "extras.lyrics": dict(label="卡拉 OK 歌词", domain="extras", policy="decide",
                          consumer="原版卡拉 OK 模式，未接入"),
    "system.ui": dict(label="界面标签与菜单", domain="system", policy="terms",
                      consumer="部分由原生页使用（见 native_page），其余为原版界面"),
    "system.message": dict(label="系统提示句", domain="system", policy="terms",
                           consumer="部分由原生存档页使用，其余为原版界面"),
    "system.name_entry": dict(label="新游戏与姓名输入", domain="system", policy="terms",
                              consumer="现代姓名页另有文案（content/ui、locales ui）；原版页被替换"),
    "help.spirit": dict(label="精神指令说明", domain="help", policy="terms",
                        consumer="原版精神说明窗，未接入"),
    "help.part": dict(label="强化零件说明", domain="help", policy="terms",
                      consumer="原版零件说明，未接入"),
    "help.upgrade": dict(label="改造奖励与零件效果短句", domain="help", policy="terms",
                         consumer="原生改造／零件页部分使用"),
    "help.counter": dict(label="反击命令说明", domain="help", policy="terms",
                         consumer="原版反击命令窗，未接入"),
    "symbol.icon": dict(label="地图图标字串", domain="symbol", policy="copy", consumer="原版地图"),
    "symbol.gauge": dict(label="刻度条与翻页箭头", domain="symbol", policy="copy", consumer="原版与原生改造页"),
    "symbol.keyboard": dict(label="原版姓名输入字盘", domain="symbol", policy="skip",
                            consumer="原版姓名页，已被现代姓名页替换"),
    "debug.menu": dict(label="开发／调试菜单", domain="debug", policy="skip", consumer="正常游玩不显示（推测）"),
    "native.ui": dict(label="原生界面文案（非 ROM）", domain="native", policy="manual",
                      consumer="RmlUi 原生页面、设置窗口、对白阅读 UI"),
}


@dataclass(frozen=True)
class Range:
    start: int
    end: int
    category: str
    group: str
    evidence: str  # "code" or "content"


RANGES: tuple[Range, ...] = (
    Range(0, 59, "names.terrain", "地形", "content"),
    Range(60, 84, "names.series", "作品名（单行）", "content"),
    Range(85, 109, "names.series", "作品名（两行排版）", "content"),
    Range(110, 224, "names.model_number", "型号", "content"),
    Range(225, 231, "extras.menu", "标题选项与特典", "content"),
    Range(232, 280, "names.music", "曲名", "content"),
    Range(281, 423, "story.chapter_title", "话名（281 + 场景）", "code"),
    Range(424, 486, "system.message", "控制器包／64GB 包／读档提示", "content"),
    Range(487, 494, "names.protagonist", "主角／对手默认昵称", "content"),
    Range(495, 502, "names.protagonist", "主角／对手默认全名", "content"),
    Range(503, 526, "system.ui", "地图指令菜单", "content"),
    Range(527, 889, "names.unit", "机体名（527 + 机体）", "code"),
    Range(890, 897, "system.ui", "效果与增减标签", "content"),
    Range(898, 942, "system.ui", "能力页标签", "content"),
    Range(943, 961, "system.ui", "出击选择", "content"),
    Range(962, 968, "system.ui", "反击命令与作战目的", "content"),
    Range(969, 998, "names.spirit", "精神指令（969 + 编号）", "code"),
    Range(999, 1018, "system.ui", "地图与系统设定", "content"),
    Range(1019, 1030, "names.ability", "机体特殊能力", "code"),
    Range(1031, 1093, "names.skill", "技能等级名", "code"),
    Range(1094, 1099, "system.ui", "尺寸（1094 + 尺寸）", "code"),
    Range(1100, 1101, "names.ability", "机体特殊能力", "code"),
    Range(1102, 1115, "system.ui", "部队表、复活表、获得提示", "content"),
    Range(1116, 1116, "names.ability", "机体特殊能力", "code"),
    Range(1117, 1128, "system.ui", "奖励、存档结束、击坠数、出售", "content"),
    Range(1129, 1148, "names.part", "强化零件（1129 + 零件）", "code"),
    Range(1149, 1178, "names.spirit_abbr", "精神单字缩写", "content"),
    Range(1179, 1369, "debug.menu", "战斗动画编辑器", "content"),
    Range(1370, 2698, "names.weapon", "武器纯名称（1370 + 武器）", "code"),
    Range(2699, 4027, "names.weapon_menu", "武器菜单名（2699 + 武器）", "code"),
    Range(4028, 4043, "names.skill", "武器必要技能标签", "content"),
    Range(4044, 4144, "system.ui", "场间菜单与各画面标签", "code"),
    Range(4145, 4160, "symbol.gauge", "改造刻度", "content"),
    Range(4161, 4162, "symbol.gauge", "翻页箭头", "code"),
    Range(4163, 4267, "symbol.gauge", "改造刻度", "content"),
    Range(4268, 4306, "help.upgrade", "满改奖励与零件效果", "code"),
    Range(4307, 4381, "debug.menu", "动画名称标签", "content"),
    Range(4382, 4742, "names.actor", "人物简称（4382 + 人物）", "code"),
    Range(4743, 5103, "names.actor_full", "人物全名（4743 + 人物）", "code"),
    Range(5104, 5149, "symbol.icon", "地图图标字串", "content"),
    Range(5150, 5179, "help.spirit", "精神说明（5150 + 编号）", "content"),
    Range(5180, 5181, "system.message", "精神使用提示", "content"),
    Range(5182, 5199, "help.part", "零件说明", "content"),
    Range(5200, 5219, "system.name_entry", "新游戏设定", "content"),
    Range(5220, 5239, "symbol.keyboard", "字盘行", "content"),
    Range(5240, 5246, "system.name_entry", "姓名输入", "content"),
    Range(5247, 5449, "symbol.keyboard", "字盘单字", "content"),
    Range(5450, 5557, "names.character_list", "角色名单", "content"),
    Range(5558, 5566, "help.counter", "反击命令说明", "content"),
    Range(5567, 5639, "story.condition", "胜败条件", "content"),
    Range(5640, 5643, "system.ui", "损伤程度", "content"),
    Range(5644, 5798, "debug.menu", "战斗与剧情标志编辑器", "content"),
    Range(5799, 5812, "battle.special", "特殊台词", "content"),
    Range(5813, 17346, "battle.quote", "战斗台词", "content"),
)

# Individual ids the native RmlUi pages render through the localization catalog
# (src/host/*_page.cpp constants); whole name ranges are covered by their category.
NATIVE_PAGE_IDS = {241, 911, 912, *range(1094, 1100), *range(4044, 4145), 4161, 4162, *range(4268, 4272)}
NATIVE_PAGE_CATEGORIES = {"story.chapter_title", "names.unit", "names.weapon", "names.weapon_menu", "names.actor",
                          "names.actor_full", "names.spirit", "names.ability", "names.skill", "names.part"}

GAP = re.compile(r"[ 　]{2,}")
FRAGMENT_HEAD = re.compile(r"^[をがにでとはのへもや、]")
FRAGMENT_TAIL = re.compile(r"[をがにでとはのへ、]$")
KANA = re.compile(r"[ぁ-ゖァ-ヺ]")
QUOTE = re.compile(r"^[「『（(]")


def category_of(table: int, text_id: int) -> tuple[str, str, str]:
    if table:
        return "extras.lyrics", f"歌词表 {table}", "content"
    if text_id >= STORY_FIRST_ID:
        return "story.dialogue", "剧情对白", "code"
    for r in RANGES:
        if r.start <= text_id <= r.end:
            return r.category, r.group, r.evidence
    raise ValueError(f"text id {text_id} has no category")


def visible(text: str) -> str:
    """Characters a reader sees: tokens and layout spaces removed."""
    return re.sub(r"[\s　]", "", TOKEN.sub("", text))


def header_fields(header: bytes) -> dict:
    digits = header.decode("ascii", "replace")
    result = {"header": digits}
    if digits[:3].isdigit():
        result["speaker_id"] = int(digits[:3])  # 8008CE54 reads the first three digits
    return result


def flags_of(category: str, source: str) -> list[str]:
    body = source.replace("<END>", "")
    flags = []
    domain = CATEGORIES[category]["domain"]
    if domain not in ("story", "battle") and GAP.search(TOKEN.sub("", body).strip(" 　")):
        flags.append("numeric-gap")
    if domain in ("system", "help") and (FRAGMENT_HEAD.search(body) or FRAGMENT_TAIL.search(body)):
        flags.append("fragment")
    if re.search(r"<G:012[4-9A-Ca-c]>", body):
        flags.append("dynamic-name")
    if "<STOP>" in body:
        flags.append("paged")
    if not visible(body):
        flags.append("blank")
    return flags


class TextExporter:
    """Joins the lossless catalog with the extracted story, actor and weapon records."""

    def __init__(self, sources: dict, hashes: dict, headers: dict, categories: dict,
                 story_documents: list[dict], locales: dict[str, dict], term_sections: list[dict] | None = None):
        self.sources, self.hashes, self.headers = sources, hashes, headers
        self.actors = {int(r["key"].rsplit(":", 1)[1]): r for r in categories.get("actors", [])}
        self.weapons = {int(r["key"].rsplit(":", 1)[1]): r for r in categories.get("weapons", [])}
        self.units = {int(r["key"].rsplit(":", 1)[1]): r for r in categories.get("units", [])}
        self.events = {r["key"]: r for r in categories.get("stage_events", [])}
        self.documents = story_documents
        self.locales = locales
        # content/locales/terms/sections.json: ranges translated once per distinct string by the term table
        self.term_section = {}
        for section in term_sections or []:
            for start, end in section["ranges"]:
                for text_id in range(start, end + 1):
                    self.term_section[text_id] = section["name"]

    def actor_name(self, actor_id: int) -> str | None:
        return self.sources.get(text_key(0, 4382 + actor_id), "").replace("<END>", "") or None

    # ------------------------------------------------------------------ contexts
    def story_occurrences(self) -> dict[str, list[dict]]:
        found: dict[str, list[dict]] = defaultdict(list)
        order = 0
        for doc in self.documents:
            for event in doc["events"]:
                section = "3DD0"
                for index, line in enumerate(event["lines"]):
                    if line["kind"] == "section":
                        section = line["marker"]
                        continue
                    if line["kind"] not in ("dialogue", "choice"):
                        continue
                    speaker = line.get("speaker") or {}
                    order += 1
                    found[line["text_key"]].append({"order": order,
                        "scene": doc["scene"], "scene_title": doc["title"], "event": event["key"],
                        "phase": event["phase"], "trigger": event["trigger"], "offset": line["offset"],
                        "line_index": index, "section": line.get("section", section), "depth": line["depth"],
                        "kind": line["kind"], "mode": line.get("mode"),
                        "speaker": speaker.get("label"), "speaker_key": speaker.get("key"),
                        "speaker_status": speaker.get("status"),
                        "speaker_candidates": [c["label"] for c in speaker.get("candidates", [])],
                        **({"options": line["count"]} if line["kind"] == "choice" else {}),
                    })
        return found

    def battle_runs(self) -> dict[str, dict]:
        """Battle quotes stay in table order; consecutive lines of one speaker form a run.

        The selection table (which situation or weapon picks which line) is not reversed yet.
        Up to about id 14000 each character has one block ordered attack → defeated → heavy /
        light damage → evade → beam block → out of ammo / range; later ids are weapon lines and
        combination-attack exchanges, where header suffix 0024 marks the leading line."""
        result, run, previous, position = {}, 0, None, 0
        for text_id in range(BATTLE_FIRST_ID - 14, STORY_FIRST_ID):
            key = text_key(0, text_id)
            fields = header_fields(self.headers[key])
            speaker = fields.get("speaker_id")
            if speaker != previous:
                run, position, previous = run + 1, 0, speaker
            result[key] = {"speaker_id": speaker, "speaker": self.actor_name(speaker) if speaker is not None else None,
                           "speaker_run": run, "position": position,
                           **({"combo_lead": True} if fields["header"].endswith("0024") else {})}
            position += 1
        return result

    # ------------------------------------------------------------------ export
    def records(self) -> list[dict]:
        story = self.story_occurrences()
        battle = self.battle_runs()
        rows = []
        for key, source in self.sources.items():
            table, text_id = int(key[6:8]), int(key[9:])
            category, group, evidence = category_of(table, text_id)
            row = {"schema": RECORD_SCHEMA, "key": key, "table": table, "id": text_id,
                   "category": category, "group": group, "evidence": evidence,
                   "source": source, "display": display_text(source), "source_sha256": self.hashes[key],
                   "chars": len(visible(source)), "flags": flags_of(category, source),
                   **header_fields(self.headers[key])}
            context: dict = {}
            if category == "story.dialogue":
                occurrences = story.get(key, [])
                if occurrences and occurrences[0]["kind"] == "choice":
                    row["category"], row["group"] = "story.choice", "选择肢（3D44 参数 3）"
                    row["choice_options"] = source.replace("<END>", "").split("<BR>")
                context["occurrences"] = occurrences
            elif category in ("battle.quote", "battle.special"):
                context.update(battle[key])
            elif category == "story.chapter_title":
                context["scene"] = text_id - 281
            elif category == "names.unit":
                context["unit_id"] = text_id - 527
            elif category in ("names.weapon", "names.weapon_menu"):
                weapon_id = text_id - (1370 if category == "names.weapon" else 2699)
                context["weapon_id"] = weapon_id
                if category == "names.weapon_menu":
                    context["pure_name_key"] = text_key(0, 1370 + weapon_id)
            elif category in ("names.actor", "names.actor_full"):
                actor_id = text_id - (4382 if category == "names.actor" else 4743)
                context["actor_id"] = actor_id
                context["pair_key"] = text_key(0, (4743 if category == "names.actor" else 4382) + actor_id)
            elif category == "names.spirit":
                context["spirit_id"] = text_id - 969
                context["help_key"] = text_key(0, 5150 + text_id - 969)
            elif category == "help.spirit":
                context["spirit_key"] = text_key(0, 969 + text_id - 5150)
            if context:
                row["context"] = context
            if table == 0 and text_id in self.term_section:
                row["terms_section"] = self.term_section[text_id]
            native = text_id in NATIVE_PAGE_IDS or category in NATIVE_PAGE_CATEGORIES
            row["native_page"] = bool(table == 0 and native)
            for locale, document in self.locales.items():
                target = document["entries"].get(key)
                if target is not None:
                    row.setdefault("existing", {})[locale] = target
            rows.append(row)
        self.check(rows, story)
        return rows

    def native_ui_records(self) -> list[dict]:
        source = self.locales["ja"]["ui"]
        rows = []
        for name, text in sorted(source.items()):
            row = {"schema": RECORD_SCHEMA, "key": f"ui:{name}", "category": "native.ui",
                   "group": name.split(".", 1)[0], "evidence": "content", "source": text, "display": text,
                   "chars": len(visible(text)), "flags": [], "native_page": True}
            for locale, document in self.locales.items():
                if locale != "ja" and name in document["ui"]:
                    row.setdefault("existing", {})[locale] = document["ui"][name]
            rows.append(row)
        return rows

    @staticmethod
    def intro_records(transcription: dict) -> list[dict]:
        routes: dict[int, list[str]] = defaultdict(list)
        for group, info in transcription["groups"].items():
            for position, resource in enumerate(info["pages"]):
                routes[resource].append(f"{group}#{position}")
        rows = []
        for page in transcription["pages"]:
            text = "\n".join(page["lines"])
            rows.append({"schema": RECORD_SCHEMA, "key": f"intro:{page['resource']}", "category": "story.intro",
                         "group": "开场序章", "evidence": "transcribed", "source": text, "display": text,
                         "chars": len(visible(text)), "flags": [], "native_page": False,
                         "context": {"resource": page["resource"], "image_sha256": page["image_sha256"],
                                     "played_in": routes[page["resource"]]}})
        return rows

    def check(self, rows: list[dict], story: dict) -> None:
        referenced = set(story)
        dialogue = {r["key"] for r in rows if r["table"] == 0 and r["id"] >= STORY_FIRST_ID}
        missing = sorted(dialogue - referenced)
        if missing:
            raise ValueError(f"{len(missing)} story-range texts have no script occurrence, e.g. {missing[:3]}")
        outside = sorted(k for k in referenced if int(k[9:]) < STORY_FIRST_ID or k[6:8] != "00")
        if outside:
            raise ValueError(f"scripts reference texts outside the story range, e.g. {outside[:3]}")
        if len({r["key"] for r in rows}) != len(self.sources):
            raise ValueError("export does not cover every catalog key exactly once")


def summarize(rows: list[dict]) -> dict:
    by_category: dict[str, dict] = {}
    for row in rows:
        entry = by_category.setdefault(row["category"], {"records": 0, "chars": 0, "unique_sources": set(),
                                                         "unique_chars": 0, "native_page": 0, "terms": 0,
                                                         "flags": Counter(), "existing": Counter()})
        entry["records"] += 1
        entry["chars"] += row["chars"]
        if row["source"] not in entry["unique_sources"]:
            entry["unique_sources"].add(row["source"])
            entry["unique_chars"] += row["chars"]
        entry["native_page"] += row["native_page"]
        entry["terms"] += "terms_section" in row
        entry["flags"].update(row["flags"])
        entry["existing"].update(row.get("existing", {}).keys())
    result = {}
    for category in CATEGORIES:
        if category not in by_category:
            continue
        entry = by_category[category]
        result[category] = {**CATEGORIES[category], "records": entry["records"], "unique": len(entry["unique_sources"]),
                            "chars": entry["chars"], "unique_chars": entry["unique_chars"],
                            "native_page_records": entry["native_page"], "terms_records": entry["terms"],
                            "flags": dict(entry["flags"]),
                            "existing_translations": dict(entry["existing"])}
    return result


def scene_batches(rows: list[dict]) -> dict[int, list[dict]]:
    """Per scene, every dialogue/choice occurrence in script order (a key can recur in shared scenes)."""
    scenes: dict[int, list[dict]] = defaultdict(list)
    for row in rows:
        for occ in (row.get("context") or {}).get("occurrences", []):
            scenes[occ["scene"]].append({"key": row["key"], "category": row["category"], **occ,
                                         "source": row["source"], "display": row["display"]})
    for lines in scenes.values():
        lines.sort(key=lambda o: o["order"])
    return dict(sorted(scenes.items()))


def name_slot_legend() -> dict:
    return {f"<G:{code:04X}>": label for code, label in NAME_SLOTS.items()}
