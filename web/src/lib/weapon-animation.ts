// A weapon's battle animation as the unit maker edits it (docs/design/weapon-animation-survey.md,
// data/new_units.json weapons.<name>.animation, src/host/mod_units.hpp): copied whole from a
// weapon, or a record of its own written out from the original's (web/src/data/
// weapon-animations.json) and changed: actors moved, borrowed from other weapons, cut-ins
// added. No DOM, no imports, so tests/test_unit_maker.py runs it under Node.

export type Ref = number | string;
export interface Actor { scene: Ref; x: number; y: number; z: number; h4: number; h5: number; h6: number; behavior: number }
export interface Animation { camera: number; hit: number; action: number; defender: number; sounds: number[]; actors: Actor[] }
export type AnimationField = { copy: Ref } | Animation;
// The original's records as exported (tools/content/export_weapon_animations.py).
export type RawRecord = [number, number, number, number, number[], number[][]];
export interface Bank { records: RawRecord[]; weapons: number[]; scenes: number[][]; poses: number[][]; cutins: [number, number]; steps: Record<string, number[]>; complex: number[] }
// The scene resources before and after its own an actor's behaviour moves to (801C5700 with
// its scene's resource number plus k, tools/content/export_weapon_animations.py): a package
// picture there needs copies of its scene in as many resources around it, the sprite's
// "around" [before, after] (src/host/mod_units.hpp).
export function sceneSpan(steps: Record<string, number[]> | undefined, behavior: number): { low: number; high: number } {
  const ks = steps?.[String(behavior)] ?? [];
  return { low: Math.min(0, ...ks), high: Math.max(0, ...ks) };
}
export const MAX_AROUND = 32;
// The original weapons the maker offers to start a new weapon from, to borrow effects from and
// to give a unit (besides its original's own): one ordinary weapon of each common kind, whose
// animation has no routine that computes with scene numbers or shows the lines after the
// speaker's (docs/design/weapon-animation-survey.md §7.4). Others later.
export const GENERIC_WEAPONS: readonly number[] = [
  228,  // 光束步枪 (高达)
  199,  // 光束枪 (冲浪者): the shot only, no gun in hand
  227,  // 光束军刀 (高达)
  226,  // 60mm火神炮 (高达)
  25,   // 火神炮: a short burst
  299,  // 100mm机枪 (高达Ez8)
  327,  // 导弹
  304,  // 火箭筒 (高达Ez8)
  204,  // 光束加农炮 (ν高达)
  224,  // 240mm加农炮 (钢加农)
  31,   // 拳击
  32,   // 踢击
  553,  // 冲撞
  391,  // 热能斧 (扎古Ⅱ)
  716,  // 军刀
];
export const isGeneric = (weapon: number) => GENERIC_WEAPONS.includes(weapon);
// A record with a routine that computes with scene numbers (compares them, keeps them, reads
// another actor's): it keeps the original's pictures, a cut-in's aside.
export const lockedPictures = (a: Animation, complex: number[] | undefined) => !!complex && a.actors.some((actor) => complex.includes(actor.behavior));

export const MAX_ACTORS = 24, MAX_SOUNDS = 8, WINDOW = 0x3c0, SCENES = 1052;
export const isCopy = (a: unknown): a is { copy: Ref } => !!a && typeof a === 'object' && 'copy' in (a as object);

export function fromRecord(r: RawRecord): Animation {
  const [camera, hit, action, defender, sounds, actors] = r;
  return { camera, hit, action, defender, sounds: [...sounds],
    actors: actors.map(([scene, x, y, z, h4, h5, h6, behavior]) => ({ scene, x, y, z, h4, h5, h6, behavior })) };
}
// The original weapon's record as an editable one.
export const original = (bank: Bank, weapon: number) => fromRecord(bank.records[bank.weapons[weapon]]);

