#!/usr/bin/env python3
"""Machine-translation drafts for SRW64 dialogue with DeepSeek on Aliyun DashScope.

Input is the local text export (tools/content/export_text.py). Every batch result is kept
under assets/translation-runs/<tag>/ with the request context, usage and checked items, so
reruns resume and nothing is paid for twice. Nothing here writes the language catalog.

    run_mt.py plan                                             # batch counts and size, no requests
    run_mt.py run --tag zh-1 --locale zh-Hans --kind all --env-file ~/.../.env
    run_mt.py review --tag zh-1-review --draft zh-1 --locale zh-Hans --model deepseek-v4-pro-0813
    run_mt.py retry --tag zh-1 --locale zh-Hans                # re-request only failed or missing lines
    run_mt.py report --tag zh-1                                # totals, cost, review.md
    run_mt.py collect --tag zh-1 --tag zh-1-review --output FILE   # later tags override earlier ones
    run_mt.py joins --tag zh-1-joins --draft zh-1 --draft zh-1-review --locale zh-Hans   # page-end marks
"""
from __future__ import annotations

import argparse
import difflib
import json
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import dashscope  # noqa: E402
from references import Characters, glossary  # noqa: E402
from srw64_native.original_story import MARKER_SECTIONS  # noqa: E402
from srw64_native.translation import (CANONICAL, NAME_PLACEHOLDER, DecodeError, Encoded, Renames, decode,  # noqa: E402
                                      encode, chinese_marks, missing_terms, normalize_quotes, ratio_problems, record_problems,
                                      relevant_terms, style_problems)

ROOT = Path(__file__).resolve().parents[2]
EXPORT = ROOT / "assets/text-export"
RUNS = ROOT / "assets/translation-runs"
PROMPT_VERSION = "srw64-mt-v5"
GENERIC_BATTLE_END = 14227  # per-character situation blocks end with the generic soldiers at 14226

WORLD = ("世界观：A.C.（After Colony）191 年，ジオン独立戦争之后，外宇宙的ムゲゾルバドス帝国入侵并占领地球圈；A.C.195 年，"
         "各地反帝国运动兴起。原创主角有四人（真实系アーク／セレイン，超级系ブラッド／マナミ），各有一名对手，由玩家选择。"
         "参战作品：機動戦士ガンダム系列（0079、第08MS小隊、0083、Z、ZZ、逆襲のシャア、F91、Gガンダム、ガンダムW）、"
         "マジンガーZ、グレートマジンガー、グレンダイザー、ゲッターロボ／G、コン・バトラーV、ザンボット3、ダイターン3、"
         "ゴーショーグン、ダンバイン、ダンクーガ、レイズナー、ゴッドマーズ、ジャイアント・ロボ。专名的译法以词表为准。")

COMMON_RULES = """通用规则：
A. 每条原文已按游戏翻页拆成若干“页”（ja 数组，pages 为页数）。tr 必须恰好有 pages 个元素，逐页对应，不能合并、拆分或调换；一句话跨页时也要在对应位置断开。页内不要换行，游戏会自动换行。
B. {placeholders} 这类占位符是游戏运行时填入的玩家角色与对手的名字，⟦G1⟧ 是图标；必须原样保留（连括号一起，不要翻译或改写），每页出现的次数和先后顺序都与原文相同，不要替换成具体名字。
C. 词表：“必用”项必须使用给定译名（大小写照抄）；“参考”项合适时采用。词表之外的专名用该作品的官方或通行译名，没有的就音译，并在 flag 写“新译名：原文=译名”。
D. 保留说话特征：口癖、结巴、拖长音、醉态、敬语的层级；喊招式名时保持气势。原文里的英文照抄。
E. 需要时补上主语或代词，依据是说话人、对话对象、前后文和角色卡的性别；指代无法确定时用名字或中性说法，并在 flag 写“指代不明”。
F. 游戏会把一条台词的各页连成一段显示：页末若是句子或分句的边界，要带上相应标点（句号、逗号、问号等）；句子跨页继续时页末不加标点。
G. 只输出 JSON 对象 {"items":[{"id":"…","tr":["…"],"flag":""}]}；每个待译 id 一条，不能漏项；flag 用中文，只在有疑问时填写（指代不明、双关、新译名、原文疑似有误），否则为空字符串。"""

LANG = {
    "zh-Hans": {
        "name": "简体中文（中国大陆玩家习惯）",
        "style": """中文规则：
1. 忠实、自然、口语化，符合人物身份和语气（军人、贵族、热血少年、反派、老管家等）；不增删信息，不加注释。
2. 引号跟随原文：原文第 1 页开头的「对应译文第 1 页开头的“，最后一页结尾的」对应最后一页结尾的”；不要给每页单独加引号。『』→‘’；（）表示内心独白，保留全角括号。
3. 省略号用“……”，破折号用“——”；！？用全角；数字用半角；外国人名用间隔号“·”。
4. 称谓与军衔：少尉/中尉/大尉=少尉/中尉/上尉，少佐/中佐/大佐=少校/中校/上校，艦長=舰长，隊長=队长，お嬢様=大小姐，閣下/殿=阁下，師匠=师父。名字后的さん、くん、ちゃん多数省略，様按语境译“大人”“小姐”等。""",
    },
    "en": {
        "name": "English",
        "style": """English rules:
1. Faithful, natural, conversational English that fits each character (soldiers, nobles, hot-blooded heroes, villains, an old butler…); no additions, no notes. Contractions are fine.
2. Quotes follow the source: the 「 at the start of page 1 becomes “ and the 」 at the end of the last page becomes ”; do not quote every page separately. 『』 becomes ‘ ’. Parentheses （） mark inner thoughts: keep them as ( ). Use curly quotes only, never straight ".
3. Only ASCII punctuation plus “ ” ‘ ’ — and ’ for apostrophes: ……/… → "...", ！ → "!", ？ → "?", ！？ → "?!". No Japanese or Chinese characters in the output.
4. Names in Western order as in the glossary (Koji Kabuto). Drop -san/-kun/-chan; keep a title where Japanese uses one as a form of address: お嬢様 = "my lady", 師匠 = "Master", 兄さん = "brother" as fits, 閣下 = "Your Excellency", 博士 = "Doctor", 艦長 = "Captain".
5. Ranks: 少尉 Ensign, 中尉 Lieutenant, 大尉 Captain, 少佐 Major, 中佐 Lieutenant Colonel, 大佐 Colonel; 隊長 Commander or Captain as fits.
6. Attack names shouted in battle stay in Title Case as in the glossary ("Getter Beam!"); grunts and cries become natural English ("Ngh!", "Gah!").""",
    },
}

SYSTEM = {
    "story": "你是资深游戏本地化译者，把 1999 年 N64 游戏《超级机器人大战64》的日文剧情对白译成{name}。\n{world}\n"
             "输入 lines 为按剧本顺序排列的台词；ctx 为 true 的行只作上下文，不要翻译；previous 为前一批的结尾及其译文。\n",
    "battle": "你是资深游戏本地化译者，把 N64 游戏《超级机器人大战64》的战斗台词译成{name}。战斗台词在战斗动画中显示，"
              "要短促有力、口语化，符合角色性格。输入 lines 按原数据顺序排列，speaker 为说话人。\n{world}\n",
    "intro": "你是资深游戏本地化译者，把 N64 游戏《超级机器人大战64》开场序章的旁白译成{name}。旁白以纵向缩放字幕逐页显示，"
             "文体庄重、书面，像动画开篇旁白。“A.C.”年号照写。ja 数组的每个元素是一段，tr 数组与之一一对应。\n{world}\n",
}

