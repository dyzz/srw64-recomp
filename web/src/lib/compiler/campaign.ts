// A campaign written stage by stage (tools/mods/stage.py): campaign.json (srw64.campaign.v2) and
// its stages (srw64.stage.v1) into the campaign image (the opening, set-up and end events,
// the deployment block and the map of every stage) and the story image (every other
// event, which the host triggers; the new lines; where those go in the three events).
import { CompileError, isInt } from './assemble.ts';
import { borrowable, compileStage, nextScenes, type Catalog, type RawEvent, type StageImage } from './stage-image.ts';
import { Builder, CHECK, compileStory, FLAG, TEXT, type Line, type StoryImage } from './story.ts';
import type { Test } from './conditions.ts';

type Obj = Record<string, any>;
const int = (value: unknown, name: string, low: number, high: number): number => {
  if (!isInt(value) || value < low || value > high) throw new CompileError(`${name} 要是 ${low}–${high} 的整数`);
  return value;
};
const isObj = (v: unknown): v is Obj => !!v && typeof v === 'object' && !Array.isArray(v);
const NATIVE: Record<string, number> = { opening: 12, setup: 13, end: 14 };
// Scenes the compiler does not hand out by itself: stage.py AVOIDED.
const AVOIDED = new Set([0, 4, 5, 6, 7, 8, 9, 105, 106]);
export const POOL = Array.from({ length: 143 }, (_, s) => s).filter((s) => borrowable(s) && !AVOIDED.has(s));
const UNIT_FIELDS: Record<string, string> = { pilot: 'actor', level: 'level_offset' };
const MAX_SIDE = 30;
const PROTAGONISTS = ['ark', 'serein', 'brad', 'manami'];

export type CampaignSource = {
  campaign: Obj;                                  // campaign.json
  stages: Record<string, Obj>;                    // stages/<key>.json
  meta?: Record<string, Obj>;                     // stages/<key>.meta.json
  status?: Obj | null;                            // the file campaign.json's "status" names
  maps?: Record<string, { like: number }>;        // the campaign's own maps, built (map.ts): the map each stands in for
  catalog?: Catalog;                              // original records copy_from and template name (the command line's)
};
const list = (keys: string[]) => `['${keys.join("', '")}']`;

function assignScenes(stages: Record<string, Obj>): Record<string, number> {
  const scenes: Record<string, number> = {}, taken = new Map<number, string>();
  for (const [key, stage] of Object.entries(stages)) {
    if (!('scene' in stage)) continue;
    const scene = stage.scene;
    if (!isInt(scene) || !borrowable(scene)) throw new CompileError(`关卡 ${key}：scene ${JSON.stringify(scene)} 不能借（0–142，避开 109–122 和「（前）」的关）`);
    if (taken.has(scene)) throw new CompileError(`关卡 ${taken.get(scene)} 和 ${key} 都写了 scene ${scene}`);
    scenes[key] = scene; taken.set(scene, key);
  }
  const free = POOL.filter((s) => !taken.has(s));
  let next = 0;
  for (const key of Object.keys(stages)) if (!(key in scenes)) {
    if (next >= free.length) throw new CompileError(`关卡太多：能借的场景号只有 ${POOL.length} 个`);
    scenes[key] = free[next++];
  }
  return scenes;
}

