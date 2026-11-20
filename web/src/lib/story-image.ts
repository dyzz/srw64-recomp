// The unit MOD maker's story file (docs/design/unit-mod-web-tool.md W2): a new character
// joining after a stage and a package's own reinforcements, compiled the way tools/mods/story.py
// compiles them (srw64.story-image.v1, read by src/host/story_mod.hpp), since a package holds
// compiled story files. Only what the maker writes: events `after` opening/end with one join,
// events on a turn with one deploy, the deployments and the stage information entries of the
// joins. tests/test_unit_maker.py checks the output against story.py's. No DOM, no imports.

export const STORY_FILE = 'story/unit-maker.json';
export const IMAGE_SCHEMA = 'srw64.story-image.v1';
const NUMBER = 0xfffd, END = 0xffff, EVENT_TYPE = 12;
const JOIN = 0x3d5a, DEPLOY = 0x3d45, JOIN_MODE = 500;
const NAMED = /^([a-z0-9][a-z0-9._-]*)?@[a-z0-9._-]{1,40}$/;
export const KEY = /^[a-z0-9][a-z0-9_.-]{0,39}$/;
const TURN_SIDES = { player: 1, enemy: 2, third: 3 } as const;
export const DEPLOY_SIDES = { player: 0, enemy: 1, third: 2, third_hostile: 4 } as const;

export type Ref = number | string;
export type Texts = Partial<Record<'ja' | 'zh-Hans' | 'zh-Hant' | 'en' | 'ko', string>>;
// A character and unit joining us when a stage opens or ends; optionally a hidden element on the
// stage information page, given again in a later playthrough (inherit).
export interface Join {
  id: string; scene: number; at: 'opening' | 'end'; pilot: Ref; unit: Ref; level: number;
  info?: { title: Texts; text: Texts; inherit: boolean };
}
export type DeploySide = keyof typeof DEPLOY_SIDES;
export interface WaveUnit { x: number; y: number; pilot: Ref; unit: Ref; side: DeploySide; level: number; upgrade: number }
// A group of the package's own that comes on a stage's map at the start of a side's turn.
export interface Wave { id: string; scene: number; turn: number; phase: keyof typeof TURN_SIDES; units: WaveUnit[] }

const named = (v: Ref): v is string => typeof v === 'string' && NAMED.test(v);
const eventBase = (id: string, scene: number, trigger: string, params: number[]) => ({
  id, scene, trigger, params, vars: {}, kills: {}, kills_below: {}, set_flags: [], fired: [], not_fired: [], repeat: false,
});
const listing = (words: number[]) => {
  // One instruction then the end, as story.py lists them.
  const [op, ...args] = words.slice(0, -1);
  return [`${op.toString(16).toUpperCase().padStart(4, '0')} ${args.join(',')}`.trimEnd(), 'FFFF'];
};

function joinEvent(j: Join) {
  const numbers: [number, string, string][] = [];
  const pilot = named(j.pilot) ? (numbers.push([1, j.pilot, 'pilot']), NUMBER) : j.pilot;
  const unit = named(j.unit) ? (numbers.push([3, j.unit, 'unit']), NUMBER) : j.unit;
  const words = [JOIN, pilot as number, j.level & 0xffff, unit as number, JOIN_MODE, END];
  return { ...eventBase(`join-${j.id}`, j.scene, `after_${j.at}`, []), words, texts: [], levels: [], crew: [],
    ...(numbers.length ? { numbers } : {}), listing: listing(words) };
}
function waveEvent(w: Wave) {
  const words = [DEPLOY, NUMBER, END];
  return { ...eventBase(`wave-${w.id}`, w.scene, 'turn', [w.turn - 1, TURN_SIDES[w.phase]]), words, texts: [], levels: [], crew: [],
    groups: [[1, w.id, false]], listing: listing(words) };
}
function joinInfo(j: Join) {
  const info = j.info!;
  return {
    scene: j.scene, title: info.title, text: info.text, id: `join-${j.id}`, done: { units: [j.unit] },
    ...(info.inherit ? { inherit: { [j.at === 'end' ? 'after' : 'before']: [j.scene], commands: [['3D5A', j.pilot, j.level, j.unit, JOIN_MODE]] } } : {}),
  };
}

// The compiled file, or null when there is nothing to write.
export function compile(joins: Join[], waves: Wave[]) {
  if (!joins.length && !waves.length) return null;
  const info = joins.filter((j) => j.info).map(joinInfo);
  return {
    schema: IMAGE_SCHEMA,
    events: [...joins.map(joinEvent), ...waves.map(waveEvent)],
    texts: [],
    ...(info.length ? { stage_info: info } : {}),
    ...(waves.length ? { deployments: waves.map((w) => ({ id: w.id, scene: w.scene, records: w.units.map((u) => ({ x: u.x, y: u.y, pilot: u.pilot, unit: u.unit, side: DEPLOY_SIDES[u.side], level: u.level, upgrade: u.upgrade })) })) } : {}),
  };
}

