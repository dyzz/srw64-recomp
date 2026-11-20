// The level editor's project (scripts/level-editor.ts): what its author sets, stage by
// stage, and how that becomes the sources the compiler takes (srw64.campaign.v2,
// srw64.stage.v1: docs/design/level-editor.md §3.1). The page keeps the project as it is
// (campaign/source/editor.json in the package it exports) and writes the sources from it
// every time; nothing is read back from sources.

export type Text3 = Record<string, string>;            // by locale: zh-Hans, ja, en
export type Side = 'player' | 'enemy' | 'third';
// Cells of the playable map, both corners in it: x0, y0, x1, y1.
export type Area = [number, number, number, number];
export type When =
  | { kind: 'turn'; turn: number; side: Side }
  | { kind: 'defeated'; who: number; by?: number }         // by: who defeats them (0 or none: anyone)
  | { kind: 'reach'; who: number; side: Side; area: Area }  // who 0: any unit of the side
  | { kind: 'downed'; side: Side; group: number; atLeast: number }   // group -1: any of the side
  | { kind: 'group_gone'; group: number }
  | { kind: 'hp_below'; who: number; percent: number }
  | { kind: 'battle'; a: number; b: number; moment: 'before' | 'after' }
  | { kind: 'enemies_gone' }
  | { kind: 'remaining'; side: Side; atMost: number }
  | { kind: 'persuade'; by: number; target: number };
export type Step =
  | { kind: 'say'; speaker: number; mode: number; text: Text3; silhouette?: boolean }   // silhouette: the original's 「???」
  | { kind: 'deploy'; group: number }
  | { kind: 'leave'; who: number }
  | { kind: 'join'; pilot: number; unit: number }
  | { kind: 'bgm'; n: number }
  | { kind: 'focus'; who: number }
  | { kind: 'wait'; frames: number }
  | { kind: 'fade'; dir: 'in' | 'out' }
  | { kind: 'win' } | { kind: 'lose' }
  | { kind: 'raw'; op: string; args: number[] }
  // A branch, written flat: the steps after `if` up to its `else` or `end` run when its
  // conditions hold, those after `else` up to `end` when they do not.
  | { kind: 'if'; conds: Conds } | { kind: 'else' } | { kind: 'end' };
// A condition (docs/design/level-editor.md §5.3); `not` turns it round.
export type Cond = { not?: boolean } & (
  | { kind: 'turn'; from: number; to: number }
  | { kind: 'on_map' | 'defeated' | 'absent'; who: number }
  | { kind: 'hp'; who: number; below: number }
  | { kind: 'count'; side: Side; atMost: number }
  | { kind: 'in_area'; who: number; side: Side; area: Area }
  | { kind: 'route'; route: number }
  | { kind: 'choice'; n: number }
  | { kind: 'funds'; atLeast: number }
  | { kind: 'downed'; side: Side; atLeast: number }
  | { kind: 'fired'; event: string }
  | { kind: 'roster'; pilot: number }
  | { kind: 'var'; n: number; is: number });
