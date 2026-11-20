// The unit MOD maker's package (docs/design/unit-mod-web-tool.md W1): the new characters,
// units and weapons a package adds, to and from its files, by the rules of
// tools/mods/validate.py (new_units_problem, data_problem, names_problem). No DOM and no
// imports, so tests/test_unit_maker.py runs it under Node and checks what it writes with
// the Python validator.

export const NEW_UNITS_SCHEMA = 'srw64.new-units.v2';
export const NAMES_SCHEMA = 'srw64.mod-names.v1';
export const LOCALES = ['ja', 'zh-Hans', 'en', 'ko'] as const;
export type Locale = (typeof LOCALES)[number];
// A new entry's name (validate.py NAME) and a reference to one ("@name", "package@name").
export const SLUG = /^[a-z0-9._-]{1,40}$/;
export const REFERENCE = /^([a-z0-9][a-z0-9._-]*)?@[a-z0-9._-]{1,40}$/;
export const isReference = (v: unknown): v is string => typeof v === 'string' && REFERENCE.test(v);
export const NAME_LONGEST = 24, MODEL_LONGEST = 48;

export type Ref = number | string;
// data/units.json, pilots.json, weapons.json rows as the files write them.
export type Changes = Record<string, unknown>;
export type PilotNames = Partial<Record<Locale, { name: string; full?: string }>>;
export type Texts = Partial<Record<Locale, string>>;

// dialogue/battle_quotes.json (src/host/mod_quotes.hpp): a new character's own battle lines.
// A line by locale (ja required) and the Ogg it may play (audio/voice/battle/<voice>).
export const QUOTE_SITUATIONS = ['attack', 'shot_down', 'heavy', 'medium', 'light', 'evade', 'no_damage', 'no_ammo', 'out_of_range'] as const;
export type Situation = (typeof QUOTE_SITUATIONS)[number];
export type Line = Partial<Record<'ja' | 'zh-Hans' | 'zh-Hant' | 'en' | 'ko', string>> & { voice?: string };
// A line for a weapon (always attacking; `sometimes` lets the general lines win now and then)
// or against an original character in a situation.
export type Special = { weapon?: Ref; opponent?: number; situation?: Situation; sometimes?: boolean; lines: Line[] };
export type Quotes = Partial<Record<Situation, Line[]>> & { special?: Special[] };
export const QUOTE_LONGEST = 42, QUOTE_SPECIALS = 119;

export interface Pilot {
  slug: string; copy: number; voice?: number; portrait?: { image: string; hd?: string } | [number, number];
  love?: { with: Ref; mutual: boolean }[]; combo_as?: number;
  quotes?: Quotes;
  names: PilotNames; data: Changes;
}
export interface Unit {
  slug: string; copy: number; weapons: Ref[]; forms?: string[]; crew?: number[];
  combine?: { parts: Ref[]; over?: number[] }; inherits_from?: Ref; icon?: number; pose?: unknown; work?: number;
  names: Texts; models: Texts; data: Changes;
}
export interface Combination { units: Ref[]; parts: Ref[]; multiplier: number; pilots?: Ref[][] }
export interface Weapon {
  slug: string; copy: Ref; inherits_from?: Ref; combination?: Combination; animation?: unknown;
  names: Texts; data: Changes;
}
// A package picture (data/new_units.json "sprites", src/host/mod_units.hpp): one frame's
// "image" or several "frames", the pixel of each that is the scene's origin (a pose's: the
// middle under its feet), mirrored for the other side unless `mirror` is false (cut-ins), the
// ticks of each frame and the frame it loops back to, an HD picture per frame.
export interface Sprite {
  image?: string; frames?: string[]; origin: [number, number]; mirror?: boolean; ticks?: number[]; loop?: number; hd?: string | string[];
}
export interface Model {
  pilots: Pilot[]; units: Unit[]; weapons: Weapon[];
  sprites?: Record<string, Sprite>;
  // What this maker does not edit yet but keeps: the scenes the animations name.
  scenes?: Record<string, unknown>;
}
export const emptyModel = (): Model => ({ pilots: [], units: [], weapons: [] });