// Cut-ins (behaviour 240, 801FBE3C): shown when the phase byte equals h4's low bits, at the
// screen's centre scaled 1.29; with z 1 a black frame leaves a 140x120 window. 0x4000: it ends
// itself when its scene has played, writing 0x56; 0x8000: it waits for another's 0x56 to go.
export const CUTIN = 240, FIRE = 0x44, CUTIN_DONE = 0x56;
// A package cut-in's one frame stays this many ticks unless the author says otherwise (终极断空我's
// is 22; Windows 2026-11-20: 22 ticks from 0x44 to its 0x56 took 0.74 s).
export const CUTIN_TICKS = 30;
// Where a new cut-in goes: after the cut-ins at the head of the list, before the effects. The
// original's records put them first, and so does 终极断空我's.
export function cutinPlace(actors: Actor[]) {
  let at = 0;
  while (at < actors.length && isCutin(actors[at])) at++;
  return at;
}
export type CutinTiming = 'fire' | 'after' | 'with';
export function cutinH4(timing: CutinTiming, phase = FIRE): number {
  if (timing === 'fire') return 0x4000 | FIRE;
  if (timing === 'after') return 0x4000 | CUTIN_DONE;
  return 0x8000 | (phase & 0xff);
}
export function cutinTiming(h4: number): { timing: CutinTiming; phase: number } {
  const phase = h4 & 0xff;
  if (h4 & 0x8000) return { timing: 'with', phase };
  return { timing: phase === CUTIN_DONE ? 'after' : 'fire', phase };
}
export function cutin(scene: Ref, timing: CutinTiming, frame: boolean, phase = FIRE): Actor {
  return { scene, x: 0, y: 0, z: frame ? 1 : 0, h4: cutinH4(timing, phase), h5: 255, h6: 0, behavior: CUTIN };
}
export const isCutin = (a: Actor) => a.behavior === CUTIN || a.behavior === 350 || a.behavior === 351 || a.behavior === 352;

// What the common behaviours do, as far as known (weapon-animation-survey.md §2–§3); the key is
// a name the page words. Others are "an original routine".
export const KNOWN: Record<number, string> = {
  118: 'beam', 111: 'shot', 359: 'aim', 360: 'aim2', 124: 'timer', 43: 'melee_body', 44: 'slash', 62: 'swing',
  119: 'burst', 120: 'burst', 291: 'vulcan', 125: 'thrown', 364: 'flail', 27: 'boomerang', 240: 'cutin',
  350: 'cutin_panel', 351: 'cutin_panel', 352: 'cutin_panel', 373: 'cutin_control', 148: 'backdrop',
};
// Where h4 means a delay in frames, a phase to wait for, or a timer.
export const H4_DELAY = new Set([111]), H4_TIMER = new Set([124]);

export const recordSize = (a: Animation) => 8 + 2 * a.sounds.length + 2 + 16 * a.actors.length + 2;

// What keeps the record from loading (validate.py new_units_problem's animation checks), as codes.
// `onlyOriginalEffects`: the package's pictures only on cut-ins (W4 for now: the game's own
// effects, plus cut-ins).
// `around`: per package scene, its sprite's "around" (a scene of resource numbers: none).
export function animationProblems(a: AnimationField, ownScenes: Set<string>, onlyOriginalEffects = false, bank?: Pick<Bank, 'steps' | 'complex'> | null,
  around?: (scene: string) => [number, number] | undefined): string[] {
  const out: string[] = [];
  if (isCopy(a)) return out;
  const steps = bank?.steps;
  if (around && lockedPictures(a, bank?.complex)
    && a.actors.some((actor) => typeof actor.scene === 'string' && actor.behavior !== CUTIN && around(actor.scene.slice(1)))) out.push('anim_locked');
  // A package picture on a routine that moves to the scene resources next to its own: they
  // have to be copies of it.
  for (const actor of a.actors) {
    if (typeof actor.scene !== 'string' || !actor.scene.startsWith('@') || !around) continue;
    const { low, high } = sceneSpan(steps, actor.behavior), has = around(actor.scene.slice(1));
    if (has && (has[0] < -low || has[1] < high)) { out.push('anim_scene_set'); break; }
  }
  if (onlyOriginalEffects && a.actors.some((actor) => typeof actor.scene === 'string' && actor.behavior !== CUTIN)) out.push('anim_own_effect');
  const int = (v: unknown, lo: number, hi: number) => Number.isInteger(v) && (v as number) >= lo && (v as number) <= hi;
  if (![a.camera, a.hit, a.action, a.defender].every((v) => int(v, 0, 0x7fff))) out.push('anim_header');
  if (a.sounds.length > MAX_SOUNDS || !a.sounds.every((s) => int(s, -2, 0x7fff))) out.push('anim_sounds');
  if (a.actors.length < 1 || a.actors.length > MAX_ACTORS) out.push('anim_actors');
  for (const actor of a.actors) {
    const scene = actor.scene;
    if (!(typeof scene === 'number' ? int(scene, 0, SCENES) : typeof scene === 'string' && (!scene.startsWith('@') || ownScenes.has(scene.slice(1))))) { out.push('anim_scene'); break; }
  }
  for (const actor of a.actors) {
    if (![actor.x, actor.y, actor.z].every((v) => int(v, -32768, 32767)) || ![actor.h4, actor.h5, actor.h6].every((v) => int(v, 0, 0xffff))) { out.push('anim_numbers'); break; }
  }
  for (const actor of a.actors) if (!int(actor.behavior, 3, 382)) { out.push('anim_behavior'); break; }
  if (recordSize(a) > WINDOW) out.push('anim_long');
  return out;
}