export type Conds = { any: boolean; list: Cond[] };      // any: one of them is enough
export type BattleLine = { id: string; who: number; against: number; when: string[]; text: Text3; once: boolean; conds?: Conds };
export type GameEvent = { id: string; name: string; when: When; once: boolean; fired: string[]; notFired: string[]; conds?: Conds; steps: Step[] };
// When a group comes on: with the opening, by a trigger of its own, or when a story event says so.
export type GroupWhen = { kind: 'opening' } | { kind: 'event' } | Extract<When, { kind: 'turn' | 'remaining' | 'defeated' | 'hp_below' | 'reach' | 'downed' | 'group_gone' }>;
export type Group = { id: number; name: string; side: 0 | 1 | 2; when: GroupWhen };
// ai: how a computer-run unit acts (mode: 0 attacks the weakest, 1 the likeliest to hit,
// 2 hunts `target`, 3 walks to (x, y) without attacking, 4 the same and never answers back,
// 5 guards `target`; wait: turns it stays put first, 31 never moves). drop: the part it
// leaves when defeated. entry: the HP / EN it comes on with (1-7, 0 full).
export type Ai = { mode: number; wait: number; target?: number; x?: number; y?: number };
export type Unit = { group: number; x: number; y: number; pilot: number; unit: number; level: number; upgrade: number; ai?: Ai; drop?: number; entry?: number };
export const AI_NAMES = ['attack', 'accurate', 'hunt', 'goto', 'flee', 'guard'];
// within: the rule counts only up to that turn (0 or none: any turn).
export type WinRule = ({ kind: 'enemies_gone' } | { kind: 'defeated'; who: number } | { kind: 'turn'; turn: number } | { kind: 'remaining'; atMost: number }
  | { kind: 'reach'; who: number; area: Area } | { kind: 'group_gone'; group: number }) & { within?: number };
// reach: an enemy gets there; downed: this many of ours are defeated.
export type LoseRule = { kind: 'defeated'; who: number } | { kind: 'turn'; turn: number } | { kind: 'reach'; area: Area } | { kind: 'downed'; atLeast: number };
// A choice's options: the original's text `text`, or the author's own (`own`, one per option).
export type After = { kind: 'ending' } | { kind: 'next'; stage: string } | { kind: 'choice'; text: number; stages: string[]; own?: Text3[] };
export type Stage = {
  key: string; title: string; map: number | string; bgm: number;
  groups: Group[]; units: Unit[];
  before: Step[];          // the opening on the world map, before the battlefield
  enter: Step[];           // the opening on the battlefield, once the opening groups are on
  events: GameEvent[];     // during the battle
  // In the stage's battles: `who` says `text` at these moments (story.ts QUOTE_MOMENTS),
  // against `against` (0 anyone), if the conditions hold; once only, if so.
  battleLines?: BattleLine[];
  end: Step[];             // after the stage is cleared, on the world map
  win: WinRule[]; lose: LoseRule[]; after: After;
  // The map menu's 作戦目的 window: which of the original's texts it shows (3D65): winning
  // 5567 + win, losing 5593 + lose. Unset: chosen from the rules (objectiveOf).
  objective?: { win?: number; lose?: number; own?: { win: Text3; lose: Text3 } };
};
export type OwnMapInfo = { name: string; like: number; columns: number; rows: number; scale: number; terrain: number[][] };
export type Project = { route: number; stages: Stage[]; maps: OwnMapInfo[] };

export const FRAME = 2;    // rings of cells that are a map's frame
export const KEY = /^[a-z0-9_-]{1,32}$/;

export function newStage(key: string): Stage {
  return { key, title: '', map: 20, bgm: 27,
    groups: [{ id: 0, name: '', side: 1, when: { kind: 'opening' } }, { id: 1, name: '', side: 0, when: { kind: 'opening' } }],
    units: [], before: [], enter: [], events: [], end: [], win: [{ kind: 'enemies_gone' }], lose: [], after: { kind: 'ending' } };
}
export const freeName = (wanted: string, taken: string[]) => { let n = 1, name = wanted; while (taken.includes(name)) name = `${wanted}-${++n}`; return name; };