// The maker's own file read back.
export function read(image: any): { joins: Join[]; waves: Wave[] } {
  const joins: Join[] = [], waves: Wave[] = [];
  const infos = new Map<string, any>((image?.stage_info ?? []).map((s: any) => [s.id, s]));
  const groups = new Map<string, any>((image?.deployments ?? []).map((d: any) => [d.id, d]));
  for (const e of image?.events ?? []) {
    const value = (at: number) => (e.numbers ?? []).find((n: any) => n[0] === at)?.[1] ?? e.words[at];
    if (typeof e.id === 'string' && e.id.startsWith('join-') && e.words?.[0] === JOIN) {
      const level = e.words[2] >= 0x8000 ? e.words[2] - 0x10000 : e.words[2];
      const j: Join = { id: e.id.slice(5), scene: e.scene, at: e.trigger === 'after_opening' ? 'opening' : 'end', pilot: value(1), unit: value(3), level };
      const s = infos.get(e.id);
      if (s) j.info = { title: s.title, text: s.text, inherit: !!s.inherit };
      joins.push(j);
    } else if (typeof e.id === 'string' && e.id.startsWith('wave-') && e.words?.[0] === DEPLOY) {
      const g = groups.get(e.id.slice(5));
      const phase = (Object.keys(TURN_SIDES) as (keyof typeof TURN_SIDES)[]).find((k) => TURN_SIDES[k] === e.params?.[1]) ?? 'player';
      const side = (n: number) => (Object.keys(DEPLOY_SIDES) as DeploySide[]).find((k) => DEPLOY_SIDES[k] === n) ?? 'enemy';
      waves.push({ id: e.id.slice(5), scene: e.scene, turn: (e.params?.[0] ?? 0) + 1, phase,
        units: (g?.records ?? []).map((r: any) => ({ x: r.x, y: r.y, pilot: r.pilot, unit: r.unit, side: side(r.side), level: r.level, upgrade: r.upgrade })) });
    }
  }
  return { joins, waves };
}

// The source story.py would compile to the same file (for the parity test and for authors who
// carry on by hand).
export function source(joins: Join[], waves: Wave[]) {
  return {
    schema: 'srw64.story.v1',
    events: [
      ...joins.map((j) => ({ id: `join-${j.id}`, scene: j.scene, when: { after: j.at }, do: [{ join: { pilot: j.pilot, unit: j.unit, level: { offset: j.level } } }] })),
      ...waves.map((w) => ({ id: `wave-${w.id}`, scene: w.scene, when: { turn: { turn: w.turn, side: w.phase } }, do: [{ deploy: w.id }] })),
    ],
    ...(joins.some((j) => j.info) ? { stage_info: joins.filter((j) => j.info).map((j) => ({
      id: `join-${j.id}`, scene: j.scene, title: j.info!.title, text: j.info!.text, done: { units: [j.unit] },
      ...(j.info!.inherit ? { inherit: { [j.at === 'end' ? 'after' : 'before']: [j.scene], do: [{ join: { pilot: j.pilot, unit: j.unit, level: { offset: j.level } } }] } } : {}),
    })) } : {}),
    ...(waves.length ? { deployments: waves.map((w) => ({ id: w.id, scene: w.scene, units: w.units })) } : {}),
  };
}

// What keeps the file from compiling, as codes the page words (story.py's checks for these).
export function storyProblems(joins: Join[], waves: Wave[]): { where: string; code: string }[] {
  const out: { where: string; code: string }[] = [];
  const add = (where: string, code: string) => out.push({ where, code });
  const ok = (v: number, lo: number, hi: number) => Number.isInteger(v) && v >= lo && v <= hi;
  const ref = (v: Ref, top: number) => (typeof v === 'number' ? ok(v, 0, top) : named(v));
  const ids = new Set<string>();
  for (const j of joins) {
    const where = `joins.${j.id}`;
    if (!KEY.test(`join-${j.id}`) || !KEY.test(j.id)) add(where, 'story_id');
    if (ids.has(`join-${j.id}`)) add(where, 'story_twice');
    ids.add(`join-${j.id}`);
    if (!ok(j.scene, 0, 255)) add(where, 'story_scene');
    if (!ref(j.pilot, 360)) add(where, 'join_pilot');
    if (!ref(j.unit, 362)) add(where, 'join_unit');
    if (!ok(j.level, -98, 98)) add(where, 'join_level');
    if (j.info) for (const t of [j.info.title, j.info.text]) {
      if (!t.ja?.trim()) add(where, 'info_ja');
      if (Object.values(t).some((v) => v && [...v].length > 200)) add(where, 'info_long');
    }
  }
  for (const w of waves) {
    const where = `waves.${w.id}`;
    if (!KEY.test(`wave-${w.id}`) || !KEY.test(w.id)) add(where, 'story_id');
    if (ids.has(`wave-${w.id}`)) add(where, 'story_twice');
    ids.add(`wave-${w.id}`);
    if (!ok(w.scene, 0, 255)) add(where, 'story_scene');
    if (!ok(w.turn, 1, 99)) add(where, 'wave_turn');
    if (w.units.length < 1 || w.units.length > 30) add(where, 'wave_count');
    for (const u of w.units)
      if (!ok(u.x, 0, 63) || !ok(u.y, 0, 63) || !ref(u.pilot, 360) || !ref(u.unit, 362) || !ok(u.level, 0, 20) || !ok(u.upgrade, 0, 5) || !(u.side in DEPLOY_SIDES)) { add(where, 'wave_unit'); break; }
  }
  return out;
}