// Each actor's box at the start, in the attacker's frame (x right, y down, the pose's origin
// at 0, 0): its scene's canvas placed at (x, -y). Cut-ins have none here (they sit at the
// screen's centre).
export function actorBox(actor: Actor, canvas: number[] | undefined) {
  if (!canvas || isCutin(actor)) return null;
  const [x0, y0, x1, y1] = canvas;
  return { x0: actor.x + x0, y0: -actor.y + y0, x1: actor.x + x1, y1: -actor.y + y1 };
}

// The fire point: where the weapon's effects start, as one point to move. The actors it moves
// are the effects placed somewhere (not the attacker's own pose, 359; its melee body, 43; the
// timer, 124; the cut-in control, 373; cut-ins; nor an effect at 0, 0, which its routine
// places itself); the point shown is the first of them.
export const BODY = new Set([359, 43, 124, 373]);
export const movesWithFirePoint = (actor: Actor) => !BODY.has(actor.behavior) && !isCutin(actor) && (actor.x !== 0 || actor.y !== 0);
export function firePoint(a: Animation): { x: number; y: number } | null {
  const first = a.actors.find(movesWithFirePoint);
  return first ? { x: first.x, y: first.y } : null;
}
// Every actor that moves with it, by the same amount; false when there is no fire point (or
// the point asked for is no number). They move as one: the move is cut short where any of
// them would leave the record's range, and none lands on 0, 0 (its routine would place it
// itself, and it would no longer move with the others), the move going a step further.
export const COORD_MIN = -32768, COORD_MAX = 32767;
export function moveFirePoint(a: Animation, x: number, y: number): boolean {
  const at = firePoint(a);
  if (!at || !Number.isFinite(x) || !Number.isFinite(y)) return false;
  const moving = a.actors.filter(movesWithFirePoint);
  const xs = moving.map((m) => m.x), ys = moving.map((m) => m.y);
  const lowX = COORD_MIN - Math.min(...xs), highX = COORD_MAX - Math.max(...xs);
  const lowY = COORD_MIN - Math.min(...ys), highY = COORD_MAX - Math.max(...ys);
  const within = (d: number, lo: number, hi: number) => Math.max(lo, Math.min(hi, d));
  const dx0 = within(Math.round(x) - at.x, lowX, highX), dy = within(Math.round(y) - at.y, lowY, highY);
  const clear = (dx: number) => dx >= lowX && dx <= highX && !moving.some((m) => m.x + dx === 0 && m.y + dy === 0);
  let dx = dx0;
  for (let k = 1; !clear(dx) && k <= 2 * moving.length + 2; k++) dx = dx0 + (k % 2 ? 1 : -1) * Math.ceil(k / 2);
  if (!clear(dx)) return false;
  for (const actor of moving) { actor.x += dx; actor.y += dy; }
  return true;
}

// A recoloured original scene (data/new_units.json "scenes": {"recolor": row, "colors":
// {"index": "#rrggbb"}}, src/host/mod_units.hpp recolor): the row's picture with some palette
// entries changed. An actor names it as "@name", like a picture of the package.
export interface Recolor { recolor: number; colors: Record<string, string> }
export const isRecolor = (v: unknown): v is Recolor => !!v && typeof v === 'object' && Number.isInteger((v as Recolor).recolor)
  && !!(v as Recolor).colors && typeof (v as Recolor).colors === 'object';
