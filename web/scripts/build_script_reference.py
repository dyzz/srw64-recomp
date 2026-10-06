#!/usr/bin/env python3
"""The stage script reference on the website (src/content/docs/<lang>/script-commands.md).

Generated from the layout lock's stage_scripts table (config/data/original-jp-v1.json): every
command, condition, context marker, event type, operand role and deployment record field. The
Chinese page carries the implementation notes and run-time findings as they are recorded; the
English and Japanese pages translate the names and operands (script-reference-i18n.json) and link
each entry to its Chinese notes. It also writes public/docs/mini-stage/llms.txt, everything an AI agent
needs to write, compile and load a mini stage in one file (linked from the mini stage page): how to use
them, the rules the compiler enforces, and every entry with its English name and Chinese notes. Run it
again whenever the table changes:

  python3 web/scripts/build_script_reference.py           # write the three pages
  python3 web/scripts/build_script_reference.py --check   # fail if a page is out of date
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOCK = ROOT / 'config/data/original-jp-v1.json'
I18N = Path(__file__).with_name('script-reference-i18n.json')
OUT = ROOT / 'web/src/content/docs'
LLMS = ROOT / 'web/public/docs/mini-stage/llms.txt'
SITE = 'https://srw64.dreamquest.club'
REPO = 'https://github.com/dyzz/srw64-recomp/blob/main'
UPDATED = '2026-10-07'
# Deployment record bytes as the mini-stage compiler names them (tools/recomp/script_lab/mini_stage.py).
STAGE_FIELDS = {0: 'group', 2: 'x', 4: 'y', 6: 'actor', 8: 'byte8', 9: 'level_offset', 10: 'unit', 12: 'upgrade',
                14: 'raw14, raw16, raw18', 20: 'faction', 22: 'behavior', 24: 'extra', 26: 'raw26'}

TEXT = {
    'zh': {
        'title': '关卡脚本指令参考',
        'summary': '关卡事件脚本的全部指令、条件指令、上下文标记、事件类型、变量与出击记录字段，逐条附实现依据与实测结果。写迷你关卡时查。',
        'intro': [
            '关卡里的每个事件是一串 16 位字：指令码 `3D31`–`3D79` 后面跟它的操作数，条件指令 `3E00`–`3E1D` 组成判断块，'
            '上下文标记 `3DD0`–`3DDB` 按当前主角路线或选择肢挑出要执行的段落，`FFFF` 结束事件。'
            '写[迷你关卡](../mini-stage/)时，指令写成 `{"op": "3D45", "args": [1]}`，条件指令和标记也用同样的写法。',
            '本页由 [`config/data/original-jp-v1.json`]({repo}/config/data/original-jp-v1.json) 的 `stage_scripts` 表生成。'
            '「代码确认」表示含义已从游戏的处理函数确认；「结构确认」表示读写位置已确认，作用还没完全弄清。'
            '「依据」是对应的游戏代码（地址为日版 Rev 0 的内存地址），「实测」是在游戏里运行得到的结果。',
        ],
        'other': '',
        'events': '事件类型', 'events_lead': '事件登记时的类型决定它什么时候被检查、四个触发参数（`header`）怎么解释。',
        'commands': '指令', 'conditions': '条件指令', 'markers': '上下文标记', 'roles': '操作数类型',
        'variables': '变量', 'record': '出击记录（28 字节）',
        'cond_lead': '条件指令在 ACC 寄存器和变量上做判断与赋值。「开头」开启一个判断块，条件为假时跳过到对应的「块结束」（`3E1D`），块可以嵌套。',
        'marker_lead': '执行到标记时，若它与当前上下文匹配就继续，否则跳过到下一个匹配的标记或事件结束。',
        'roles_lead': '操作数的类型说明它的取值范围；表里没有的类型见各指令的操作数名称。',
        'record_lead': '出击记录是关卡的单位配置表，每条 14 个半字，组号为 999 时结束。「迷你关卡字段」是关卡定义 `deployments` 里对应的字段名。',
        'cols_event': ('类型', '名称', '检查时机', '触发参数'),
        'cols_marker': ('标记', '名称', '匹配方式'),
        'cols_role': ('类型', '含义'),
        'cols_record': ('偏移', '字节', '名称', '迷你关卡字段', '确认程度', '依据'),
        'operands': '操作数', 'no_operands': '无操作数', 'confidence': '确认程度', 'basis': '依据', 'runtime': '实测',
        'handler': '处理函数', 'mode': '对白显示模式', 'kind': '种类', 'notes': '',
        'conf': {'code-confirmed': '代码确认', 'structure-confirmed': '结构确认', 'unknown': '未解明'},
        'kinds': {'statement': '语句', 'opener': '开头', 'block-end': '块结束'},
        'matches': {'always': '总是', 'protagonist': '当前主角', 'derived': '主角的派生分类', 'choice': '选择肢结果'},
        'var_text': '共 {count} 个 {bits} 位变量（取值 0–3），存放在 `{vram}`，用条件指令读写，跨关保留。{basis}',
        'words': '{n} 个字',
    },
    'en': {
        'title': 'Stage script reference',
        'summary': 'Every command, condition, context marker, event type, variable and deployment record field of the stage event scripts, for writing mini stages.',
        'intro': [
            'Each event in a stage is a run of 16-bit words: a command opcode `3D31`–`3D79` followed by its operands, '
            'conditions `3E00`–`3E1D` forming if-blocks, context markers `3DD0`–`3DDB` that pick the passages for the current '
            'hero’s route or a choice, and `FFFF` to end the event. In a [mini stage](../mini-stage/) a command is written as '
            '`{"op": "3D45", "args": [1]}`; conditions and markers are written the same way.',
            'This page is generated from the `stage_scripts` table in '
            '[`config/data/original-jp-v1.json`]({repo}/config/data/original-jp-v1.json). “Confirmed in code” means the '
            'meaning was read from the game’s handler; “structure confirmed” means what it reads and writes is known but not '
            'fully what it is for.',
        ],
        'other': 'The implementation notes (which game code each entry rests on) and the in-game findings are recorded in '
                 'Chinese; every entry links to them on the [Chinese page](../../../zh/docs/script-commands/).',
        'events': 'Event types', 'events_lead': 'An event’s type decides when it is checked and how its four trigger parameters (`header`) are read.',
        'commands': 'Commands', 'conditions': 'Conditions', 'markers': 'Context markers', 'roles': 'Operand types',
        'variables': 'Variables', 'record': 'Deployment records (28 bytes)',
        'cond_lead': 'Conditions test and set the ACC register and the variables. An “opener” starts a block; when it is false, '
                     'the script skips to the matching “block end” (`3E1D`). Blocks nest.',
        'marker_lead': 'At a marker the script carries on if it matches the current context, otherwise it skips to the next matching marker or the end of the event.',
        'roles_lead': 'An operand’s type gives its range; types not listed here are described by the operand names.',
        'record_lead': 'A stage’s deployment records place its units, 14 halfwords each, ending at group 999. “Mini stage field” is the matching field name in a stage’s `deployments`.',
        'cols_event': ('Type', 'Name', 'Checked', 'Trigger parameters'),
        'cols_marker': ('Marker', 'Name', 'Matches'),
        'cols_role': ('Type', 'Meaning'),
        'cols_record': ('Offset', 'Bytes', 'Name', 'Mini stage field', 'Confidence'),
        'operands': 'Operands', 'no_operands': 'No operands', 'confidence': 'Confidence', 'basis': '', 'runtime': '',
        'handler': 'Handler', 'mode': 'Dialogue display mode', 'kind': 'Kind', 'notes': 'Notes (Chinese)',
        'conf': {'code-confirmed': 'Confirmed in code', 'structure-confirmed': 'Structure confirmed', 'unknown': 'Not yet understood'},
        'kinds': {'statement': 'statement', 'opener': 'opener', 'block-end': 'block end'},
        'matches': {'always': 'always', 'protagonist': 'current hero', 'derived': 'hero category', 'choice': 'choice result'},
        'var_text': '{count} variables of {bits} bits each (values 0–3), stored at `{vram}`, read and written by the conditions and kept across stages.',
        'words': '{n} word(s)',
    },
    'ja': {
        'title': 'ステージスクリプト命令リファレンス',
        'summary': 'ステージのイベントスクリプトで使う命令・条件命令・コンテキストマーカー・イベントの種類・変数・出撃データの項目の一覧。ミニステージを書くときに。',
        'intro': [
            'ステージの各イベントは 16 ビットのワードの並びです。命令コード `3D31`〜`3D79` のあとにその引数が続き、'
            '条件命令 `3E00`〜`3E1D` が条件ブロックを作り、コンテキストマーカー `3DD0`〜`3DDB` が現在の主人公のルートや選択肢に応じて'
            '実行する部分を選び、`FFFF` でイベントが終わります。[ミニステージ](../mini-stage/)では命令を '
            '`{"op": "3D45", "args": [1]}` のように書き、条件命令とマーカーも同じ書き方です。',
            'このページは [`config/data/original-jp-v1.json`]({repo}/config/data/original-jp-v1.json) の `stage_scripts` '
            'テーブルから生成しています。「コードで確認」はゲームの処理関数から意味を確認済み、「構造を確認」は読み書きする場所は'
            '分かっているものの、役割はまだ完全には分かっていないことを示します。',
        ],
        'other': '各項目の根拠（どのゲームコードに基づくか）とゲーム内での検証結果は中国語で記録しています。各項目から'
                 '[中国語のページ](../../../zh/docs/script-commands/)の該当箇所へリンクしています。',
        'events': 'イベントの種類', 'events_lead': 'イベントの種類によって、いつ判定されるかと 4 つの発動条件（`header`）の読み方が決まります。',
        'commands': '命令', 'conditions': '条件命令', 'markers': 'コンテキストマーカー', 'roles': '引数の種類',
        'variables': '変数', 'record': '出撃データ（28 バイト）',
        'cond_lead': '条件命令は ACC レジスタと変数を判定・設定します。「開始」はブロックを始め、偽のときは対応する「ブロック終了」（`3E1D`）'
                     'まで飛ばします。ブロックは入れ子にできます。',
        'marker_lead': 'マーカーに来たとき、現在の状況に合えばそのまま続け、合わなければ次に合うマーカーかイベントの終わりまで飛ばします。',
        'roles_lead': '引数の種類は値の範囲を表します。表にない種類は各命令の引数名を参照してください。',
        'record_lead': '出撃データはステージのユニット配置で、1 件 14 ハーフワード、グループ番号 999 で終わります。「ミニステージの項目」はステージ定義の `deployments` での項目名です。',
        'cols_event': ('種類', '名前', '判定のタイミング', '発動条件'),
        'cols_marker': ('マーカー', '名前', '一致の条件'),
        'cols_role': ('種類', '意味'),
        'cols_record': ('オフセット', 'バイト', '名前', 'ミニステージの項目', '確認の程度'),
        'operands': '引数', 'no_operands': '引数なし', 'confidence': '確認の程度', 'basis': '', 'runtime': '',
        'handler': '処理関数', 'mode': '会話の表示モード', 'kind': '種類', 'notes': '根拠（中国語）',
        'conf': {'code-confirmed': 'コードで確認', 'structure-confirmed': '構造を確認', 'unknown': '未解明'},
        'kinds': {'statement': '文', 'opener': '開始', 'block-end': 'ブロック終了'},
        'matches': {'always': '常に', 'protagonist': '現在の主人公', 'derived': '主人公の分類', 'choice': '選択肢の結果'},
        'var_text': '2 ビット（値 0〜3）の変数が {count} 個あり、`{vram}` に置かれます。条件命令で読み書きし、ステージをまたいで保持されます。',
        'words': '{n} ワード',
    },
}


def cell(text: str) -> str:
    return str(text).replace('|', '\\|').replace('\n', ' ')


class Page:
    def __init__(self, lang: str, table: dict, i18n: dict):
        self.lang, self.t, self.d, self.i18n = lang, TEXT[lang], table, i18n
        self.missing: set[str] = set()

    def tr(self, text: str) -> str:
        if self.lang == 'zh' or not text:
            return text
        if text not in self.i18n:
            self.missing.add(text)
            return text
        return self.i18n[text][self.lang]

    def role(self, role: str | None) -> str:
        if not role:
            return ''
        known = self.d['operand_roles'].get(role)
        return f'`{role}`' + (f'：{self.tr(known)}' if known and self.lang != 'en' else f': {self.tr(known)}' if known else '')

    def notes_link(self, anchor: str) -> str:
        return f'[{self.t["notes"]}](../../../zh/docs/script-commands/#{anchor})'

    def entry(self, opcode: str, v: dict, condition: bool) -> list[str]:
        t, anchor = self.t, f'op-{opcode}'
        out = [f'<a id="{anchor}"></a>', '', f'### {opcode.upper()} · {self.tr(v["name"])}', '']
        facts = []
        operands = v.get('operands', [])
        words = v.get('operand_words', len(operands))
        if words or operands:
            facts.append(f'{t["operands"]}（{t["words"].format(n=words)}）' if self.lang != 'en' else f'{t["operands"]} ({t["words"].format(n=words)})')
        else:
            facts.append(t['no_operands'])
        if condition and v.get('kind'):
            facts.append(f'{t["kind"]}：{t["kinds"].get(v["kind"], v["kind"])}' if self.lang != 'en' else f'{t["kind"]}: {t["kinds"].get(v["kind"], v["kind"])}')
        if v.get('dialogue_mode') is not None:
            facts.append(f'{t["mode"]} {v["dialogue_mode"]}')
        conf = v.get('semantic_confidence') or v.get('confidence')
        if conf:
            facts.append(t['conf'].get(conf, conf))
        if v.get('handler_vram'):
            facts.append(f'{t["handler"]} `{v["handler_vram"]}`')
        out.append(' · '.join(facts))
        out.append('')
        for i, o in enumerate(operands, 1):
            out.append(f'{i}. {self.tr(o["name"])}' + (f' — {self.role(o.get("role"))}' if o.get('role') else ''))
        if operands:
            out.append('')
        if self.lang == 'zh':
            if v.get('basis'):
                out += [f'**{t["basis"]}**：{v["basis"]}', '']
            runtime = v.get('runtime')
            if isinstance(runtime, dict) and runtime.get('finding'):
                date = f'（{runtime["date"]}）' if runtime.get('date') else ''
                out += [f'**{t["runtime"]}**{date}：{runtime["finding"]}', '']
        elif v.get('basis') or v.get('runtime'):
            out += [self.notes_link(anchor), '']
        return out

    def build(self) -> str:
        t, d = self.t, self.d
        out = ['---', f'title: {t["title"]}', f'summary: {t["summary"]}', 'section: tools', 'order: 3', f'updated: {UPDATED}', '---', '']
        for p in t['intro']:
            out += [p.replace('{repo}', REPO), '']
        if t['other']:
            out += [t['other'], '']
        # Event types
        out += [f'## {t["events"]}', '', t['events_lead'], '', '| ' + ' | '.join(t['cols_event']) + ' |', '|---|---|---|---|']
        for key in sorted(d['event_types'], key=int):
            e = d['event_types'][key]
            header = '<br>'.join(f'{i}. {self.tr(h["name"])}' + (f'：{self.tr(h["note"])}' if h.get('note') and self.lang != 'en'
                                                                    else f': {self.tr(h["note"])}' if h.get('note') else '')
                                 for i, h in enumerate(e.get('header', []), 1))
            polled = '、'.join(self.tr(p) for p in d['polling'].get(key, [])) if self.lang != 'en' else ', '.join(self.tr(p) for p in d['polling'].get(key, []))
            out.append(f'| {key} | {cell(self.tr(e["name"]))} | {cell(polled or self.tr(e.get("polled", "")))} | {cell(header)} |')
        out.append('')
        # Commands and conditions
        out += [f'## {t["commands"]}', '']
        for opcode in sorted(d['commands']):
            out += self.entry(opcode, d['commands'][opcode], False)
        out += [f'## {t["conditions"]}', '', t['cond_lead'], '']
        for opcode in sorted(d['conditions']):
            out += self.entry(opcode, d['conditions'][opcode], True)
        # Markers
        out += [f'## {t["markers"]}', '', t['marker_lead'], '', '| ' + ' | '.join(t['cols_marker']) + ' |', '|---|---|---|']
        for opcode in sorted(d['context_markers']):
            m = d['context_markers'][opcode]
            out.append(f'| `{opcode.upper()}` | {cell(self.tr(m["name"]))} | {cell(t["matches"].get(m.get("matches"), m.get("matches", "")))} |')
        out.append('')
        # Operand roles
        out += [f'## {t["roles"]}', '', t['roles_lead'], '', '| ' + ' | '.join(t['cols_role']) + ' |', '|---|---|']
        for role, meaning in d['operand_roles'].items():
            out.append(f'| `{role}` | {cell(self.tr(meaning))} |')
        out.append('')
        # Variables
        var = d['variables']
        out += [f'## {t["variables"]}', '', t['var_text'].format(count=var['count'], bits=var['bits'], vram=var['storage_vram'],
                                                              basis=var.get('basis', '') if self.lang == 'zh' else ''), '']
        # Deployment record
        rec = d['auxiliary_record']
        cols = t['cols_record']
        out += [f'## {t["record"]}', '', t['record_lead'], '', '| ' + ' | '.join(cols) + ' |', '|' + '---|' * len(cols)]
        for f in rec['fields']:
            row = [f'`+{f["offset"]}`', str(f['size']), cell(self.tr(f['name'])), f'`{STAGE_FIELDS.get(f["offset"], "")}`',
                   t['conf'].get(f.get('confidence'), f.get('confidence', ''))]
            if self.lang == 'zh':
                row.append(cell(f.get('basis', '')))
            out.append('| ' + ' | '.join(row) + ' |')
        out.append('')
        return '\n'.join(out).rstrip() + '\n'


LLMS_HEAD = """# Marchwind 64 mini stages