REVIEW_SYSTEM = ("你是资深游戏本地化审校，审校《超级机器人大战64》日文文本的{name}机翻初稿。每行给出原文 ja 和初稿 tr。\n{world}\n"
                 "请检查：误译或漏译；同一名称、称谓、口癖在本批内前后不一致或与词表不符；人称代词、性别、说话对象弄错；"
                 "语气与人物身份不符；不通顺或翻译腔；页的对应关系错乱（某页的内容跑到别页）。\n"
                 "只返回需要修改的条目，输出 JSON 对象 {{\"items\":[{{\"id\":\"…\",\"revised\":[\"…\"],\"reason\":\"…\"}}]}}：\n"
                 "revised 是按你的意见改好后的完整译文（页数与 ja 相同），必须与 tr 不同，把 reason 里说的修改真正写进去；"
                 "reason 用中文简述改了什么。没有问题的条目不要返回；没有任何需要改的就返回 {{\"items\":[]}}。"
                 "revised 同样必须满足下面全部规则。\n")


JOINS_SYSTEM = ("你是资深游戏本地化审校。《超级机器人大战64》的中文台词原本按原版游戏拆成几页，每页单独显示，"
                "有些页的结尾没有标点；现在游戏把同一条台词的各页连成一段显示，于是有的地方前后两句粘在了一起。\n"
                "每条给出 text（连起来的中文）和 ja（日文原文，作语气参考）。请通读 text，在缺少标点、读起来前后粘连的地方补上标点：\n"
                "- 一个意思说完了就用句号“。”，按语气用“！”“？”；不要用逗号把几句完整的话串成一句；\n"
                "- 只有前后是同一句话的两个分句（如“虽然……，但……”“就算……，也……”）才用逗号“，”；\n"
                "- 句子本来就连着、只是被拆开的地方（如“吉翁公国的独立战争”“被帝国定为A级市民”）不要加。\n"
                "示例：“傻的是你游击队员在战斗中败北是无可奈何的事就算我们现在出手，他们也不会有任何成长”"
                "→“傻的是你。游击队员在战斗中败北是无可奈何的事。就算我们现在出手，他们也不会有任何成长”。\n"
                "只能插入标点，不能删改任何文字，也不要改动已有的标点。\n"
                "输出 JSON 对象 {\"items\":[{\"id\":\"…\",\"text\":\"补好标点的全文\"}]}，每条都要返回。")
FINAL_SYSTEM = ("你是资深游戏本地化审校。下面是《超级机器人大战64》的中文台词，每条给出 text（全文）和 ja（日文原文，作语气参考）。"
                "这些台词在整条的最后（收尾的引号或括号之前）缺少句末标点。请按语气在最后补上：陈述用“。”，感叹用“！”，"
                "疑问用“？”，话没说完、欲言又止用“……”。只能在最后补标点，不能删改任何文字。\n"
                "输出 JSON 对象 {\"items\":[{\"id\":\"…\",\"text\":\"补好标点的全文\"}]}，每条都要返回。")
FINAL_MARKS = ("。", "！", "？", "……", "！？")
CLOSERS = "”’」』）)"
JOIN_MARKS = ("", "。", "，", "！", "？", "……", "——", "、", "；", "！？")
PAGE_END_MARKS = "。，、！？…—；：!?"  # ASCII !? become full-width when written (chinese_marks)


def rules(locale: str) -> str:
    examples = "、".join(list(NAME_PLACEHOLDER.get(locale, NAME_PLACEHOLDER["en"]).values())[:4])
    return COMMON_RULES.replace("{placeholders}", examples)


def system_prompt(kind: str, locale: str) -> str:
    lang = LANG[locale]
    return SYSTEM[kind].format(name=lang["name"], world=WORLD) + lang["style"] + "\n" + rules(locale)


def review_prompt(locale: str) -> str:
    lang = LANG[locale]
    return REVIEW_SYSTEM.format(name=lang["name"], world=WORLD) + lang["style"] + "\n" + rules(locale).split("G.")[0]


# ------------------------------------------------------------------ export input
def load_records() -> dict[str, dict]:
    with (EXPORT / "records.jsonl").open(encoding="utf-8") as file:
        return {r["key"]: r for r in map(json.loads, file)}


def load_scenes() -> dict[int, dict]:
    return {int(p.stem[-4:]): json.loads(p.read_text()) for p in sorted((EXPORT / "story").glob("scene-*.json"))}


def paragraphs(text: str) -> list[str]:
    """Intro pages: visual lines back into paragraphs (a blank line or a full-width indent starts one)."""
    result: list[str] = []
    for line in text.split("\n"):
        if not line.strip():
            result.append("")
        elif line.startswith("　") or not result or result[-1] == "":
            result.append(line.strip("　"))
        else:
            result[-1] += line
    return [p for p in result if p]


# ------------------------------------------------------------------ batches
def story_batches(records: dict, scenes: dict, wanted: set[int] | None, size: int = 40, chars: int = 2400,
                  prefix: str = "story") -> list[dict]:
    owner: dict[str, int] = {}
    for scene_id in sorted(scenes):
        for line in scenes[scene_id]["lines"]:
            owner.setdefault(line["key"], scene_id)
    batches = []
    for scene_id in sorted(scenes):
        if wanted is not None and scene_id not in wanted:
            continue
        scene, seen, sequence = scenes[scene_id], set(), []
        for line in scene["lines"]:
            if line["key"] in seen:
                continue
            seen.add(line["key"])
            sequence.append({**line, "ctx": owner[line["key"]] != scene_id})
        if not any(not l["ctx"] for l in sequence):
            continue
        chunk, count, weight, part = [], 0, 0, 0
        for line in sequence:
            if not line["ctx"] and chunk and (count >= size or weight >= chars):
                batches.append(story_batch(scene, part, chunk, prefix))
                chunk, count, weight, part = [], 0, 0, part + 1
            chunk.append(line)
            if not line["ctx"]:
                count += 1
                weight += records[line["key"]]["chars"]
        if count:
            batches.append(story_batch(scene, part, chunk, prefix))
    return batches


def story_batch(scene: dict, part: int, lines: list[dict], prefix: str) -> dict:
    return {"id": f"{prefix}/s{scene['scene']:04d}-{part:02d}", "kind": "story", "lane": f"s{scene['scene']:04d}",
            "scene": scene["scene"], "part": part, "lines": lines,
            "keys": [l["key"] for l in lines if not l["ctx"]]}


def battle_batches(records: dict, span: tuple[int, int] | None, size: int = 60, limit: int = 80) -> list[dict]:
    keys = sorted((r for r in records.values() if r["category"] in ("battle.quote", "battle.special")),
                  key=lambda r: r["id"])
    if span:
        keys = [r for r in keys if span[0] <= r["id"] <= span[1]]
    batches, chunk = [], []
    for row in keys:
        run_start = row["context"]["position"] == 0
        if chunk and (len(chunk) >= limit or (len(chunk) >= size and run_start)):
            batches.append(battle_batch(chunk))
            chunk = []
        chunk.append(row)
    if chunk:
        batches.append(battle_batch(chunk))
    return batches


def battle_batch(rows: list[dict]) -> dict:
    first = rows[0]["id"]
    return {"id": f"battle/b{first:05d}", "kind": "battle", "lane": f"b{first:05d}", "rows": rows,
            "keys": [r["key"] for r in rows]}


