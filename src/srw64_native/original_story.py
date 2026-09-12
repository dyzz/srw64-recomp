"""Per-scene story documents assembled from the decoded stage scripts.

The story view is a projection of the event IR: it keeps the original Japanese
text, the speaker resolved from the text header, the protagonist route sections
and the condition structure, and summarises the few commands a reader needs
(deployments, BGM, next scene). It never reorders events, evaluates conditions or
invents titles: the chapter title is text 281 + scene index, as displayed by the
intermission code, and every other command is counted as omitted.
"""
from __future__ import annotations

import re

from .catalog import text_key

STORY_SCHEMA = "srw64.story-scene.v1"
NAME_SLOTS = {0x124: "主角昵称", 0x125: "主角全名", 0x126: "主角名字", 0x127: "主角姓氏",
              0x128: "搭档昵称", 0x129: "搭档全名", 0x12A: "搭档名字", 0x12B: "搭档姓氏",
              0x12C: "主角机体名（8010F698，候选）"}
NAME_TOKEN = re.compile(r"(<G:012[4-9A-Ca-c]>)(?:\1)*")
# 801C69D0 stores the protagonist selection as the first scene index and derives the route marker from it.
FIRST_STAGE_PROTAGONIST = {0: (27, 0x3DD3), 1: (28, 0x3DD4), 2: (25, 0x3DD1), 3: (26, 0x3DD2)}
PROTAGONIST_BASE, RIVAL_BASE = 25, 29
PHASES = {12: "opening", 13: "deployment", 14: "ending"}
PHASE_LABELS = {"opening": "开场", "deployment": "初期配置", "map": "战场事件", "ending": "结束"}
MARKER_SECTIONS = {0x3DD0: "共通", 0x3DD1: "アーク路线", 0x3DD2: "セレイン路线", 0x3DD3: "ブラッド路线",
                   0x3DD4: "マナミ路线", 0x3DD5: "リアル系路线", 0x3DD6: "スーパー系路线",
                   0x3DD7: "男性主角", 0x3DD8: "女性主角", 0x3DD9: "选择肢 1", 0x3DDA: "选择肢 2", 0x3DDB: "选择肢 3"}


def story_search_rows(documents: list[dict]) -> list[list]:
    """Search original dialogue with stable event/offset links, including aliases."""
    rows = []
    for doc in documents:
        for event in doc["events"]:
            for line in event["lines"]:
                if line["kind"] != "dialogue":
                    continue
                speaker = line["speaker"]
                names = [speaker["label"]] + [c["label"] for c in speaker.get("candidates", [])]
                rows.append([doc["scene"], event["key"].split(":")[-1], line["offset"],
                             line["text_id"], " / ".join(dict.fromkeys(names)), line["display"]])
    return rows


def display_text(text: str) -> str:
    """Collapse each run of one dynamic-name token into a readable placeholder."""
    def replace(match: re.Match) -> str:
        code = int(match.group(0)[3:7], 16)
        return f"【{NAME_SLOTS[code]}】"
    return NAME_TOKEN.sub(replace, text.replace("<END>", ""))


def field_text(f: dict) -> str:
    return str(f.get("label") or f.get("meaning") or f.get("value"))


def trigger_summary(trigger: dict) -> str:
    fields = {f["name"]: f for f in trigger["fields"]}
    kind = trigger["type"]
    if kind == 0:
        return f"第 {fields['回合数']['value']} 回合起 · {field_text(fields['阶段'])}"
    if kind == 1:
        return "由 3D52 启动的延迟计数"
    if kind == 2:
        gate = fields["门槛变量"]
        return f"{field_text(fields['角色'])} 击破／退场" + ("" if gate["value"] == 0 else f"（{field_text(gate)}）")
    if kind == 3:
        return f"{field_text(fields['角色'])} HP ≤ {fields['百分比']['value']}%"
    if kind in (4, 5):
        return f"{field_text(fields['角色 A'])} 与 {field_text(fields['角色 B'])} 交战（{'战斗后' if kind == 4 else '战斗前'}）"
    if kind == 6:
        return f"敌方全灭（第 {fields['最早回合']['value']} 回合起 · {field_text(fields['阶段'])}）"
    if kind == 7:
        side = "第三方" if fields["阵营选择"]["value"] == 2 else "敌方"
        gate = fields["门槛变量"]
        return f"{side}残存 ≤ {fields['数量上限']['value']} · {field_text(fields['阶段'])}" + ("" if gate["value"] == 0 else f"（{field_text(gate)}）")
    if kind == 8:
        x, y = fields["x 范围"], fields["y 范围"]
        return f"{field_text(fields['目标'])} 到达区域 x[{x['start']}, {x['start'] + x['span']}) y[{y['start']}, {y['start'] + y['span']}) · {field_text(fields['回合或模式'])}"
    return trigger["name"]