> Everything an AI agent needs to write, compile and load a mini stage in Marchwind 64, the native recompilation of
> Super Robot Wars 64 (N64): how to use them, the rules the compiler enforces, and every script command, condition,
> context marker, event type, variable and deployment record field. Generated from the game's script table; the
> implementation notes ("basis") and in-game findings are in Chinese, as recorded.

A mini stage is a small scenario in one JSON file (schema `srw64.mini-stage.v1`): an original map, the units placed on
it, and event scripts. Loaded on the title's main menu, it takes the place of a scenario and the game enters it
directly (no New Game, prologue or hero selection). Use it to reproduce a battle, a screen or a script command.
There is no menu for it in the game: it is loaded through the debug interface's MCP server only.

Human-readable pages: {site}/en/docs/mini-stage/ (usage), {site}/en/docs/script-commands/ (reference),
{site}/en/docs/mcp/ (connecting an agent). Source: https://github.com/dyzz/srw64-recomp

## Workflow

1. Connect: the player turns on Options → About → AI debug interface (MCP), or starts the game with `--debug`; the
   agent calls `srw64_attach`. A development session from the repository uses `srw64_launch` instead.
2. Write the stage definition (below). Start from the closest of the 69 stages in `config/recomp/mini-stages/`; each
   file's `note` says what it tests.