const DATA_FILES = { pilots: 'data/pilots.json', units: 'data/units.json', weapons: 'data/weapons.json' } as const;
type Kind = keyof typeof DATA_FILES;
export const KINDS: Kind[] = ['pilots', 'units', 'weapons'];

// --- To files --------------------------------------------------------------------------

const pruned = <T extends Record<string, unknown>>(o: T) => Object.fromEntries(Object.entries(o).filter(([, v]) => v !== undefined)) as T;

// The package's files this model writes, over the ones kept from an opened package: its
// data/*.json rows for original numbers and names.json entries other makers wrote stay.
export function toFiles(model: Model, kept: Record<string, unknown> = {}): Record<string, unknown> {
  const out: Record<string, unknown> = {};
  const units: Record<string, unknown> = {}, weapons: Record<string, unknown> = {}, pilots: Record<string, unknown> = {};
  for (const p of model.pilots)
    pilots[p.slug] = pruned({ copy: p.copy, voice: p.voice !== undefined && p.voice !== p.copy ? p.voice : undefined, portrait: p.portrait,
      love: p.love?.length ? p.love : undefined, combo_as: p.combo_as });
  for (const u of model.units)
    units[u.slug] = pruned({ copy: u.copy, pose: u.pose, icon: u.icon, weapons: u.weapons, inherits_from: u.inherits_from,
      crew: u.crew?.length ? u.crew : undefined, combine: u.combine && pruned({ parts: u.combine.parts, over: u.combine.over?.length ? u.combine.over : undefined }), work: u.work, forms: u.forms?.length ? u.forms : undefined });
  for (const w of model.weapons)
    weapons[w.slug] = pruned({ copy: w.copy, inherits_from: w.inherits_from, combination: w.combination, animation: w.animation });
  const doc: Record<string, unknown> = { schema: NEW_UNITS_SCHEMA };
  if (model.sprites && Object.keys(model.sprites).length) doc.sprites = model.sprites;
  if (model.scenes && Object.keys(model.scenes).length) doc.scenes = model.scenes;
  if (Object.keys(weapons).length) doc.weapons = weapons;
  if (Object.keys(units).length) doc.units = units;
  if (Object.keys(pilots).length) doc.pilots = pilots;
  if (Object.keys(doc).length > 1) out['data/new_units.json'] = doc;

  for (const kind of KINDS) {
    const rows: Record<string, unknown> = { ...((kept[DATA_FILES[kind]] as Record<string, unknown>) ?? {}) };
    for (const key of Object.keys(rows)) if (key.startsWith('@')) delete rows[key];
    for (const e of model[kind] as { slug: string; data: Changes }[]) if (Object.keys(e.data).length) rows[`@${e.slug}`] = e.data;
    if (Object.keys(rows).length) out[DATA_FILES[kind]] = rows;
  }

  // An opened package's names.json stays as it was (its schema line is optional); a new one has it.
  const names: Record<string, any> = { ...((kept['dialogue/names.json'] as Record<string, unknown>) ?? { schema: NAMES_SCHEMA }) };
  for (const kind of [...KINDS, 'models'] as const) {
    const part: Record<string, unknown> = { ...(names[kind] ?? {}) };
    for (const key of Object.keys(part)) if (key.startsWith('@')) delete part[key];
    names[kind] = part;
  }
  const filled = <V>(by: Partial<Record<Locale, V>>, empty: (v: V) => boolean) =>
    Object.fromEntries(Object.entries(by).filter(([, v]) => v !== undefined && !empty(v as V)));
  for (const p of model.pilots) {
    const by = filled(p.names, (v) => !v.name.trim());
    for (const v of Object.values(by) as { name: string; full?: string }[]) if (!v.full?.trim()) delete v.full;
    if (Object.keys(by).length) names.pilots[`@${p.slug}`] = by;
  }
  for (const u of model.units) {
    const by = filled(u.names, (v) => !v.trim()), models = filled(u.models, (v) => !v.trim());
    if (Object.keys(by).length) names.units[`@${u.slug}`] = by;
    if (Object.keys(models).length) names.models[`@${u.slug}`] = models;
  }
  for (const w of model.weapons) {
    const by = filled(w.names, (v) => !v.trim());
    if (Object.keys(by).length) names.weapons[`@${w.slug}`] = by;
  }
  for (const kind of [...KINDS, 'models']) if (!Object.keys(names[kind]).length) delete names[kind];
  // battle_quotes.json: this package's characters' entries over the opened file's others.
  const quotes: Record<string, unknown> = { ...(((kept['dialogue/battle_quotes.json'] as Record<string, any>)?.pilots) ?? {}) };
  for (const key of Object.keys(quotes)) if (key.startsWith('@')) delete quotes[key];
  for (const p of model.pilots) {
    const q = cleanQuotes(p.quotes);
    if (q) quotes[`@${p.slug}`] = q;
  }
  if (Object.keys(quotes).length) out['dialogue/battle_quotes.json'] = { schema: BATTLE_QUOTES_SCHEMA, pilots: quotes };
  if (Object.keys(names).some((k) => k !== 'schema')) out['dialogue/names.json'] = names;
  return out;
}