// A unit's behaviour as the deployment record holds it (static analysis 2026-11-20,
// level-editor.md §5.4): +0x10 the AI's mode, +0xE turns it stays put (31 never moves),
// +0x18/+0x1A its target pilot or cell, +0x12 the part it leaves (0xFFFF none), +0x16
// the HP/EN it comes on with (1-7).
const AI_MODES: Record<string, number> = { attack: 0, accurate: 1, hunt: 2, goto: 3, flee: 4, guard: 5 };
function friendly(row: Obj, where: string): Obj {
  const out: Obj = { ...row };
  const clash = (name: string, raw: string[]) => { const both = raw.filter((r) => r in row); if (both.length) throw new CompileError(`${where} 同时写了 ${name} 和 ${both[0]}`); };
  if ('drop' in row) {
    clash('drop', ['raw18']);
    out.raw18 = row.drop === null ? 0xFFFF : int(row.drop, `${where}.drop`, 0, 37);
    delete out.drop;
  } else if (!('template' in row) && !('raw18' in row)) out.raw18 = 0xFFFF;   // leaves nothing
  if ('entry' in row) {
    clash('entry', ['behavior']);
    out.behavior = int(row.entry, `${where}.entry`, 0, 7);
    delete out.entry;
  }
  if ('ai' in row) {
    clash('ai', ['raw14', 'raw16', 'extra', 'raw26']);
    const ai = row.ai;
    if (!isObj(ai) || Object.keys(ai).some((k) => !['mode', 'wait', 'target', 'to'].includes(k)) || !(ai.mode in AI_MODES))
      throw new CompileError(`${where}.ai 写 {"mode": ${Object.keys(AI_MODES).join('|')}, "wait": 回合, "target": 人物号, "to": [x, y]}`);
    const mode = AI_MODES[ai.mode];
    out.raw16 = mode;
    out.raw14 = 'wait' in ai ? int(ai.wait, `${where}.ai.wait`, 0, 31) : 0;
    if (mode === 2 || mode === 5) {
      if (!('target' in ai) || 'to' in ai) throw new CompileError(`${where}.ai：${ai.mode} 要写 target（人物号），不写 to`);
      out.extra = int(ai.target, `${where}.ai.target`, 1, 360);
    } else if (mode === 3 || mode === 4) {
      if (!Array.isArray(ai.to) || ai.to.length !== 2 || 'target' in ai) throw new CompileError(`${where}.ai：${ai.mode} 要写 to: [x, y]，不写 target`);
      out.extra = int(ai.to[0], `${where}.ai.to`, 0, 127); out.raw26 = int(ai.to[1], `${where}.ai.to`, 0, 127);
    } else if ('target' in ai || 'to' in ai) throw new CompileError(`${where}.ai：${ai.mode} 不写 target 和 to`);
    delete out.ai;
  }
  return out;
}

function units(stage: Obj, key: string): Obj {
  const spec = stage.units ?? {};
  if (!isObj(spec) || Object.keys(spec).some((k) => k !== 'copy_from' && k !== 'place')) throw new CompileError(`关卡 ${key}：units 只能写 copy_from 和 place`);
  const out: Obj = {};
  if ('copy_from' in spec) out.deployments_from = spec.copy_from;
  const rows = ((spec.place ?? []) as unknown[]).map((row, n) => {
    if (!isObj(row)) throw new CompileError(`关卡 ${key}：units.place[${n}] 要是对象`);
    const both = Object.keys(UNIT_FIELDS).filter((name) => name in row && UNIT_FIELDS[name] in row);
    if (both.length) throw new CompileError(`关卡 ${key}：units.place[${n}] 同时写了 ${both[0]} 和 ${UNIT_FIELDS[both[0]]}`);
    return friendly(Object.fromEntries(Object.entries(row).map(([name, value]) => [UNIT_FIELDS[name] ?? name, value])), `关卡 ${key}：units.place[${n}]`);
  });
  if (rows.length) out.deployments = rows;
  return out;
}

function splitEvents(stage: Obj, key: string): [Record<string, Obj>, Obj[]] {
  const native: Record<string, Obj> = {}, hosted: Obj[] = [];
  for (const row of (stage.events ?? []) as unknown[]) {
    if (!isObj(row)) throw new CompileError(`关卡 ${key}：每个事件是一个对象`);
    const when = row.when;
    if (typeof when === 'string') {
      const where = `关卡 ${key} 的 ${when} 事件`;
      if (!(when in NATIVE)) throw new CompileError(`关卡 ${key}：when 写成字符串时只能是 opening、setup、end`);
      if (when in native) throw new CompileError(`${where}：只能有一个；接在它后面的写 {"after": "${when}"}`);
      const unknown = Object.keys(row).filter((k) => !['id', 'when', 'do', 'note'].includes(k)).sort();
      if (unknown.length) throw new CompileError(`${where}：不认识的字段 ${list(unknown)}（它总会执行，条件写在 do 里的 if）`);
      native[when] = row;
    } else hosted.push(row);
  }
  return [native, hosted];
}

function campaignStatus(status: Obj, compiled: Obj[], start: number): Obj {
  if (status.schema !== 'srw64.stage-status.v1' || !Array.isArray(status.rules)) throw new CompileError('status 不是带 rules 的 srw64.stage-status.v1');
  const out = { ...status };
  if (!('graph' in out)) {
    const edges = Object.fromEntries(compiled.map((s) => [String(s.scene), nextScenes(s.image).filter((t) => t !== 500).sort((a, b) => a - b)]));
    out.graph = Object.fromEntries(PROTAGONISTS.map((p) => [p, JSON.parse(JSON.stringify(edges))]));
    if (!('starts' in out)) out.starts = Object.fromEntries(PROTAGONISTS.map((p) => [p, start]));
  }
  if (!('edge_guards' in out)) out.edge_guards = [];
  return out;
}