3. Compile: `python3 tools/recomp/script_lab/mini_stage.py compile stage.json --out stage.image.json` in the
   repository. Stages using `template`, `deployments_from` or `copy_from` need the original data extracted from the
   player's ROM first: put the ROM in the repository as `rom.z64` and run `make recomp-data` once.
4. Wait for the title's main menu (`srw64_status`: `title_major` 3, or `srw64_wait` with `title_major: 3`), then call
   `srw64_mini_stage_load` with the image's path on the machine the game runs on. The game enters at once; poll
   `srw64_status` until `mini_stage.ready` is true (the map is idle and waiting for input).
5. Play it with `srw64_keys` / `srw64_pad` / `srw64_buttons`, check with `srw64_screenshot`, `srw64_status`,
   `srw64_events`, and `srw64_quit` or return to the title when done. Loading another stage replaces the first.

An installed game loads compiled images only; a `srw64_launch` session also accepts a stage definition and compiles
it itself, and its `mini_stage` argument prepares a stage at launch (entered with F8 on the main menu).

Caveats: the game writes to the player's own saves if the stage saves or autosaves are on (suggest a separate
`--user-dir`); the hero's and partner's names and route are empty; random numbers differ from a normal game, so
reproduce from the same image and inputs; images contain data copied from the ROM and must not be shared.