// A sprite's pictures, as it names them.
export function spriteFiles(s: Sprite): { frames: string[]; hd: string[] } {
  const frames = s.frames ?? (s.image ? [s.image] : []);
  return { frames, hd: s.hd === undefined ? [] : Array.isArray(s.hd) ? s.hd : [s.hd] };
}
// The maker writes a sprite's pictures at art/battle/<name>.png (<name>-1.png… for several
// frames), each HD one beside it as -hd.png, and keeps the entry's other fields.
export function placeSprite(s: Sprite, name: string, count: number, hd: boolean): Sprite {
  const stem = (i: number) => `art/battle/${name}${count > 1 ? `-${i + 1}` : ''}`;
  const frames = Array.from({ length: count }, (_, i) => `${stem(i)}.png`), hds = frames.map((_, i) => `${stem(i)}-hd.png`);
  const out: Sprite = { ...(count === 1 ? { image: frames[0] } : { frames }), origin: s.origin };
  if (s.mirror === false) out.mirror = false;
  if (s.ticks?.length === count && s.ticks.some((t) => t !== 1)) out.ticks = s.ticks;
  if (s.loop && s.loop < count) out.loop = s.loop;
  if (hd) out.hd = count === 1 ? hds[0] : hds;
  return out;
}

// A character's lines as the file has them: empty situations and empty locales left out.
export const BATTLE_QUOTES_SCHEMA = 'srw64.battle-quotes.v1';
const cleanLine = (l: Line): Line => Object.fromEntries(Object.entries(l).filter(([, v]) => typeof v === 'string' && v.trim() !== '')) as Line;
export function cleanQuotes(q?: Quotes): Quotes | undefined {
  if (!q) return undefined;
  const out: Quotes = {};
  for (const s of QUOTE_SITUATIONS) { const lines = (q[s] ?? []).map(cleanLine).filter((l) => Object.keys(l).length); if (lines.length) out[s] = lines; }
  const special = (q.special ?? []).map((c) => ({ ...pruned({ weapon: c.weapon, opponent: c.opponent, situation: c.opponent !== undefined && c.situation !== 'attack' ? c.situation : undefined,
    sometimes: c.weapon !== undefined && c.sometimes ? true : undefined }), lines: c.lines.map(cleanLine).filter((l) => Object.keys(l).length) })).filter((c) => c.lines.length);
  if (special.length) out.special = special;
  return Object.keys(out).length ? out : undefined;
}
// The voice files a model's lines name.
export const voicesOf = (model: Model) => model.pilots.flatMap((p) => [...QUOTE_SITUATIONS.flatMap((s) => p.quotes?.[s] ?? []), ...(p.quotes?.special ?? []).flatMap((c) => c.lines)])
  .map((l) => l.voice).filter((v): v is string => !!v);

// --- From files --------------------------------------------------------------------------