// A line's texts as the compiler wants them: the locales written, and Japanese always (the
// original interface and a missing translation show it), from Chinese or English if unwritten.
function lines(text: Text3): Text3 {
  const out: Text3 = {};
  for (const locale of ['ja', 'zh-Hans', 'en']) if (text[locale]?.trim()) out[locale] = text[locale].trim();
  if (!out.ja) { const from = out['zh-Hans'] ?? out.en; if (from) out.ja = from; }
  return out;
}
function action(step: Step): Record<string, unknown> {
  switch (step.kind) {
    case 'say': return { say: step.speaker, ...(step.mode ? { mode: step.mode } : {}), ...(step.silhouette ? { silhouette: true } : {}), text: lines(step.text) };
    case 'deploy': return { deploy: step.group };
    case 'leave': return { leave: step.who };
    case 'join': return { join: { pilot: step.pilot, unit: step.unit } };
    case 'bgm': return { bgm: step.n };
    case 'focus': return { focus: step.who };
    case 'wait': return { wait: step.frames };
    case 'fade': return { fade: step.dir };
    case 'win': return { win: true };
    case 'lose': return { lose: true };
    case 'raw': return step.args.length ? { op: step.op, args: step.args } : { op: step.op };
    default: throw new Error(`${step.kind} is not a step of its own`);
  }
}
// One condition as the compiler takes it.
function condition(c: Cond): Record<string, unknown> {
  let out: Record<string, unknown>;
  switch (c.kind) {
    case 'turn': out = { turn: { from: c.from, to: c.to } }; break;
    case 'on_map': case 'defeated': case 'absent': out = { [c.kind]: [c.who] }; break;
    case 'hp': out = { hp: { who: c.who, below: c.below } }; break;
    case 'count': out = { count: { side: c.side, at_most: c.atMost } }; break;
    case 'in_area': out = { in_area: { ...(c.who ? { who: c.who } : { side: c.side }), area: c.area } }; break;
    case 'route': out = { route: [c.route] }; break;
    case 'choice': out = { choice: c.n }; break;
    case 'funds': out = { funds: { at_least: c.atLeast } }; break;
    case 'downed': out = { downed: { side: c.side, at_least: c.atLeast } }; break;
    case 'fired': out = { fired: [c.event] }; break;
    case 'roster': out = { roster: { pilots: [c.pilot] } }; break;
    case 'var': out = { vars: { [c.n]: c.is } }; break;
  }
  return c.not ? { not: out } : out;
}
// A set of conditions as an `if`: null when there is none.
export function conditions(set: Conds | undefined): Record<string, unknown> | null {
  const list = (set?.list ?? []).map(condition);
  if (!list.length) return null;
  if (list.length === 1) return list[0];
  return set!.any ? { any: list } : { all: list };
}
// Flat steps (if / else / end markers) into the compiler's nested ones. An `if` without its
// `end` runs to the end of the list; a stray `else` or `end` is dropped (problems() says so).
export function actions(steps: Step[]): Record<string, unknown>[] {
  let at = 0;
  const block = (inside: boolean): Record<string, unknown>[] => {
    const out: Record<string, unknown>[] = [];
    while (at < steps.length) {
      const step = steps[at];
      if (step.kind === 'else' || step.kind === 'end') { if (inside) return out; ++at; continue; }
      ++at;
      if (step.kind !== 'if') { out.push(action(step)); continue; }
      const then = block(true);
      let otherwise: Record<string, unknown>[] = [];
      if (steps[at]?.kind === 'else') { ++at; otherwise = block(true); }
      if (steps[at]?.kind === 'end') ++at;
      out.push({ if: conditions(step.conds) ?? { turn: { from: 1 } }, then, ...(otherwise.length ? { else: otherwise } : {}) });
    }
    return out;
  };
  return block(false);
}
// Whether the flat markers pair up.
export function balanced(steps: Step[]): boolean {
  const open: boolean[] = [];   // per open if: whether its else came
  for (const step of steps) {
    if (step.kind === 'if') open.push(false);
    else if (step.kind === 'else') { if (!open.length || open[open.length - 1]) return false; open[open.length - 1] = true; }
    else if (step.kind === 'end') { if (!open.length) return false; open.pop(); }
  }
  return !open.length;
}
function trigger(when: When | GroupWhen): Record<string, unknown> {
  switch (when.kind) {
    case 'turn': return { turn: { turn: when.turn, side: when.side } };
    case 'defeated': return { defeated: when.by ? { who: when.who, by: when.by } : when.who };
    case 'reach': return { reach: { ...(when.who ? { who: when.who } : { side: when.side }), area: when.area } };
    case 'downed': return { downed: { side: when.side, ...(when.group >= 0 ? { group: when.group } : {}), at_least: when.atLeast } };
    case 'group_gone': return { group_gone: when.group };
    case 'hp_below': return { hp_below: { who: when.who, percent: when.percent } };
    case 'battle': return { battle: { between: [when.a, when.b], moment: when.moment } };
    case 'enemies_gone': return { enemies_gone: true };
    case 'remaining': return { remaining: { side: when.side, at_most: when.atMost } };
    case 'persuade': return { persuade: { by: when.by, target: when.target } };
    default: throw new Error(`no trigger for ${when.kind}`);
  }
}
// The objective texts the rules suggest, as 3D65's numbers: 17 「敵の全滅」, losing 31
// 「主人公機の撃墜」, 41 「味方機の撃墜」, 21 「指定ターンの経過」, 38 「味方の全滅」.
export function objectiveOf(stage: Stage): { win: number; lose: number } {
  if (stage.objective?.own) return { win: 127, lose: 127 };   // the host shows the stage's own
  const lost = stage.lose[0];
  const lose = !lost ? 38 : lost.kind === 'turn' ? 21 : lost.kind === 'downed' ? 41 : lost.kind === 'defeated' ? 31 : 38;
  return { win: stage.objective?.win ?? 17, lose: stage.objective?.lose ?? lose };
}
const manned = (stage: Stage, group: Group) => stage.units.some((u) => u.group === group.id);