## Stage definition

```json
{{
  "schema": "srw64.mini-stage.v1",
  "name": "my-test",
  "note": "what this stage tests",
  "map": 20,
  "deployments": [
    {{"template": "base:stage_deployments:001f0f1c", "group": 0, "x": 8, "y": 8, "faction": 0}},
    {{"template": "base:stage_deployments:001f2720", "group": 1, "x": 12, "y": 8, "faction": 1}}
  ],
  "events": [
    {{"name": "opening", "type": 12, "header": [0, 0, 0, 0], "commands": [
      {{"op": "3DD0"}}, {{"op": "3D32", "args": [4]}}, {{"op": "3D4D"}}, {{"op": "3D65", "args": [17, 31]}},
      {{"op": "3D3B", "args": [1]}}, {{"op": "3D45", "args": [0]}}, {{"op": "3D45", "args": [1]}},
      {{"op": "3D35", "args": [16384, 0]}}, {{"op": "3D48"}}
    ]}},
    {{"name": "ending", "type": 14, "header": [0, 0, 0, 0], "commands": [{{"op": "3DD0"}}, {{"op": "3D4B", "args": [4]}}]}}
  ]
}}
```

Fields and the limits `mini_stage.py compile` enforces:

- `map`: original map number 0–255 (`base:map_assets`). `slot`: optional scene number 0–255 to borrow; without it, the
  scenario the game registers after loading.