// A package's files read back: the model, and what it keeps as it was (the files' other rows).
export function fromFiles(files: Record<string, unknown>): { model: Model; kept: Record<string, unknown> } {
  const model = emptyModel();
  const doc = (files['data/new_units.json'] ?? {}) as Record<string, any>;
  const names = (files['dialogue/names.json'] ?? {}) as Record<string, any>;
  const rows = (kind: Kind) => ((files[DATA_FILES[kind]] ?? {}) as Record<string, Changes>);
  const quoteFile = (files['dialogue/battle_quotes.json'] ?? {}) as Record<string, any>;
  for (const [slug, e] of Object.entries((doc.pilots ?? {}) as Record<string, any>))
    model.pilots.push(pruned({ slug, copy: e.copy, voice: e.voice, portrait: e.portrait, love: e.love, combo_as: e.combo_as, quotes: quoteFile.pilots?.[`@${slug}`],
      names: names.pilots?.[`@${slug}`] ?? {}, data: rows('pilots')[`@${slug}`] ?? {} }));
  for (const [slug, e] of Object.entries((doc.units ?? {}) as Record<string, any>))
    model.units.push(pruned({ slug, copy: e.copy, weapons: e.weapons ?? [], forms: e.forms, crew: e.crew, combine: e.combine,
      inherits_from: e.inherits_from, icon: e.icon, pose: e.pose, work: e.work,
      names: names.units?.[`@${slug}`] ?? {}, models: names.models?.[`@${slug}`] ?? {}, data: rows('units')[`@${slug}`] ?? {} }));
  for (const [slug, e] of Object.entries((doc.weapons ?? {}) as Record<string, any>))
    model.weapons.push(pruned({ slug, copy: e.copy, inherits_from: e.inherits_from, combination: e.combination, animation: e.animation,
      names: names.weapons?.[`@${slug}`] ?? {}, data: rows('weapons')[`@${slug}`] ?? {} }));
  if (doc.sprites) model.sprites = doc.sprites as Record<string, Sprite>;
  if (doc.scenes) model.scenes = doc.scenes;
  const kept: Record<string, unknown> = {};
  for (const kind of KINDS) if (files[DATA_FILES[kind]]) kept[DATA_FILES[kind]] = files[DATA_FILES[kind]];
  if (files['dialogue/names.json']) kept['dialogue/names.json'] = files['dialogue/names.json'];
  if (files['dialogue/battle_quotes.json']) kept['dialogue/battle_quotes.json'] = files['dialogue/battle_quotes.json'];
  return { model, kept };
}

// --- Problems ----------------------------------------------------------------------------

// What keeps the package from loading, as codes the page words in its language. `where` is
// "pilots.kai" style; the page shows the entry's name for it.
export type Problem = { where: string; code: string; detail?: Record<string, string | number> };

const nameProblem = (v: string, longest: number) =>
  v !== v.replace(/^ +| +$/g, '') ? 'name_spaces' : /[\u0000-\u001f\u007f<>{}　]/.test(v) ? 'name_chars' : [...v].length > longest ? 'name_long' : '';