// The stage's events as its source has them. The opening goes from the world map to the
// battlefield and brings the opening groups on; a group with a trigger of its own gets an
// event that brings it on; every win and lose rule is an event; the end leads on. The
// fixed instructions around them are the sample campaign's (config/recomp/campaigns/sample).
export function stageEvents(stage: Stage): Record<string, unknown>[] {
  const events: Record<string, unknown>[] = [];
  events.push({ when: 'opening', do: [{ bgm: 49 }, { op: '3D32', args: [4] }, { op: '3D32', args: [0] }, { fade: 'in' }, ...actions(stage.before),
    { fade: 'out' }, { to_map: true }, { op: '3D65', args: [objectiveOf(stage).win, objectiveOf(stage).lose] }, { fade: 'in' }, { bgm: stage.bgm },
    ...stage.groups.filter((g) => g.when.kind === 'opening' && manned(stage, g)).map((g) => ({ deploy: g.id })), ...actions(stage.enter), { op: '3D48' }] });
  for (const group of stage.groups) if (group.when.kind !== 'opening' && group.when.kind !== 'event' && manned(stage, group))
    events.push({ id: `group-${group.id}`, when: trigger(group.when), do: [{ deploy: group.id }] });
  for (const event of stage.events) {
    const cond: Record<string, unknown> = {};
    if (event.fired.length) cond.fired = event.fired;
    if (event.notFired.length) cond.not_fired = event.notFired;
    const more = conditions(event.conds);
    if (more) Object.assign(cond, 'any' in more || 'all' in more || 'not' in more ? more : { all: [more] });
    events.push({ id: event.id, when: trigger(event.when), ...(Object.keys(cond).length ? { if: cond } : {}), ...(event.once ? {} : { repeat: true }), do: actions(event.steps) });
  }
  stage.win.forEach((rule, n) => {
    const when: When = rule.kind === 'turn' ? { kind: 'turn', turn: rule.turn, side: 'player' } : rule.kind === 'remaining' ? { kind: 'remaining', side: 'enemy', atMost: rule.atMost }
      : rule.kind === 'defeated' ? { kind: 'defeated', who: rule.who } : rule.kind === 'reach' ? { kind: 'reach', who: rule.who, side: 'player', area: rule.area }
      : rule.kind === 'group_gone' ? { kind: 'group_gone', group: rule.group } : { kind: 'enemies_gone' };
    const within = rule.kind !== 'turn' && rule.within ? { if: { turn: { to: rule.within } } } : {};
    events.push({ id: `win-${n + 1}`, when: trigger(when), ...within, do: [{ win: true }] });
  });
  // A turn limit is lost when the turn after it begins.
  stage.lose.forEach((rule, n) => events.push({ id: `lose-${n + 1}`,
    when: trigger(rule.kind === 'turn' ? { kind: 'turn', turn: rule.turn + 1, side: 'player' } : rule.kind === 'reach' ? { kind: 'reach', who: 0, side: 'enemy', area: rule.area }
      : rule.kind === 'downed' ? { kind: 'downed', side: 'player', group: -1, atLeast: rule.atLeast } : { kind: 'defeated', who: rule.who }), do: [{ lose: true }] }));
  const after = stage.after;
  const leads = after.kind === 'ending' ? [{ ending: true }] : after.kind === 'next' ? [{ next: after.stage }]
    : [{ choose: { text: after.own ? optionTexts(after.own) : after.text, options: after.stages.map((s) => [{ next: s }]) } }];
  events.push({ when: 'end', do: [{ bgm: 49 }, { op: '3D32', args: [1] }, { fade: 'in' }, ...actions(stage.end),
    ...(stage.end.some((s) => s.kind === 'say') ? [{ close: true }] : []), ...leads] });
  return events;
}
// The options' own text by locale, as the compiler takes it: a locale every option has
// (Japanese filled in as lines() does).
function optionTexts(own: Text3[]): Record<string, string[]> {
  const each = own.map(lines), out: Record<string, string[]> = {};
  for (const locale of ['ja', 'zh-Hans', 'en']) if (each.every((t) => t[locale])) out[locale] = each.map((t) => t[locale]);
  return out;
}
export function stageSource(stage: Stage): Record<string, unknown> {
  const side = new Map(stage.groups.map((g) => [g.id, g.side]));
  const own = stage.objective?.own;
  return { schema: 'srw64.stage.v1', ...(stage.title ? { title: stage.title } : {}), map: stage.map,
    ...(own ? { objective: { win: lines(own.win), lose: lines(own.lose) } } : {}),
    units: { place: stage.units.map((u) => {
      const ours = (side.get(u.group) ?? 1) === 0, ai = u.ai;
      return { group: u.group, x: u.x, y: u.y, pilot: u.pilot, unit: u.unit, faction: side.get(u.group) ?? 1, level: u.level,
        ...(u.upgrade ? { upgrade: u.upgrade } : {}), ...(u.entry ? { entry: u.entry } : {}),
        ...(!ours && u.drop !== undefined ? { drop: u.drop } : {}),
        ...(!ours && ai && (ai.mode || ai.wait) ? { ai: { mode: AI_NAMES[ai.mode], ...(ai.wait ? { wait: ai.wait } : {}),
          ...(ai.mode === 2 || ai.mode === 5 ? { target: ai.target ?? 0 } : {}), ...(ai.mode === 3 || ai.mode === 4 ? { to: [ai.x ?? 0, ai.y ?? 0] } : {}) } } : {}) };
    }) },
    events: stageEvents(stage),
    ...(stage.battleLines?.length ? { battle_lines: stage.battleLines.map((b) => ({ id: b.id, who: b.who, ...(b.against ? { against: b.against } : {}), when: b.when,
      text: lines(b.text), ...(b.once ? { once: true } : {}), ...(conditions(b.conds) ? { if: conditions(b.conds) } : {}) })) } : {}) };
}
export const campaignSource = (project: Project, id: string, name: unknown, version: string) => ({ schema: 'srw64.campaign.v2', id, name, version,
  start: project.stages[0].key, route: project.route, stages: project.stages.map((s) => s.key) });