export function recolorProblem(v: Recolor): string | null {
  if (Object.keys(v).some((k) => k !== 'recolor' && k !== 'colors')) return 'recolor_field';
  if (!(v.recolor >= 0 && v.recolor <= SCENES)) return 'recolor_scene';
  const entries = Object.entries(v.colors);
  if (!entries.length) return 'recolor_empty';
  if (entries.some(([k, c]) => !/^\d{1,3}$/.test(k) || Number(k) > 255 || typeof c !== 'string' || !/^#[0-9a-f]{6}$/i.test(c))) return 'recolor_color';
  return null;
}

// The original row an actor draws: its scene number, or the row a recolour of the package's names.
export function recolorRow(scenes: Record<string, unknown>, actor: Actor): number | undefined {
  if (typeof actor.scene === 'number') return actor.scene;
  const v = actor.scene.startsWith('@') ? scenes[actor.scene.slice(1)] : undefined;
  return isRecolor(v) ? v.recolor : undefined;
}
const recolorNameOf = (scenes: Record<string, unknown>, actor: Actor) =>
  typeof actor.scene === 'string' && actor.scene.startsWith('@') && isRecolor(scenes[actor.scene.slice(1)]) ? actor.scene.slice(1) : undefined;
// A name for a weapon's new recolour of `row`: "<slug>-color-<row>", the slug shortened so the
// name keeps to the 40 characters a name may have (src/host/mod_units.hpp valid_name), and
// numbered when that one is taken.
export const NAME_MOST = 40;
export function recolorName(slug: string, row: number, taken: (name: string) => boolean): string {
  for (let n = 1; ; n++) {
    const tail = `-color-${row}${n > 1 ? `-${n}` : ''}`;
    const name = `${slug.slice(0, NAME_MOST - tail.length)}${tail}`;
    if (!taken(name)) return name;
  }
}
// Gives `targets` (actors of one weapon's record) these colours, or their original scenes back
// when there are none. The targets drawing one row share one recolour scene: one of theirs
// when no other actor (`everyone`: every actor of every record, these included) names it,
// else a new one, so actors left out keep their colours. Scenes no one names any more stay
// until pruneRecolors.
export function applyRecolor(scenes: Record<string, unknown>, everyone: Actor[], targets: Actor[], colors: Record<string, string>, slug: string) {
  const byRow = new Map<number, Actor[]>();
  for (const t of targets) {
    const row = recolorRow(scenes, t);
    if (row !== undefined) byRow.set(row, [...(byRow.get(row) ?? []), t]);
  }
  const outside = new Set(everyone.filter((x) => !targets.includes(x)).map((x) => recolorNameOf(scenes, x)));
  for (const [row, group] of byRow) {
    if (!Object.keys(colors).length) { for (const t of group) t.scene = row; continue; }
    const name = group.map((t) => recolorNameOf(scenes, t)).find((n) => n !== undefined && !outside.has(n))
      ?? recolorName(slug, row, (n) => n in scenes);
    scenes[name] = { recolor: row, colors: { ...colors } };
    for (const t of group) t.scene = `@${name}`;
  }
}
// Recolour scenes no record names any more go (each takes a scene row and a palette in the game).
export function pruneRecolors(scenes: Record<string, unknown>, animations: unknown[]) {
  const named = new Set<string>();
  for (const a of animations) if (a && typeof a === 'object' && !isCopy(a) && Array.isArray((a as Animation).actors))
    for (const actor of (a as Animation).actors) if (typeof actor.scene === 'string') named.add(actor.scene.slice(1));
  for (const [k, v] of Object.entries(scenes)) if (isRecolor(v) && !named.has(k)) delete scenes[k];
}
// Whether two colours are one to the game, which keeps 5 bits a channel (mod_units.hpp recolor).
export function sameColor(a: string, b: string): boolean {
  const five = (hex: string) => { const v = parseInt(hex.slice(1), 16); return (v >> 19 & 31) << 10 | (v >> 11 & 31) << 5 | (v >> 3 & 31); };
  return /^#[0-9a-f]{6}$/i.test(a) && /^#[0-9a-f]{6}$/i.test(b) && five(a) === five(b);
}
