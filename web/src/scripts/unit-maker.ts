// The unit MOD maker (pages/[lang]/mods/units.astro, docs/design/unit-mod-web-tool.md W1): new
// characters, units and weapons copied from the original's and changed, written as
// data/new_units.json, data/*.json "@name" rows and dialogue/names.json (lib/unit-package.ts),
// with the characters' portraits, the units' battle pictures and map icons made in the browser
// (lib/pixels.ts): the pictures are the player's own files and never leave the page.
import data from '../data/data-mods.json';
import { el, fill, jsonBytes, offer, openPackage, packageForm, readJson, say, search, size, strToU8, thunderstoreZip, writePackage, zipPackage, type Text } from './mod-package';
import { GameLink, PORT } from '../lib/game-link';
import * as ts from '../lib/thunderstore';
import { picker, type Pickable } from './picker';
import { dataUrl, encodeIcon, frame, framer, loadIcon, type Picture } from './framer';
import { LOCALES, KINDS, SLUG, QUOTE_SITUATIONS, emptyModel, fromFiles, placeSprite, voicesOf, problems, rename, renameSprite, spriteFiles, toFiles, type Changes, type Locale, type Model, type Pilot, type Problem, type Ref, type Unit, type Weapon, type Line, type Quotes, type Special } from '../lib/unit-package';
import { vorbisEncoder, vorbisInfo } from './audio-tools';
import * as storyImage from '../lib/story-image';
import type { Join, Wave, WaveUnit, DeploySide } from '../lib/story-image';
import { foot, reduce } from '../lib/pixels';

type Kind = 'pilots' | 'units' | 'weapons';
type Tab = Kind | 'sprites' | 'story';
type Field = { key: string; label: string; low: number; high: number; step?: number };
type Labels = {
  fields: Record<Kind, Field[]>;
  abilities: { key: string; label: string }[];
  skills: { key: string; label: string; row: number }[];
  spirits: { key: string; label: string }[];
  sizes: string[];
  choices: Record<string, { value: string | null; label: string }[]>;
};
type Entry = Record<string, any> & { id: number; name: { ja: string; zh: string; en: string; ko?: string } };
type AnyNew = Pilot | Unit | Weapon;
// A character's own portrait: the framed picture, its HD PNG and the 96×96 one.
type Portrait = { pic: Picture | null; hd: Uint8Array; image: Uint8Array; url: string };
// A battle picture's frames: the player's pictures (decoded when the size changes), the HD PNGs
// made of them and the game's pixel frames (one palette of at most 127 colours for them all).
type Art = { sources: (ImageBitmap | null)[]; hds: Uint8Array[]; images: Uint8Array[]; urls: string[]; rgba: ImageData[]; colors: number };
type Way = 'left' | 'right' | 'front';
// A new unit's map icon (art/icons/@name.png) and the way the picture looks.
type Icon = { pic: Picture; way: Way };

const CHANNELS = ['air', 'land', 'sea', 'space'];
const MOVE_TYPES = ['land', 'air', 'sea', 'space', 'underground'];
const RANKS = ['A', 'B', 'C', 'D', '-'];
const PORTRAIT = 96, PORTRAIT_COLORS = 63, PORTRAIT_HD = 768;
// Battle pictures: 127 colours, at most 1024 a side (mod_sprites.hpp); new ones fit 128 (the
// original's poses are about 130×96).
const SPRITE_COLORS = 127, SPRITE_LARGEST = 1024, SPRITE_FIT = 128, SPRITE_VIEW = 3;
const LOCALE_LABEL: Record<Locale, string> = { ja: '日本語', 'zh-Hans': '中文', en: 'English', ko: '한국어' };
const GAME = '0.5.4';