- `events`: at most 63, together at most 0x1A00 bytes (each event is its type, four header words, its commands and
  FFFF, padded to 4 bytes). Each event is either
  - `{{"name", "type": 0–14, "header": [four 16-bit words], "commands": [...]}}`, where a command is
    `{{"op": "3D45", "args": [...]}}` with exactly as many 16-bit args as the opcode's operand words (listed below).
    Conditions (3E00–3E1D) and context markers (3DD0–3DDB, no operands) are written the same way. 3D76–3D79 are
    rejected (no reachable handler); FFFF is appended for you. Or
  - `{{"name", "copy_from": "base:stage_events:<key>"}}`, an original event copied word for word, optionally with a
    `header` override that keeps its commands but changes when it fires.
- `deployments_from`: optional `base:stage_auxiliary:<key>`, a scenario's whole original deployment block.
  `deployments`: extra 28-byte records with the fields listed under "Deployment records"; `template`
  (`base:stage_deployments:<key>`) starts from an original record. Together at most 0x2000 bytes with the 999
  terminator.
- `initial_resources`: optional, at most 90 rows `{{"side": 0–2, "slot": 0–29, "hp_percent": 1–100,
  "en_percent": 0–100}}`, applied once when the map first becomes idle; the unit must be deployed.

Looking up numbers (after `make recomp-data`, in `assets/original-data/records/*.jsonl`, one JSON object per line
with `key`, `label`, `summary` and `search_terms`): `actors` (character numbers, `base:actors:0300` = 300),
`units` (unit numbers), `stage_deployments` (records to use as `template`, with a decoded `deployment` object),
`stage_auxiliary` (whole deployment blocks), `stage_events` (original events for `copy_from`), `map_assets`
(maps). Text numbers for dialogue commands are entries of text table 0 (`base:t00_<number>`).