// Where a stage leads, for the flow chart and the checks.
export const leadsTo = (stage: Stage): string[] => stage.after.kind === 'next' ? [stage.after.stage] : stage.after.kind === 'choice' ? stage.after.stages : [];

export type Problem = { stage: string; code: string; n?: number; hard: boolean };
// What the page finds wrong by itself, before the compiler's own checks. `passable(stage,
// x, y)`: whether a unit may stand on that playable cell; `known`: the numbers that exist.
export function problems(project: Project, size: (s: Stage) => { columns: number; rows: number }, passable: (s: Stage, x: number, y: number) => boolean,
  known: { unit: (n: number) => boolean; pilot: (n: number) => boolean }): Problem[] {
  const out: Problem[] = [];
  const keys = project.stages.map((s) => s.key);
  for (const s of project.stages) {
    const add = (code: string, hard = true, n?: number) => out.push({ stage: s.key, code, n, hard });
    if (!KEY.test(s.key)) add('key');
    if (keys.filter((k) => k === s.key).length > 1) add('keyTwice');
    if (leadsTo(s).some((t) => !keys.includes(t))) add('next');
    const sides = [0, 1, 2].map((side) => s.units.filter((u) => s.groups.find((g) => g.id === u.group)?.side === side).length);
    if (!sides[0]) add('noPlayer');
    if (!sides[1] && s.win.some((r) => r.kind === 'enemies_gone')) add('noEnemy', false);
    if (sides.some((count) => count > 30)) add('tooMany');
    if (!s.win.length) add('noWin');
    const { columns, rows } = size(s);
    s.units.forEach((u, n) => {
      if (u.x < 0 || u.y < 0 || u.x >= columns - 2 * FRAME || u.y >= rows - 2 * FRAME) add('outside', true, n + 1);
      else if (!passable(s, u.x, u.y)) add('onWall', true, n + 1);
      if (!known.unit(u.unit)) add('unit', true, n + 1);
      if (!known.pilot(u.pilot)) add('pilot', true, n + 1);
      if (s.units.some((o, k) => k < n && o.x === u.x && o.y === u.y && o.group === u.group)) add('sameCell', true, n + 1);
    });
    for (const g of s.groups) if (g.when.kind === 'event' && manned(s, g)
      && ![...s.before, ...s.enter, ...s.end, ...s.events.flatMap((e) => e.steps)].some((step) => step.kind === 'deploy' && step.group === g.id)) add('groupNever', false, g.id);
    const says = [...s.before, ...s.enter, ...s.end, ...s.events.flatMap((e) => e.steps)].filter((step) => step.kind === 'say');
    if (says.some((step) => step.kind === 'say' && !Object.values(step.text).some((t) => t?.trim()))) add('emptyLine');
    if ((s.battleLines ?? []).some((b) => !Object.values(b.text).some((t) => t?.trim()) || !b.when.length)) add('emptyBattleLine');
    if (![s.before, s.enter, s.end, ...s.events.map((e) => e.steps)].every(balanced)) add('unbalanced');
    const { columns: c, rows: r } = size(s), cells = (a: Area) => a.every((v, k) => v >= 0 && v < (k % 2 ? r : c) - 2 * FRAME);
    const areas = [...s.events.flatMap((e) => [e.when, ...(e.conds?.list ?? []), ...e.steps.flatMap((x) => x.kind === 'if' ? x.conds.list : [])]), ...s.groups.map((gr) => gr.when), ...s.win, ...s.lose]
      .flatMap((x) => 'area' in x ? [x.area] : []);
    if (!areas.every(cells)) add('areaOutside');
  }
  // Stages nothing leads to (the first is where the campaign starts).
  const reached = new Set([keys[0]]);
  for (let grew = true; grew;) { grew = false; for (const s of project.stages) if (reached.has(s.key)) for (const t of leadsTo(s)) if (!reached.has(t)) { reached.add(t); grew = true; } }
  for (const s of project.stages) if (!reached.has(s.key)) out.push({ stage: s.key, code: 'unreached', hard: false });
  return out;
}
