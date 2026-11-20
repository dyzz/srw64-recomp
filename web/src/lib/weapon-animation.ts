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
export interface Bank { records: RawRecord[]; weapons: number[]; scenes: number[][]; poses: number[][]; cutins: [number, number]; steps: Record<string, number[]> }
// The scene resources before and after its own an actor's behaviour moves to (801C5700 with
// its scene's resource number plus k, tools/content/export_weapon_animations.py): a package
// picture there needs copies of its scene in as many resources around it, the sprite's
// "around" [before, after] (src/host/mod_units.hpp).
export function sceneSpan(steps: Record<string, number[]> | undefined, behavior: number): { low: number; high: number } {
  const ks = steps?.[String(behavior)] ?? [];
  return { low: Math.min(0, ...ks), high: Math.max(0, ...ks) };
}
export const MAX_AROUND = 16;

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
export function animationProblems(a: AnimationField, ownScenes: Set<string>, onlyOriginalEffects = false, steps?: Record<string, number[]>,
  around?: (scene: string) => [number, number] | undefined): string[] {
  const out: string[] = [];
  if (isCopy(a)) return out;
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