Events run when their type's trigger holds (see "Event types"); a stage needs at least an opening event (type 12)
that switches to the battlefield (`3D4D`), deploys groups (`3D45`) and hands over to the player (`3D48` closes the
dialogue window), and usually an ending event (type 14) with `3D4B 4`.
"""


def llms(table: dict, i18n: dict) -> str:
    page = Page('en', table, i18n)
    d = table
    out = [LLMS_HEAD.format(site=SITE)]

    def name(text: str) -> str:
        english = page.tr(text)
        return english if english == text else f'{english} [{text}]'

    out += ['## Event types', '', 'Type: name. Checked when. Header words 1–4.', '']
    for key in sorted(d['event_types'], key=int):
        e = d['event_types'][key]
        polled = ', '.join(page.tr(p) for p in d['polling'].get(key, [])) or page.tr(e.get('polled', ''))
        out.append(f'- {key}: {name(e["name"])}. Checked: {polled}.')
        for i, h in enumerate(e.get('header', []), 1):
            note = f' — {page.tr(h["note"])}' if h.get('note') else ''
            out.append(f'  {i}. {page.tr(h["name"])} (`{h.get("role", "")}`){note}')
    out.append('')

    def entries(section: str, title: str) -> None:
        out.extend([f'## {title}', ''])
        for opcode in sorted(d[section]):
            v = d[section][opcode]
            operands = v.get('operands', [])
            words = v.get('operand_words', len(operands))
            head = f'### {opcode.upper()} — {name(v["name"])}'
            facts = [f'{words} operand word(s)']
            if v.get('kind'):
                facts.append(f'kind: {v["kind"]}')
            if v.get('dialogue_mode') is not None:
                facts.append(f'dialogue display mode {v["dialogue_mode"]}')
            conf = v.get('semantic_confidence') or v.get('confidence')
            if conf:
                facts.append(conf)
            if v.get('handler_vram'):
                facts.append(f'handler {v["handler_vram"]}')
            out.extend([head, '; '.join(facts)])
            for i, o in enumerate(operands, 1):
                role = o.get('role')
                meaning = d['operand_roles'].get(role) if role else None
                out.append(f'{i}. {page.tr(o["name"])}' + (f' (`{role}`' + (f': {page.tr(meaning)}' if meaning else '') + ')' if role else ''))
            if v.get('basis'):
                out.append(f'Basis (zh): {v["basis"]}')
            runtime = v.get('runtime')
            if isinstance(runtime, dict) and runtime.get('finding'):
                out.append(f'In game (zh{", " + runtime["date"] if runtime.get("date") else ""}): {runtime["finding"]}')
            out.append('')

    entries('commands', 'Commands (3D31–3D79)')
    out += ['Conditions test and set the ACC register and variables. An opener starts a block; when false, the script',
            'skips to the matching block end (3E1D). Blocks nest.', '']
    entries('conditions', 'Conditions (3E00–3E1D)')
    out += ['## Context markers (3DD0–3DDB, no operands)', '',
            'At a marker the script continues if it matches the current context, otherwise it skips to the next matching',
            'marker or the end of the event.', '']
    for opcode in sorted(d['context_markers']):
        m = d['context_markers'][opcode]
        out.append(f'- {opcode.upper()}: {name(m["name"])} (matches: {m.get("matches", "")})')
    out += ['', '## Operand types', '']
    for role, meaning in d['operand_roles'].items():
        out.append(f'- `{role}`: {page.tr(meaning)}')
    var = d['variables']
    out += ['', '## Variables', '', f'{var["count"]} variables of {var["bits"]} bits (values 0–3) at {var["storage_vram"]}, '
            f'read and written by the conditions, kept across stages. Basis (zh): {var.get("basis", "")}', '',
            '## Deployment records (28 bytes, 14 halfwords, group 999 ends the block)', '',
            'Offset, bytes: name (mini stage field) — confidence. Basis (zh).', '']
    for f in d['auxiliary_record']['fields']:
        basis = f' Basis (zh): {f["basis"]}' if f.get('basis') else ''
        out.append(f'- +{f["offset"]}, {f["size"]}: {name(f["name"])} (`{STAGE_FIELDS.get(f["offset"], "")}`) — '
                   f'{f.get("confidence", "")}.{basis}')
    out.append('')
    if page.missing:
        raise SystemExit('Untranslated in llms.txt: ' + ', '.join(sorted(page.missing)))
    return '\n'.join(out).rstrip() + '\n'


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--check', action='store_true', help='fail if a page differs from what the table gives')
    args = parser.parse_args()
    table = json.loads(LOCK.read_text(encoding='utf-8'))['stage_scripts']
    i18n = json.loads(I18N.read_text(encoding='utf-8'))
    stale, missing = [], set()
    for lang in TEXT:
        page = Page(lang, table, i18n)
        text = page.build()
        missing |= page.missing
        target = OUT / lang / 'script-commands.md'
        if args.check:
            if not target.exists() or target.read_text(encoding='utf-8') != text:
                stale.append(str(target.relative_to(ROOT)))
        else:
            target.write_text(text, encoding='utf-8')
            print(target.relative_to(ROOT))
    text = llms(table, i18n)
    if args.check:
        if not LLMS.exists() or LLMS.read_text(encoding='utf-8') != text:
            stale.append(str(LLMS.relative_to(ROOT)))
    else:
        LLMS.parent.mkdir(parents=True, exist_ok=True)
        LLMS.write_text(text, encoding='utf-8')
        print(LLMS.relative_to(ROOT))
    if missing:
        print('Untranslated (add to script-reference-i18n.json):', *sorted(missing), sep='\n  ', file=sys.stderr)
        return 1
    if stale:
        print('Out of date, run web/scripts/build_script_reference.py:', *stale, sep='\n  ', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