def condition_text(item: dict) -> str:
    fields = item.get("fields", [])
    if not fields:
        return item["name"]
    return f"{item['name']}：" + "，".join(f"{f['name']} = {field_text(f)}" for f in fields)


class StoryBuilder:
    def __init__(self, categories: dict, sources: dict, spec: dict):
        self.categories, self.sources, self.spec = categories, sources, spec
        self.actors = {r["key"]: r for r in categories.get("actors", [])}
        self.units = {r["key"]: r for r in categories.get("units", [])}
        self.deployments = {r["key"]: r for r in categories.get("stage_deployments", [])}
        self.events = {r["key"]: r for r in categories.get("stage_events", [])}
        self.scenarios = categories["scenarios"]

    def portrait(self, actor_key: str | None) -> str | None:
        row = self.actors.get(actor_key or "")
        thumbnail = (row or {}).get("thumbnail") or {}
        return thumbnail.get("thumbnail")

    def speaker(self, item: dict, protagonist: dict | None = None) -> dict:
        result = {"label": item.get("speaker_label", "说话人未解析"), "status": item.get("speaker_status", "unparsed")}
        if item.get("speaker_key"):
            result.update(key=item["speaker_key"], portrait=self.portrait(item["speaker_key"]))
        if item.get("speaker_candidates"):
            result["candidates"] = [{"key": c["key"], "label": c["label"], "portrait": self.portrait(c["key"])}
                                    for c in item["speaker_candidates"] if "key" in c]
        if protagonist and result["status"] == "route-relative" and item.get("speaker_id") in (PROTAGONIST_BASE, RIVAL_BASE):
            actor_id = item["speaker_id"] + protagonist["offset"]
            row = self.actors.get(f"base:actors:{actor_id:04d}")
            if row:
                result.update(key=row["key"], label=f"{row['label']}（本话主角{'' if item['speaker_id'] == PROTAGONIST_BASE else '的对手'}）",
                              portrait=self.portrait(row["key"]), status="route-resolved-by-scene")
        return result

    def deployment_note(self, scene: int, group: int) -> str:
        keys = self.scenarios[scene]["scenario"]["deployment_group_keys"].get(str(group), [])
        names = []
        for key in keys:
            row = self.deployments[key]["deployment"]
            unit = self.units.get(f"base:units:{row['unit']:04d}", {}).get("label", f"机体 {row['unit']}")
            actor = self.actors.get(f"base:actors:{row['actor']:04d}", {}).get("label")
            names.append(f"{unit}（{actor}）" if actor else unit)
        if not names:
            return f"登场：组 {group}（本场景配套数据中没有该组）"
        shown = "、".join(names[:6]) + (f" 等 {len(names)} 台" if len(names) > 6 else "")
        return f"登场：组 {group} · {shown}"

    def note(self, scene: int, item: dict) -> str | None:
        opcode, fields = item["opcode"], item.get("fields", [])
        if opcode == 0x3D45:
            return self.deployment_note(scene, fields[0]["value"])
        if opcode == 0x3D3A:
            return "BGM 停止" if fields[0]["value"] == 0 else f"BGM {fields[0]['value']}"
        if opcode == 0x3D4B:
            target = fields[0]
            return f"下一话：{target.get('label') or target.get('meaning') or target['value']}"
        if opcode == 0x3D4A:
            return "关卡胜利结算"
        if opcode == 0x3D4C:
            return "游戏结束"
        if opcode == 0x3D5B:
            return f"资金 +{fields[0]['value'] * 1000}"
        if opcode == 0x3D62:
            return f"角色身份：{field_text(fields[0])} → {field_text(fields[1])}"
        if opcode == 0x3D60:
            return f"我方部队部署于 ({fields[0]['value']}, {fields[1]['value']})，每行 {fields[2]['value']} 台"
        if opcode in (0x3D46, 0x3D4F):
            kind = "单位列表演出 A（候选：退场）" if opcode == 0x3D46 else "单位列表演出 B（候选：登场）"
            return f"{kind}：{field_text(fields[0])}"
        return None

    def event_lines(self, scene: int, event: dict, protagonist: dict | None = None) -> tuple[list[dict], int]:
        lines, omitted = [], 0
        instructions = event["script"]["instructions"]
        for index, item in enumerate(instructions):
            kind = item["kind"]
            base = {"depth": item["depth"], "section": item["section"], "offset": item["offset"]}
            if kind == "dialogue":
                text = item.get("text")
                if text is None:
                    lines.append({"kind": "dialogue", "speaker": {"label": "文本缺失", "status": "unresolved"},
                                  "text_id": item["operands"][0], "text": "", "display": "", "mode": item["dialogue_mode"], **base})
                    continue
                lines.append({"kind": "dialogue", "speaker": self.speaker(item, protagonist), "text_key": item["text_key"],
                              "text_id": item["operands"][0], "text": text, "display": display_text(text),
                              "mode": item["dialogue_mode"], **base})
            elif kind == "marker":
                lines.append({"kind": "section", "marker": f"{item['opcode']:04X}",
                              "label": MARKER_SECTIONS[item["opcode"]], **base})
            elif kind == "condition":
                if item["block"] == "opener":
                    lines.append({"kind": "condition", "text": condition_text(item), **base})
                elif item["block"] == "block-end":
                    lines.append({"kind": "block-end", **base})
                else:
                    lines.append({"kind": "statement", "text": condition_text(item), **base})
            elif kind == "command" and item["opcode"] == 0x3D44:
                text_id, count = item["operands"][0], item["operands"][1]
                options = []
                for k in range(count):
                    key = text_key(0, text_id + k)
                    options.append({"text_key": key, "text": self.sources.get(key, ""),
                                    "display": display_text(self.sources.get(key, ""))})
                lines.append({"kind": "choice", "options": options, **base})
            elif kind == "command":
                text = self.note(scene, item)
                if text is None:
                    omitted += 1
                else:
                    lines.append({"kind": "note", "opcode": f"{item['opcode']:04X}", "text": text, **base})
        return lines, omitted

    def scene_document(self, scene: int) -> dict:
        row = self.scenarios[scene]
        info = row["scenario"]
        title = row.get("title") or {}
        protagonist = None
        if scene in FIRST_STAGE_PROTAGONIST:
            actor_id, marker = FIRST_STAGE_PROTAGONIST[scene]
            actor = self.actors.get(f"base:actors:{actor_id:04d}", {})
            protagonist = {"key": actor.get("key"), "label": actor.get("label"), "marker": f"{marker:04X}",
                           "offset": marker - 0x3DD1, "portrait": self.portrait(actor.get("key")),
                           "basis": "801C69D0：主角选择序号即首个场景索引，并写入对应路线标记。"}
        events = []
        for entry in info["events"]:
            event = self.events[entry["key"]]
            lines, omitted = self.event_lines(scene, event, protagonist)
            events.append({"key": entry["key"], "slot": entry["slot"], "type": entry["type"],
                           "phase": PHASES.get(entry["type"], "map"),
                           "phase_label": PHASE_LABELS[PHASES.get(entry["type"], "map")],
                           "type_name": entry["type_name"],
                           "trigger": trigger_summary(event["script"]["trigger"]),
                           "lines": lines, "commands_omitted": omitted,
                           "dialogue_count": sum(l["kind"] == "dialogue" for l in lines)})
        next_scenes = [int(k[-4:]) for k in info["next_scene_keys"]]
        return {"schema": STORY_SCHEMA, "scene": scene, "title": title.get("text"), "title_key": title.get("key"),
                "protagonist": protagonist,
                "map_key": info["map_key"], "shared_scene_indices": info["shared_scene_indices"],
                "next_scenes": [{"scene": n, "title": (self.scenarios[n].get("title") or {}).get("text")} for n in next_scenes],
                "events": events,
                "counts": {"events": len(events), "dialogue": sum(e["dialogue_count"] for e in events),
                           "lines": sum(len(e["lines"]) for e in events),
                           "commands_omitted": sum(e["commands_omitted"] for e in events)},
                "scope": "原文与说话人来自静态脚本解析；路线段与条件块未求值，未按实际游戏路径筛选。"}

    def build(self) -> tuple[dict, list[dict]]:
        documents = [self.scene_document(scene) for scene in range(len(self.scenarios))]
        previous: dict[int, list[int]] = {}
        for document in documents:
            for target in document["next_scenes"]:
                previous.setdefault(target["scene"], []).append(document["scene"])
        for document in documents:
            document["previous_scenes"] = [{"scene": p, "title": documents[p]["title"]} for p in previous.get(document["scene"], [])]
        index = {"schema": "srw64.story-index.v1", "title_rule": "文本 281 + 场景索引（整备画面 801CE0F8 显示）",
                 "scenes": [{"scene": d["scene"], "title": d["title"], "file": f"story/{d['scene']:04d}.json",
                             "counts": d["counts"], "next_scenes": [n["scene"] for n in d["next_scenes"]],
                             "previous_scenes": [p["scene"] for p in d["previous_scenes"]],
                             "shared_scene_indices": d["shared_scene_indices"]} for d in documents],
                 "name_slots": {f"{code:04X}": label for code, label in NAME_SLOTS.items()},
                 "sections": {f"{code:04X}": label for code, label in MARKER_SECTIONS.items()},
                 "scope": "静态剧情视图；不等于可玩关卡数，不代表实际执行路径。"}
        return index, documents