export function problems(model: Model, packages: Set<string> = new Set()): Problem[] {
  const out: Problem[] = [];
  const add = (where: string, code: string, detail?: Problem['detail']) => out.push({ where, code, ...(detail ? { detail } : {}) });
  const slugs: Record<Kind, Set<string>> = { pilots: new Set(), units: new Set(), weapons: new Set() };
  for (const kind of KINDS)
    for (const e of model[kind] as { slug: string }[]) {
      if (!SLUG.test(e.slug)) add(`${kind}.${e.slug}`, 'slug_bad');
      else if (slugs[kind].has(e.slug)) add(`${kind}.${e.slug}`, 'slug_twice');
      slugs[kind].add(e.slug);
    }
  // A reference to this package's entry must name one; another package's is the author's to give.
  const known = (kind: Kind, v: Ref) => typeof v === 'number' || !v.startsWith('@') || slugs[kind].has(v.slice(1)) || packages.has(v.split('@')[0]);
  for (const p of model.pilots) {
    const where = `pilots.${p.slug}`;
    if (!Object.values(p.names).some((v) => v?.name.trim())) add(where, 'needs_name');
    for (const [loc, v] of Object.entries(p.names)) for (const field of ['name', 'full'] as const) {
      const text = v?.[field];
      if (text && nameProblem(text, NAME_LONGEST)) add(where, nameProblem(text, NAME_LONGEST), { locale: loc, longest: NAME_LONGEST });
    }
    const q = cleanQuotes(p.quotes);
    const lines = q ? [...QUOTE_SITUATIONS.flatMap((s) => q[s] ?? []), ...(q.special ?? []).flatMap((c) => c.lines)] : [];
    for (const l of lines) {
      if (!l.ja) add(where, 'quote_ja');
      else if ([...l.ja].length > QUOTE_LONGEST) add(where, 'quote_long', { longest: QUOTE_LONGEST });
      if (Object.entries(l).some(([k, v]) => k !== 'voice' && [...(v as string)].length > 200)) add(where, 'quote_long', { longest: 200 });
    }
    for (const s of QUOTE_SITUATIONS) if ((q?.[s]?.length ?? 0) > 255) add(where, 'quote_many');
    if ((q?.special ?? []).reduce((n, c) => n + c.lines.length, 0) > QUOTE_SPECIALS) add(where, 'quote_specials', { most: QUOTE_SPECIALS });
    for (const c of q?.special ?? []) {
      if ((c.weapon === undefined) === (c.opponent === undefined)) add(where, 'quote_special');
      else if (c.weapon !== undefined && !known('weapons', c.weapon)) add(where, 'quote_weapon');
      else if (c.opponent !== undefined && !(Number.isInteger(c.opponent) && c.opponent >= 0 && c.opponent <= 359)) add(where, 'quote_opponent');
    }
    for (const l of p.love ?? []) if (!(typeof l.with === 'number' ? l.with >= 0 && l.with <= 360 : isReference(l.with) && known('pilots', l.with))) add(where, 'love_bad');
  }
  const formOwners = new Map<string, string>();
  for (const u of model.units) {
    const where = `units.${u.slug}`;
    if (!Object.values(u.names).some((v) => v?.trim())) add(where, 'needs_name');
    for (const [loc, v] of Object.entries(u.names)) if (v && nameProblem(v, NAME_LONGEST)) add(where, nameProblem(v, NAME_LONGEST), { locale: loc, longest: NAME_LONGEST });
    for (const [loc, v] of Object.entries(u.models)) if (v && nameProblem(v, MODEL_LONGEST)) add(where, nameProblem(v, MODEL_LONGEST), { locale: loc, longest: MODEL_LONGEST });
    if (u.weapons.length < 1 || u.weapons.length > 63) add(where, 'weapons_count');
    for (const w of u.weapons) if (!known('weapons', w)) add(where, 'weapon_missing', { weapon: String(w) });
    if (u.crew && (u.crew.length > 5 || u.crew.some((c) => c < 0 || c > 360))) add(where, 'crew_bad');
    if (u.icon !== undefined && !(Number.isInteger(u.icon) && u.icon >= 688 && u.icon <= 1009)) add(where, 'icon_bad');
    if (u.combine?.over?.some((o) => !Number.isInteger(o) || o < 0 || o > 362)) add(where, 'combine_over');
    if (u.combine && (u.combine.parts.length < 2 || u.combine.parts.length > 5 || u.combine.parts.some((p) => !known('units', p)))) add(where, 'combine_bad');
    if (u.forms?.length) {
      if (u.forms.length > 2 || u.forms.some((f) => !f.startsWith('@') || !slugs.units.has(f.slice(1)) || f.slice(1) === u.slug) || new Set(u.forms).size !== u.forms.length)
        add(where, 'forms_bad');
      for (const name of [u.slug, ...u.forms.map((f) => f.slice(1))]) {
        if (formOwners.has(name) && formOwners.get(name) !== u.slug) add(where, 'forms_family', { unit: name });
        formOwners.set(name, u.slug);
      }
    }
    if (u.inherits_from !== undefined && !known('units', u.inherits_from)) add(where, 'inherits_missing');
  }
  const sprites = model.sprites ?? {};
  for (const [name, s] of Object.entries(sprites)) {
    const where = `sprites.${name}`, n = spriteFiles(s).frames.length, hd = spriteFiles(s).hd.length;
    if (!SLUG.test(name)) add(where, 'slug_bad');
    if (n < 1 || n > 64) add(where, 'sprite_frames');
    if (hd && hd !== n) add(where, 'sprite_hd');
    if (!Array.isArray(s.origin) || s.origin.length !== 2 || s.origin.some((v) => !Number.isInteger(v) || v < -2048 || v > 2048)) add(where, 'sprite_origin');
    if (s.ticks?.length && (s.ticks.length !== n || s.ticks.some((t) => !Number.isInteger(t) || t < 1 || t > 255))) add(where, 'sprite_ticks');
    if (s.loop !== undefined && !(Number.isInteger(s.loop) && s.loop >= 0 && s.loop < n)) add(where, 'sprite_loop');
  }
  // A pose or scene naming this package's picture must name one it has.
  const ownPicture = (v: unknown) => typeof v !== 'string' || !v.startsWith('@') || v.slice(1) in sprites;
  for (const u of model.units) if (u.pose !== undefined && !ownPicture(u.pose)) add(`units.${u.slug}`, 'pose_missing', { sprite: String(u.pose) });
  for (const [name, v] of Object.entries(model.scenes ?? {})) if (!ownPicture(v)) add(`scenes.${name}`, 'scene_missing', { sprite: String(v) });
  for (const w of model.weapons) {
    const where = `weapons.${w.slug}`;
    if (!Object.values(w.names).some((v) => v?.trim())) add(where, 'needs_name');
    for (const [loc, v] of Object.entries(w.names)) if (v && nameProblem(v, NAME_LONGEST)) add(where, nameProblem(v, NAME_LONGEST), { locale: loc, longest: NAME_LONGEST });
    if (!known('weapons', w.copy)) add(where, 'copy_missing');
    const c = w.combination;
    if (c) {
      if (c.units.length < 2 || c.units.length > 5 || new Set(c.units.map(String)).size !== c.units.length || c.units.some((u) => !known('units', u)))
        add(where, 'combination_units');
      if (c.parts.length !== c.units.length || c.parts.some((p) => !known('weapons', p))) add(where, 'combination_parts');
      if (!(c.multiplier >= 0.5 && c.multiplier <= 5)) add(where, 'combination_multiplier');
    }
    if (!model.units.some((u) => u.weapons.includes(`@${w.slug}`))) add(where, 'weapon_unused');
  }
  return out;
}