export function initUnitMaker() {
  const root = document.querySelector<HTMLElement>('[data-unit-maker]');
  const payload = document.getElementById('unit-maker-data');
  if (!root || !payload) return;
  const { text, labels, pick, works, scenes, locale, lang } = JSON.parse(payload.textContent!) as {
    text: Text & { terrainNames: string[] }; labels: Labels; locale: string; lang: 'zh' | 'en' | 'ja' | 'ko'; scenes: { id: number; title: string }[];
    pick: { units: Pickable[]; pilots: Pickable[]; series: { id: number | null; name: string }[] }; works: { id: number; name: string }[];
  };
  const T = (key: string, values: Record<string, string | number> = {}) => fill(text[key] ?? key, values);
  const lists = data as unknown as Record<Kind, Entry[]>;
  const byId = Object.fromEntries(KINDS.map((k) => [k, new Map(lists[k].map((e) => [e.id, e]))])) as Record<Kind, Map<number, Entry>>;
  const nameOf = (e: Entry) => e.name[lang] || (lang === 'ko' ? e.name.en : '') || e.name.ja;
  const sep = lang === 'en' || lang === 'ko' ? ', ' : '、';
  const form = packageForm(root, text, locale, GAME);
  const unitWeapons = new Map<number, number[]>();
  for (const w of lists.weapons) for (const u of w.units as number[]) unitWeapons.set(u, [...(unitWeapons.get(u) ?? []), w.id]);

  let model: Model = emptyModel();
  let keptJson: Record<string, unknown> = {};        // the opened package's rows this maker does not own
  let keptFiles: Record<string, Uint8Array> = {};    // and its other files, as they were
  const portraits = new Map<string, Portrait>();     // by character slug
  const voices = new Map<string, Uint8Array>();      // battle voices by file name (audio/voice/battle/)
  const openSituations = new Set<string>();          // the quote lists shown open
  const art = new Map<string, Art>();                // by sprite name
  const icons = new Map<string, Icon>();             // by unit slug
  let keptFacing: Record<string, string> = {};       // art/icons.json ways of others' icons
  let kind: Tab = 'pilots';
  let current: string | null = null;
  let adding = false;

  const entries = (k: Kind) => model[k] as AnyNew[];
  const find = (k: Kind, slug: string | null) => entries(k).find((e) => e.slug === slug) ?? null;
  // A new entry's shown name: the page language's, else any, else its slug.
  const shownName = (e: AnyNew) => {
    const names = e.names as Record<string, string | { name: string } | undefined>;
    const pickName = (l: string) => { const v = names[l]; return typeof v === 'string' ? v : v?.name; };
    const page = lang === 'zh' ? 'zh-Hans' : lang;
    return pickName(page) || LOCALES.map(pickName).find(Boolean) || e.slug;
  };
  const refName = (k: Kind, v: Ref) => {
    if (typeof v === 'number') { const e = byId[k].get(v); return e ? `${v} ${nameOf(e)}` : String(v); }
    const own = v.startsWith('@') ? find(k, v.slice(1)) : null;
    return own ? `${v} ${shownName(own)}` : v;
  };
  const freeSlug = (k: Kind, base: string) => {
    const stem = base.toLowerCase().replace(/[^a-z0-9._-]+/g, '-').replace(/^-+|-+$/g, '') || k.slice(0, -1);
    let slug = stem.slice(0, 36), n = 2;
    while (find(k, slug)) slug = `${stem.slice(0, 36)}-${n++}`;
    return slug;
  };

  // --- Choosers: originals and this package's entries in one datalist per kind --------------
  const datalists: Record<Kind, HTMLDataListElement> = {} as any;
  for (const k of KINDS) { datalists[k] = el('datalist', { id: `unit-maker-${k}` }) as HTMLDataListElement; root.append(datalists[k]); }
  function refreshLists() {
    for (const k of KINDS)
      datalists[k].replaceChildren(...entries(k).map((e) => el('option', { value: `@${e.slug}` }, shownName(e))),
        ...lists[k].map((e) => el('option', { value: String(e.id) }, nameOf(e))));
  }
  // A number of the original's or "@name" of this package's, typed or chosen.
  function refInput(k: Kind, value: Ref | undefined, onchange: (v: Ref | undefined) => void, placeholder = '') {
    const input = el('input', { list: datalists[k].id, value: value ?? '', placeholder, autocomplete: 'off', spellcheck: 'false', class: 'ref' }) as HTMLInputElement;
    const label = el('span', { class: 'was' }, value !== undefined ? refName(k, value) : '');
    input.addEventListener('change', () => {
      const raw = input.value.trim();
      const v: Ref | undefined = raw === '' ? undefined : /^\d+$/.test(raw) ? Number(raw) : raw;
      label.textContent = v !== undefined ? refName(k, v) : '';
      onchange(v);
    });
    return el('span', { class: 'ref-field' }, input, label);
  }
  const textInput = (value: string, onchange: (v: string) => void, placeholder = '') => {
    const box = el('input', { value, placeholder, autocomplete: 'off', spellcheck: 'false' }) as HTMLInputElement;
    box.addEventListener('change', () => { box.value = box.value.trim(); onchange(box.value); });
    return box;
  };

  // --- Number fields, against the copied original (as the number maker does) ---------------
  // A copy of another new entry of the package starts from the original that one copies (the
  // host follows the chain the same way, mod_units.hpp rooted); its own changes are its own.
  function baseOf(k: Kind, e: AnyNew): Entry | undefined {
    let copy = (e as any).copy;
    for (let hop = 0; hop < 16 && typeof copy === 'string' && copy.startsWith('@'); hop++) copy = (find(k, copy.slice(1)) as any)?.copy;
    return typeof copy === 'number' ? byId[k].get(copy) : undefined;
  }
  function dataEditor(k: Kind, e: AnyNew): Node[] {
    const base = baseOf(k, e);
    if (!base) return [el('p', { class: 'hint' }, T('copyNewHint'))];
    const chained = typeof (e as any).copy === 'string' ? [el('p', { class: 'hint' }, T('copyChainHint', { from: String((e as any).copy), original: `${base.id} ${nameOf(base)}` }))] : [];
    const row = e.data as Changes;
    const original = (key: string): any => {
      if (key.startsWith('terrain.')) return base.terrain[CHANNELS.indexOf(key.slice(8))];
      if (key.startsWith('skill.')) return (base.skills?.[key.slice(6)] ?? []).join(', ');
      return base[key];
    };
    const value = (key: string): any => {
      if (key.startsWith('terrain.')) { const t = row.terrain as Record<string, string> | undefined; return t?.[key.slice(8)] ?? original(key); }
      if (key.startsWith('skill.')) { const s = row.skills as Record<string, number[] | null> | undefined; const v = s?.[key.slice(6)]; return v === undefined ? original(key) : (v ?? []).join(', '); }
      return key in row ? row[key] : original(key);
    };
    const same = (key: string, a: any, b: any) =>
      key === 'abilities' || key === 'move_types' ? [...(a ?? [])].sort().join(' ') === [...(b ?? [])].sort().join(' ') : key === 'spirits' ? JSON.stringify(a) === JSON.stringify(b) : a === b;
    const set = (key: string, v: any) => {
      if (key.startsWith('terrain.')) {
        const t = { ...((row.terrain as object) ?? {}) } as Record<string, string>;
        if (v === original(key)) delete t[key.slice(8)]; else t[key.slice(8)] = v;
        if (Object.keys(t).length) row.terrain = t; else delete row.terrain;
      } else if (key.startsWith('skill.')) {
        const s = { ...((row.skills as object) ?? {}) } as Record<string, number[] | null>;
        const levels = String(v).split(/[,，、\s]+/).filter(Boolean).map(Number);
        if (String(v) === original(key)) delete s[key.slice(6)]; else s[key.slice(6)] = levels.length ? levels : null;
        if (Object.keys(s).length) row.skills = s; else delete row.skills;
      } else if (same(key, v, original(key))) delete row[key];
      else row[key] = v;
      changed(false);
    };
    const mark = (wrap: HTMLElement, key: string) => wrap.classList.toggle('changed', !same(key, value(key), original(key)));
    const number = (f: Field) => {
      const input = el('input', { type: 'number', min: f.low, max: f.high, step: f.step ?? 1, value: value(f.key) }) as HTMLInputElement;
      const wrap = el('label', { class: 'field' }, f.label, input, el('span', { class: 'was' }, `${text.original} ${original(f.key)}`));
      input.addEventListener('change', () => { const v = input.value.trim() === '' ? original(f.key) : Math.round(Number(input.value)); set(f.key, Number.isFinite(v) ? v : original(f.key)); mark(wrap, f.key); });
      mark(wrap, f.key);
      return wrap;
    };
    const select = (label: string, key: string, options: { value: string | null; label: string }[]) => {
      const s = el('select', {}, ...options.map((o) => el('option', { value: o.value ?? '' }, o.label))) as HTMLSelectElement;
      s.value = value(key) ?? '';
      const was = options.find((o) => o.value === (original(key) ?? null))?.label ?? String(original(key) ?? '—');
      const wrap = el('label', { class: 'field' }, label, s, el('span', { class: 'was' }, `${text.original} ${was}`));
      s.addEventListener('change', () => { set(key, s.value === '' ? null : s.value); mark(wrap, key); });
      mark(wrap, key);
      return wrap;
    };
    const check = (label: string, key: string) => {
      const input = el('input', { type: 'checkbox', checked: !!value(key) }) as HTMLInputElement;
      const wrap = el('label', { class: 'check' }, input, label);
      input.addEventListener('change', () => { set(key, input.checked); mark(wrap, key); });
      mark(wrap, key);
      return wrap;
    };
    const plain = (options: string[]) => options.map((o) => ({ value: o, label: o }));
    const parts: Node[] = [...chained];
    const fields = [...labels.fields[k], ...(k !== 'weapons' ? [{ key: 'swap_class', label: T('swapClass'), low: 0, high: 8 }] : [])];
    parts.push(el('div', { class: 'grid' }, ...fields.map(number), k === 'units' ? select(text.size, 'size', plain(labels.sizes)) : null));
    parts.push(el('div', { class: 'sub' }, text.terrain), el('div', { class: 'grid' }, ...CHANNELS.map((c, n) => select(text.terrainNames[n], `terrain.${c}`, plain(RANKS)))));
    if (k === 'weapons') parts.push(el('div', { class: 'sub' }, text.weaponKind),
      el('div', { class: 'grid' }, select(text.attack, 'attack', labels.choices.attack), select(text.terrainAt, 'terrain_at', labels.choices.terrain_at), select(text.requires, 'requires', labels.choices.requires)),
      el('div', { class: 'checks' }, check(text.postMove, 'post_move'), check(text.beam, 'beam'), check(text.parryable, 'parryable'), check(text.locked, 'locked')));
    if (k === 'units') {
      parts.push(el('div', { class: 'sub' }, text.equipment), el('div', { class: 'checks' }, check(text.shield, 'shield'), check(text.sword, 'sword')));
      // Where it can move (units.json move_types): at least one.
      const moves: string[] = value('move_types') ?? [];
      parts.push(el('div', { class: 'sub' }, T('moveTypes')), el('div', { class: 'checks' }, ...MOVE_TYPES.map((m) => {
        const input = el('input', { type: 'checkbox', checked: moves.includes(m) }) as HTMLInputElement;
        const wrap = el('label', { class: `check${input.checked !== (base.move_types ?? []).includes(m) ? ' changed' : ''}` }, input, T(`move_${m}`));
        input.addEventListener('change', () => {
          const now = MOVE_TYPES.filter((x) => (x === m ? input.checked : (value('move_types') ?? []).includes(x)));
          if (!now.length) { input.checked = true; return; }
          set('move_types', now); render();
        });
        return wrap;
      })));
      const have = new Set<string>(value('abilities'));
      parts.push(el('div', { class: 'sub' }, text.abilities), el('div', { class: 'checks' }, ...labels.abilities.map((a) => {
        const input = el('input', { type: 'checkbox', checked: have.has(a.key) }) as HTMLInputElement;
        const wrap = el('label', { class: `check${input.checked !== base.abilities.includes(a.key) ? ' changed' : ''}` }, input, a.label);
        input.addEventListener('change', () => {
          const now = new Set<string>(value('abilities'));
          if (input.checked) now.add(a.key); else now.delete(a.key);
          if (input.checked && a.key.startsWith('hp_regen_')) now.delete(a.key === 'hp_regen_10' ? 'hp_regen_20' : 'hp_regen_10');
          set('abilities', [...now]); render();
        });
        return wrap;
      })), el('p', { class: 'hint' }, text.abilityRules));
    }
    if (k === 'pilots') {
      parts.push(el('div', { class: 'sub' }, text.skills), el('p', { class: 'hint' }, text.skillRow),
        el('div', { class: 'grid' }, ...labels.skills.map((s) => {
          const key = `skill.${s.key}`;
          const input = el('input', { value: value(key), placeholder: '1, 5, 10', autocomplete: 'off' }) as HTMLInputElement;
          const wrap = el('label', { class: 'field' }, s.label, input, el('span', { class: 'was' }, `${text.original} ${original(key) || '—'}`));
          input.addEventListener('change', () => { const v = input.value.split(/[,，、\s]+/).filter(Boolean).join(', '); input.value = v; set(key, v); mark(wrap, key); });
          mark(wrap, key);
          return wrap;
        })));
      const rows = el('div', { class: 'grid wide' });
      const list: [string, number][] = value('spirits');
      rows.replaceChildren(...Array.from({ length: 6 }, (_, n) => {
        const [key, level] = list[n] ?? ['', 0];
        const s = el('select', {}, el('option', { value: '' }, '—'), ...labels.spirits.map((x) => el('option', { value: x.key }, x.label))) as HTMLSelectElement;
        s.value = key;
        const lv = el('input', { type: 'number', min: 1, max: 99, value: key ? level : '', placeholder: text.level }) as HTMLInputElement;
        const commit = () => {
          const next: [string, number][] = [];
          for (const r of rows.querySelectorAll('.spirit-row')) {
            const sk = r.querySelector('select')!.value, l = Number(r.querySelector('input')!.value);
            if (sk) next.push([sk, Number.isFinite(l) && l > 0 ? Math.round(l) : 1]);
          }
          set('spirits', next);
        };
        s.addEventListener('change', commit); lv.addEventListener('change', commit);
        return el('div', { class: 'spirit-row' }, s, lv);
      }));
      parts.push(el('div', { class: 'sub' }, text.spirits), rows);
    }
    return parts;
  }

  // --- Portraits ----------------------------------------------------------------------------
  async function png(canvas: HTMLCanvasElement) {
    const blob: Blob = await new Promise((resolve, reject) => canvas.toBlob((b) => b ? resolve(b) : reject(new Error('png')), 'image/png'));
    return new Uint8Array(await blob.arrayBuffer());
  }
  // The framed square: an HD PNG of a multiple of 96 (up to 768) and the game's 96×96 of at most 63 colours.
  async function encodePortrait(slug: string, pic: Picture) {
    const { side, x, y } = frame(pic);
    const size = Math.min(PORTRAIT_HD, Math.max(PORTRAIT, Math.floor(side / PORTRAIT) * PORTRAIT));
    const big = document.createElement('canvas');
    big.width = big.height = size;
    const g = big.getContext('2d')!;
    g.imageSmoothingQuality = 'high';
    g.drawImage(pic.source, x, y, side, side, 0, 0, size, size);
    const small = document.createElement('canvas');
    small.width = small.height = PORTRAIT;
    const s = small.getContext('2d')!;
    s.imageSmoothingQuality = 'high';
    s.drawImage(big, 0, 0, PORTRAIT, PORTRAIT);
    const pixels = s.getImageData(0, 0, PORTRAIT, PORTRAIT);
    reduce(pixels.data, PORTRAIT_COLORS);
    s.putImageData(pixels, 0, 0);
    const image = await png(small);
    portraits.set(slug, { pic, hd: await png(big), image, url: dataUrl(image) });
  }
  function portraitEditor(p: Pilot): Node {
    const own = portraits.get(p.slug);
    const file = el('input', { type: 'file', accept: 'image/*', hidden: true }) as HTMLInputElement;
    file.addEventListener('change', async () => {
      const f = file.files?.[0];
      if (!f) return;
      const source = await createImageBitmap(f);
      portraits.set(p.slug, { pic: { source, cx: source.width / 2, cy: source.height / 2, zoom: 1, png: new Uint8Array(), size: 0, url: '', framing: true }, hd: new Uint8Array(), image: new Uint8Array(), url: '' });
      changed();
    });
    const choose = el('label', { class: 'mini' }, own ? text.otherPicture : text.choosePicture, file);
    const copied = pick.pilots.find((x) => x.id === p.copy)?.img;
    if (own?.pic?.framing) return el('div', {}, framer(own.pic, () => changed(), text, (pic) => encodePortrait(p.slug, pic)), choose);
    return el('div', {},
      el('div', { class: 'faces' }, copied ? el('img', { src: copied, alt: '' }) : el('span', { class: 'hint' }, '—'), el('span', { class: 'arrow' }, '→'),
        own?.url ? el('img', { src: own.url, alt: '', class: 'pixel-art', width: 96, height: 96 }) : el('span', { class: 'hint' }, T('portraitCopied'))),
      el('p', { class: 'hint' }, T('portraitHint')),
      el('div', {}, choose, own ? ' ' : null, own ? el('button', { type: 'button', class: 'mini', onclick: () => { portraits.delete(p.slug); changed(); } }, text.remove) : null));
  }

  // --- Battle pictures --------------------------------------------------------------------------
  const sprites = () => (model.sprites ??= {});
  const story = () => ((model as Model & { story?: { joins: Join[]; waves: Wave[] } }).story ??= { joins: [], waves: [] });
  const freeSprite = (base: string) => {
    const stem = base.slice(0, 36);
    let name = stem, n = 2;
    while (name in sprites()) name = `${stem}-${n++}`;
    return name;
  };
  const bitmap = (png: Uint8Array) => createImageBitmap(new Blob([png as BlobPart], { type: 'image/png' }));
  // A picture drawn at width × height, halving first so a large one does not alias.
  function scaled(source: CanvasImageSource & { width: number; height: number }, width: number, height: number) {
    let from: CanvasImageSource = source, w = source.width, h = source.height;
    while (w / 2 >= width * 1.0001 && h / 2 >= height * 1.0001) {
      const half = document.createElement('canvas');
      half.width = Math.max(1, Math.round(w / 2)); half.height = Math.max(1, Math.round(h / 2));
      const g = half.getContext('2d')!;
      g.imageSmoothingQuality = 'high';
      g.drawImage(from, 0, 0, half.width, half.height);
      from = half; w = half.width; h = half.height;
    }
    const out = document.createElement('canvas');
    out.width = width; out.height = height;
    const g = out.getContext('2d')!;
    g.imageSmoothingEnabled = width < w;
    g.imageSmoothingQuality = 'high';
    g.drawImage(from, 0, 0, width, height);
    return out;
  }
  const imageData = (bytes: Uint8Array) => bitmap(bytes).then((b) => { const c = document.createElement('canvas'); c.width = b.width; c.height = b.height; const g = c.getContext('2d')!; g.drawImage(b, 0, 0); return g.getImageData(0, 0, b.width, b.height); });
  // The pixel frames at `width` for the first (the others scaled alike), with one palette.
  async function makeFrames(a: Art, width: number) {
    for (let i = 0; i < a.sources.length; i++) a.sources[i] ??= await bitmap(a.hds[i] ?? a.images[i]);
    const scale = width / a.sources[0]!.width;
    const frames = a.sources.map((s) => {
      const c = scaled(s!, Math.min(SPRITE_LARGEST, Math.max(1, Math.round(s!.width * scale))), Math.min(SPRITE_LARGEST, Math.max(1, Math.round(s!.height * scale))));
      return c.getContext('2d')!.getImageData(0, 0, c.width, c.height);
    });
    const all = new Uint8ClampedArray(frames.reduce((n, f) => n + f.data.length, 0));
    let at = 0;
    for (const f of frames) { all.set(f.data, at); at += f.data.length; }
    a.colors = reduce(all, SPRITE_COLORS);
    at = 0;
    a.images = []; a.urls = []; a.rgba = [];
    for (const f of frames) {
      const data = new ImageData(all.slice(at, at + f.data.length), f.width, f.height); at += f.data.length;
      const c = document.createElement('canvas'); c.width = f.width; c.height = f.height; c.getContext('2d')!.putImageData(data, 0, 0);
      const image = await png(c);
      a.images.push(image); a.urls.push(dataUrl(image)); a.rgba.push(data);
    }
  }
  // The player's files as frames, each kept as PNG bytes (a PNG as it is).
  async function readFrames(files: File[]) {
    const sources: ImageBitmap[] = [], bytes: Uint8Array[] = [];
    for (const f of files) {
      const b = await createImageBitmap(f);
      sources.push(b);
      if (f.type === 'image/png') bytes.push(new Uint8Array(await f.arrayBuffer()));
      else { const c = document.createElement('canvas'); c.width = b.width; c.height = b.height; c.getContext('2d')!.drawImage(b, 0, 0); bytes.push(await png(c)); }
    }
    return { sources, bytes };
  }
  // A new picture from the player's files: they are its HD frames unless already about the
  // game's size, and the pixel frames fit 128.
  async function addSprite(base: string, files: File[], mirror: boolean): Promise<string> {
    const { sources, bytes } = await readFrames(files);
    const first = sources[0];
    const width = Math.round(first.width * Math.min(1, SPRITE_FIT / first.width, SPRITE_FIT / first.height));
    const small = width >= first.width;
    const a: Art = { sources, hds: small ? [] : bytes, images: small ? bytes : [], urls: [], rgba: [], colors: 0 };
    await makeFrames(a, width);
    const name = freeSprite(base);
    const r = a.rgba[0];
    sprites()[name] = { origin: mirror ? foot(r.data, r.width, r.height) : [Math.floor(r.width / 2), Math.floor(r.height / 2)], ...(mirror ? {} : { mirror: false }) };
    art.set(name, a);
    place(name);
    return name;
  }
  // The entry's picture paths for its frames now (written at export the same way).
  const place = (name: string) => {
    const a = art.get(name), s = sprites()[name];
    if (a && s) sprites()[name] = placeSprite(s, name, a.images.length, a.hds.length === a.images.length && a.hds.length > 0);
  };
  const usersOf = (name: string) => [
    ...model.units.filter((u) => u.pose === `@${name}`).map((u) => T('poseOf', { unit: shownName(u) })),
    ...Object.entries(model.scenes ?? {}).filter(([, v]) => v === `@${name}`).map(([k]) => T('sceneOf', { scene: k })),
  ];
  const fileButton = (label: string, multiple: boolean, take: (files: File[]) => Promise<void>) => {
    const input = el('input', { type: 'file', accept: 'image/*', multiple, hidden: true }) as HTMLInputElement;
    input.addEventListener('change', async () => {
      const files = [...(input.files ?? [])];
      input.value = '';
      if (!files.length) return;
      try { await take(files); } catch (error) { say(statusLine, T('errPicture', { detail: String((error as Error).message ?? error) }), true); }
    });
    return el('label', { class: 'mini' }, label, input);
  };
  // One picture: the frame shown at 3× with its origin; a click there moves the origin.
  function spriteCard(name: string, inUnit = false): Node {
    const s = sprites()[name], a = art.get(name);
    if (!s || !a) return el('p', { class: 'warn' }, T('spriteNoArt'));
    const count = a.images.length;
    let shown = 0;
    const view = el('canvas', { class: 'sprite-view pixel-art' }) as HTMLCanvasElement;
    const draw = () => {
      const r = a.rgba[shown];
      view.width = r.width * SPRITE_VIEW; view.height = r.height * SPRITE_VIEW;
      const g = view.getContext('2d')!;
      const c = document.createElement('canvas'); c.width = r.width; c.height = r.height; c.getContext('2d')!.putImageData(r, 0, 0);
      g.imageSmoothingEnabled = false;
      g.drawImage(c, 0, 0, view.width, view.height);
      const [ox, oy] = s.origin;
      g.strokeStyle = '#e8b04a'; g.lineWidth = 1;
      g.beginPath();
      g.moveTo(0, (oy + 0.5) * SPRITE_VIEW); g.lineTo(view.width, (oy + 0.5) * SPRITE_VIEW);
      g.moveTo((ox + 0.5) * SPRITE_VIEW, 0); g.lineTo((ox + 0.5) * SPRITE_VIEW, view.height);
      g.stroke();
    };
    const ox = el('input', { type: 'number', min: -2048, max: 2048, value: s.origin[0] }) as HTMLInputElement;
    const oy = el('input', { type: 'number', min: -2048, max: 2048, value: s.origin[1] }) as HTMLInputElement;
    const setOrigin = (x: number, y: number) => { s.origin = [x, y]; ox.value = String(x); oy.value = String(y); draw(); changed(false); };
    view.addEventListener('click', (e) => {
      const box = view.getBoundingClientRect();
      setOrigin(Math.floor(((e.clientX - box.left) / box.width) * a.rgba[shown].width), Math.floor(((e.clientY - box.top) / box.height) * a.rgba[shown].height));
    });
    for (const box of [ox, oy]) box.addEventListener('change', () => setOrigin(Math.round(Number(ox.value) || 0), Math.round(Number(oy.value) || 0)));
    draw();
    const width = el('input', { type: 'number', min: 8, max: SPRITE_LARGEST, value: a.rgba[0].width }) as HTMLInputElement;
    width.addEventListener('change', async () => {
      const w = Math.min(SPRITE_LARGEST, Math.max(8, Math.round(Number(width.value) || a.rgba[0].width)));
      const was = a.rgba[0].width;
      await makeFrames(a, w);
      s.origin = [Math.round((s.origin[0] * w) / was), Math.round((s.origin[1] * w) / was)];
      changed();
    });
    const mirror = el('input', { type: 'checkbox', checked: s.mirror !== false }) as HTMLInputElement;
    mirror.addEventListener('change', () => { if (mirror.checked) delete s.mirror; else s.mirror = false; changed(false); });
    const frames: HTMLElement = el('div', { class: 'sprite-frames' }, ...a.urls.map((url, i) => {
      const tick = el('input', { type: 'number', min: 1, max: 255, value: s.ticks?.[i] ?? 1, title: T('ticks') }) as HTMLInputElement;
      tick.addEventListener('change', () => {
        const ticks = Array.from({ length: count }, (_, k) => s.ticks?.[k] ?? 1);
        ticks[i] = Math.min(255, Math.max(1, Math.round(Number(tick.value) || 1)));
        s.ticks = ticks; changed(false);
      });
      return el('figure', { class: i === shown ? 'on' : '' },
        el('img', { src: url, alt: '', class: 'pixel-art', onclick: () => { shown = i; [...frames.children].forEach((f, k) => f.classList.toggle('on', k === i)); draw(); } }),
        count > 1 ? el('figcaption', {}, `${i + 1} · `, tick, ` ${T('tickUnit')} `,
          el('button', { type: 'button', class: 'mini', title: text.remove, onclick: () => {
            for (const list of [a.sources, a.hds, a.images, a.urls, a.rgba] as unknown[][]) if (list.length > i) list.splice(i, 1);
            if (s.ticks) s.ticks = s.ticks.filter((_, k) => k !== i);
            if ((s.loop ?? 0) >= a.images.length) delete s.loop;
            place(name); changed();
          } }, '×')) : null);
    }));
    const loop = count > 1 ? (() => {
      const l = el('select', {}, ...a.urls.map((_, i) => el('option', { value: String(i) }, String(i + 1)))) as HTMLSelectElement;
      l.value = String(s.loop ?? 0);
      l.addEventListener('change', () => { const v = Number(l.value); if (v) s.loop = v; else delete s.loop; changed(false); });
      return el('label', { class: 'field' }, T('loop'), l, el('span', { class: 'was' }, T('loopHint')));
    })() : null;
    const addFrames = fileButton(T('addFrames'), true, async (files) => {
      const more = await readFrames(files);
      const hasHd = a.hds.length === a.images.length && a.hds.length > 0;
      for (let i = 0; i < a.sources.length; i++) a.sources[i] ??= await bitmap(a.hds[i] ?? a.images[i]);
      a.sources.push(...more.sources);
      if (hasHd) a.hds.push(...more.bytes);
      await makeFrames(a, a.rgba[0].width);
      if (s.ticks?.length) s.ticks = [...s.ticks, ...more.sources.map(() => 1)];
      place(name); changed();
    });
    const used = usersOf(name);
    const hd = a.hds.length === count && count > 0;
    return el('div', { class: inUnit ? 'sprite-card' : '' },
      inUnit ? null : el('label', { class: 'field' }, T('spriteName'), textInput(name, (v) => {
        if (!SLUG.test(v) || (v !== name && v in sprites())) { say(statusLine, T('slugBad'), true); render(); return; }
        if (v !== name) { art.set(v, art.get(name)!); art.delete(name); }
        renameSprite(model, name, v); current = v; changed();
      }), el('span', { class: 'was' }, used.length ? used.join(sep) : T('spriteUnused'))),
      el('div', { class: 'sprite-box' }, view),
      el('p', { class: 'hint' }, T('originClick')),
      el('p', { class: 'hint' }, `${a.rgba[0].width} × ${a.rgba[0].height} · ${T('colorCount', { n: a.colors || '—' })} · ${T(hd ? 'hdKept' : 'hdNone')}`),
      el('div', { class: 'grid' },
        el('label', { class: 'field' }, T('pixelWidth'), width, el('span', { class: 'was' }, T('pixelWidthHint'))),
        el('label', { class: 'field' }, T('originX'), ox, el('span', { class: 'was' }, T('originHint'))),
        el('label', { class: 'field' }, T('originY'), oy)),
      el('label', { class: 'check' }, mirror, T('mirror')),
      el('div', { class: 'sub' }, T('frames')), el('p', { class: 'hint' }, T('framesHint')), frames, loop, el('div', {}, addFrames));
  }
  // The unit's battle picture and map icon.
  function picturesEditor(u: Unit): Node[] {
    const copied = pick.units.find((x) => x.id === u.copy)?.img;
    const own = typeof u.pose === 'string' && u.pose.startsWith('@') && u.pose.slice(1) in sprites() ? u.pose.slice(1) : null;
    const pose = el('select', {}, el('option', { value: '' }, T('poseCopy')),
      Array.isArray(u.pose) ? el('option', { value: 'kept' }, `${T('poseResources')} ${(u.pose as number[]).join(', ')}`) : null,
      ...Object.keys(sprites()).map((n) => el('option', { value: `@${n}` }, `@${n}`))) as HTMLSelectElement;
    pose.value = u.pose === undefined ? '' : Array.isArray(u.pose) ? 'kept' : String(u.pose);
    pose.addEventListener('change', () => { if (pose.value === '') delete u.pose; else if (pose.value !== 'kept') u.pose = pose.value; changed(); });
    const make = fileButton(T(own ? 'poseAnother' : 'poseMake'), true, async (files) => {
      const name = await addSprite(`${u.slug}-stand`, files, true);
      u.pose = `@${name}`; changed();
    });
    const icon = icons.get(u.slug);
    const chooseIcon = fileButton(icon ? text.otherPicture : T('iconMake'), false, async ([f]) => {
      icons.set(u.slug, { pic: await loadIcon(f), way: icon?.way ?? 'left' }); changed();
    });
    let iconPart: Node[];
    if (icon?.pic.framing) iconPart = [framer(icon.pic, () => changed(), text, encodeIcon, false, T('iconFrameHint')), el('div', {}, chooseIcon)];
    else if (icon) {
      const way = el('select', {}, ...(['left', 'right', 'front'] as Way[]).map((w) => el('option', { value: w, selected: w === icon.way }, T(`way_${w}`)))) as HTMLSelectElement;
      way.addEventListener('change', () => { icon.way = way.value as Way; changed(); });
      // The player's side looks left, the enemy's right (unit_icon_art.hpp face).
      const side = (left: boolean) => el('img', { src: icon.pic.url, alt: '', class: 'pixel-art icon-look', style: icon.way !== 'front' && (icon.way === 'left') !== left ? 'transform:scaleX(-1)' : '' });
      iconPart = [el('div', { class: 'faces' }, el('figure', {}, side(true), el('figcaption', {}, T('sidePlayer'))), el('figure', {}, side(false), el('figcaption', {}, T('sideEnemy')))),
        el('label', { class: 'field' }, T('iconWay'), way, el('span', { class: 'was' }, T('iconWayHint'))),
        el('div', {}, el('button', { type: 'button', class: 'mini', onclick: () => { icon.pic.framing = true; changed(); } }, T('reframe')), ' ', chooseIcon, ' ',
          el('button', { type: 'button', class: 'mini', onclick: () => { icons.delete(u.slug); changed(); } }, text.remove))];
    } else iconPart = [el('p', { class: 'hint' }, T('iconCopied')), el('div', {}, chooseIcon)];
    // The original pictures draw a ROM icon (native_icon.cpp draws a package's own only in HD).
    const copiedIcon = byId.units.get(baseOf('units', u)?.id ?? -1)?.icon as number | undefined;
    const iconNumber = el('input', { type: 'number', min: 688, max: 1009, value: u.icon ?? '', placeholder: copiedIcon !== undefined ? String(copiedIcon) : '' }) as HTMLInputElement;
    const shownIcon = u.icon ?? copiedIcon;
    iconNumber.addEventListener('change', () => {
      const v = iconNumber.value.trim() === '' ? undefined : Math.round(Number(iconNumber.value));
      u.icon = v === undefined || v === copiedIcon ? undefined : v; changed();
    });
    iconPart.push(el('div', { class: 'faces' },
      el('label', { class: 'field' }, T('iconNumber'), iconNumber, el('span', { class: 'was' }, T('iconNumberHint'))),
      shownIcon !== undefined ? el('img', { src: `/gen/icons/${shownIcon}.png`, alt: '', class: 'pixel-art icon-look' }) : null));
    return [
      el('div', { class: 'sub' }, T('pose')), el('p', { class: 'hint' }, T('poseHint')),
      el('div', { class: 'faces' }, copied ? el('img', { src: copied, alt: '' }) : el('span', { class: 'hint' }, '—'), el('span', { class: 'arrow' }, '→'), pose),
      ...(own ? [spriteCard(own, true)] : []), el('div', {}, make),
      el('div', { class: 'sub' }, T('mapIcon')), ...iconPart,
    ];
  }

  // --- Battle quotes (dialogue/battle_quotes.json) ------------------------------------------------
  // A voice: an Ogg as it is, anything else the browser decodes is encoded (as the voice maker).
  let encoder: ReturnType<typeof vorbisEncoder> | null = null;
  let audio: AudioContext | null = null;
  let playing: AudioBufferSourceNode | null = null;
  async function toOgg(file: File): Promise<Uint8Array> {
    const bytes = new Uint8Array(await file.arrayBuffer());
    if (vorbisInfo(bytes.subarray(0, 65536))) return bytes;
    audio ??= new AudioContext();
    const buffer = await audio.decodeAudioData(bytes.slice().buffer);
    encoder ??= vorbisEncoder();
    return encoder(buffer, 4);
  }
  async function play(name: string) {
    const ogg = voices.get(name);
    if (!ogg) return;
    audio ??= new AudioContext();
    playing?.stop();
    const source = audio.createBufferSource();
    source.buffer = await audio.decodeAudioData(ogg.slice().buffer);
    source.connect(audio.destination); source.start();
    playing = source;
  }
  const freeVoice = (slug: string) => { let n = 1; while (voices.has(`${slug}-${n}.ogg`) || voicesOf(model).includes(`${slug}-${n}.ogg`)) n++; return `${slug}-${n}.ogg`; };
  const QUOTE_LOCALES = ['ja', 'zh-Hans', 'en', 'ko'] as const;
  function lineRow(p: Pilot, line: Line, list: Line[], i: number): Node {
    const cells = QUOTE_LOCALES.map((loc) => textInput(line[loc] ?? '', (v) => { if (v) line[loc] = v; else delete line[loc]; changed(false); }, LOCALE_LABEL[loc]));
    const voiceCell: Node[] = [];
    const pick = fileButton(line.voice ? '♪' : T('voicePick'), false, async ([f]) => {
      say(statusLine, T('voiceEncoding'));
      const ogg = await toOgg(f);
      const name = line.voice ?? freeVoice(p.slug);
      voices.set(name, ogg); line.voice = name;
      say(statusLine, ''); changed();
    });
    voiceCell.push(pick);
    if (line.voice) voiceCell.push(el('button', { type: 'button', class: 'mini', title: line.voice, onclick: () => play(line.voice!) }, '▶'),
      el('button', { type: 'button', class: 'mini', onclick: () => { voices.delete(line.voice!); delete line.voice; changed(); } }, T('voiceRemove')));
    return el('div', { class: 'quote-row' }, ...cells, ...voiceCell,
      el('button', { type: 'button', class: 'mini', onclick: () => { list.splice(i, 1); changed(); } }, text.remove));
  }
  function linesEditor(p: Pilot, list: Line[]): Node[] {
    return [...list.map((l, i) => lineRow(p, l, list, i)), el('button', { type: 'button', class: 'mini', onclick: () => { list.push({}); changed(); } }, T('quoteAdd'))];
  }
  function quotesEditor(p: Pilot): Node {
    const q: Quotes = (p.quotes ??= {});
    const situations = QUOTE_SITUATIONS.map((s) => {
      const list = (q[s] ??= []);
      const key = `${p.slug}:${s}`;
      const box = el('details', { open: openSituations.has(key) }, el('summary', {}, `${T(`sit_${s}`)} (${list.filter((l) => l.ja).length})`), ...linesEditor(p, list)) as HTMLDetailsElement;
      box.addEventListener('toggle', () => { if (box.open) openSituations.add(key); else openSituations.delete(key); });
      return box;
    });
    q.special ??= [];
    const specials = q.special.map((c: Special, n: number) => {
      const kindSelect = el('select', {}, el('option', { value: 'weapon' }, T('specialWeapon')), el('option', { value: 'opponent' }, T('specialOpponent'))) as HTMLSelectElement;
      kindSelect.value = c.opponent !== undefined ? 'opponent' : 'weapon';
      kindSelect.addEventListener('change', () => {
        if (kindSelect.value === 'weapon') { delete c.opponent; delete c.situation; c.weapon = c.weapon ?? 0; } else { delete c.weapon; delete c.sometimes; c.opponent = c.opponent ?? 0; }
        changed();
      });
      const head: Node[] = [kindSelect];
      if (c.opponent === undefined) {
        head.push(refInput('weapons', c.weapon, (v) => { c.weapon = v ?? 0; changed(false); }, T('specialWeaponHint')));
        const s = el('input', { type: 'checkbox', checked: !!c.sometimes }) as HTMLInputElement;
        s.addEventListener('change', () => { c.sometimes = s.checked || undefined; changed(false); });
        head.push(el('label', { class: 'check' }, s, T('specialSometimes')));
      } else {
        const who = el('select', {}, ...lists.pilots.filter((x) => x.id <= 359).map((x) => el('option', { value: String(x.id) }, `${x.id} ${nameOf(x)}`))) as HTMLSelectElement;
        who.value = String(c.opponent);
        who.addEventListener('change', () => { c.opponent = Number(who.value); changed(false); });
        const when = el('select', {}, ...QUOTE_SITUATIONS.map((s) => el('option', { value: s }, T(`sit_${s}`)))) as HTMLSelectElement;
        when.value = c.situation ?? 'attack';
        when.addEventListener('change', () => { c.situation = when.value as Special['situation']; changed(false); });
        head.push(who, when);
      }
      head.push(el('button', { type: 'button', class: 'mini', onclick: () => { q.special!.splice(n, 1); changed(); } }, text.remove));
      return el('div', { class: 'special' }, el('div', { class: 'list-row' }, ...head), ...linesEditor(p, c.lines));
    });
    return el('div', {}, el('div', { class: 'sub' }, T('quotes')), el('p', { class: 'hint' }, T('quotesHint')), ...situations,
      el('div', { class: 'sub' }, T('specials')), el('p', { class: 'hint' }, T('specialsHint')), ...specials,
      el('button', { type: 'button', class: 'mini', onclick: () => { q.special!.push({ weapon: 0, lines: [{}] }); changed(); } }, T('specialAdd')));
  }

  // --- The entry editors ----------------------------------------------------------------------
  function namesEditor(e: AnyNew, k: Kind): Node {
    const rows = LOCALES.map((loc) => {
      if (k === 'pilots') {
        const n = (e as Pilot).names[loc] ?? { name: '' };
        return el('div', { class: 'name-row' }, el('span', { class: 'loc' }, LOCALE_LABEL[loc]),
          textInput(n.name, (v) => { (e as Pilot).names[loc] = { ...((e as Pilot).names[loc] ?? { name: '' }), name: v }; changed(); }, T('shortName')),
          textInput(n.full ?? '', (v) => { (e as Pilot).names[loc] = { ...((e as Pilot).names[loc] ?? { name: '' }), full: v }; changed(false); }, T('fullName')));
      }
      const names = (e as Unit | Weapon).names;
      const cells: Node[] = [el('span', { class: 'loc' }, LOCALE_LABEL[loc]), textInput(names[loc] ?? '', (v) => { names[loc] = v; changed(); }, T('name'))];
      if (k === 'units') cells.push(textInput((e as Unit).models[loc] ?? '', (v) => { (e as Unit).models[loc] = v; changed(false); }, T('model')));
      return el('div', { class: 'name-row' }, ...cells);
    });
    return el('div', {}, el('div', { class: 'sub' }, T('names')), el('p', { class: 'hint' }, T(k === 'pilots' ? 'namesPilotHint' : 'namesHint')), ...rows);
  }
  function slugEditor(e: AnyNew, k: Kind): Node {
    const box = textInput(e.slug, (v) => {
      if (!SLUG.test(v) || (v !== e.slug && find(k, v))) { box.value = e.slug; say(statusLine, T('slugBad'), true); return; }
      if (k === 'pilots' && portraits.has(e.slug)) { portraits.set(v, portraits.get(e.slug)!); portraits.delete(e.slug); }
      if (k === 'units' && icons.has(e.slug)) { icons.set(v, icons.get(e.slug)!); icons.delete(e.slug); }
      rename(model, k, e.slug, v);
      if (k !== 'weapons') {
        const swap = (r: Ref) => (r === `@${e.slug}` ? `@${v}` : r);
        for (const j of story().joins) { if (k === 'pilots') j.pilot = swap(j.pilot); else j.unit = swap(j.unit); }
        for (const w of story().waves) for (const u of w.units) { if (k === 'pilots') u.pilot = swap(u.pilot); else u.unit = swap(u.unit); }
      }
      current = v; changed();
    });
    return el('label', { class: 'field' }, T('slug'), box, el('span', { class: 'was' }, T('slugHint')));
  }
  function listEditor<V>(title: string, hint: string, values: V[], row: (v: V, i: number) => Node, add: () => void, most: number): Node {
    return el('div', {}, el('div', { class: 'sub' }, title), hint ? el('p', { class: 'hint' }, hint) : null,
      ...values.map((v, i) => el('div', { class: 'list-row' }, row(v, i),
        el('button', { type: 'button', class: 'mini', title: '↑', disabled: i === 0, onclick: () => { [values[i - 1], values[i]] = [values[i], values[i - 1]]; changed(); } }, '↑'),
        el('button', { type: 'button', class: 'mini', onclick: () => { values.splice(i, 1); changed(); } }, text.remove))),
      values.length < most ? el('button', { type: 'button', class: 'mini', onclick: add }, T('add')) : null);
  }

  function pilotEditor(p: Pilot): Node[] {
    const pilotsSelect = (value: number | undefined, onchange: (v: number | undefined) => void, noneLabel: string) => {
      const s = el('select', {}, el('option', { value: '' }, noneLabel), ...lists.pilots.map((x) => el('option', { value: String(x.id) }, `${x.id} ${nameOf(x)}`))) as HTMLSelectElement;
      s.value = value === undefined ? '' : String(value);
      s.addEventListener('change', () => onchange(s.value === '' ? undefined : Number(s.value)));
      return s;
    };
    p.love ??= [];
    return [
      namesEditor(p, 'pilots'),
      el('div', { class: 'sub' }, T('portrait')), portraitEditor(p),
      el('div', { class: 'grid' },
        el('label', { class: 'field' }, T('voice'), pilotsSelect(p.voice, (v) => { p.voice = v; changed(false); }, T('voiceCopy')), el('span', { class: 'was' }, T('voiceHint'))),
        el('label', { class: 'field' }, T('comboAs'), pilotsSelect(p.combo_as, (v) => { p.combo_as = v; changed(false); }, T('nobody')), el('span', { class: 'was' }, T('comboAsHint')))),
      listEditor(T('love'), T('loveHint'), p.love, (l) => el('span', {}, refInput('pilots', l.with, (v) => { l.with = v ?? 0; changed(); }),
        (() => { const c = el('input', { type: 'checkbox', checked: l.mutual }) as HTMLInputElement; c.addEventListener('change', () => { l.mutual = c.checked; changed(false); }); return el('label', { class: 'check' }, c, T('mutual')); })()),
        () => { p.love!.push({ with: p.copy, mutual: true }); changed(); }, 8),
      quotesEditor(p),
      el('div', { class: 'sub' }, T('stats')), el('p', { class: 'hint' }, text.stats1), ...dataEditor('pilots', p),
    ];
  }
  function unitEditor(u: Unit): Node[] {
    u.crew ??= [];
    const others = model.units.filter((x) => x !== u);
    const work = el('select', {}, el('option', { value: '' }, T('workCopy')), ...works.map((w) => el('option', { value: String(w.id) }, w.name))) as HTMLSelectElement;
    work.value = u.work === undefined ? '' : String(u.work);
    work.addEventListener('change', () => { u.work = work.value === '' ? undefined : Number(work.value); changed(false); });
    const addWeapon = el('div', { class: 'add-weapon' },
      refInput('weapons', undefined, (v) => { if (v !== undefined) { u.weapons.push(v); changed(); } }, T('addWeaponHint')));
    return [
      namesEditor(u, 'units'),
      el('div', { class: 'grid' }, el('label', { class: 'field' }, T('work'), work, el('span', { class: 'was' }, T('workHint')))),
      ...picturesEditor(u),
      listEditor(T('weaponList'), T('weaponListHint'), u.weapons, (w) => el('span', { class: 'ref-name' }, refName('weapons', w)), () => {}, 63),
      addWeapon,
      model.weapons.length ? el('div', { class: 'checks' }, ...model.weapons.map((w) => {
        const c = el('input', { type: 'checkbox', checked: u.weapons.includes(`@${w.slug}`) }) as HTMLInputElement;
        c.addEventListener('change', () => { const ref = `@${w.slug}`; if (c.checked) u.weapons.push(ref); else u.weapons = u.weapons.filter((x) => x !== ref); changed(); });
        return el('label', { class: 'check' }, c, `${T('carries')} ${shownName(w)}`);
      })) : null,
      others.length ? el('div', {}, el('div', { class: 'sub' }, T('forms')), el('p', { class: 'hint' }, T('formsHint')), el('div', { class: 'checks' }, ...others.map((o) => {
        const ref = `@${o.slug}`, c = el('input', { type: 'checkbox', checked: !!u.forms?.includes(ref) }) as HTMLInputElement;
        c.addEventListener('change', () => { const f = new Set(u.forms ?? []); if (c.checked) f.add(ref); else f.delete(ref); u.forms = [...f]; changed(); });
        return el('label', { class: 'check' }, c, shownName(o));
      }))) : null,
      combineEditor(u),
      listEditor(T('crew'), T('crewHint'), u.crew, (c, i) => refInput('pilots', c, (v) => { u.crew![i] = typeof v === 'number' ? v : 0; changed(); }),
        () => { u.crew!.push(0); changed(); }, 5),
      el('div', { class: 'grid' },
        el('label', { class: 'field' }, T('inherits'), refInput('units', u.inherits_from, (v) => { u.inherits_from = v; changed(false); }), el('span', { class: 'was' }, T('inheritsHint')))),
      el('div', { class: 'sub' }, T('stats')), ...dataEditor('units', u),
    ];
  }
  // A unit others combine into (new_units.json combine, mod_combine.hpp): its parts in crew
  // order, the first the leader, and the original combined units whose 合体 makes this one.
  function combineEditor(u: Unit): Node {
    const on = el('input', { type: 'checkbox', checked: !!u.combine }) as HTMLInputElement;
    on.addEventListener('change', () => { u.combine = on.checked ? { parts: [0, 0], over: [] } : undefined; changed(); });
    const c = u.combine;
    if (c) c.over ??= [];
    return el('div', {}, el('div', { class: 'sub' }, T('combine')), el('label', { class: 'check' }, on, T('combineOn')), el('p', { class: 'hint' }, T('combineHint')),
      c ? listEditor(T('combineParts'), T('combinePartsHint'), c.parts, (part, i) => refInput('units', part, (v) => { c.parts[i] = v ?? 0; changed(); }),
        () => { c.parts.push(0); changed(); }, 5) : null,
      c ? listEditor(T('combineOver'), T('combineOverHint'), c.over, (o, i) => refInput('units', o, (v) => { c.over[i] = typeof v === 'number' ? v : 0; changed(); }),
        () => { c.over.push(0); changed(); }, 8) : null);
  }
  function weaponEditor(w: Weapon): Node[] {
    const carriers = model.units.filter((u) => u.weapons.includes(`@${w.slug}`));
    const combo = w.combination;
    const comboBox = el('input', { type: 'checkbox', checked: !!combo }) as HTMLInputElement;
    comboBox.addEventListener('change', () => {
      w.combination = comboBox.checked ? { units: carriers.length ? [`@${carriers[0].slug}`, 0] : [0, 0], parts: [w.copy as number, 0], multiplier: 1.5 } : undefined;
      changed();
    });
    const comboEditor = combo ? el('div', {},
      ...combo.units.map((u, i) => el('div', { class: 'list-row' },
        refInput('units', u, (v) => { combo.units[i] = v ?? 0; changed(); }, T('comboUnit')),
        refInput('weapons', combo.parts[i], (v) => { combo.parts[i] = v ?? 0; changed(); }, T('comboPart')),
        combo.units.length > 2 ? el('button', { type: 'button', class: 'mini', onclick: () => { combo.units.splice(i, 1); combo.parts.splice(i, 1); changed(); } }, text.remove) : null)),
      combo.units.length < 5 ? el('button', { type: 'button', class: 'mini', onclick: () => { combo.units.push(0); combo.parts.push(0); changed(); } }, T('add')) : null,
      (() => {
        const m = el('input', { type: 'number', min: 0.5, max: 5, step: 0.1, value: combo.multiplier }) as HTMLInputElement;
        m.addEventListener('change', () => { combo.multiplier = Number(m.value); changed(); });
        return el('label', { class: 'field' }, T('multiplier'), m, el('span', { class: 'was' }, T('multiplierHint')));
      })(),
      el('label', { class: 'field' }, T('comboPilots'), textInput((combo.pilots ?? []).map((row) => row.join(' ')).join(' / '), (v) => {
        const rows = v.split('/').map((r) => r.trim().split(/[\s,，、]+/).filter(Boolean).map((x) => (/^\d+$/.test(x) ? Number(x) : x)));
        combo.pilots = rows.filter((r) => r.length).length ? rows.filter((r) => r.length) : undefined;
        changed(false);
      }, '166 167 / 168'), el('span', { class: 'was' }, T('comboPilotsHint')))) : null;
    return [
      namesEditor(w, 'weapons'),
      el('p', { class: carriers.length ? 'hint' : 'warn' }, carriers.length ? `${T('carriedBy')} ${carriers.map(shownName).join(sep)}` : T('notCarried')),
      el('div', { class: 'grid' },
        el('label', { class: 'field' }, T('inheritsWeapon'), refInput('weapons', w.inherits_from, (v) => { w.inherits_from = v; changed(false); }), el('span', { class: 'was' }, T('inheritsWeaponHint')))),
      el('div', { class: 'sub' }, T('combination')), el('label', { class: 'check' }, comboBox, T('combinationOn')), el('p', { class: 'hint' }, T('combinationHint')), comboEditor,
      el('div', { class: 'sub' }, T('stats')), ...dataEditor('weapons', w),
    ];
  }

  // --- Adding ---------------------------------------------------------------------------------
  const pilotIds = new Set(lists.pilots.map((p) => p.id));
  const pickers = {
    pilots: picker(pick.pilots.filter((x) => pilotIds.has(x.id)), pick.series, lang, { allSeries: text.allSeries, search: text.search, none: text.none }, (id) => addFrom('pilots', id)),
    units: picker(pick.units.filter((x) => x.id <= 353), pick.series, lang, { allSeries: text.allSeries, search: text.search, none: text.none }, (id) => addFrom('units', id)),
  };
  const weaponFind = el('input', { type: 'search', class: 'find', placeholder: text.search, autocomplete: 'off', spellcheck: 'false' }) as HTMLInputElement;
  const weaponResults = el('ul', { class: 'results' });
  weaponFind.addEventListener('input', () => {
    const found = search(lists.weapons, weaponFind.value);
    weaponResults.replaceChildren(...found.map((e) => el('li', {}, el('button', { type: 'button', onclick: () => addFrom('weapons', e.id) },
      el('span', { class: 'num' }, String(e.id)), nameOf(e), el('span', { class: 'stat' }, String(e.power))))));
  });
  function addFrom(k: Kind, id: number) {
    const base = byId[k].get(id)!;
    const slug = freeSlug(k, base.name.en || `${k}-${id}`);
    if (k === 'pilots') model.pilots.push({ slug, copy: id, names: {}, data: {} });
    if (k === 'units') model.units.push({ slug, copy: id, weapons: [...(unitWeapons.get(id) ?? [])], names: {}, models: {}, data: {} });
    if (k === 'weapons') model.weapons.push({ slug, copy: id, names: {}, data: {} });
    current = slug; adding = false;
    changed();
    editor.scrollIntoView({ behavior: 'smooth', block: 'start' });
  }

  // --- Rendering ------------------------------------------------------------------------------
  const listBox = root.querySelector<HTMLElement>('[data-list]')!;
  const addBox = root.querySelector<HTMLElement>('[data-add]')!;
  const editor = root.querySelector<HTMLElement>('[data-editor]')!;
  const problemBox = root.querySelector<HTMLElement>('[data-problems]')!;
  const statusLine = root.querySelector<HTMLElement>('[data-status]')!;
  const kindTitle: Record<string, string> = { pilots: T('newPilots'), units: T('newUnits'), weapons: T('newWeapons'), sprites: T('sprites'), scenes: T('scenes'), story: T('story'), joins: T('joinsTitle'), waves: T('wavesTitle') };
  const count = (k: Tab) => (k === 'sprites' ? Object.keys(sprites()).length : k === 'story' ? story().joins.length + story().waves.length : entries(k).length);
  const first = (k: Tab) => (k === 'sprites' ? Object.keys(sprites())[0] : k === 'story' ? storyKeys()[0] : entries(k)[0]?.slug) ?? null;

  // --- Joins and reinforcements (lib/story-image.ts, story/unit-maker.json) --------------------------
  const sceneTitle = (n: number) => `${n} ${scenes.find((s) => s.id === n)?.title ?? ''}`.trim();
  const storyKeys = () => [...story().joins.map((j) => `join:${j.id}`), ...story().waves.map((w) => `wave:${w.id}`)];
  const freeStoryId = (prefix: string, taken: string[]) => { let n = 1; while (taken.includes(`${prefix}${n}`)) n++; return `${prefix}${n}`; };
  const sceneSelect = (value: number, onchange: (v: number) => void) => {
    const s = el('select', {}, ...scenes.map((x) => el('option', { value: String(x.id) }, sceneTitle(x.id)))) as HTMLSelectElement;
    s.value = String(value);
    s.addEventListener('change', () => onchange(Number(s.value)));
    return s;
  };
  const numberBox = (value: number, low: number, high: number, onchange: (v: number) => void) => {
    const i = el('input', { type: 'number', min: low, max: high, value }) as HTMLInputElement;
    i.addEventListener('change', () => { const v = Math.round(Number(i.value)); onchange(Number.isFinite(v) ? v : low); });
    return i;
  };
  function textsEditor(title: string, value: storyImage.Texts, onchange: () => void): Node {
    return el('div', {}, el('div', { class: 'sub' }, title), ...LOCALES.map((loc) => el('div', { class: 'name-row' }, el('span', { class: 'loc' }, LOCALE_LABEL[loc]),
      textInput(value[loc] ?? '', (v) => { if (v) value[loc] = v; else delete value[loc]; onchange(); }))));
  }
  function joinEditor(j: Join): Node[] {
    const at = el('select', {}, el('option', { value: 'end' }, T('joinAtEnd')), el('option', { value: 'opening' }, T('joinAtOpening'))) as HTMLSelectElement;
    at.value = j.at;
    at.addEventListener('change', () => { j.at = at.value as Join['at']; changed(false); });
    const info = el('input', { type: 'checkbox', checked: !!j.info }) as HTMLInputElement;
    info.addEventListener('change', () => { j.info = info.checked ? { title: {}, text: {}, inherit: true } : undefined; changed(); });
    const parts: Node[] = [
      el('p', { class: 'hint' }, T('joinHint')),
      el('div', { class: 'grid' },
        el('label', { class: 'field' }, T('storyScene'), sceneSelect(j.scene, (v) => { j.scene = v; changed(false); }), el('span', { class: 'was' }, T('storySceneHint'))),
        el('label', { class: 'field' }, T('joinAt'), at),
        el('label', { class: 'field' }, T('joinPilot'), refInput('pilots', j.pilot, (v) => { j.pilot = v ?? 0; changed(false); })),
        el('label', { class: 'field' }, T('joinUnit'), refInput('units', j.unit, (v) => { j.unit = v ?? 0; changed(false); })),
        el('label', { class: 'field' }, T('joinLevel'), numberBox(j.level, -98, 98, (v) => { j.level = v; changed(false); }), el('span', { class: 'was' }, T('joinLevelHint')))),
      el('label', { class: 'check' }, info, T('joinInfo')), el('p', { class: 'hint' }, T('joinInfoHint')),
    ];
    if (j.info) {
      const keep = el('input', { type: 'checkbox', checked: j.info.inherit }) as HTMLInputElement;
      keep.addEventListener('change', () => { j.info!.inherit = keep.checked; changed(false); });
      parts.push(textsEditor(T('infoTitle'), j.info.title, () => changed(false)), textsEditor(T('infoText'), j.info.text, () => changed(false)),
        el('label', { class: 'check' }, keep, T('joinInherit')), el('p', { class: 'hint' }, T('joinInheritHint')));
    }
    return parts;
  }
  function waveEditor(w: Wave): Node[] {
    const phase = el('select', {}, ...(['player', 'enemy', 'third'] as const).map((p) => el('option', { value: p }, T(`phase_${p}`)))) as HTMLSelectElement;
    phase.value = w.phase;
    phase.addEventListener('change', () => { w.phase = phase.value as Wave['phase']; changed(false); });
    const rows = w.units.map((u: WaveUnit, i: number) => {
      const side = el('select', {}, ...(Object.keys(storyImage.DEPLOY_SIDES) as DeploySide[]).map((s) => el('option', { value: s }, T(`side_${s}`)))) as HTMLSelectElement;
      side.value = u.side;
      side.addEventListener('change', () => { u.side = side.value as DeploySide; changed(false); });
      return el('div', { class: 'wave-row' },
        el('label', {}, 'x', numberBox(u.x, 0, 63, (v) => { u.x = v; changed(false); })), el('label', {}, 'y', numberBox(u.y, 0, 63, (v) => { u.y = v; changed(false); })),
        refInput('pilots', u.pilot, (v) => { u.pilot = v ?? 0; changed(false); }, T('joinPilot')),
        refInput('units', u.unit, (v) => { u.unit = v ?? 0; changed(false); }, T('joinUnit')),
        side, el('label', {}, T('waveLevel'), numberBox(u.level, 0, 20, (v) => { u.level = v; changed(false); })),
        el('label', {}, T('waveUpgrade'), numberBox(u.upgrade, 0, 5, (v) => { u.upgrade = v; changed(false); })),
        el('button', { type: 'button', class: 'mini', onclick: () => { w.units.splice(i, 1); changed(); } }, text.remove));
    });
    return [
      el('p', { class: 'hint' }, T('waveHint')),
      el('div', { class: 'grid' },
        el('label', { class: 'field' }, T('storyScene'), sceneSelect(w.scene, (v) => { w.scene = v; changed(false); }), el('span', { class: 'was' }, T('storySceneHint'))),
        el('label', { class: 'field' }, T('waveTurn'), numberBox(w.turn, 1, 99, (v) => { w.turn = v; changed(false); })),
        el('label', { class: 'field' }, T('wavePhase'), phase)),
      el('div', { class: 'sub' }, T('waveUnits')), el('p', { class: 'hint' }, T('waveUnitsHint')), ...rows,
      w.units.length < 30 ? el('button', { type: 'button', class: 'mini', onclick: () => {
        const last = w.units[w.units.length - 1];
        w.units.push(last ? { ...last, x: Math.min(63, last.x + 1) } : { x: 10, y: 10, pilot: model.pilots[0] ? `@${model.pilots[0].slug}` : 298, unit: model.units[0] ? `@${model.units[0].slug}` : 170, side: 'enemy', level: 0, upgrade: 0 });
        changed();
      } }, T('add')) : null,
    ];
  }
  function renderStoryList() {
    const s = story();
    listBox.replaceChildren(el('div', { class: 'new-list' },
      ...s.joins.map((j) => el('button', { type: 'button', class: `new-entry${current === `join:${j.id}` ? ' on' : ''}`, onclick: () => { current = `join:${j.id}`; adding = false; render(); } },
        el('span', { class: 'num' }, T('joinTag')), `${j.id} · ${sceneTitle(j.scene)}`)),
      ...s.waves.map((w) => el('button', { type: 'button', class: `new-entry${current === `wave:${w.id}` ? ' on' : ''}`, onclick: () => { current = `wave:${w.id}`; adding = false; render(); } },
        el('span', { class: 'num' }, T('waveTag')), `${w.id} · ${sceneTitle(w.scene)}`)),
      el('button', { type: 'button', class: `new-entry add${adding ? ' on' : ''}`, onclick: () => { adding = !adding; render(); } }, `＋ ${T('addNew')}`)));
    addBox.replaceChildren(...(adding ? [el('p', { class: 'hint' }, T('copyHint_story')), el('div', {},
      el('button', { type: 'button', class: 'mini', onclick: () => {
        const id = freeStoryId(model.pilots[0]?.slug ? `${model.pilots[0].slug}-` : 'join-', s.joins.map((j) => j.id));
        s.joins.push({ id, scene: scenes[0]?.id ?? 0, at: 'end', pilot: model.pilots[0] ? `@${model.pilots[0].slug}` : 0, unit: model.units[0] ? `@${model.units[0].slug}` : 0, level: 0 });
        current = `join:${id}`; adding = false; changed();
      } }, T('joinNew')), ' ',
      el('button', { type: 'button', class: 'mini', onclick: () => {
        const id = freeStoryId('wave-', s.waves.map((w) => w.id));
        s.waves.push({ id, scene: scenes[0]?.id ?? 0, turn: 2, phase: 'player', units: [{ x: 10, y: 10, pilot: model.pilots[0] ? `@${model.pilots[0].slug}` : 298, unit: model.units[0] ? `@${model.units[0].slug}` : 170, side: 'enemy', level: 0, upgrade: 0 }] });
        current = `wave:${id}`; adding = false; changed();
      } }, T('waveNew')))] : []));
  }
  function renderStoryEditor() {
    const s = story();
    const [what, id] = (current ?? '').split(/:(.*)/);
    const j = what === 'join' ? s.joins.find((x) => x.id === id) : undefined, w = what === 'wave' ? s.waves.find((x) => x.id === id) : undefined;
    if (!j && !w) { editor.replaceChildren(el('p', { class: 'hint' }, T(count('story') ? 'pickOne' : 'empty_story'))); return; }
    const entry = (j ?? w)!;
    const idBox = textInput(entry.id, (v) => {
      const list = j ? s.joins : s.waves;
      if (!storyImage.KEY.test(v) || (v !== entry.id && list.some((x) => x.id === v))) { say(statusLine, T('slugBad'), true); render(); return; }
      entry.id = v; current = `${what}:${v}`; changed();
    });
    editor.replaceChildren(el('div', { class: 'card' },
      el('h3', {}, el('span', { class: 'num' }, T(j ? 'joinTag' : 'waveTag')), entry.id),
      el('label', { class: 'field' }, T('storyId'), idBox, el('span', { class: 'was' }, T('storyIdHint'))),
      ...(j ? joinEditor(j) : waveEditor(w!)),
      el('div', {}, el('button', { type: 'button', class: 'mini danger', onclick: () => {
        if (j) s.joins.splice(s.joins.indexOf(j), 1); else s.waves.splice(s.waves.indexOf(w!), 1);
        current = null; changed();
      } }, T('deleteEntry')))));
  }

  function renderSpriteList() {
    listBox.replaceChildren(el('div', { class: 'new-list' },
      ...Object.keys(sprites()).map((n) => el('button', { type: 'button', class: `new-entry${n === current ? ' on' : ''}`, onclick: () => { current = n; adding = false; render(); } },
        el('span', { class: 'num' }, `@${n}`), usersOf(n)[0] ?? T('spriteUnused'))),
      el('button', { type: 'button', class: `new-entry add${adding ? ' on' : ''}`, onclick: () => { adding = !adding; render(); } }, `＋ ${T('addNew')}`)));
    const add = (mirror: boolean) => fileButton(T(mirror ? 'spriteAddPose' : 'spriteAddCutin'), true, async (files) => {
      current = await addSprite(mirror ? 'pose' : 'cutin', files, mirror); adding = false; changed();
    });
    addBox.replaceChildren(...(adding ? [el('p', { class: 'hint' }, T('copyHint_sprites')), el('div', {}, add(true), ' ', add(false))] : []));
  }
  function renderSpriteEditor() {
    if (!current || !(current in sprites())) { editor.replaceChildren(el('p', { class: 'hint' }, T(count('sprites') ? 'pickOne' : 'empty_sprites'))); return; }
    const name = current;
    editor.replaceChildren(el('div', { class: 'card' }, el('h3', {}, el('span', { class: 'num' }, `@${name}`)), spriteCard(name),
      el('div', {}, el('button', { type: 'button', class: 'mini danger', onclick: () => {
        delete sprites()[name]; art.delete(name);
        for (const u of model.units) if (u.pose === `@${name}`) delete u.pose;
        current = null; changed();
      } }, T('deleteEntry')))));
  }

  function renderList() {
    if (kind === 'sprites') return renderSpriteList();
    if (kind === 'story') return renderStoryList();
    listBox.replaceChildren(el('div', { class: 'new-list' },
      ...entries(kind).map((e) => el('button', { type: 'button', class: `new-entry${e.slug === current ? ' on' : ''}`, onclick: () => { current = e.slug; adding = false; render(); } },
        el('span', { class: 'num' }, `@${e.slug}`), shownName(e))),
      el('button', { type: 'button', class: `new-entry add${adding ? ' on' : ''}`, onclick: () => { adding = !adding; render(); } }, `＋ ${T('addNew')}`)));
    addBox.replaceChildren(...(adding ? [el('p', { class: 'hint' }, T(`copyHint_${kind}`)),
      kind === 'weapons' ? el('div', {}, weaponFind, weaponResults) : pickers[kind].node] : []));
  }
  function renderEditor() {
    if (kind === 'sprites') return renderSpriteEditor();
    if (kind === 'story') return renderStoryEditor();
    const e = find(kind, current);
    if (!e) { editor.replaceChildren(entries(kind).length ? el('p', { class: 'hint' }, T('pickOne')) : el('p', { class: 'hint' }, T(`empty_${kind}`))); return; }
    const base = baseOf(kind, e);
    const body = kind === 'pilots' ? pilotEditor(e as Pilot) : kind === 'units' ? unitEditor(e as Unit) : weaponEditor(e as Weapon);
    editor.replaceChildren(el('div', { class: 'card' },
      el('h3', {}, el('span', { class: 'num' }, `@${e.slug}`), shownName(e), base ? el('span', { class: 'ja' }, `${T('copyOf')} ${base.id} ${nameOf(base)}`) : null),
      slugEditor(e, kind), ...body,
      el('div', {}, el('button', { type: 'button', class: 'mini danger', onclick: () => {
        model[kind as Kind] = entries(kind as Kind).filter((x) => x !== e) as any;
        if (kind === 'pilots') portraits.delete(e.slug);
        if (kind === 'units') icons.delete(e.slug);
        current = null; changed();
      } }, T('deleteEntry')))));
  }
  const PROBLEM_TEXT = (p: Problem) => {
    const [k, slug] = p.where.split('.') as [Tab | 'scenes', string];
    const picture = k === 'sprites' || k === 'scenes';
    const e = picture ? null : find(k, slug);
    return `${kind === k ? '' : kindTitle[k] + ' · '}${e ? shownName(e) : picture ? `@${slug}` : slug}：${T(`p_${p.code}`, p.detail ?? {})}`;
  };
  function allProblems(): string[] {
    const out = problems(model).map(PROBLEM_TEXT);
    for (const k of KINDS) for (const e of entries(k)) {
      const base = baseOf(k, e);
      for (const [key, v] of Object.entries(e.data)) {
        const f = labels.fields[k].find((x) => x.key === key);
        if (f && typeof v === 'number' && (v < f.low || v > f.high)) out.push(`${shownName(e)}：${fill(text.outOfRange, { field: f.label, low: f.low, high: f.high })}`);
        if (f?.step && typeof v === 'number' && v % f.step) out.push(`${shownName(e)}：${text.power100}`);
      }
      if (k === 'weapons' && base) {
        const lo = (e.data.range_min as number) ?? base.range_min, hi = (e.data.range_max as number) ?? base.range_max;
        if (lo > hi) out.push(`${shownName(e)}：${text.rangeOrder}`);
      }
      if (k === 'pilots' && portraits.get(e.slug)?.pic?.framing) out.push(`${shownName(e)}：${T('p_framing')}`);
      if (k === 'units' && icons.get(e.slug)?.pic.framing) out.push(`${shownName(e)}：${T('p_iconFraming')}`);
    }
    for (const name of Object.keys(sprites())) if (!art.has(name)) out.push(`@${name}：${T('p_spriteNoArt')}`);
    for (const name of voicesOf(model)) if (!voices.has(name)) out.push(`${name}：${T('p_voiceMissing')}`);
    const s = story();
    for (const p of storyImage.storyProblems(s.joins, s.waves)) { const [k, id] = p.where.split(/\.(.*)/); out.push(`${kindTitle[k]} ${id}：${T(`p_${p.code}`)}`); }
    // A new character or unit by name must be one of the package's.
    const own = (k: Kind, v: Ref) => typeof v !== 'string' || !v.startsWith('@') || !!find(k, v.slice(1));
    for (const j of s.joins) if (!own('pilots', j.pilot) || !own('units', j.unit)) out.push(`${kindTitle.joins} ${j.id}：${T('p_story_missing')}`);
    for (const w of s.waves) if (w.units.some((u) => !own('pilots', u.pilot) || !own('units', u.unit))) out.push(`${kindTitle.waves} ${w.id}：${T('p_story_missing')}`);
    return out;
  }
  function renderProblems() {
    const list = allProblems();
    problemBox.replaceChildren(...(list.length ? [el('ul', { class: 'problems' }, ...list.map((t) => el('li', {}, t)))] : [el('p', { class: 'hint' }, T('noProblems'))]));
  }
  function render() {
    for (const o of root!.querySelectorAll<HTMLElement>('[data-kind]')) {
      o.classList.toggle('on', o.dataset.kind === kind);
      o.querySelector('.count')!.textContent = String(count(o.dataset.kind as Tab));
    }
    renderList(); renderEditor(); renderProblems();
  }
  // After an edit: the lists and problems; the editor too unless the field keeps its focus.
  let problemTimer = 0;
  function changed(redraw = true) {
    refreshLists();
    if (redraw) render();
    else { clearTimeout(problemTimer); problemTimer = window.setTimeout(renderProblems, 200); renderList(); }
    try { localStorage.setItem('unit-maker-draft', JSON.stringify({ model: { ...model, sprites: undefined } })); } catch { /* private window */ }
  }
  for (const b of root.querySelectorAll<HTMLButtonElement>('[data-kind]'))
    b.addEventListener('click', () => { kind = b.dataset.kind as Tab; current = first(kind); adding = !count(kind); render(); });

  // --- Files ----------------------------------------------------------------------------------
  function packageFiles(): Record<string, Uint8Array> {
    for (const p of model.pilots) {
      const own = portraits.get(p.slug);
      if (own?.image.length) p.portrait = { image: `art/pilots/${p.slug}.png`, hd: `art/pilots/${p.slug}-hd.png` };
      else if (p.portrait && !Array.isArray(p.portrait)) delete p.portrait;
    }
    for (const name of art.keys()) place(name);
    const out: Record<string, Uint8Array> = { ...keptFiles };
    for (const [path, doc] of Object.entries(toFiles(model, keptJson))) out[path] = jsonBytes(doc);
    for (const [name, a] of art) {
      const s = sprites()[name];
      if (!s) continue;
      const { frames, hd } = spriteFiles(s);
      frames.forEach((path, i) => (out[path] = a.images[i]));
      hd.forEach((path, i) => (out[path] = a.hds[i]));
    }
    for (const name of voicesOf(model)) { const ogg = voices.get(name); if (ogg) out[`audio/voice/battle/${name}`] = ogg; }
    const image = storyImage.compile(story().joins, story().waves);
    if (image) out[storyImage.STORY_FILE] = jsonBytes(image);
    const facing: Record<string, string> = { ...keptFacing };
    for (const u of model.units) {
      const icon = icons.get(u.slug);
      if (!icon) continue;
      out[`art/icons/@${u.slug}.png`] = icon.pic.png;
      facing[`@${u.slug}`] = icon.way;
    }
    if (Object.keys(facing).length) out['art/icons.json'] = jsonBytes({ schema: 'srw64.icons.v1', facing });
    for (const p of model.pilots) {
      const own = portraits.get(p.slug);
      if (own?.image.length) { out[`art/pilots/${p.slug}.png`] = own.image; out[`art/pilots/${p.slug}-hd.png`] = own.hd; }
    }
    return out;
  }
  // Thunderstore (lib/thunderstore.ts): beside the .srw64mod, the zip its upload page takes.
  let tsIcon: Uint8Array | null = null;
  const tsOn = el('input', { type: 'checkbox' }) as HTMLInputElement;
  const tsTeam = el('input', { placeholder: 'YourTeam', autocomplete: 'off', spellcheck: 'false' }) as HTMLInputElement;
  const tsName = el('input', { placeholder: '', autocomplete: 'off', spellcheck: 'false' }) as HTMLInputElement;
  const tsIconView = el('span', { class: 'hint' }, T('tsIconAuto'));
  const tsIconPick = fileButton(T('tsIcon'), false, async ([f]) => {
    const b = await createImageBitmap(f);
    if (b.width !== b.height) { say(statusLine, T('tsIconSquare'), true); return; }
    const c = document.createElement('canvas'); c.width = c.height = ts.ICON_SIZE;
    const g = c.getContext('2d')!; g.imageSmoothingEnabled = b.width > ts.ICON_SIZE; g.imageSmoothingQuality = 'high';
    g.drawImage(b, 0, 0, ts.ICON_SIZE, ts.ICON_SIZE);
    tsIcon = await png(c);
    tsIconView.replaceChildren(el('img', { src: dataUrl(tsIcon), alt: '', width: 64, height: 64 }));
  });
  const tsBox = el('div', { class: 'ts-options', hidden: true },
    el('p', { class: 'hint' }, T('tsHint')),
    el('div', { class: 'grid' },
      el('label', { class: 'field' }, T('tsTeam'), tsTeam, el('span', { class: 'was' }, T('tsTeamHint'))),
      el('label', { class: 'field' }, T('tsName'), tsName, el('span', { class: 'was' }, T('tsNameHint')))),
    el('div', { class: 'faces' }, tsIconPick, tsIconView));
  tsOn.addEventListener('change', () => { tsBox.hidden = !tsOn.checked; });
  root.querySelector('[data-export]')!.before(el('label', { class: 'check' }, tsOn, T('tsOn')), tsBox);
  // Without a chosen icon: the first new unit's map icon, else the first portrait, made 256.
  async function defaultTsIcon(): Promise<Uint8Array | null> {
    const own = [...icons.values()].find((i) => !i.pic.framing)?.pic.png ?? [...portraits.values()].find((p) => p.hd.length)?.hd;
    if (!own) return null;
    const b = await createImageBitmap(new Blob([own as BlobPart], { type: 'image/png' }));
    const c = document.createElement('canvas'); c.width = c.height = ts.ICON_SIZE;
    const g = c.getContext('2d')!; g.imageSmoothingEnabled = b.width > ts.ICON_SIZE;
    g.drawImage(b, 0, 0, ts.ICON_SIZE, ts.ICON_SIZE);
    return png(c);
  }
  root.querySelector('[data-export]')!.addEventListener('click', async () => {
    const status = root.querySelector<HTMLElement>('[data-export-status]')!;
    const m = form.manifest();
    if (typeof m === 'string') return say(status, m, true);
    const list = allProblems();
    if (list.length) return say(status, text.fixFirst + list.join(lang === 'en' || lang === 'ko' ? '; ' : '；'), true);
    if (!model.pilots.length && !model.units.length && !model.weapons.length && !count('sprites') && !count('story')) return say(status, T('errEmpty'), true);
    let tsPlan: { team: string; name: string; icon: Uint8Array } | null = null;
    if (tsOn.checked) {
      const team = tsTeam.value.trim(), name = tsName.value.trim() || ts.defaultName(String(m.manifest.id));
      if (team && !ts.NAME_RE.test(team)) return say(status, T('tsBadTeam'), true);
      if (!ts.NAME_RE.test(name) || name.length > ts.NAME_LIMIT) return say(status, T('tsBadName'), true);
      if (ts.unmappedRequires(m.manifest).length) return say(status, T('tsRequires', { ids: ts.unmappedRequires(m.manifest).join(', ') }), true);
      const icon = tsIcon ?? (await defaultTsIcon());
      if (!icon) return say(status, T('tsNeedIcon'), true);
      tsPlan = { team, name, icon };
    }
    const made = writePackage(m.manifest, m.version, packageFiles(), status, text);
    if (!tsPlan) return;
    const zip = thunderstoreZip(ts.entries(m.manifest, tsPlan.name, tsPlan.icon, made.name, made.zip, strToU8));
    const file = ts.zipName(tsPlan.team, tsPlan.name, m.version);
    setTimeout(() => offer(zip, file), 400);   // a second download, after the first
    say(status, `${status.textContent} ${T('tsExported', { file, size: size(zip.length) })}`);
  });
  root.querySelector<HTMLInputElement>('[data-import]')!.addEventListener('change', async (ev) => {
    const input = ev.target as HTMLInputElement, file = input.files?.[0];
    const status = root.querySelector<HTMLElement>('[data-import-status]')!;
    input.value = '';
    if (!file) return;
    try {
      const { mod, files } = await openPackage(file);
      form.fill(mod);
      const json: Record<string, unknown> = {};
      const owned = ['data/new_units.json', 'data/pilots.json', 'data/units.json', 'data/weapons.json', 'dialogue/names.json', 'dialogue/battle_quotes.json'];
      keptFiles = {};
      for (const [path, bytes] of Object.entries(files)) {
        if (path === 'mod.json') continue;
        if (owned.includes(path)) json[path] = readJson(bytes); else keptFiles[path] = bytes;
      }
      const read = fromFiles(json);
      model = read.model; keptJson = read.kept;
      portraits.clear();
      for (const p of model.pilots) {
        const portrait = p.portrait;
        if (!portrait || Array.isArray(portrait)) continue;
        const image = keptFiles[portrait.image], hd = portrait.hd ? keptFiles[portrait.hd] : undefined;
        if (!image) continue;
        portraits.set(p.slug, { pic: null, image, hd: hd ?? image, url: dataUrl(image) });
        delete keptFiles[portrait.image]; if (portrait.hd) delete keptFiles[portrait.hd];
      }
      art.clear(); icons.clear(); keptFacing = {}; voices.clear();
      if (keptFiles[storyImage.STORY_FILE]) {
        (model as Model & { story?: unknown }).story = storyImage.read(readJson(keptFiles[storyImage.STORY_FILE]));
        delete keptFiles[storyImage.STORY_FILE];
      }
      for (const name of voicesOf(model)) {
        const bytes = keptFiles[`audio/voice/battle/${name}`];
        if (bytes) { voices.set(name, bytes); delete keptFiles[`audio/voice/battle/${name}`]; }
      }
      for (const [name, s] of Object.entries(model.sprites ?? {})) {
        const { frames, hd } = spriteFiles(s);
        if (!frames.length || frames.some((f) => !keptFiles[f]) || hd.some((f) => !keptFiles[f])) continue;
        const a: Art = { sources: frames.map(() => null), hds: hd.map((f) => keptFiles[f]), images: frames.map((f) => keptFiles[f]), urls: [], rgba: [], colors: 0 };
        a.urls = a.images.map(dataUrl);
        a.rgba = await Promise.all(a.images.map(imageData));
        const seen = new Set<number>();
        for (const r of a.rgba) for (let i = 0; i < r.data.length; i += 4) if (r.data[i + 3]) seen.add((r.data[i] << 16) | (r.data[i + 1] << 8) | r.data[i + 2]);
        a.colors = seen.size;
        art.set(name, a);
        for (const f of [...frames, ...hd]) delete keptFiles[f];
      }
      const said: Record<string, string> = keptFiles['art/icons.json'] ? readJson(keptFiles['art/icons.json'])?.facing ?? {} : {};
      for (const u of model.units) {
        const bytes = keptFiles[`art/icons/@${u.slug}.png`];
        if (!bytes) continue;
        const way = (['left', 'right', 'front'] as Way[]).find((w) => w === said[`@${u.slug}`]) ?? 'left';
        icons.set(u.slug, { pic: await loadIcon(new Blob([bytes as BlobPart], { type: 'image/png' }), bytes), way });
        delete keptFiles[`art/icons/@${u.slug}.png`];
      }
      if (keptFiles['art/icons.json']) {
        for (const [key, way] of Object.entries(said)) if (!(key.startsWith('@') && icons.has(key.slice(1)))) keptFacing[key] = way;
        delete keptFiles['art/icons.json'];
      }
      kind = model.pilots.length ? 'pilots' : model.units.length ? 'units' : model.weapons.length ? 'weapons' : count('sprites') ? 'sprites' : 'story';
      current = first(kind); adding = false;
      changed();
      say(status, fill(text.imported, { id: mod.id ?? file.name }));
    } catch (error) {
      say(status, fill(text.errImport, { detail: String((error as Error).message ?? error) }), true);
    }
  });

  // --- In the game (lib/game-link.ts, W3) -----------------------------------------------------------
  // The package goes to the game on this computer as a package to try, then the battle viewer
  // shows a new unit of it against an original one.
  const linkBox = root.querySelector<HTMLElement>('[data-link]');
  if (linkBox) {
    const link = new GameLink();
    try { link.token = sessionStorage.getItem('unit-maker-link') ?? ''; } catch { /* none */ }
    const linkStatus = el('p', { class: 'status' });
    const port = el('input', { type: 'number', min: 1024, max: 65535, value: PORT, class: 'port' }) as HTMLInputElement;
    const shot = el('div', { class: 'link-shot' });
    const controls = el('div', { class: 'link-controls', hidden: true });
    const sayLink = (key: string, values: Record<string, string | number> = {}, error = false) => say(linkStatus, T(key, values), error);
    const why = (error: unknown) => { const m = String((error as Error)?.message ?? error); return m === 'Failed to fetch' ? T('linkNoGame') : m; };
    async function connect() {
      link.port = Number(port.value) || PORT;
      try {
        const hello = await link.hello();
        if (hello.protocol !== 1) return sayLink('linkProtocol', {}, true);
        if (!link.token || !hello.paired) {
          link.token = '';
          await link.pair(() => sayLink('linkWaiting'));
          try { sessionStorage.setItem('unit-maker-link', link.token); } catch { /* private window */ }
        }
        sayLink('linkReady', { version: hello.version });
        controls.hidden = false; renderControls();
      } catch (error) {
        sayLink(String((error as Error)?.message) === 'refused' ? 'linkRefused' : 'linkFailed', { detail: why(error) }, true);
      }
    }
    // Attacker: a new unit of the package with a character and one of its weapons; defender: any.
    const choice = { unit: '', pilot: '', weapon: '', defender: { unit: 170, pilot: 298, weapon: 677 }, damage: 500 };
    function renderControls() {
      const unitSelect = el('select', {}, ...model.units.map((u) => el('option', { value: `@${u.slug}` }, `@${u.slug} ${shownName(u)}`))) as HTMLSelectElement;
      choice.unit ||= model.units[0] ? `@${model.units[0].slug}` : '';
      unitSelect.value = choice.unit;
      const u = model.units.find((x) => `@${x.slug}` === choice.unit);
      const pilotSelect = el('select', {}, ...model.pilots.map((p) => el('option', { value: `@${p.slug}` }, `@${p.slug} ${shownName(p)}`)),
        ...lists.pilots.map((p) => el('option', { value: String(p.id) }, `${p.id} ${nameOf(p)}`))) as HTMLSelectElement;
      choice.pilot ||= model.pilots[0] ? `@${model.pilots[0].slug}` : '166';
      pilotSelect.value = choice.pilot;
      const weaponSelect = el('select', {}, ...(u?.weapons ?? []).map((w) => el('option', { value: String(w) }, refName('weapons', w)))) as HTMLSelectElement;
      if (!u?.weapons.map(String).includes(choice.weapon)) choice.weapon = String(u?.weapons[0] ?? '');
      weaponSelect.value = choice.weapon;
      unitSelect.addEventListener('change', () => { choice.unit = unitSelect.value; choice.weapon = ''; renderControls(); });
      pilotSelect.addEventListener('change', () => { choice.pilot = pilotSelect.value; });
      weaponSelect.addEventListener('change', () => { choice.weapon = weaponSelect.value; });
      const number = (v: number, set: (n: number) => void) => { const i = el('input', { type: 'number', value: v, class: 'port' }) as HTMLInputElement; i.addEventListener('change', () => set(Math.round(Number(i.value)) || 0)); return i; };
      controls.replaceChildren(
        el('div', { class: 'grid' },
          el('label', { class: 'field' }, T('linkUnit'), unitSelect), el('label', { class: 'field' }, T('linkPilot'), pilotSelect),
          el('label', { class: 'field' }, T('linkWeapon'), weaponSelect),
          el('label', { class: 'field' }, T('linkDefender'), el('span', { class: 'link-numbers' },
            number(choice.defender.unit, (n) => (choice.defender.unit = n)), number(choice.defender.pilot, (n) => (choice.defender.pilot = n)), number(choice.defender.weapon, (n) => (choice.defender.weapon = n))),
            el('span', { class: 'was' }, T('linkDefenderHint'))),
          el('label', { class: 'field' }, T('linkDamage'), number(choice.damage, (n) => (choice.damage = n)))),
        el('div', {}, el('button', { type: 'button', class: 'btn', onclick: tryIt }, T('linkTry')), ' ',
          el('button', { type: 'button', class: 'mini', onclick: screenshot }, T('linkShot'))));
    }
    async function tryIt() {
      const m = form.manifest();
      if (typeof m === 'string') return sayLink('linkPackage', { detail: m }, true);
      const list = allProblems();
      if (list.length) return say(linkStatus, text.fixFirst + list.join(lang === 'en' || lang === 'ko' ? '; ' : '；'), true);
      if (!choice.unit) return sayLink('linkNoUnit', {}, true);
      try {
        sayLink('linkInstalling');
        const done = await link.install(zipPackage(m.manifest, packageFiles()));
        if (!done.ok || !done.numbers) return sayLink('linkInstallFailed', { detail: (done.problems ?? []).join('；') }, true);
        const resolve = (kind: 'units' | 'pilots' | 'weapons', v: string) => (/^\d+$/.test(v) ? Number(v) : done.numbers![kind][v]);
        const attacker = { unit: resolve('units', choice.unit), pilot: resolve('pilots', choice.pilot), weapon: resolve('weapons', choice.weapon) };
        if ([attacker.unit, attacker.pilot, attacker.weapon].some((n) => n === undefined)) return sayLink('linkInstallFailed', { detail: T('linkNumbers') }, true);
        const started = await link.viewer({ attacker, defender: choice.defender, damage: choice.damage, scene: [0, 0] });
        if (!started.ok) return sayLink('linkViewerFailed', { detail: started.error ?? '' }, true);
        sayLink('linkStarted', { unit: attacker.unit });
      } catch (error) {
        if (String((error as Error)?.message) === 'unpaired') { controls.hidden = true; try { sessionStorage.removeItem('unit-maker-link'); } catch { /* none */ } }
        sayLink('linkFailed', { detail: why(error) }, true);
      }
    }
    async function screenshot() {
      try {
        const blob = await link.screenshot();
        shot.replaceChildren(el('img', { src: dataUrl(new Uint8Array(await blob.arrayBuffer())), alt: '' }));
      } catch (error) { sayLink('linkFailed', { detail: why(error) }, true); }
    }
    linkBox.append(el('p', { class: 'hint' }, T('linkHint')),
      el('div', { class: 'faces' }, el('label', { class: 'field' }, T('linkPort'), port), el('button', { type: 'button', class: 'btn', onclick: connect }, T('linkConnect'))),
      linkStatus, controls, shot);
  }

  // A draft from this browser (the model only; pictures are not kept).
  try {
    const draft = JSON.parse(localStorage.getItem('unit-maker-draft') ?? 'null');
    if (draft?.model?.pilots) {
      model = draft.model;
      for (const p of model.pilots) if (p.portrait && !Array.isArray(p.portrait)) delete p.portrait;
      for (const u of model.units) if (typeof u.pose === 'string' && u.pose.startsWith('@')) delete u.pose;
      delete model.sprites;
      // Voices are files too: the lines stay, their voices go.
      for (const p of model.pilots) for (const l of [...QUOTE_SITUATIONS.flatMap((s) => p.quotes?.[s] ?? []), ...(p.quotes?.special ?? []).flatMap((c) => c.lines)]) delete l.voice;
    }
  } catch { /* none */ }
  current = first(kind);
  adding = !count(kind);
  refreshLists();
  render();
  addEventListener('beforeunload', (e) => { if (model.pilots.length || model.units.length || model.weapons.length) e.preventDefault(); });
}