def intro_batches(records: dict) -> list[dict]:
    rows = sorted((r for r in records.values() if r["category"] == "story.intro"), key=lambda r: r["key"])
    return [{"id": "intro/pages", "kind": "intro", "lane": "intro", "rows": rows, "keys": [r["key"] for r in rows]}] if rows else []


# ------------------------------------------------------------------ prompts
class Context:
    def __init__(self, locale: str):
        self.locale = locale
        self.terms = glossary(locale)
        self.characters = Characters(locale)


def encoded_for(record: dict, locale: str) -> Encoded | None:
    if record["category"] == "story.intro":
        return None
    return encode(record["source"], choice=record["category"] == "story.choice", locale=locale)


def source_pages(record: dict, locale: str) -> list[str]:
    return paragraphs(record["source"]) if record["category"] == "story.intro" else encoded_for(record, locale).pages


def where(line: dict) -> str:
    section = MARKER_SECTIONS.get(int(line["section"], 16), line["section"]) if line.get("section") else ""
    phase = {"opening": "开场", "deployment": "初期配置", "map": "战场事件", "ending": "结束"}.get(line["phase"], line["phase"])
    return "｜".join(x for x in (phase, section, line.get("trigger") if line["phase"] == "map" else "") if x)


def term_block(ctx: Context, texts: list[str]) -> list[dict]:
    return [{"ja": t.ja, "tr": t.zh, "kind": "必用" if t.binding else "参考"} for t in relevant_terms(texts, ctx.terms)]


def speaker_cards(ctx: Context, labels: list[str | None]) -> list[dict]:
    names = dict.fromkeys(Characters.base_name(l) for l in labels if l)
    return [ctx.characters.card(n) for n in names if n and n not in ("???", "？？？")]


def story_payload(ctx: Context, batch: dict, records: dict, scenes: dict, previous: list[dict],
                  drafts: dict | None = None) -> tuple[dict, dict]:
    scene = scenes[batch["scene"]]
    ids, lines = {}, []
    for n, line in enumerate(batch["lines"], 1):
        record = records[line["key"]]
        item = {"speaker": line.get("speaker") or "（旁白／系统）", "where": where(line)}
        if line["category"] == "story.choice":
            item["speaker"] = "【选择肢】"
            item["note"] = "玩家可选的选项，每页一个选项"
        pages = source_pages(record, ctx.locale)
        if line["ctx"]:
            lines.append({**item, "ctx": True, "ja": pages,
                          **({"tr": drafts[line["key"]]} if drafts and line["key"] in drafts else {})})
        else:
            entry = {"id": f"L{n}", **item, "pages": len(pages), "ja": pages}
            if drafts is not None:
                entry["tr"] = drafts.get(line["key"])
            lines.append(entry)
            ids[f"L{n}"] = line["key"]
    protagonist = scene.get("protagonist") or {}
    texts = [p for l in lines for p in l["ja"]] + [c.get("ja", "") for c in previous]
    payload = {"scene": {"title": scene["title"], "protagonist": protagonist.get("label"),
                         "note": "说话人“（本话主角）”由首话路线确定；“（按段落 3DDx）”表示只在该主角路线出现；"
                                 "【主角…】【搭档…】是玩家角色与对手的名字。"},
               "speakers": speaker_cards(ctx, [l.get("speaker") for l in batch["lines"]]),
               "terms": term_block(ctx, texts), **({"previous": previous} if previous else {}), "lines": lines}
    return payload, ids


def battle_payload(ctx: Context, batch: dict, drafts: dict | None = None) -> tuple[dict, dict]:
    ids, lines = {}, []
    for n, row in enumerate(batch["rows"], 1):
        pages = source_pages(row, ctx.locale)
        item = {"id": f"L{n}", "speaker": row["context"].get("speaker") or "？", "pages": len(pages), "ja": pages}
        if row["context"].get("combo_lead"):
            item["note"] = "合体技起句"
        if drafts is not None:
            item["tr"] = drafts.get(row["key"])
        lines.append(item)
        ids[f"L{n}"] = row["key"]
    first = batch["rows"][0]["id"]
    section = ("各角色的通用情境台词：每人一块，顺序大致为 攻击→被击坠→大伤害→小伤害→回避→防御光束→弹尽／射程外"
               if first < GENERIC_BATTLE_END else "武器专用台词与多人合体技对话（同一招式的几句是不同角色轮流说的）")
    payload = {"section": section, "speakers": speaker_cards(ctx, [l["speaker"] for l in lines]),
               "terms": term_block(ctx, [p for l in lines for p in l["ja"]]), "lines": lines}
    return payload, ids


def intro_payload(ctx: Context, batch: dict, drafts: dict | None = None) -> tuple[dict, dict]:
    ids, pages = {}, []
    for row in batch["rows"]:
        pid = f"P{row['context']['resource']}"
        ja = paragraphs(row["source"])
        pages.append({"id": pid, "played_in": row["context"]["played_in"], "pages": len(ja), "ja": ja,
                      **({"tr": drafts.get(row["key"])} if drafts is not None else {})})
        ids[pid] = row["key"]
    payload = {"note": "common 为四条路线共用；route-1 武机霸拳流的少年（ブラッド），route-2 罗姆菲拉的大小姐（マナミ），"
                       "route-3 殖民卫星出生的少年（アーク），route-4 游击队少女（セレイン）。",
               "terms": term_block(ctx, [p for x in pages for p in x["ja"]]), "lines": pages}
    return payload, ids


def localized(tr, locale: str):
    """A stored translation with its name placeholders in this locale's form (drafts may predate a change)."""
    if not isinstance(tr, list):
        return tr
    table = NAME_PLACEHOLDER.get(locale, NAME_PLACEHOLDER["en"])
    out = []
    for page in tr:
        if isinstance(page, str):
            for text, code in CANONICAL.items():
                page = page.replace(text, table[code])
        out.append(page)
    return out


def payload_for(ctx, batch, records, scenes, previous, drafts=None):
    if batch["kind"] == "story":
        return story_payload(ctx, batch, records, scenes, previous, drafts)
    if batch["kind"] == "battle":
        return battle_payload(ctx, batch, drafts)
    return intro_payload(ctx, batch, drafts)


# ------------------------------------------------------------------ checking
def check_item(record: dict, tr, terms, locale: str) -> dict:
    result: dict = {"key": record["key"], "tr": tr}
    pages = tr if isinstance(tr, list) else [tr]
    if record["category"] == "story.intro":
        ja = paragraphs(record["source"])
        if len(pages) != len(ja) or not all(isinstance(p, str) for p in pages):
            result["errors"] = [f"段数应为 {len(ja)}，实际给了 {len(pages)}"]
            return result
        result["target"] = "\n\n".join(pages)
    else:
        encoded = encoded_for(record, locale)
        try:
            result["target"] = decode(encoded, pages)
        except DecodeError as exc:
            result["errors"] = [str(exc)]
            return result
        ja = encoded.pages
    problems = ([p for a, b in zip(ja, pages) for p in style_problems(a, b, locale)]
                + record_problems(pages, locale) + ratio_problems(ja, pages, locale))
    if problems:
        result["errors"] = problems
    missing = missing_terms(ja, pages, terms)
    if missing:
        result["warnings"] = ["未用必用译名：" + "、".join(missing)]
    return result


def response_items(doc: dict) -> list[dict]:
    """Items as the model returned them; tolerate {"items": {id: {...}}} and drop non-objects."""
    items = doc.get("items", [])
    if isinstance(items, dict):
        items = [{"id": k, **v} if isinstance(v, dict) else {"id": k, "tr": v} for k, v in items.items()]
    return [i for i in items if isinstance(i, dict)]