// Renaming a picture renames the poses and scenes that name it.
export function renameSprite(model: Model, from: string, to: string) {
  if (!model.sprites?.[from] || from === to) return;
  model.sprites = Object.fromEntries(Object.entries(model.sprites).map(([k, v]) => [k === from ? to : k, v]));
  for (const u of model.units) if (u.pose === `@${from}`) u.pose = `@${to}`;
  for (const [k, v] of Object.entries(model.scenes ?? {})) if (v === `@${from}`) model.scenes![k] = `@${to}`;
}

// Renaming an entry renames every reference to it in this model.
export function rename(model: Model, kind: Kind, from: string, to: string) {
  const swap = (v: Ref) => (v === `@${from}` ? `@${to}` : v);
  for (const e of model[kind] as { slug: string }[]) if (e.slug === from) e.slug = to;
  if (kind === 'weapons') {
    for (const u of model.units) u.weapons = u.weapons.map(swap);
    for (const w of model.weapons) {
      if (w.inherits_from !== undefined) w.inherits_from = swap(w.inherits_from);
      w.copy = swap(w.copy);
      if (w.combination) w.combination.parts = w.combination.parts.map(swap);
    }
  }
  if (kind === 'units')
    for (const u of model.units) {
      if (u.forms) u.forms = u.forms.map((f) => String(swap(f)));
      if (u.combine) u.combine.parts = u.combine.parts.map(swap);
      if (u.inherits_from !== undefined) u.inherits_from = swap(u.inherits_from);
    }
  if (kind === 'units') for (const w of model.weapons) if (w.combination) w.combination.units = w.combination.units.map(swap);
  if (kind === 'pilots') for (const p of model.pilots) for (const l of p.love ?? []) l.with = swap(l.with);
}