// The campaign image and its story image (null when no stage needs one).
export function compileCampaign(source: CampaignSource): { campaign: Obj; story: StoryImage | null } {
  const document = source.campaign;
  if (!isObj(document) || document.schema !== 'srw64.campaign.v2') throw new CompileError('campaign.json：schema 要是 srw64.campaign.v2');
  const unknown = Object.keys(document).filter((k) => !['schema', 'id', 'name', 'description', 'author', 'version', 'note', 'start', 'route', 'status', 'stages'].includes(k)).sort();
  if (unknown.length) throw new CompileError(`campaign.json：不认识的字段 ${list(unknown)}`);
  const keys = document.stages;
  if (!Array.isArray(keys) || !keys.length || new Set(keys).size !== keys.length || !keys.every((k) => typeof k === 'string' && FLAG.test(k)))
    throw new CompileError('campaign.json：stages 是关卡名的数组（小写字母、数字、_、-），不能重复');
  if (!keys.includes(document.start)) throw new CompileError('campaign.json：start 要是 stages 里的一个关卡');
  const stages: Record<string, Obj> = {};
  for (const key of keys as string[]) {
    const stage = source.stages[key];
    if (!stage) throw new CompileError(`${key}.json：找不到文件`);
    if (!isObj(stage) || stage.schema !== 'srw64.stage.v1') throw new CompileError(`${key}.json：schema 要是 srw64.stage.v1`);
    stages[key] = { ...stage };
  }
  for (const [key, stage] of Object.entries(stages)) {
    const extra = Object.keys(stage).filter((k) => !['schema', 'title', 'scene', 'map', 'note', 'units', 'setup', 'events', 'raw_events', 'objective', 'battle_lines'].includes(k)).sort();
    if (extra.length) throw new CompileError(`关卡 ${key}：不认识的字段 ${list(extra)}`);
    if (!isInt(stage.map) && typeof stage.map !== 'string') throw new CompileError(`关卡 ${key}：要写 map（原版地图号，或 maps/ 下自己的地图名）`);
  }
  const scenes = assignScenes(stages);
  const route = document.route ?? null;
  if (route !== null && !(isInt(route) && route >= 0 && route <= 3)) throw new CompileError('campaign.json：route 是 0（布拉德）、1（真奈美）、2（阿克）或 3（赛莲）');
  for (const [key, stage] of Object.entries(stages)) {
    if (!isObj(stage.setup ?? {})) throw new CompileError(`关卡 ${key}：setup 要是对象`);
    if (!('route' in (stage.setup ?? {}))) {
      if (route === null) throw new CompileError(`关卡 ${key}：没有主角路线。在 campaign.json 写 route（0–3），或在这一关的 setup 里写`);
      stage.setup = { ...(stage.setup ?? {}), route };
    }
  }

  const split = Object.fromEntries(Object.entries(stages).map(([key, stage]) => [key, splitEvents(stage, key)]));
  const hosted: Obj[] = [];
  for (const [key, [, rows]] of Object.entries(split)) for (const row of rows) {
    if (typeof row.id !== 'string' || !row.id) throw new CompileError(`关卡 ${key}：每个事件要有 id`);
    if ('scene' in row) throw new CompileError(`关卡 ${key} 的事件 ${row.id}：不写 scene，场景号由编译器分配`);
    const event: Obj = { ...row, id: `${key}.${row.id}`, scene: scenes[key] };
    if (isObj(row.if)) event.if = Object.fromEntries(Object.entries(row.if).map(([name, value]) =>
      [name, (name === 'fired' || name === 'not_fired') && Array.isArray(value) ? value.map((other) => `${key}.${other}`) : value]));
    hosted.push(event);
  }
  const own = (eventId: string) => (id: string) => `${eventId.slice(0, eventId.indexOf('.'))}.${id}`;
  // A stage's battle lines: its scene, ids prefixed as its events' are.
  const lines: Obj[] = [];
  for (const [key, stage] of Object.entries(stages)) {
    const rows = stage.battle_lines ?? [];
    if (!Array.isArray(rows)) throw new CompileError(`关卡 ${key}：battle_lines 要是数组`);
    rows.forEach((row: unknown, n: number) => {
      if (!isObj(row)) throw new CompileError(`关卡 ${key}：battle_lines[${n}] 要是对象`);
      if ('scene' in row) throw new CompileError(`关卡 ${key} 的战斗台词：不写 scene`);
      lines.push({ ...row, id: `${key}.${row.id ?? `line-${n + 1}`}`, scene: scenes[key] });
    });
  }
  const image = compileStory({ schema: 'srw64.story.v1', events: hosted, battle_lines: lines }, scenes, own);
  const known = new Set(image.events.map((e) => e.id as string));
  const texts: Line[] = image.texts;

  const custom: Record<string, string> = {};
  for (const [key, stage] of Object.entries(stages)) {
    if (typeof stage.map !== 'string') continue;
    if (!FLAG.test(stage.map)) throw new CompileError(`关卡 ${key}：map 写原版地图号，或 maps/ 下的地图名（小写字母、数字、_、-）`);
    const built = source.maps?.[stage.map];
    if (!built) throw new CompileError(`地图 ${stage.map}：${stage.map}：找不到 map.json`);
    custom[key] = stage.map;
    stage.map = built.like;
  }

  const compiled: Obj[] = [], patches: Obj[] = [], objectives: Obj[] = [];
  for (const [key, stage] of Object.entries(stages)) {
    const [native] = split[key];
    const events: RawEvent[] = [], refs: [number, number[], Record<number, number>][] = [], checks: [number, [number, Test][]][] = [];
    for (const [when, row] of Object.entries(native)) {
      let builder: Builder;
      try {
        builder = new Builder(texts, scenes);
        builder.prefix = own(`${key}.`);
        builder.actions(row.do ?? []);
        for (const other of builder.refs_fired.fired) if (!known.has(other)) throw new CompileError(`条件里的 fired 引用了不存在的事件 ${other.slice(key.length + 1)}`);
        if (builder.like.length || builder.crew.length || builder.builds.length || builder.named.length)
          throw new CompileError('这里还不能用 join 的 level.like、crew、build 和 @新机体；放到 {"after": …} 的事件里');
      } catch (error) {
        throw new CompileError(`关卡 ${key} 的 ${when} 事件：${(error as Error).message}`);
      }
      refs.push([events.length, builder.refs, builder.refOperand]);
      checks.push([events.length, builder.checks]);
      // 3DD0 opens the section every context runs, as each of the original's events does.
      events.push({ name: when, type: NATIVE[when], header: [0, 0, 0, 0], commands: [{ op: 0x3DD0, args: [] }, ...builder.commands] });
    }
    const raw = stage.raw_events ?? [];
    if (!Array.isArray(raw)) throw new CompileError(`关卡 ${key}：raw_events 要是数组`);
    const setup = stage.setup ?? {};
    if (!isObj(setup) || Object.keys(setup).some((k) => !['route', 'story_vars', 'last_choice', 'initial_resources'].includes(k)))
      throw new CompileError(`关卡 ${key}：setup 只能写 route、story_vars、last_choice、initial_resources`);
    let stageImage: StageImage;
    try {
      stageImage = compileStage({ name: key, map: stage.map, slot: scenes[key], ...units(stage, key), ...setup, events: [...events, ...raw] }, source.catalog);
    } catch (error) {
      if (error instanceof CompileError && /^关卡 /.test(error.message)) throw error;
      throw new CompileError(`关卡 ${key}：${(error as Error).message}`);
    }
    if (stageImage.map === null) throw new CompileError(`关卡 ${key}：要写 map（原版地图号）`);
    if (!stageImage.events.some((event) => event.type === 12)) throw new CompileError(`关卡 ${key}：要有开场事件（"when": "opening"）`);
    for (const kind of Object.values(NATIVE)) if (stageImage.events.filter((event) => event.type === kind).length > 1)
      throw new CompileError(`关卡 ${key}：开场、配置、结束事件各只能有一个（events 和 raw_events 合起来）`);
    const sides = new Map<number, number>();
    for (const record of stageImage.deployments) sides.set(record.faction, (sides.get(record.faction) ?? 0) + 1);
    const crowded = [...sides].filter(([, count]) => count > MAX_SIDE).map(([faction]) => faction);
    if (crowded.length && !('copy_from' in (stage.units ?? {}))) throw new CompileError(`关卡 ${key}：阵营 ${crowded[0]} 的单位超过 ${MAX_SIDE} 个`);
    // The new lines of the three events: their place in the scene's event block holds
    // TEXT until the host, which numbers the lines, writes the number there.
    let block = stageImage.events_hex;
    for (const [index, commands, operands] of refs) {
      const event = stageImage.events[index];
      for (const command of commands) {
        const at = event.offset + event.listing[command + 1].offset + 2 + 2 * (operands[command] ?? 0);   // after 3DD0
        const text = parseInt(block.substr(at * 2, 4), 16);
        block = block.slice(0, at * 2) + TEXT.toString(16).toUpperCase() + block.slice(at * 2 + 4);
        patches.push({ scene: scenes[key], at, text });
      }
    }
    // Steps' conditions the host decides: CHECK there until it writes 0 or 1, before each step.
    for (const [index, list] of checks) {
      const event = stageImage.events[index];
      for (const [command, t] of list) {
        const at = event.offset + event.listing[command + 1].offset + 2;
        block = block.slice(0, at * 2) + CHECK.toString(16).toUpperCase() + block.slice(at * 2 + 4);
        patches.push({ scene: scenes[key], at, test: t });
      }
    }
    stageImage.events_hex = block;
    // The stage's own 作戦目的: its opening writes 3D65 127,127, the host shows these two there.
    if ('objective' in stage) {
      const o = stage.objective;
      if (!isObj(o) || Object.keys(o).sort().join(',') !== 'lose,win') throw new CompileError(`关卡 ${key}：objective 写 {"win": {"ja": …}, "lose": {"ja": …}}`);
      const opening = stageImage.events.find((event) => event.type === 12)!;
      if (!opening.listing.some((row) => row.opcode === '3D65' && row.operands[0] === 127 && row.operands[1] === 127))
        throw new CompileError(`关卡 ${key}：写了 objective，开场要有 {"op": "3D65", "args": [127, 127]}`);
      const text = (value: unknown, name: string) => {
        if (!isObj(value) || !('ja' in value)) throw new CompileError(`关卡 ${key}：objective.${name} 写成 {"ja": …, "zh-Hans": …}，至少有 ja`);
        for (const [locale, words] of Object.entries(value))
          if (typeof words !== 'string' || !words.trim() || [...words].length > 30 || words.includes('<')) throw new CompileError(`关卡 ${key}：objective.${name}.${locale} 为空或超过 30 字`);
        texts.push({ speaker: 0, lines: { ...value }, kind: 'objective' });
        return texts.length - 1;
      };
      objectives.push({ scene: scenes[key], win: text(o.win, 'win'), lose: text(o.lose, 'lose') });
    }
    if (key in custom) stageImage.custom_map = custom[key];
    const entry: Obj = { key, scene: scenes[key], title: stage.title ?? null, image: stageImage };
    const meta = source.meta?.[key];
    if (meta) {
      if (meta.schema !== 'srw64.stage-meta.v1') throw new CompileError(`Stage ${key}: ${key}.meta.json is not srw64.stage-meta.v1`);
      if (!Array.isArray(meta.cards) || !meta.cards.length) throw new CompileError(`Stage ${key}: ${key}.meta.json has no cards`);
      entry.meta = { ...meta, scene: scenes[key] };
    }
    compiled.push(entry);
  }

  // Every 3D4B stays inside the campaign (500 goes back to the scene the last one named).
  const inside = new Set(Object.values(scenes));
  const byScene = Object.fromEntries(Object.entries(scenes).map(([key, scene]) => [scene, key]));
  const targets: [string, number][] = compiled.flatMap((stage) => nextScenes(stage.image).map((target): [string, number] => [stage.key, target]));
  for (const event of image.events) for (const line of event.listing as string[])
    if (line.startsWith('3D4B ')) targets.push([byScene[event.scene], Number(line.split(' ')[1])]);
  for (const [key, target] of targets)
    if (target !== 500 && !inside.has(target)) throw new CompileError(`关卡 ${key}：3D4B ${target} 指到战役外面，用 {"next": "关卡名"}`);

  const id = document.id;
  const result: Obj = { schema: 'srw64.campaign-image.v2', id: id ?? null, name: document.name ?? id ?? null, description: document.description ?? null,
    author: String(document.author ?? ''), version: String(document.version ?? ''), start_scene: scenes[document.start], stages: compiled };
  if (typeof id !== 'string' || !id) throw new CompileError('campaign.json：要有 id（和 mod.json 的 id 相同）');
  if (document.status) {
    if (!source.status) throw new CompileError(`${document.status} is not srw64.stage-status.v1 with rules`);
    result.status = campaignStatus(source.status, compiled, result.start_scene);
    // A graph drawn from the stages' 3D4B also follows those in the events the host runs.
    if (!('graph' in source.status)) for (const protagonist of Object.values(result.status.graph) as Obj[]) for (const [key, target] of targets) {
      const edges: number[] = protagonist[String(scenes[key])] ??= [];
      if (target !== 500 && !edges.includes(target)) { edges.push(target); edges.sort((a, b) => a - b); }
    }
  }
  if (!(image.events.length || texts.length || patches.length)) return { campaign: result, story: null };
  image.campaign = id;
  if (patches.length) image.patches = patches;
  if (objectives.length) image.objectives = objectives;
  return { campaign: result, story: image };
}