# ------------------------------------------------------------------ running
def request(args, system: str, payload: dict) -> tuple[dashscope.Call, dict]:
    messages = [{"role": "system", "content": system},
                {"role": "user", "content": json.dumps(payload, ensure_ascii=False)}]
    call = dashscope.chat(messages, credentials=args.credentials, model=args.model)
    return call, dashscope.parse_json(call.text)


def usage_of(call: dashscope.Call) -> dict:
    return {k: v for k, v in call.__dict__.items() if k != "text"}


def save(out: Path, result: dict) -> None:
    out.parent.mkdir(parents=True, exist_ok=True)
    tmp = out.with_suffix(".tmp")
    tmp.write_text(json.dumps(result, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    tmp.replace(out)


def done(out: Path) -> dict | None:
    if out.exists():
        doc = json.loads(out.read_text())
        if doc.get("status") == "done":
            return doc
    return None


def run_batch(ctx: Context, args, batch: dict, records: dict, scenes: dict, previous: list[dict]) -> dict:
    out = RUNS / args.tag / f"{batch['id']}.json"
    finished = done(out)
    if finished:
        return finished
    payload, ids = payload_for(ctx, batch, records, scenes, previous)
    system = system_prompt(batch["kind"], ctx.locale)
    usage = []
    call, doc = request(args, system, payload)
    usage.append(usage_of(call))
    items = {}
    for item in response_items(doc):
        if item.get("id") in ids:
            checked = check_item(records[ids[item["id"]]], item.get("tr"), ctx.terms, ctx.locale)
            checked["flag"] = item.get("flag") or ""
            items[item["id"]] = checked
    bad = {i: items.get(i) for i in ids if i not in items or items[i].get("errors")}
    if bad and not args.no_repair:
        repair = {**payload, "lines": [
            {**line, "problem": ("上次漏译" if bad[line["id"]] is None else "；".join(bad[line["id"]]["errors"])),
             **({"previous_tr": bad[line["id"]]["tr"]} if bad[line["id"]] else {})}
            for line in payload["lines"] if line.get("id") in bad],
            "instruction": "以下条目上次的译文有问题（见 problem），请只重译这些条目，修正问题。"}
        call, doc = request(args, system, repair)
        usage.append(usage_of(call))
        for item in response_items(doc):
            if item.get("id") in bad:
                checked = check_item(records[ids[item["id"]]], item.get("tr"), ctx.terms, ctx.locale)
                checked["flag"] = item.get("flag") or ""
                old = items.get(item["id"])
                if old is None or len(checked.get("errors", [])) < len(old.get("errors", [])):
                    checked["repaired"] = True
                    items[item["id"]] = checked
    result = {"schema": "srw64.mt-batch.v1", "status": "done", "id": batch["id"], "kind": batch["kind"],
              "locale": ctx.locale, "stage": "draft", "model": args.model, "prompt_version": PROMPT_VERSION,
              "created": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "usage": usage,
              "items": [{"id": i, **(items.get(i) or {"key": ids[i], "errors": ["漏译"]})} for i in ids],
              "context_payload": payload}
    save(out, result)
    ok = sum(not x.get("errors") for x in result["items"])
    print(f"{batch['id']}: {ok}/{len(ids)} ok, {sum(u['prompt_tokens'] for u in usage)}+"
          f"{sum(u['completion_tokens'] for u in usage)} tokens, {sum(u['elapsed'] for u in usage):.0f}s", flush=True)
    return result


def run_lane(ctx: Context, args, lane: list[dict], records: dict, scenes: dict) -> None:
    previous: list[dict] = []
    for batch in lane:
        try:
            result = run_batch(ctx, args, batch, records, scenes, previous)
        except Exception as exc:  # one bad response must not stop the other lanes; rerun resumes it
            print(f"{batch['id']}: FAILED {type(exc).__name__}: {exc}", flush=True)
            return
        if batch["kind"] == "story":
            tail = [x for x in result["items"] if x.get("target")][-12:]
            previous = [{"ja": records[x["key"]]["display"], "tr": "／".join(x["tr"])} for x in tail]


def parse_ids(text: str | None) -> tuple[int, int] | None:
    if not text:
        return None
    a, b = text.split("-")
    return int(a), int(b)


def select(args, records, scenes, review: bool = False) -> list[dict]:
    wanted = {int(s) for s in args.scenes.split(",")} if getattr(args, "scenes", None) else None
    batches = []
    if args.kind in ("story", "all"):
        batches += (story_batches(records, scenes, wanted, size=70, chars=4200) if review
                    else story_batches(records, scenes, wanted))
    if args.kind in ("battle", "all"):
        batches += battle_batches(records, parse_ids(getattr(args, "ids", None)))
    if args.kind in ("intro", "all"):
        batches += intro_batches(records)
    return batches


def cmd_plan(args) -> None:
    records, scenes = load_records(), load_scenes()
    batches = select(args, records, scenes)
    kinds: dict[str, dict] = {}
    for b in batches:
        k = kinds.setdefault(b["kind"], {"batches": 0, "items": 0, "chars": 0, "lanes": set()})
        k["batches"] += 1
        k["items"] += len(b["keys"])
        k["chars"] += sum(records[key]["chars"] for key in b["keys"])
        k["lanes"].add(b["lane"])
    print(json.dumps({k: {**v, "lanes": len(v["lanes"])} for k, v in kinds.items()}, ensure_ascii=False, indent=1))


def cmd_run(args) -> None:
    args.credentials = dashscope.load_env(args.env_file)
    records, scenes = load_records(), load_scenes()
    ctx = Context(args.locale)
    lanes: dict[str, list[dict]] = {}
    for batch in select(args, records, scenes):
        lanes.setdefault(batch["lane"], []).append(batch)
    print(f"{sum(map(len, lanes.values()))} batches in {len(lanes)} lanes, {args.locale}, model {args.model}", flush=True)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for future in [pool.submit(run_lane, ctx, args, lane, records, scenes) for lane in lanes.values()]:
            future.result()


# ------------------------------------------------------------------ review
def effective(tags: list[str]) -> dict[str, dict]:
    """key -> the latest error-free item over the given runs (later tags override earlier ones)."""
    result: dict[str, dict] = {}
    for tag in tags:
        for path in sorted((RUNS / tag).rglob("*.json")):
            doc = json.loads(path.read_text())
            if doc.get("schema") != "srw64.mt-batch.v1":
                continue
            for item in doc["items"]:
                if item.get("target") and not item.get("errors"):
                    result[item["key"]] = {**item, "model": doc["model"], "prompt": doc["prompt_version"],
                                           "run": tag, "stage": doc.get("stage", "draft"), "locale": doc.get("locale")}
    return result


def review_batch(ctx: Context, args, batch: dict, records: dict, scenes: dict, drafts: dict) -> None:
    out = RUNS / args.tag / f"{batch['id']}.json"
    if done(out):
        return
    current = {k: localized(v["tr"], ctx.locale) for k, v in drafts.items()}
    payload, ids = payload_for(ctx, batch, records, scenes, [], current)
    ids = {i: k for i, k in ids.items() if k in drafts}  # untranslated lines wait for a draft rerun
    payload["lines"] = [l for l in payload["lines"] if l.get("id") in ids or l.get("ctx")]
    if not ids:
        return
    call, doc = request(args, review_prompt(ctx.locale), payload)
    items = []
    for item in response_items(doc):
        key = ids.get(item.get("id"))
        if not key:
            continue
        checked = check_item(records[key], item.get("revised", item.get("tr")), ctx.terms, ctx.locale)
        checked["reason"] = item.get("reason") or ""
        checked["draft"] = drafts[key]["tr"]
        if not checked.get("errors") and checked["target"] == drafts[key]["target"]:
            checked["errors"] = ["与初稿相同"]
        items.append({"id": item["id"], **checked})
    result = {"schema": "srw64.mt-batch.v1", "status": "done", "id": batch["id"], "kind": batch["kind"],
              "locale": ctx.locale, "stage": "review", "model": args.model, "prompt_version": PROMPT_VERSION,
              "draft_runs": args.draft, "created": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
              "usage": [usage_of(call)], "reviewed": len(ids), "items": items}
    save(out, result)
    changed = sum(not i.get("errors") for i in items)
    print(f"{batch['id']}: {changed}/{len(ids)} changed, {call.prompt_tokens}+{call.completion_tokens} tokens, "
          f"{call.elapsed:.0f}s", flush=True)


def cmd_review(args) -> None:
    args.credentials = dashscope.load_env(args.env_file)
    records, scenes = load_records(), load_scenes()
    ctx = Context(args.locale)
    drafts = effective(args.draft)
    batches = select(args, records, scenes, review=True)
    print(f"{len(batches)} review batches over {len(drafts)} drafts, {args.locale}, model {args.model}", flush=True)

    def guarded(batch):
        try:
            review_batch(ctx, args, batch, records, scenes, drafts)
        except Exception as exc:
            print(f"{batch['id']}: FAILED {type(exc).__name__}: {exc}", flush=True)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        list(pool.map(guarded, batches))


# ------------------------------------------------------------------ retry
class Blocked(Exception):
    """DashScope's input inspection refused the request (HTTP 400 data_inspection_failed)."""


def retry_request(ctx: Context, args, batch: dict, records: dict, scenes: dict, wanted: set[str],
                  current: dict[str, dict], context: bool) -> tuple[dict, list[dict]]:
    payload, ids = payload_for(ctx, batch, records, scenes, [])
    lines = []
    for line in payload["lines"]:
        key = ids.get(line.get("id"))
        if key in wanted:
            old = current.get(key) or {}
            lines.append({**line, **({"problem": "；".join(old["errors"])} if old.get("errors") else {}),
                          **({"previous_tr": localized(old["tr"], ctx.locale)} if old.get("tr") else {})})
        elif context:
            line = {k: v for k, v in line.items() if k not in ("id", "pages")}
            done_tr = (current.get(key) or {}).get("tr") if key else None
            lines.append({**line, "ctx": True, **({"tr": localized(done_tr, ctx.locale)}
                                                  if done_tr and not current[key].get("errors") else {})})
    payload = {**payload, "lines": lines,
               "instruction": "只翻译带 id 的条目（其中有 problem 的是上次译文的问题，请修正）；其余行只作上下文。"}
    try:
        call, doc = request(args, system_prompt(batch["kind"], ctx.locale), payload)
    except RuntimeError as exc:
        if "data_inspection_failed" in str(exc):
            raise Blocked(str(exc)) from exc
        raise
    return usage_of(call), [dict(i, key=ids[i["id"]]) for i in response_items(doc) if ids.get(i.get("id")) in wanted]


def retry_keys(ctx, args, batch, records, scenes, wanted, current, usage, depth=0) -> dict[str, dict]:
    """Translate the wanted keys; on an inspection refusal drop the context, then bisect to the blocked line."""
    for context in (True, False):
        try:
            used, items = retry_request(ctx, args, batch, records, scenes, wanted, current, context)
            usage.append(used)
            return {i["key"]: i for i in items}
        except Blocked:
            continue
    if len(wanted) == 1:
        (key,) = wanted
        return {key: {"key": key, "blocked": True}}
    ordered = sorted(wanted)
    half = len(ordered) // 2
    result = retry_keys(ctx, args, batch, records, scenes, set(ordered[:half]), current, usage, depth + 1)
    result.update(retry_keys(ctx, args, batch, records, scenes, set(ordered[half:]), current, usage, depth + 1))
    return result


def cmd_retry(args) -> None:
    """Re-request only the lines of a run that failed a check or were never written, batch by batch."""
    args.credentials = dashscope.load_env(args.env_file)
    records, scenes = load_records(), load_scenes()
    ctx = Context(args.locale)
    work = []
    for batch in select(args, records, scenes):
        out = RUNS / args.tag / f"{batch['id']}.json"
        doc = done(out)
        if doc is None:
            work.append((batch, out, None, set(batch["keys"])))
        else:
            bad = {i["key"] for i in doc["items"] if i.get("errors")}
            if bad:
                work.append((batch, out, doc, bad))
    print(f"{len(work)} batches to retry, {sum(len(w[3]) for w in work)} lines, {args.locale}", flush=True)

    def one(entry):
        batch, out, doc, wanted = entry
        try:
            current = {i["key"]: i for i in (doc or {}).get("items", [])}
            usage: list[dict] = []
            got = retry_keys(ctx, args, batch, records, scenes, wanted, current, usage)
            fixed = 0
            for key in wanted:
                item = got.get(key)
                if item is None:
                    new = {"key": key, "errors": ["漏译"]}
                elif item.get("blocked"):
                    new = {"key": key, "errors": ["内容审核拦截（data_inspection_failed），需人工翻译"]}
                else:
                    new = check_item(records[key], item.get("tr"), ctx.terms, ctx.locale)
                    new["flag"] = item.get("flag") or ""
                old = current.get(key)
                if old is None or len(new.get("errors", [])) < len(old.get("errors", [])):
                    current[key] = {**new, "retried": True}
                    fixed += not new.get("errors")
            ids = {k: i for i, k in enumerate(batch["keys"])}
            items = [{"id": f"K{ids[k]}", **v} for k, v in sorted(current.items(), key=lambda kv: ids.get(kv[0], 0))]
            result = doc or {"schema": "srw64.mt-batch.v1", "status": "done", "id": batch["id"], "kind": batch["kind"],
                             "locale": ctx.locale, "stage": "draft", "model": args.model,
                             "prompt_version": PROMPT_VERSION, "created": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
                             "usage": []}
            result = {**result, "items": items, "usage": result["usage"] + usage,
                      "retries": result.get("retries", 0) + 1}
            save(out, result)
            print(f"{batch['id']}: {fixed}/{len(wanted)} fixed, {len(usage)} requests", flush=True)
        except Exception as exc:
            print(f"{batch['id']}: FAILED {type(exc).__name__}: {exc}", flush=True)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        list(pool.map(one, work))


# ------------------------------------------------------------------ inspection and output
# ------------------------------------------------------------------ page ends
def bare_ends(pages: list[str]) -> list[int]:
    """Pages (all but the last) whose end has no punctuation."""
    return [i for i, page in enumerate(pages[:-1]) if page.rstrip()[-1:] not in PAGE_END_MARKS]


def final_end(pages: list[str]) -> int | None:
    """Where the last page's closing mark belongs when it has none: before closing quotes."""
    core = pages[-1].rstrip().rstrip(CLOSERS).rstrip()
    return len(core) if core and core[-1] not in PAGE_END_MARKS else None


def inserted_at(original: str, revised: str, positions: set[int]) -> dict[int, str]:
    """Punctuation the revision inserts at the given offsets of the original; everything else is ignored."""
    result = {}
    for op, i1, i2, j1, j2 in difflib.SequenceMatcher(None, original, revised, autojunk=False).get_opcodes():
        piece = revised[j1:j2]
        if op == "insert" and i1 in positions and piece in JOIN_MARKS:
            result[i1] = piece
    return result


def joins_batch(ctx: Context, args, batch: dict, records: dict, drafts: dict) -> None:
    """Insert punctuation where a page without a closing mark now runs into the next one.

    The model punctuates the joined text; only marks it inserts exactly at those page ends are
    kept, and only ones from JOIN_MARKS. Every other character stays as it was."""
    out = RUNS / args.tag / f"{batch['id']}.json"
    if done(out):
        return
    keys = {str(n + 1): key for n, key in enumerate(batch["keys"])}
    current = {k: localized(drafts[k]["tr"], ctx.locale) for k in batch["keys"]}
    payload = {"lines": [{"id": i, "speaker": speaker(records[k]), "text": "".join(current[k]),
                          "ja": "".join(source_pages(records[k], ctx.locale))} for i, k in keys.items()]}
    call, doc = request(args, FINAL_SYSTEM if args.final else JOINS_SYSTEM, payload)
    items = []
    answered = {str(item.get("id")): item for item in response_items(doc)}
    for i, key in keys.items():
        pages, item = current[key], answered.get(i)
        if not item or not isinstance(item.get("text"), str):
            items.append({"id": i, "key": key, "errors": ["漏项"]})
            continue
        # offset in the joined text -> (page, offset in the page)
        offsets, at = {}, 0
        for n, page in enumerate(pages):
            if args.final and n == len(pages) - 1 and final_end(pages) is not None:
                offsets[at + final_end(pages)] = (n, final_end(pages))
                # A mark the model put after the closing quote goes inside it.
                offsets.setdefault(at + len(page), (n, final_end(pages)))
            elif not args.final and n in bare_ends(pages):
                offsets[at + len(page)] = (n, len(page))
            at += len(page)
        allowed = FINAL_MARKS if args.final else JOIN_MARKS
        added = {offsets[k]: v for k, v in inserted_at("".join(pages), item["text"], set(offsets)).items() if v in allowed}
        revised = list(pages)
        for (n, within), mark in added.items():
            revised[n] = revised[n][:within] + mark + revised[n][within:]
        checked = check_item(records[key], revised, ctx.terms, ctx.locale)
        # Only page-end marks change: keep what the draft or review said about the line.
        base = drafts[key].get("base_stage") if drafts[key]["stage"] == "joins" else drafts[key]["stage"]
        checked.update({"draft": drafts[key]["tr"], "base_stage": base,
                        "marks": [added.get(place, "") for place in sorted(set(offsets.values()))],
                        "reason": drafts[key].get("reason") or "", "flag": drafts[key].get("flag") or ""})
        if not checked.get("errors") and checked["target"] == drafts[key]["target"]:
            checked["errors"] = ["与初稿相同"]
        items.append({"id": i, **checked})
    result = {"schema": "srw64.mt-batch.v1", "status": "done", "id": batch["id"], "kind": "story",
              "locale": ctx.locale, "stage": "joins", "model": args.model, "prompt_version": PROMPT_VERSION,
              "draft_runs": args.draft, "created": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
              "usage": [usage_of(call)], "reviewed": len(keys), "items": items}
    save(out, result)
    changed = sum(not i.get("errors") for i in items)
    print(f"{batch['id']}: {changed}/{len(keys)} changed, {call.prompt_tokens}+{call.completion_tokens} tokens, "
          f"{call.elapsed:.0f}s", flush=True)


def speaker(record: dict) -> str:
    ctx = record.get("context") or {}
    return ctx.get("speaker") or ((ctx.get("occurrences") or [{}])[0].get("speaker")) or ""


def cmd_joins(args) -> None:
    """Page-end punctuation for records the game now shows as one text (docs/design/dialogue-typesetting.md §3)."""
    args.credentials = dashscope.load_env(args.env_file)
    records = load_records()
    ctx = Context(args.locale)
    drafts = effective(args.draft)
    wanted = (lambda pages: final_end(pages) is not None) if args.final else bare_ends
    keys = sorted((k for k, v in drafts.items() if records[k]["category"] == "story.dialogue"
                   and isinstance(v.get("tr"), list) and wanted(localized(v["tr"], args.locale))),
                  key=lambda k: records[k]["id"])
    prefix = "final" if args.final else "joins"
    if args.missing:
        # Lines a finished pass left without an answer (dropped or invalid): ask again, once.
        failed, answered = set(), set()
        for path in sorted((RUNS / args.tag).glob(f"{prefix}*/*.json")):
            for i in json.loads(path.read_text())["items"]:
                ok = not i.get("errors") or i["errors"] == ["与初稿相同"]
                (answered if ok else failed).add(i["key"])
        keys = [k for k in keys if k in failed - answered]
        round_ = 1
        while (RUNS / args.tag / f"{prefix}-missing{round_ if round_ > 1 else ''}").exists():
            round_ += 1
        prefix = f"{prefix}-missing{round_ if round_ > 1 else ''}"
    if args.limit:
        keys = keys[:args.limit]
    batches = [{"id": f"{prefix}/{n // args.size:04d}", "keys": keys[n:n + args.size]} for n in range(0, len(keys), args.size)]
    print(f"{len(batches)} batches over {len(keys)} records with unpunctuated page ends, {args.locale}, "
          f"model {args.model}", flush=True)

    def guarded(batch):
        try:
            joins_batch(ctx, args, batch, records, drafts)
        except Exception as exc:
            print(f"{batch['id']}: FAILED {type(exc).__name__}: {exc}", flush=True)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        list(pool.map(guarded, batches))


# ------------------------------------------------------------------ one-character names
NAMES_SYSTEM = ("你是资深游戏本地化审校。《超级机器人大战64》里一个角色的中文译名改了：日文「{ja}」，旧译“{old}”，新译“{new}”。"
                "旧译只有一个字，也会出现在别的词里（例如“修理”“修行”“方法”“索尔迪法”），所以不能整体替换。\n"
                "每条给出 text（中文，每个“{old}”前面标了编号〔1〕〔2〕…）和 ja（日文原文）。"
                "请判断哪些编号的“{old}”是在指这个角色（日文里对应「{ja}」）。\n"
                "输出 JSON 对象 {{\"items\":[{{\"id\":\"…\",\"replace\":[1,2]}}]}}，每条都要返回；一个都不是就给空数组。")


def numbered(pages: list[str], old: str) -> str:
    n, out = 0, []
    for page in pages:
        parts = page.split(old)
        text = parts[0]
        for part in parts[1:]:
            n += 1
            text += f"〔{n}〕{old}{part}"
        out.append(text)
    return "｜".join(out)


def swapped(pages: list[str], old: str, new: str, chosen: set[int]) -> list[str]:
    n, out = 0, []
    for page in pages:
        parts = page.split(old)
        text = parts[0]
        for part in parts[1:]:
            n += 1
            text += (new if n in chosen else old) + part
        out.append(text)
    return out


def names_batch(ctx: Context, args, batch: dict, records: dict, drafts: dict, name: tuple[str, str, str]) -> None:
    out = RUNS / args.tag / f"{batch['id']}.json"
    if done(out):
        return
    ja, old, new = name
    keys = {str(n + 1): key for n, key in enumerate(batch["keys"])}
    # Known longer names holding the character stay as they are: renamed full names (修·撒玛 →
    # 座间翔) are left to renames.json, other terms (索尔迪法) are not this character at all.
    masks = [n for n in renames(ctx.locale).names if old in n and n != old]

    def masked(page: str) -> str:
        for n, m in enumerate(masks):
            page = page.replace(m, f"\x00{n}\x01")
        return page

    def unmasked(page: str) -> str:
        for n, m in enumerate(masks):
            page = page.replace(f"\x00{n}\x01", m)
        return page
    current = {k: [masked(p) for p in localized(drafts[k]["tr"], ctx.locale)] for k in batch["keys"]}
    payload = {"lines": [{"id": i, "text": unmasked(numbered(current[k], old)), "ja": "｜".join(source_pages(records[k], ctx.locale))}
                         for i, k in keys.items()]}
    call, doc = request(args, NAMES_SYSTEM.format(ja=ja, old=old, new=new), payload)
    answered = {str(item.get("id")): item for item in response_items(doc)}
    items = []
    for i, key in keys.items():
        pages, chosen = current[key], (answered.get(i) or {}).get("replace")
        count = sum(p.count(old) for p in pages)
        if not count:  # only inside a renamed full name, which renames.json handles
            items.append({"id": i, "key": key, "errors": ["与初稿相同"]})
            continue
        if not (isinstance(chosen, list) and all(isinstance(c, int) and 1 <= c <= count for c in chosen)):
            items.append({"id": i, "key": key, "errors": ["漏项或编号无效"]})
            continue
        revised = [unmasked(p) for p in swapped(pages, old, new, set(chosen))]
        checked = check_item(records[key], revised, ctx.terms, ctx.locale)
        base = drafts[key].get("base_stage") if drafts[key]["stage"] == "joins" else drafts[key]["stage"]
        checked.update({"draft": drafts[key]["tr"], "base_stage": base, "rename": [ja, old, new], "replace": sorted(chosen),
                        "reason": drafts[key].get("reason") or "", "flag": drafts[key].get("flag") or ""})
        if not checked.get("errors") and checked["target"] == drafts[key]["target"]:
            checked["errors"] = ["与初稿相同"]
        items.append({"id": i, **checked})
    save(out, {"schema": "srw64.mt-batch.v1", "status": "done", "id": batch["id"], "kind": "story", "locale": ctx.locale,
               "stage": "joins", "model": args.model, "prompt_version": PROMPT_VERSION, "draft_runs": args.draft,
               "created": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "usage": [usage_of(call)], "reviewed": len(keys), "items": items})
    print(f"{batch['id']}: {sum(not x.get('errors') for x in items)}/{len(keys)} changed, "
          f"{call.prompt_tokens}+{call.completion_tokens} tokens, {call.elapsed:.0f}s", flush=True)


def cmd_names(args) -> None:
    """A one-character name that changed: only the mentions of that character (docs/design/translation-plan.md)."""
    args.credentials = dashscope.load_env(args.env_file)
    records = load_records()
    ctx = Context(args.locale)
    drafts = effective(args.draft)
    answered = set()
    for path in (RUNS / args.tag).rglob("*.json") if args.retry else []:
        answered |= {i["key"] for i in json.loads(path.read_text())["items"]
                     if not i.get("errors") or i["errors"] == ["与初稿相同"]}
    batches = []
    for spec in args.name:
        ja, old, new = spec.split("=")
        plain = ja.replace("・", "")
        keys = sorted((k for k, v in drafts.items() if isinstance(v.get("tr"), list)
                       and plain in records[k]["source"].replace("・", "")
                       and any(old in p for p in localized(v["tr"], args.locale) if isinstance(p, str))),
                      key=lambda k: records[k]["id"])
        if args.retry:
            keys = [k for k in keys if k not in answered]
        prefix = f"names-{ja}" + ("-retry" if args.retry else "")
        batches += [({"id": f"{prefix}/{n // args.size:04d}", "keys": keys[n:n + args.size]}, (ja, old, new))
                    for n in range(0, len(keys), args.size)]
        print(f"{ja} {old}→{new}: {len(keys)} lines", flush=True)

    def guarded(job):
        try:
            names_batch(ctx, args, job[0], records, drafts, job[1])
        except Exception as exc:
            print(f"{job[0]['id']}: FAILED {type(exc).__name__}: {exc}", flush=True)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        list(pool.map(guarded, batches))


def cmd_recheck(args) -> None:
    """Re-run the mechanical checks on stored draft results (after a checker change); no requests."""
    records = load_records()
    terms: dict[str, list] = {}
    for path in sorted((RUNS / args.tag).rglob("*.json")):
        doc = json.loads(path.read_text())
        if doc.get("schema") != "srw64.mt-batch.v1" or doc.get("stage", "draft") != "draft":
            continue
        locale = doc.get("locale", "zh-Hans")
        terms.setdefault(locale, glossary(locale))
        for n, item in enumerate(doc["items"]):
            tr = item.get("tr", item.get("zh"))
            if tr is None:
                continue
            checked = check_item(records[item["key"]], tr, terms[locale], locale)
            doc["items"][n] = {"id": item["id"], **checked, **{k: item[k] for k in ("flag", "repaired") if k in item}}
        save(path, doc)


_RENAMES: dict[str, Renames] = {}


def renames(locale: str) -> Renames:
    """Term renames made after the drafts (content/translation/renames.json), whole names only."""
    if locale not in _RENAMES:
        path = ROOT / "content/translation/renames.json"
        rows = json.loads(path.read_text(encoding="utf-8"))["renames"] if path.exists() else []
        pairs = {r[locale]["old"]: r[locale]["new"] for r in rows if locale in r}
        gates: dict[str, set[str]] = {}
        for r in rows:
            if locale in r and r.get("gate", True):
                gates.setdefault(r[locale]["old"], set()).update(r["ja"])
        sections = json.loads((ROOT / f"content/locales/terms/{locale}.json").read_text(encoding="utf-8"))["sections"]
        protected = {v.strip() for name in ("units", "weapons", "pilots", "pilot_full_names", "character_list", "series",
                                            "default_names")
                     for v in (dict(sections[name]).values() if isinstance(sections[name], list) else sections[name].values())}
        story = json.loads((ROOT / "content/translation/story-terms.json").read_text(encoding="utf-8"))
        terms = story.get("terms", story)
        for term in (terms if isinstance(terms, list) else terms.values()):
            value = term.get(locale) or (term.get("zh") if locale == "zh-Hans" else None)
            if isinstance(value, str):
                protected.add(value.strip())
        protected |= set(pairs.values())
        _RENAMES[locale] = Renames(pairs, protected, latin=locale == "en", gates=gates)
    return _RENAMES[locale]


def final_target(record: dict, item: dict, locale: str) -> str:
    """The shipped text: the checked translation with quote marks normalized to mirror the source."""
    pages = localized(item["tr"], locale) if isinstance(item.get("tr"), list) else None
    if not pages:
        return item["target"]
    pages = chinese_marks(normalize_quotes(source_pages(record, locale), pages, locale), locale)
    pages = [renames(locale).apply(p, record["source"]) for p in pages]
    if record["category"] == "story.intro":
        return "\n\n".join(pages)
    try:
        return decode(encoded_for(record, locale), pages)
    except DecodeError:
        return item["target"]


def cmd_collect(args) -> None:
    """Checked drafts (with review changes layered on) as locale entries; verified with compile_locale.

    The output is a standalone file; merging into content/locales/<locale>.json is a separate,
    coordinated step because the term table rewrites that file too."""
    from srw64_native.catalog import compile_locale, source_catalog
    sources, hashes, _ = source_catalog(ROOT, ROOT / "rom.z64")
    items = effective(args.tag)
    locales = {i["locale"] for i in items.values() if i.get("locale")}
    if len(locales) > 1:
        raise SystemExit(f"runs mix locales {sorted(locales)}")
    locale = locales.pop() if locales else "zh-Hans"
    catalog = json.loads((ROOT / f"content/locales/{locale}.json").read_text())
    records = load_records()
    entries, intro = [], []
    for key, item in sorted(items.items()):
        target = final_target(records[key], item, locale)
        if not key.startswith("base:"):
            intro.append({"key": key, "target": target, "run": item["run"]})
            continue
        entries.append({"key": key, "source_sha256": hashes[key], "target": target,
                        "review_status": "draft", "origin": "mt",
                        "mt": {"model": item["model"], "prompt": item["prompt"], "run": item["run"],
                               "stage": item["stage"],
                               **({"flag": item["flag"]} if item.get("flag") else {}),
                               **({"reason": item["reason"]} if item.get("reason") else {}),
                               **({"warnings": item["warnings"]} if item.get("warnings") else {})}})
    document = {"schema": "srw64.locale.v1", "locale": locale, "source_locale": "ja", "font": catalog["font"],
                "ui": {}, "entries": entries}
    compiled = compile_locale(document, sources, hashes)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({"schema": "srw64.mt-drafts.v1", "locale": locale, "runs": args.tag,
                                       "entries": entries, "intro": intro}, ensure_ascii=False, indent=1) + "\n",
                           encoding="utf-8")
    print(json.dumps({"locale": locale, "entries": len(entries), "compiled": len(compiled), "intro": len(intro),
                      "output": str(args.output)}, ensure_ascii=False))


def cmd_report(args) -> None:
    folder = RUNS / args.tag
    records = load_records()
    totals = {"batches": 0, "items": 0, "ok": 0, "errors": 0, "warnings": 0, "flags": 0, "repaired": 0,
              "reviewed": 0, "changed": 0, "prompt_tokens": 0, "completion_tokens": 0, "cached_tokens": 0,
              "elapsed": 0.0, "requests": 0, "source_chars": 0}
    models, review = set(), {}
    for path in sorted(folder.rglob("*.json")):
        doc = json.loads(path.read_text())
        if doc.get("schema") != "srw64.mt-batch.v1":
            continue
        totals["batches"] += 1
        models.add(doc["model"])
        totals["reviewed"] += doc.get("reviewed", 0)
        for u in doc["usage"]:
            totals["requests"] += 1
            for key in ("prompt_tokens", "completion_tokens", "cached_tokens", "elapsed"):
                totals[key] += u[key]
        for item in doc["items"]:
            if doc.get("stage") in ("review", "joins"):
                totals["changed"] += not item.get("errors")
            else:
                totals["items"] += 1
                totals["source_chars"] += records[item["key"]]["chars"]
                totals["ok"] += not item.get("errors")
                totals["errors"] += bool(item.get("errors"))
            totals["warnings"] += bool(item.get("warnings"))
            totals["flags"] += bool(item.get("flag"))
            totals["repaired"] += bool(item.get("repaired"))
            review.setdefault(doc["kind"], []).append((doc, item))
    model = next(iter(models)) if len(models) == 1 else None
    totals["cost"] = dashscope.cost(model, totals["prompt_tokens"], totals["completion_tokens"],
                                    totals["cached_tokens"]) if model else None
    totals["models"] = sorted(models)
    lines = [f"# 机翻审阅：{args.tag}", "", "```json", json.dumps(totals, ensure_ascii=False, indent=1), "```", ""]
    for kind, rows in review.items():
        lines += [f"## {kind}", "", "| 键 | 说话人 | 原文 | 译文 | 标记 |", "| --- | --- | --- | --- | --- |"]
        for doc, item in rows:
            record = records[item["key"]]
            ctx = record.get("context") or {}
            speaker = ctx.get("speaker") or ((ctx.get("occurrences") or [{}])[0].get("speaker")) or ""
            marks = "；".join(item.get("errors", []) + item.get("warnings", []) +
                             [x for x in (item.get("flag"), item.get("reason")) if x])
            tr = item.get("tr", item.get("zh"))
            tr = "▸".join(tr) if isinstance(tr, list) else str(tr or "")
            if doc.get("stage") in ("review", "joins") and isinstance(item.get("draft"), list):
                tr = "▸".join(item["draft"]) + " → " + tr
            ja = record["display"].replace("<BR>", "").replace("<STOP>", "▸").replace("\n", "")
            lines.append(f"| `{item['key'].split(':', 1)[1]}` | {speaker} | {ja} | {tr} | {marks} |".replace("\n", " "))
        lines.append("")
    (folder / "review.md").write_text("\n".join(lines), encoding="utf-8")
    (folder / "report.json").write_text(json.dumps(totals, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    print(json.dumps(totals, ensure_ascii=False, indent=1))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("plan", "run", "review", "retry"):
        p = sub.add_parser(name)
        p.add_argument("--kind", choices=("story", "battle", "intro", "all"), default="all")
        p.add_argument("--scenes", help="comma-separated scene indices (story)")
        p.add_argument("--ids", help="text id range for battle lines, e.g. 5813-5912")
        if name != "plan":
            p.add_argument("--tag", required=True)
            p.add_argument("--locale", choices=sorted(LANG), default="zh-Hans")
            p.add_argument("--model", default=dashscope.DEFAULT_MODEL)
            p.add_argument("--jobs", type=int, default=4)
            p.add_argument("--env-file", type=Path)
        if name == "run":
            p.add_argument("--no-repair", action="store_true")
        if name == "review":
            p.add_argument("--draft", action="append", required=True, help="draft run tag(s), later ones win")
    for name in ("report", "recheck"):
        p = sub.add_parser(name)
        p.add_argument("--tag", required=True)
    p = sub.add_parser("joins")
    p.add_argument("--tag", required=True)
    p.add_argument("--draft", action="append", required=True, help="run tag(s) of the current text, later ones win")
    p.add_argument("--locale", choices=sorted(LANG), default="zh-Hans")
    p.add_argument("--model", default=dashscope.DEFAULT_MODEL)
    p.add_argument("--jobs", type=int, default=4)
    p.add_argument("--size", type=int, default=40, help="records per request")
    p.add_argument("--limit", type=int, help="only the first N records (a pilot)")
    p.add_argument("--missing", action="store_true", help="only lines the finished pass left unanswered")
    p.add_argument("--final", action="store_true", help="the closing mark of the last page instead of page joins")
    p.add_argument("--env-file", type=Path)
    p = sub.add_parser("names")
    p.add_argument("--tag", required=True)
    p.add_argument("--draft", action="append", required=True, help="run tag(s) of the current text, later ones win")
    p.add_argument("--name", action="append", required=True, help="日文=旧译=新译, e.g. ショウ=修=翔")
    p.add_argument("--locale", choices=sorted(LANG), default="zh-Hans")
    p.add_argument("--model", default=dashscope.DEFAULT_MODEL)
    p.add_argument("--jobs", type=int, default=4)
    p.add_argument("--size", type=int, default=30)
    p.add_argument("--retry", action="store_true", help="only lines the finished pass left unanswered")
    p.add_argument("--env-file", type=Path)
    p = sub.add_parser("collect")
    p.add_argument("--tag", action="append", required=True, help="run tags in order; later ones override")
    p.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    {"plan": cmd_plan, "run": cmd_run, "review": cmd_review, "retry": cmd_retry, "report": cmd_report,
     "recheck": cmd_recheck, "collect": cmd_collect, "joins": cmd_joins, "names": cmd_names}[args.command](args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
