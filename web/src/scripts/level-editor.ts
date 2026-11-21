// The level editor (pages/[lang]/mods/editor.astro, docs/design/level-editor.md §5): a campaign
// put together stage by stage in five parts: the overview (the stages and where each leads),
// the map (an original one, or a picture cut into cells with a terrain for each), the units
// in their groups, the story (events, their steps, their lines) and the rules (winning,
// losing, when each group comes on). The project is the page's own model (lib/editor-model.ts);
// exporting writes the sources from it and compiles them here (lib/compiler, the command
// line's compiler in TypeScript). What it shows of the original game comes from the site's
// exports (/gen/editor.json, /gen/maps, the Library's pictures): no ROM is read, nothing is
// uploaded.
import { strFromU8, strToU8, unzipSync, zipSync } from 'fflate';
import { el, fill, openPackage, packageForm, readJson, say, writePackage, type Text } from './mod-package';
import { picker, type Pickable } from './picker';
import { CompileError } from '../lib/compiler/assemble.ts';
import { QUOTE_MOMENTS, SILHOUETTES } from '../lib/compiler/story.ts';
import { COMMANDS, commandOf, defaults, type Field, type Pos } from '../lib/editor-commands.ts';
import { compileCampaign } from '../lib/compiler/campaign.ts';
import { buildMap, type Template } from '../lib/compiler/map.ts';
import { campaignSource, FRAME, freeName, leadsTo, newStage, objectiveOf, problems, stageSource, type Area, type Cond, type Conds, type GameEvent, type Group, type GroupWhen,
  type OwnMapInfo, type Project, type Side, type Stage, type Step, type When, type BattleLine } from '../lib/editor-model.ts';

type Names = { ja: string; zh: string; en: string; ko?: string };
type Terrain = { id: number; name: Names; defense: number; hit: number; hp: number; en: number; move: number[] };
type OriginalMap = { map: number; columns: number; rows: number; mode: number; space: number; terrain: string; template?: Template; scenes?: number[] };
type Data = { text: Text; units: (Pickable & { icon: number })[]; pilots: Pickable[]; series: { id: number | null; name: string }[]; lang: 'zh' | 'en' | 'ja' | 'ko'; locale: string; game: string };
type Tab = 'overview' | 'map' | 'units' | 'story' | 'rules';
type Picture = { bitmap: ImageBitmap; blob: Blob };
type Say = Extract<Step, { kind: 'say' }>;

const SIDES = ['#3fa0ff', '#ff5a6e', '#ffd24a'];
const LOCALES: [string, string][] = [['zh-Hans', '中文'], ['ja', '日本語'], ['en', 'English']];
const MAX_PIXELS = 24_000_000;      // of a map's picture as it is cut: more is cut at a smaller scale

export async function initLevelEditor() {
  const root = document.querySelector<HTMLElement>('[data-level-editor]');
  if (!root) return;
  const { text: T, units, pilots, series, lang, locale, game }: Data = JSON.parse(document.getElementById('editor-data')!.textContent!);
  const $ = <E extends HTMLElement>(q: string) => root.querySelector<E>(q)!;
  const nameOf = (n?: Names) => n ? n[lang] || n.en || n.ja : '?';
  const form = packageForm(root, T, locale, game);
  const original: { maps: OriginalMap[]; terrain: Terrain[]; objectives?: ({ id: number } & Names)[]; parts?: ({ id: number } & Names)[];
    places?: { id: number; region: Names; scenes: number[] }[]; battles?: { id: number; pilot: number; scenes: number[] }[];
    songs?: { id: number; title: string }[]; sounds?: { id: number; times: number; scenes: number[] }[]; weapons?: ({ id: number; unit: number | null } & Names)[]; titles?: Record<string, Names>; terrain_colours?: Record<string, number[]> } =
    await (await fetch('/gen/editor.json')).json();
  const terrainOf = new Map(original.terrain.map((t) => [t.id, t]));
  const templates = new Map(original.maps.filter((m) => m.template).map((m) => [m.map, m.template!]));
  const unitOf = new Map(units.map((u) => [u.id, u])), pilotOf = new Map(pilots.map((p) => [p.id, p]));
  const unitName = (n: number) => nameOf(unitOf.get(n)?.name), pilotName = (n: number) => nameOf(pilotOf.get(n)?.name);

  // Terrain kinds for the brush: the table's rows with the same name and values are one kind.
  type Kind = { ids: number[]; t: Terrain; colour: string };
  const kinds: Kind[] = [];
  const signature = (t: Terrain) => JSON.stringify([t.name, t.defense, t.hit, t.hp, t.en, t.move]);
  for (const t of original.terrain) if (t.id < 99) {
    const same = kinds.find((k) => signature(k.t) === signature(t));
    if (same) same.ids.push(t.id); else kinds.push({ ids: [t.id], t, colour: '' });
  }
  kinds.forEach((k, n) => { k.colour = `hsl(${(n * 137.5) % 360} 62% ${38 + (n % 3) * 9}%)`; });
  const kindOf = new Map(kinds.flatMap((k) => k.ids.map((id): [number, Kind] => [id, k])));
  const stats = (t: Terrain) => fill(T.terrainStats, { defense: t.defense, hit: t.hit, move: t.move[0] === 255 ? '×' : t.move[0] })
    + (t.hp || t.en ? fill(T.terrainRecover, { hp: t.hp, en: t.en }) : '');

  // --- The project and what the page remembers of it ----------------------------------------
  const project: Project = { route: 1, stages: [newStage('stage-1')], maps: [] };
  const pictures = new Map<string, Picture>();
  let current = 0, tab: Tab = 'overview';
  let brush = 0, tool: 'brush' | 'rect' | 'fill' | 'pick' = 'brush', overlay = true, grid = true, cell = 22, compare: ImageBitmap | null = null;
  let group = 0, picked = -1, placing = false;
  // Picking an area on the units tab's map for a trigger or a condition, then back.
  let areaPick: { area: Area; back: Tab; done?: (a: Area) => void } | null = null;
  let eventAt = 'enter', stepAt = -1, language = lang === 'ja' ? 'ja' : lang === 'en' ? 'en' : 'zh-Hans';
  const stage = () => project.stages[current];
  const ownMap = (name: unknown) => project.maps.find((m) => m.name === name);
  const size = (s: Stage) => { const m = ownMap(s.map) ?? original.maps[Number(s.map)] ?? original.maps[0]; return { columns: m.columns, rows: m.rows }; };
  const terrainAt = (s: Stage, x: number, y: number) => {
    const own = ownMap(s.map);
    if (own) return x < FRAME || y < FRAME || x >= own.columns - FRAME || y >= own.rows - FRAME ? 99 : own.terrain[y][x];
    const m = original.maps[Number(s.map)];
    return parseInt(m.terrain.substr((y * m.columns + x) * 2, 2), 16);
  };
  const passable = (s: Stage, x: number, y: number) => !terrainOf.get(terrainAt(s, x + FRAME, y + FRAME))?.move.every((m) => m === 255);
  const found = () => problems(project, size, passable, { unit: (n) => unitOf.has(n), pilot: (n) => pilotOf.has(n) });
  const stageLabel = (s: Stage) => s.title || s.key;
  const problemText = (p: ReturnType<typeof found>[number]) => fill(T[`err_${p.code}`] ?? p.code, { stage: stageLabel(project.stages.find((s) => s.key === p.stage) ?? stage()), n: p.n ?? '' });
  const sideOf = (s: Stage, u: { group: number }) => s.groups.find((gr) => gr.id === u.group)?.side ?? 1;

  // Kept in the browser between visits: the project, the pictures, the package form.
  const database = new Promise<IDBDatabase | null>((resolve) => {
    try {
      const request = indexedDB.open('srw64-level-editor', 1);
      request.onupgradeneeded = () => request.result.createObjectStore('kv');
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => resolve(null);
    } catch { resolve(null); }
  });
  const saved = $('[data-saved]');
  let saving: number | undefined;
  const fields = ['id', 'version', 'name-zh-Hans', 'name-en', 'name-ja', 'authors', 'license', 'description'];
  const fieldOf = (k: string) => root.querySelector<HTMLInputElement>(`[data-f="${k}"]`)!;
  function changed() {
    window.clearTimeout(saving);
    saved.textContent = '';
    saving = window.setTimeout(async () => {
      const db = await database;
      if (!db) return;
      // Only the pictures the project uses: a picture left behind by 新建 or a removed map stays in
      // memory for undo, not in the browser's store.
      const record = { project, pictures: Object.fromEntries([...pictures].filter(([name]) => ownMap(name)).map(([name, p]) => [name, p.blob])), form: Object.fromEntries(fields.map((k) => [k, fieldOf(k).value])) };
      const done = db.transaction('kv', 'readwrite');
      done.objectStore('kv').put(record, 'project');
      done.oncomplete = () => { saved.textContent = T.saved; };
    }, 600);
  }
  async function restore() {
    const db = await database;
    if (!db) return;
    const record: any = await new Promise((resolve) => { const r = db.transaction('kv').objectStore('kv').get('project'); r.onsuccess = () => resolve(r.result); r.onerror = () => resolve(null); });
    if (!record?.project?.stages?.length) return;
    Object.assign(project, record.project);
    for (const [name, blob] of Object.entries<Blob>(record.pictures ?? {})) try { pictures.set(name, { bitmap: await createImageBitmap(blob), blob }); } catch { /* a picture the browser no longer reads */ }
    project.maps = project.maps.filter((m) => pictures.has(m.name));
    for (const s of project.stages) if (typeof s.map === 'string' && !ownMap(s.map)) s.map = 20;
    for (const [k, v] of Object.entries<string>(record.form ?? {})) if (fieldOf(k)) fieldOf(k).value = v;
  }

  // --- Small builders ----------------------------------------------------------------------------
  const input = (value: string | number, change: (v: string) => void, props: Record<string, unknown> = {}) =>
    el('input', { value, ...props, onchange: (e: Event) => { change((e.target as HTMLInputElement).value); update(); } });
  const number = (value: number, change: (v: number) => void, low: number, high: number, props: Record<string, unknown> = {}) =>
    input(value, (v) => change(Math.max(low, Math.min(high, Math.trunc(Number(v)) || 0))), { type: 'number', min: low, max: high, class: 'num-in', ...props });
  const select = (options: [string, string][], value: string, change: (v: string) => void, props: Record<string, unknown> = {}) => {
    const node = el('select', { ...props, onchange: (e: Event) => { change((e.target as HTMLSelectElement).value); update(); } }, ...options.map(([v, label]) => el('option', { value: v }, label)));
    node.value = value;
    return node;
  };
  const field = (label: string, control: Node, hint?: string) => el('label', { class: 'field' }, label, control, hint ? el('span', { class: 'hint' }, hint) : null);
  const button = (label: string, click: () => void, cls = 'mini', props: Record<string, unknown> = {}) => el('button', { type: 'button', class: cls, ...props, onclick: click }, label);
  const act = (label: string, click: () => void, cls = 'mini') => button(label, () => { click(); update(); }, cls);
  const card = (on: boolean, click: () => void, ...kids: Node[]) => { const node = button('', click, `stage-card${on ? ' on' : ''}`); node.append(...kids); return node; };
  const dialog = el('dialog', { class: 'pick-dialog' }) as HTMLDialogElement;
  root.append(dialog);
  // Pick a unit or a pilot from the Library's lists.
  function choose(kind: 'unit' | 'pilot', done: (id: number) => void) {
    const list = picker(kind === 'unit' ? units : pilots, series, lang, { allSeries: T.allSeries, search: T.search, none: T.none }, (id) => { dialog.close(); done(id); update(); });
    dialog.replaceChildren(el('div', { class: 'pick-head' }, el('strong', {}, kind === 'unit' ? T.pickUnit : T.pickPilot), button(T.close, () => dialog.close())), list.node);
    dialog.showModal();
  }
  const who = (kind: 'unit' | 'pilot', id: number, done: (id: number) => void) =>
    button(`${kind === 'unit' ? unitName(id) : pilotName(id)} ▾`, () => choose(kind, done), 'pick-button');
  const face = (src?: string | null) => src ? el('img', { src, alt: '' }) : el('span', { class: 'blank' });

  // --- The map canvas, shared by the map and the units tabs ----------------------------------
  const canvas = el('canvas', {}) as HTMLCanvasElement;
  const g = canvas.getContext('2d')!;
  const loaded = new Map<string, HTMLImageElement>();
  const image = (src: string) => { let i = loaded.get(src); if (!i) { i = new Image(); i.onload = draw; i.src = src; loaded.set(src, i); } return i.complete && i.naturalWidth ? i : null; };
  let drag: { x: number; y: number } | null = null, hoverCell: { x: number; y: number } | null = null;
  function draw() {
    if (tab !== 'map' && tab !== 'units') return;
    const s = stage(), { columns, rows } = size(s), own = ownMap(s.map);
    canvas.width = columns * cell; canvas.height = rows * cell;
    g.imageSmoothingEnabled = true;
    if (own) g.drawImage(tab === 'map' && compare ? compare : pictures.get(own.name)!.bitmap, 0, 0, canvas.width, canvas.height);
    else { const picture = image(`/gen/maps/${s.map}.png`); if (picture) g.drawImage(picture, 0, 0, canvas.width, canvas.height); }
    const tinted = tab === 'map' && overlay && !compare;
    for (let y = 0; y < rows; ++y) for (let x = 0; x < columns; ++x) {
      const t = terrainAt(s, x, y);
      if (t === 99) { g.fillStyle = 'rgba(0,0,0,.5)'; g.fillRect(x * cell, y * cell, cell, cell); }
      else if (tinted) { g.globalAlpha = .42; g.fillStyle = kindOf.get(t)?.colour ?? '#888'; g.fillRect(x * cell, y * cell, cell, cell); g.globalAlpha = 1; }
    }
    if (grid) {
      g.strokeStyle = 'rgba(255,255,255,.2)'; g.lineWidth = 1; g.beginPath();
      for (let x = 0; x <= columns; ++x) { g.moveTo(x * cell + .5, 0); g.lineTo(x * cell + .5, canvas.height); }
      for (let y = 0; y <= rows; ++y) { g.moveTo(0, y * cell + .5); g.lineTo(canvas.width, y * cell + .5); }
      g.stroke();
    }
    if (tab === 'map' && tool === 'rect' && drag && hoverCell) {
      const x0 = Math.min(drag.x, hoverCell.x), y0 = Math.min(drag.y, hoverCell.y), x1 = Math.max(drag.x, hoverCell.x), y1 = Math.max(drag.y, hoverCell.y);
      g.strokeStyle = '#ffd75e'; g.lineWidth = 2; g.strokeRect(x0 * cell + 1, y0 * cell + 1, (x1 - x0 + 1) * cell - 2, (y1 - y0 + 1) * cell - 2);
    }
    if (tab === 'units' && areaPick) {
      const a = drag && hoverCell ? [Math.min(drag.x, hoverCell.x) - FRAME, Math.min(drag.y, hoverCell.y) - FRAME, Math.max(drag.x, hoverCell.x) - FRAME, Math.max(drag.y, hoverCell.y) - FRAME] : areaPick.area;
      g.fillStyle = 'rgba(255,215,94,.28)'; g.fillRect((a[0] + FRAME) * cell, (a[1] + FRAME) * cell, (a[2] - a[0] + 1) * cell, (a[3] - a[1] + 1) * cell);
      g.strokeStyle = '#ffd75e'; g.lineWidth = 2; g.strokeRect((a[0] + FRAME) * cell + 1, (a[1] + FRAME) * cell + 1, (a[2] - a[0] + 1) * cell - 2, (a[3] - a[1] + 1) * cell - 2);
    }
    if (tab === 'units') s.units.forEach((u, n) => {
      const side = sideOf(s, u), x = (u.x + FRAME) * cell, y = (u.y + FRAME) * cell;
      const icon = unitOf.get(u.unit)?.icon, picture = icon === undefined ? null : image(`/gen/icons/${icon}${side === 0 ? '' : '-1011'}.png`);
      g.globalAlpha = u.group === group ? 1 : .55;
      g.imageSmoothingEnabled = false;
      if (picture) g.drawImage(picture, x, y, cell, cell); else { g.fillStyle = SIDES[side]; g.fillRect(x + 3, y + 3, cell - 6, cell - 6); }
      g.globalAlpha = 1;
      g.strokeStyle = n === picked ? '#ffffff' : SIDES[side]; g.lineWidth = n === picked ? 3 : 1.5; g.strokeRect(x + 1, y + 1, cell - 2, cell - 2);
    });
  }
  const cellAt = (e: MouseEvent) => { const box = canvas.getBoundingClientRect(); return { x: Math.floor((e.clientX - box.left) / box.width * canvas.width / cell), y: Math.floor((e.clientY - box.top) / box.height * canvas.height / cell) }; };
  const inside = (own: OwnMapInfo, x: number, y: number) => x >= FRAME && y >= FRAME && x < own.columns - FRAME && y < own.rows - FRAME;
  canvas.addEventListener('mousedown', (e) => {
    const s = stage(), at = cellAt(e), own = ownMap(s.map);
    if (tab === 'units' && areaPick) { drag = at; draw(); return; }
    if (tab === 'units') {
      const hit = s.units.findIndex((u) => u.x === at.x - FRAME && u.y === at.y - FRAME);
      const { columns, rows } = size(s);
      const free = at.x >= FRAME && at.y >= FRAME && at.x < columns - FRAME && at.y < rows - FRAME;
      if (hit >= 0) { picked = hit; group = s.units[hit].group; placing = false; }
      else if (placing && free) {
        const last = [...s.units].reverse().find((u) => u.group === group), side = s.groups.find((gr) => gr.id === group)?.side ?? 1;
        s.units.push({ group, x: at.x - FRAME, y: at.y - FRAME, pilot: last?.pilot ?? (side === 0 ? 28 : 298), unit: last?.unit ?? (side === 0 ? 36 : 239), level: last?.level ?? 0, upgrade: last?.upgrade ?? 0 });
        picked = s.units.length - 1;
      } else if (picked >= 0 && free) { s.units[picked].x = at.x - FRAME; s.units[picked].y = at.y - FRAME; }
      update();
      return;
    }
    if (!own || compare || !inside(own, at.x, at.y)) return;
    if (tool === 'pick' || e.altKey) { brush = own.terrain[at.y][at.x]; tool = 'brush'; update(); return; }
    if (tool === 'fill') {
      const from = own.terrain[at.y][at.x], queue = [at];
      if (from !== brush) while (queue.length) {
        const c = queue.pop()!;
        if (!inside(own, c.x, c.y) || own.terrain[c.y][c.x] !== from) continue;
        own.terrain[c.y][c.x] = brush;
        queue.push({ x: c.x + 1, y: c.y }, { x: c.x - 1, y: c.y }, { x: c.x, y: c.y + 1 }, { x: c.x, y: c.y - 1 });
      }
      update();
      return;
    }
    drag = at;
    if (tool === 'brush') { own.terrain[at.y][at.x] = brush; draw(); }
  });
  // Undo and redo of every edit, from the keyboard (not while typing in a field: the field's own).
  window.addEventListener('keydown', (e) => {
    if (!(e.ctrlKey || e.metaKey) || (e.target as HTMLElement)?.closest?.('input, textarea, select')) return;
    const key = e.key.toLowerCase();
    if (key === 'z') { e.preventDefault(); history(!e.shiftKey); }
    else if (key === 'y') { e.preventDefault(); history(false); }
  });
  const hover = el('p', { class: 'hint hover' }, ' ');
  canvas.addEventListener('mousemove', (e) => {
    const s = stage(), at = cellAt(e), { columns, rows } = size(s), own = ownMap(s.map);
    hoverCell = at;
    if (at.x >= 0 && at.y >= 0 && at.x < columns && at.y < rows) {
      const t = terrainOf.get(terrainAt(s, at.x, at.y))!;
      hover.textContent = t.id === 99 ? T.hoverFrame : fill(T.hover, { x: at.x - FRAME, y: at.y - FRAME, id: t.id, name: nameOf(t.name), stats: stats(t) });
    }
    if (tab === 'units' && areaPick && drag) { draw(); return; }
    if (tab !== 'map' || !drag || !own) return;
    if (tool === 'brush' && inside(own, at.x, at.y) && own.terrain[at.y][at.x] !== brush) own.terrain[at.y][at.x] = brush;
    draw();
  });
  window.addEventListener('mouseup', () => {
    if (!drag) return;
    if (tab === 'units' && areaPick && hoverCell) {
      const { columns, rows } = size(stage()), clamp = (v: number, top: number) => Math.max(0, Math.min(top - 2 * FRAME - 1, v - FRAME));
      areaPick.area.splice(0, 4, clamp(Math.min(drag.x, hoverCell.x), columns), clamp(Math.min(drag.y, hoverCell.y), rows),
        clamp(Math.max(drag.x, hoverCell.x), columns), clamp(Math.max(drag.y, hoverCell.y), rows));
      areaPick.done?.(areaPick.area);
      tab = areaPick.back; areaPick = null; drag = null; update();
      return;
    }
    const own = ownMap(stage().map);
    if (own && tab === 'map' && tool === 'rect' && hoverCell)
      for (let y = Math.min(drag.y, hoverCell.y); y <= Math.max(drag.y, hoverCell.y); ++y) for (let x = Math.min(drag.x, hoverCell.x); x <= Math.max(drag.x, hoverCell.x); ++x)
        if (inside(own, x, y)) own.terrain[y][x] = brush;
    drag = null;
    update();
  });
  const board = (...bar: (Node | null)[]) => el('div', { class: 'board' }, el('div', { class: 'toolbar' }, ...bar,
    el('span', { class: 'spacer' }),
    el('label', { class: 'check' }, el('input', { type: 'checkbox', checked: grid, onchange: (e: Event) => { grid = (e.target as HTMLInputElement).checked; draw(); } }), T.gridLines),
    el('label', { class: 'zoom' }, T.zoom, el('input', { type: 'range', min: 12, max: 44, step: 2, value: cell, oninput: (e: Event) => { cell = Number((e.target as HTMLInputElement).value); draw(); } }))),
    el('div', { class: 'scroll' }, canvas), hover);

  // --- Pictures: a map of the campaign's own ---------------------------------------------------
  // The scale a picture is cut at: 4, 2 or 1 times the game's pixels, the largest it is big
  // enough for and that keeps the cut picture within MAX_PIXELS.
  const scaleFor = (width: number, columns: number, rows: number) => [4, 2, 1].find((k) => columns * 16 * k <= width * 1.01 && columns * rows * 256 * k * k <= MAX_PIXELS) ?? 1;
  async function importPicture(file: Blob) {
    const s = stage();
    let bitmap: ImageBitmap;
    try { bitmap = await createImageBitmap(file); } catch { window.alert(T.errPicture); return; }
    const k = [4, 2, 1].find((n) => bitmap.width / (16 * n) >= 20 && bitmap.height / (16 * n) >= 15) ?? 1;
    const clamp = (v: number, low: number) => Math.max(low, Math.min(128, Math.round(v)));
    const columns = clamp(bitmap.width / (16 * k), 20), rows = clamp(bitmap.height / (16 * k), 15);
    const like = typeof s.map === 'number' && templates.has(s.map) ? s.map : 20;
    const from = original.maps[like], same = from.columns === columns && from.rows === rows;
    // The terrain starts as the template's where the two are the same size, else as plain.
    const terrain = Array.from({ length: rows }, (_, y) => Array.from({ length: columns }, (_, x) => {
      const t = same ? parseInt(from.terrain.substr((y * columns + x) * 2, 2), 16) : 0;
      return t > 98 ? 0 : t;
    }));
    const map: OwnMapInfo = { name: freeName('map', project.maps.map((m) => m.name)), like, columns, rows, scale: scaleFor(bitmap.width, columns, rows), terrain };
    project.maps.push(map);
    pictures.set(map.name, { bitmap, blob: file });
    s.map = map.name;
    compare = null;
    update();
  }
  // A first guess at every playable cell's terrain: the terrain whose usual colour on the
  // original's ground maps (editor.json terrain_colours) is nearest the cell's mean colour.
  function guess(own: OwnMapInfo) {
    // Common terrains a unit can enter only: a guessed wall would shut the map off.
    const colours = Object.entries(original.terrain_colours ?? {}).map(([id, c]) => ({ id: Number(id), c }))
      .filter((t) => (t.c[3] ?? 0) >= 1000 && !terrainOf.get(t.id)?.move.every((m) => m === 255));
    if (!colours.length) return;
    const small = new OffscreenCanvas(own.columns, own.rows), c = small.getContext('2d')!;
    c.imageSmoothingEnabled = true; c.imageSmoothingQuality = 'high';
    c.drawImage(pictures.get(own.name)!.bitmap, 0, 0, own.columns, own.rows);
    const pixels = c.getImageData(0, 0, own.columns, own.rows).data;
    for (let y = FRAME; y < own.rows - FRAME; ++y) for (let x = FRAME; x < own.columns - FRAME; ++x) {
      const at = (y * own.columns + x) * 4;
      let best = colours[0], far = Infinity;
      for (const t of colours) {
        const d = (t.c[0] - pixels[at]) ** 2 + (t.c[1] - pixels[at + 1]) ** 2 + (t.c[2] - pixels[at + 2]) ** 2;
        if (d < far) { far = d; best = t; }
      }
      own.terrain[y][x] = best.id;
    }
    update();
  }
  function resize(own: OwnMapInfo, columns: number, rows: number) {
    const old = own.terrain;
    own.terrain = Array.from({ length: rows }, (_, y) => Array.from({ length: columns }, (_, x) => old[y]?.[x] ?? 0));
    own.columns = columns; own.rows = rows;
    own.scale = scaleFor(pictures.get(own.name)!.bitmap.width, columns, rows);
    compare = null;
  }
  // The picture as it is cut: stretched to the cells, `scale` times the game's pixels.
  function cut(own: OwnMapInfo) {
    const width = own.columns * 16 * own.scale, height = own.rows * 16 * own.scale;
    const surface = new OffscreenCanvas(width, height), c = surface.getContext('2d', { willReadFrequently: true })!;
    c.imageSmoothingQuality = 'high';
    c.drawImage(pictures.get(own.name)!.bitmap, 0, 0, width, height);
    return { surface, rgba: c.getImageData(0, 0, width, height).data, width, height };
  }
  const build = (own: OwnMapInfo) => { const { surface, rgba, width, height } = cut(own); return { surface, built: buildMap({ name: own.name, like: own.like, scale: own.scale, terrain: own.terrain }, templates.get(own.like), rgba, width, height) }; };
  const bytesOf = async (blob: Blob) => new Uint8Array(await blob.arrayBuffer());

  // --- Overview -------------------------------------------------------------------------------------
  function overview() {
    const list = found();
    const rail = el('aside', { class: 'rail' }, el('div', { class: 'rail-head' }, T.stages),
      ...project.stages.map((s, n) => { const bad = list.filter((p) => p.stage === s.key);
        return card(n === current, () => { current = n; update(); }, el('strong', {}, `${n + 1} · ${stageLabel(s)}`),
          el('span', { class: bad.length ? 'sub late' : 'sub' }, bad.length ? fill(T.stageProblems, { n: bad.length }) : `${n === 0 ? `${T.startStage} · ` : ''}${ownMap(s.map) ? `${T.mapOwn} ${s.map}` : `${T.mapOriginal} ${s.map}`}`)); }),
      act(T.addStage, () => { project.stages.push(newStage(freeName('stage', project.stages.map((s) => s.key)))); current = project.stages.length - 1; }, 'add'),
      el('p', { class: 'hint' }, T.stagesNote));
    // The flow: columns by distance from the first stage, what nothing leads to last.
    const depth = new Map<string, number>([[project.stages[0].key, 0]]);
    for (let grew = true; grew;) { grew = false; for (const s of project.stages) if (depth.has(s.key)) for (const t of leadsTo(s)) if (!depth.has(t) && project.stages.some((o) => o.key === t)) { depth.set(t, depth.get(s.key)! + 1); grew = true; } }
    const deepest = Math.max(...depth.values());
    const columns: Stage[][] = [];
    for (const s of project.stages) { const d = depth.get(s.key) ?? deepest + 1; (columns[d] ??= []).push(s); }
    const W = 190, H = 70, GX = 80, GY = 22, place = new Map<string, { x: number; y: number }>();
    columns.forEach((column, cx) => column?.forEach((s, cy) => place.set(s.key, { x: 24 + cx * (W + GX), y: 24 + cy * (H + GY) })));
    const height = 48 + Math.max(...columns.map((c) => c?.length ?? 0)) * (H + GY), width = 48 + (columns.length + 1) * (W + GX);
    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    svg.setAttribute('width', String(width)); svg.setAttribute('height', String(height)); svg.setAttribute('aria-hidden', 'true');
    const line = (x1: number, y1: number, x2: number, y2: number, colour: string) => { const l = document.createElementNS('http://www.w3.org/2000/svg', 'line');
      for (const [k, v] of Object.entries({ x1, y1, x2, y2, stroke: colour, 'stroke-width': 2 })) l.setAttribute(k, String(v)); svg.append(l); };
    const chart = el('div', { class: 'flow', style: `height:${height}px;min-width:${width}px` });
    chart.append(svg);
    project.stages.forEach((s, n) => {
      const at = place.get(s.key)!;
      for (const t of leadsTo(s)) { const to = place.get(t); if (to) line(at.x + W, at.y + H / 2, to.x, to.y + H / 2, '#3fd0ff'); }
      if (s.after.kind === 'ending') { line(at.x + W, at.y + H / 2, at.x + W + 36, at.y + H / 2, '#57e389'); chart.append(el('span', { class: 'flow-end', style: `left:${at.x + W + 36}px;top:${at.y + H / 2 - 14}px` }, T.ending)); }
      const node = button('', () => { current = n; update(); }, `flow-node${n === current ? ' on' : ''}`, { style: `left:${at.x}px;top:${at.y}px;width:${W}px;height:${H}px` });
      node.append(el('span', { class: 'sub' }, n === 0 ? T.startStage : fill(T.stageNumber, { n: n + 1 })), el('strong', {}, stageLabel(s)),
        el('span', { class: 'sub' }, fill(T.stageCounts, { player: s.units.filter((u) => sideOf(s, u) === 0).length, enemy: s.units.filter((u) => sideOf(s, u) === 1).length, events: s.events.length })));
      chart.append(node);
    });
    const s = stage();
    const go = (label: string, to: Tab) => act(label, () => { tab = to; });
    const main = el('main', { class: 'center' },
      el('h2', {}, T.flow), el('p', { class: 'note' }, T.flowNote), el('div', { class: 'flow-box' }, chart),
      el('div', { class: 'two' },
        el('section', { class: 'panel' }, el('h3', {}, T.checks), ...(list.length ? list.map((p) => el('p', { class: p.hard ? 'problem hard' : 'problem' }, el('span', {}, p.hard ? '✕' : '!'), problemText(p)))
          : [el('p', { class: 'problem ok' }, el('span', {}, '✓'), T.noProblems)])),
        el('section', { class: 'panel' }, el('h3', {}, T.varsTitle, el('span', { class: 'sub' }, T.varsNote)),
          ...(project.vars ?? []).map((v) => el('div', { class: 'row' }, input(v.name, (name) => { v.name = name.trim() || v.name; }, { 'aria-label': T.varsTitle }), el('span', { class: 'sub' }, `#${v.id}`),
            act('✕', () => { project.vars = project.vars!.filter((x) => x !== v); }, 'icon'))),
          el('div', { class: 'row' }, act(T.varAdd, () => { newVar(); }, 'chip')), el('p', { class: 'hint' }, T.varsHint)),
        el('section', { class: 'panel' }, el('h3', {}, fill(T.thisStage, { stage: stageLabel(s) })),
          el('div', { class: 'grid2' },
            field(T.stageTitle, input(s.title, (v) => { s.title = v.trim(); })),
            field(T.stageKey, input(s.key, (v) => { const old = s.key, key = v.trim(); if (!key) return;
              for (const o of project.stages) { if (o.after.kind === 'next' && o.after.stage === old) o.after.stage = key; if (o.after.kind === 'choice') o.after.stages = o.after.stages.map((t) => t === old ? key : t); }
              s.key = key; }), T.stageKeyHint)),
          el('div', { class: 'row' }, go(T.editMap, 'map'), go(T.editUnits, 'units'), go(T.editStory, 'story'), go(T.editRules, 'rules'),
            project.stages.length > 1 ? act(T.removeStage, () => { if (!window.confirm(fill(T.confirmRemove, { stage: stageLabel(s) }))) return; project.stages.splice(current, 1);
              for (const o of project.stages) if (leadsTo(o).includes(s.key)) o.after = { kind: 'ending' }; current = 0; }, 'mini danger') : null))));
    return [rail, main];
  }

  // --- Map --------------------------------------------------------------------------------------------
  function mapTab() {
    const s = stage(), own = ownMap(s.map);
    const pick = el('input', { type: 'file', accept: 'image/png,image/jpeg,image/webp', hidden: true, onchange: (e: Event) => { const file = (e.target as HTMLInputElement).files?.[0]; if (file) void importPicture(file); } });
    const importer = el('label', { class: 'mini file' }, own ? T.otherPicture : T.importPicture, pick);
    const step = (n: number, title: string, done: boolean, ...body: (Node | null)[]) => el('section', { class: 'step' }, el('h3', {}, el('span', { class: done ? 'dot done' : 'dot' }, String(n)), title), ...body);
    const spaceTag = (o: OriginalMap) => `${o.map} · ${o.columns - 2 * FRAME}×${o.rows - 2 * FRAME}${o.space ? ` · ${T.space}` : ''}${o.scenes?.length ? ` · ${usedIn(o.scenes)}` : ''}`;
    const left: Node[] = [];
    if (!own) {
      const m = original.maps[Number(s.map)];
      left.push(step(1, T.stepPicture, false, el('p', { class: 'note' }, T.mapOwnNote), importer),
        el('section', { class: 'step' }, el('h3', {}, T.mapOriginal),
          field(T.mapNumber, select(original.maps.map((o): [string, string] => [String(o.map), spaceTag(o)]), String(s.map), (v) => { s.map = Number(v); })),
          el('p', { class: 'note' }, fill(T.mapOriginalNote, { columns: m.columns - 2 * FRAME, rows: m.rows - 2 * FRAME })),
          project.maps.length ? field(T.useOwn, select([['', '—'], ...project.maps.map((o): [string, string] => [o.name, o.name])], '', (v) => { if (v) s.map = v; })) : null));
    } else {
      const bitmap = pictures.get(own.name)!.bitmap, counts = new Map<number, number>();
      for (let y = FRAME; y < own.rows - FRAME; ++y) for (let x = FRAME; x < own.columns - FRAME; ++x) counts.set(own.terrain[y][x], (counts.get(own.terrain[y][x]) ?? 0) + 1);
      const used = (k: Kind) => k.ids.reduce((sum, id) => sum + (counts.get(id) ?? 0), 0);
      const ordered = [...kinds].sort((a, b) => used(b) - used(a) || a.ids[0] - b.ids[0]);
      left.push(
        step(1, T.stepPicture, true, el('p', { class: 'note' }, `${own.name} · ${bitmap.width} × ${bitmap.height}`),
          el('div', { class: 'row' }, importer, act(T.useOriginal, () => { s.map = own.like; compare = null; }))),
        step(2, T.stepCut, true,
          el('div', { class: 'grid2' },
            field(T.columns, number(own.columns, (v) => resize(own, v, own.rows), 20, 128)),
            field(T.rows, number(own.rows, (v) => resize(own, own.columns, v), 15, 128))),
          el('p', { class: 'note' }, fill(T.cutNote, { px: 16 * own.scale, scale: own.scale, columns: own.columns - 2 * FRAME, rows: own.rows - 2 * FRAME }))),
        step(3, T.stepTerrain, false, el('p', { class: 'note' }, T.terrainNote),
          original.terrain_colours ? el('div', { class: 'row' }, act(T.guessTerrain, () => { if (window.confirm(T.guessConfirm)) guess(own); }), el('span', { class: 'hint' }, T.guessHint)) : null,
          el('div', { class: 'brushes' }, ...ordered.map((k) => { const node = button('', () => { brush = k.ids[0]; if (tool === 'pick') tool = 'brush'; update(); }, `brush${k.ids.includes(brush) ? ' on' : ''}`);
            node.append(el('span', { class: 'swatch', style: `background:${k.colour}` }), el('span', { class: 'brush-name' }, el('strong', {}, nameOf(k.t.name)), el('span', { class: 'sub' }, stats(k.t))),
              el('span', { class: 'sub' }, used(k) ? fill(T.cells, { n: used(k) }) : '')); return node; }))));
    }
    const tools: [typeof tool, string][] = [['brush', T.toolBrush], ['rect', T.toolRect], ['fill', T.toolFill], ['pick', T.toolPick]];
    const center = el('main', { class: 'center' }, board(
      own ? el('div', { class: 'segment' }, ...tools.map(([t, label]) => act(label, () => { tool = t; }, tool === t ? 'seg on' : 'seg'))) : null,
      el('label', { class: 'check' }, el('input', { type: 'checkbox', checked: overlay, onchange: (e: Event) => { overlay = (e.target as HTMLInputElement).checked; draw(); } }), T.showTerrain)));
    const right: Node[] = [el('h3', {}, T.thisMap)];
    if (own) {
      const note = el('p', { class: 'status', 'data-preview-note': true });
      right.push(
        field(T.mapName, input(own.name, (v) => { const name = v.trim(); if (!/^[a-z0-9_-]{1,32}$/.test(name) || ownMap(name)) return;
          for (const o of project.stages) if (o.map === own.name) o.map = name; pictures.set(name, pictures.get(own.name)!); own.name = name; }), T.stageKeyHint),
        field(T.mapLike, select([...templates.keys()].map((n): [string, string] => [String(n), spaceTag(original.maps[n])]), String(own.like), (v) => { own.like = Number(v); compare = null; }), T.mapLikeHint),
        original.maps[own.like].space ? el('p', { class: 'warn' }, T.spaceNote) : null,
        el('section', { class: 'panel' }, el('h3', {}, T.originalMode), el('p', { class: 'note' }, T.originalModeNote),
          button(compare ? T.backToPicture : T.previewOriginal, async () => {
            if (compare) { compare = null; update(); return; }
            say(note, T.working);
            await new Promise((r) => setTimeout(r, 30));
            try {
              const { built } = build(own);
              compare = await createImageBitmap(new ImageData(built.preview as never, built.columns * 16, built.rows * 16));
              const made = built.image as { tiles: number; kinds: number; psnr: number | null };
              previewNote = fill(made.kinds > made.tiles ? T.previewMerged : T.previewExact, { kinds: made.kinds, tiles: made.tiles, psnr: made.psnr ?? '∞' });
              update();
            } catch (error) { say(note, error instanceof CompileError ? error.message : String(error), true); }
          }), note));
      if (compare) say(note, previewNote);
    } else right.push(el('p', { class: 'note' }, T.originalMapNote));
    return [el('aside', { class: 'rail wide' }, ...left), center, el('aside', { class: 'inspector' }, ...right)];
  }
  let previewNote = '';

  // --- Units ------------------------------------------------------------------------------------------
  const sideName = (n: number) => [T.sidePlayer, T.sideEnemy, T.sideThird][n];
  const groupName = (gr: Group) => gr.name || `${sideName(gr.side)} ${gr.id}`;
  const groupLabel = (id: number) => { const gr = stage().groups.find((o) => o.id === id); return gr ? groupName(gr) : `#${id}`; };
  const SIDE_NAMES: Side[] = ['player', 'enemy', 'third'];
  const areaText = (a: Area) => `(${a[0]}, ${a[1]})–(${a[2]}, ${a[3]})`;
  const sideSelect = (value: Side, change: (v: Side) => void, sides: Side[] = SIDE_NAMES) => select(sides.map((k): [string, string] => [k, sideName(SIDE_NAMES.indexOf(k))]), value, (v) => change(v as Side));
  // An area: its corners as numbers, or picked on the map (the units tab, then back here).
  const areaFields = (a: Area): Node[] => [el('span', { class: 'sub' }, areaText(a)),
    act(T.pickArea, () => { areaPick = { area: a, back: tab }; tab = 'units'; }, 'mini')];
  // Someone, or anyone of a side (0).
  const whoOrSide = (whoId: number, side: Side, set: (whoId: number, side: Side) => void): Node[] => [
    select([['0', T.anyOfSide], ['1', T.somebody]], whoId ? '1' : '0', (v) => { set(v === '1' ? stage().units[0]?.pilot ?? 28 : 0, side); }),
    whoId ? who('pilot', whoId, (id) => { set(id, side); }) : sideSelect(side, (v) => { set(0, v); })];
  function whenText(w: When | GroupWhen): string {
    switch (w.kind) {
      case 'opening': return T.whenOpening;
      case 'event': return T.whenEvent;
      case 'spot': return T.whenSpot;
      case 'turn': return fill(T.whenTurn, { turn: w.turn, side: sideName(['player', 'enemy', 'third'].indexOf(w.side)) });
      case 'defeated': return fill(w.by ? T.whenDefeatedBy : T.whenDefeated, { who: pilotName(w.who), by: pilotName(w.by ?? 0) });
      case 'reach': return fill(T.whenReach, { who: w.who ? pilotName(w.who) : sideName(SIDE_NAMES.indexOf(w.side)), area: areaText(w.area) });
      case 'downed': return fill(T.whenDowned, { side: w.group >= 0 ? groupLabel(w.group) : sideName(SIDE_NAMES.indexOf(w.side)), n: w.atLeast });
      case 'group_gone': return fill(T.whenGroupGone, { group: groupLabel(w.group) });
      case 'turns_after': return fill(T.whenTurnsAfter, { event: stage().events.find((e) => e.id === w.event)?.name || w.event, n: w.turns });
      case 'hp_below': return fill(T.whenHp, { who: pilotName(w.who), percent: w.percent });
      case 'battle': return fill(w.moment === 'before' ? T.whenBattleBefore : T.whenBattleAfter, { a: pilotName(w.a), b: pilotName(w.b) });
      case 'enemies_gone': return T.whenGone;
      case 'remaining': return fill(T.whenRemaining, { side: sideName(SIDE_NAMES.indexOf(w.side)), n: w.atMost });
      case 'persuade': return fill(T.whenPersuade, { by: pilotName(w.by), target: pilotName(w.target) });
    }
  }
  // How a computer-run unit acts, and what it leaves (level-editor.md §5.4).
  function behaviour(s: Stage, u: Stage['units'][number]): Node[] {
    const ai = u.ai ?? { mode: 0, wait: 0 };
    const set = (change: Partial<typeof ai>) => { u.ai = { ...ai, ...change }; };
    const out: Node[] = [el('h3', {}, T.aiTitle),
      field(T.aiMode, select([0, 1, 2, 3, 4, 5].map((n): [string, string] => [String(n), T[`ai${n}`]]), String(ai.mode), (v) => {
        const mode = Number(v);
        set({ mode, target: mode === 2 || mode === 5 ? ai.target ?? s.units.find((o) => sideOf(s, o) === 0)?.pilot ?? 28 : undefined,
          x: mode === 3 || mode === 4 ? ai.x ?? u.x : undefined, y: mode === 3 || mode === 4 ? ai.y ?? u.y : undefined });
      }), T[`aiHint${ai.mode}`]),
      field(T.aiWait, number(ai.wait, (v) => set({ wait: v }), 0, 31), T.aiWaitHint)];
    if (ai.mode === 2 || ai.mode === 5) out.push(field(T.aiTarget, who('pilot', ai.target ?? 28, (id) => set({ target: id }))));
    if (ai.mode === 3 || ai.mode === 4) {
      const cell: Area = [ai.x ?? u.x, ai.y ?? u.y, ai.x ?? u.x, ai.y ?? u.y];
      out.push(field(T.aiTo, el('div', { class: 'row' }, el('span', { class: 'static' }, `(${cell[0]}, ${cell[1]})`),
        act(T.pickCell, () => { areaPick = { area: cell, back: 'units', done: (a) => set({ x: a[0], y: a[1] }) }; }, 'mini'))));
    }
    const parts = original.parts ?? [];
    out.push(field(T.drop, select([['', T.dropNone], ...parts.map((p): [string, string] => [String(p.id), nameOf(p)])], u.drop === undefined ? '' : String(u.drop),
      (v) => { u.drop = v === '' ? undefined : Number(v); })));
    return out;
  }
  function unitsTab() {
    const s = stage();
    if (!s.groups.some((gr) => gr.id === group)) group = s.groups[0].id;
    const left = el('aside', { class: 'rail wide' }, el('div', { class: 'rail-head' }, T.groups, el('span', { class: 'sub' }, T.groupsNote)),
      ...s.groups.map((gr) => card(gr.id === group, () => { group = gr.id; picked = -1; update(); },
        el('strong', {}, el('span', { class: 'side-dot', style: `background:${SIDES[gr.side]}` }), groupName(gr), el('span', { class: 'sub count' }, String(s.units.filter((u) => u.group === gr.id).length))),
        el('span', { class: gr.when.kind === 'opening' ? 'sub' : 'sub late' }, whenText(gr.when)))),
      act(T.addGroup, () => { const id = Math.max(...s.groups.map((gr) => gr.id)) + 1; s.groups.push({ id, name: '', side: 1, when: { kind: 'turn', turn: 3, side: 'enemy' } }); group = id; picked = -1; }, 'add'),
      el('p', { class: 'hint' }, T.groupsHint));
    const gr = s.groups.find((o) => o.id === group)!;
    const center = el('main', { class: 'center' }, areaPick
      ? board(el('strong', {}, T.areaPicking), act(T.cancel, () => { tab = areaPick!.back; areaPick = null; }))
      : board(el('div', { class: 'segment' },
        act(T.toolSelect, () => { placing = false; }, placing ? 'seg' : 'seg on'), act(fill(T.toolPlace, { group: groupName(gr) }), () => { placing = true; }, placing ? 'seg on' : 'seg'))));
    const right: Node[] = [el('h3', {}, T.thisGroup),
      el('div', { class: 'grid2' }, field(T.groupName, input(gr.name, (v) => { gr.name = v.trim(); })),
        field(T.side, select([['0', T.sidePlayer], ['1', T.sideEnemy], ['2', T.sideThird]], String(gr.side), (v) => { gr.side = Number(v) as 0 | 1 | 2; }))),
      el('p', { class: 'note' }, `${whenText(gr.when)} · `, el('a', { href: '#', onclick: (e: Event) => { e.preventDefault(); tab = 'rules'; update(); } }, T.changeWhen)),
      s.groups.length > 1 && !s.units.some((u) => u.group === gr.id) ? act(T.removeGroup, () => { s.groups = s.groups.filter((o) => o !== gr); }, 'mini danger') : null];
    const u = s.units[picked];
    if (u) {
      const t = terrainOf.get(terrainAt(s, u.x + FRAME, u.y + FRAME));
      right.push(el('h3', {}, T.thisUnit),
        el('div', { class: 'faces' }, face(unitOf.get(u.unit)?.img), face(pilotOf.get(u.pilot)?.img)),
        field(T.unit, who('unit', u.unit, (id) => { u.unit = id; })), field(T.pilot, who('pilot', u.pilot, (id) => { u.pilot = id; })),
        el('div', { class: 'grid2' },
          field(T.inGroup, select(s.groups.map((o): [string, string] => [String(o.id), groupName(o)]), String(u.group), (v) => { u.group = Number(v); group = u.group; })),
          field(T.level, number(u.level, (v) => { u.level = v; }, 0, 98), T.levelHint),
          field(T.upgrade, number(u.upgrade, (v) => { u.upgrade = v; }, 0, 15)),
          field(T.cellAt, el('span', { class: 'static' }, `(${u.x}, ${u.y}) ${nameOf(t?.name)}`)),
          field(T.entry, select([0, 1, 2, 3, 4, 5, 6, 7].map((n): [string, string] => [String(n), T[`entry${n}`]]), String(u.entry ?? 0), (v) => { u.entry = Number(v) || undefined; }))),
        ...(sideOf(s, u) === 0 ? [] : behaviour(s, u)),
        el('div', { class: 'row' }, act(T.duplicate, () => { placing = true; }), act(T.removeUnit, () => { s.units.splice(picked, 1); picked = -1; }, 'mini danger')));
    } else right.push(el('p', { class: 'note' }, T.unitsNote));
    return [left, center, el('aside', { class: 'inspector' }, ...right)];
  }

  // --- Story ------------------------------------------------------------------------------------------
  // 「原版「…」「…」」: the original's stages that use something.
  const usedIn = (scenes: number[]) => fill(T.placeUsed, { titles: scenes.slice(0, 3).map((n) => `「${nameOf(original.titles?.[String(n)])}」`).join('') });
  // Music by its title (the game's 49 songs, titles in Japanese everywhere); a number outside them kept as it is.
  const songSelect = (value: number, set: (v: number) => void) => {
    const songs = original.songs ?? [];
    return select([...(songs.some((s) => s.id === value) ? [] : [[String(value), `${value}`] as [string, string]]), ...songs.map((s): [string, string] => [String(s.id), `${s.id} · ${s.title}`])],
      String(value), (v) => set(Number(v)));
  };
  // Sound effects have no names: how often the original plays each, and where.
  const soundSelect = (value: number, set: (v: number) => void) => {
    const sounds = original.sounds ?? [];
    return select([...(sounds.some((s) => s.id === value) ? [] : [[String(value), `${value}`] as [string, string]]),
      ...sounds.map((s): [string, string] => [String(s.id), [String(s.id), fill(T.soundTimes, { n: s.times }), ...(s.scenes.length ? [usedIn(s.scenes)] : [])].join(' · ')])],
      String(value), (v) => set(Number(v)));
  };
  // A weapon from the picker: its unit's picture and series; `of` keeps it to one unit's.
  const weaponOf = new Map((original.weapons ?? []).map((w) => [w.id, w]));
  const weaponName = (id: number) => { const w = weaponOf.get(id); return w ? `${nameOf(w)}${w.unit !== null ? ` · ${unitName(w.unit)}` : ''}` : `#${id}`; };
  function chooseWeapon(of: number | null, done: (id: number) => void) {
    const items: Pickable[] = (original.weapons ?? []).filter((w) => of === null || w.unit === of).map((w) => {
      const u = w.unit === null ? undefined : unitOf.get(w.unit);
      return { id: w.id, name: { ja: w.ja, zh: w.zh, en: w.en, ko: w.ko }, series: u?.series ?? null, img: u?.img ?? null };
    });
    const list = picker(items, series, lang, { allSeries: T.allSeries, search: T.search, none: T.none }, (id) => { dialog.close(); done(id); update(); },
      { badge: (id) => { const w = weaponOf.get(id); return w?.unit !== null && w?.unit !== undefined ? unitName(w.unit) : null; } });
    dialog.replaceChildren(el('div', { class: 'pick-head' }, el('strong', {}, T.pickWeapon), button(T.close, () => dialog.close())), list.node);
    dialog.showModal();
  }
  const weaponButton = (id: number, of: number | null, done: (id: number) => void) => button(`${weaponName(id)} ▾`, () => chooseWeapon(of, done), 'pick-button');
  // Story variables by the author's names. New ones take numbers the original's scripts never
  // touch (60–99): 100–114 are the game's own switches, reset as a stage begins.
  const FREE_VARS = Array.from({ length: 40 }, (_, n) => 60 + n);
  const varName = (id: number) => project.vars?.find((v) => v.id === id)?.name;
  function newVar(): number | null {
    const id = FREE_VARS.find((n) => !project.vars?.some((v) => v.id === n));
    if (id === undefined) return null;
    (project.vars ??= []).push({ id, name: fill(T.varNew, { n: (project.vars?.length ?? 0) + 1 }) });
    return id;
  }
  const varSelect = (value: number, set: (v: number) => void) => select([
    ...(varName(value) === undefined ? [[String(value), `#${value}`] as [string, string]] : []),
    ...(project.vars ?? []).map((v): [string, string] => [String(v.id), v.name]), ['new', T.varAdd]], String(value),
    (v) => { if (v === 'new') { const id = newVar(); if (id !== null) set(id); } else set(Number(v)); });
  // A world map place: its region, and which of the original's stages show it.
  const placeSelect = (value: number, set: (v: number) => void) => select((original.places ?? []).map((p): [string, string] => [String(p.id),
    [String(p.id), nameOf(p.region), ...(p.scenes.length ? [usedIn(p.scenes)] : [])].join(' · ')]),
    String(value), (v) => set(Number(v)));
  const stepsOf = (s: Stage, at: string): Step[] | null => at === 'before' ? s.before : at === 'enter' ? s.enter : at === 'end' ? s.end : s.events.find((e) => e.id === at)?.steps ?? null;
  const STEP_KINDS: [Step['kind'], string][] = [['say', 'stepSay'], ['deploy', 'stepDeploy'], ['leave', 'stepLeave'], ['join', 'stepJoin'], ['bgm', 'stepBgm'], ['focus', 'stepFocus'], ['wait', 'stepWait'],
    ['fade', 'stepFade'], ['win', 'stepWin'], ['lose', 'stepLose'], ['if', 'stepIf'], ['else', 'stepElse'], ['end', 'stepEnd'], ['choose', 'stepChoose'], ['or', 'stepOr'], ['raw', 'stepRaw']];
  const blank = (kind: Step['kind'], s: Stage): Step => {
    const someone = s.units[0]?.pilot ?? 28;
    switch (kind) {
      case 'say': return { kind, speaker: someone, mode: 0, text: {} };
      case 'deploy': return { kind, group: s.groups[s.groups.length - 1].id };
      case 'leave': case 'focus': return { kind, who: someone };
      case 'join': return { kind, pilot: someone, unit: s.units[0]?.unit ?? 36 };
      case 'bgm': return { kind, n: 27 };
      case 'wait': return { kind, frames: 30 };
      case 'fade': return { kind, dir: 'in' };
      case 'raw': return { kind, op: '3D48', args: [] };
      case 'if': return { kind, conds: { any: false, list: [] } };
      case 'choose': return { kind, options: [{}, {}] };
      default: return { kind } as Step;
    }
  };
  // A further command's form (editor-commands.ts): a control a field.
  function commandFields(s: Stage, args: Record<string, unknown>, fields: Field[]): Node[] {
    const out: Node[] = [];
    for (const f of fields) {
      const set = (v: unknown) => { args[f.key] = v; };
      const label = el('span', { class: 'sub' }, T[`f_${f.key}`] ?? '');
      if (f.type === 'pilot') out.push(label, who('pilot', Number(args[f.key]), set));
      else if (f.type === 'unit') out.push(label, who('unit', Number(args[f.key]), set));
      else if (f.type === 'group') out.push(label, select(s.groups.map((gr): [string, string] => [String(gr.id), groupName(gr)]), String(args[f.key]), (v) => set(Number(v))));
      else if (f.type === 'bool') out.push(el('label', { class: 'check' }, el('input', { type: 'checkbox', checked: !!args[f.key], onchange: (e: Event) => { set((e.target as HTMLInputElement).checked); update(); } }), T[`f_${f.key}`] ?? f.key));
      else if (f.type === 'number') out.push(label, number(Number(args[f.key]), set, f.min, f.max, f.step ? { step: f.step } : {}));
      else if (f.type === 'select') out.push(label, select(f.options.map(([v, k]): [string, string] => [v, T[k] ?? v]), String(args[f.key]), set));
      else if (f.type === 'area') out.push(label, ...areaFields(args[f.key] as Area));
      else if (f.type === 'place') out.push(label, placeSelect(Number(args[f.key]), (v) => set(v)));
      else if (f.type === 'song') out.push(label, songSelect(Number(args[f.key]), (v) => set(v)));
      else if (f.type === 'sound') out.push(label, soundSelect(Number(args[f.key]), (v) => set(v)));
      else if (f.type === 'var') out.push(label, varSelect(Number(args[f.key]), (v) => set(v)));
      else if (f.type === 'weapon') out.push(label, weaponButton(Number(args[f.key]), fields.some((x) => x.type === 'unit') ? Number(args.unit) : null, (v) => set(v)));
      else if (f.type === 'battle') out.push(label, select((original.battles ?? []).map((b): [string, string] => [String(b.id),
        [String(b.id), pilotName(b.pilot), ...(b.scenes.length ? [fill(T.placeUsed, { titles: b.scenes.map((n) => `「${nameOf(original.titles?.[String(n)])}」`).join('') })] : [])].join(' · ')]),
        String(args[f.key]), (v) => set(Number(v))));
      else if (f.type === 'map') out.push(label, select(original.maps.map((m): [string, string] => [String(m.map), `${m.map}`]), String(args[f.key]), (v) => set(Number(v))));
      else if (f.type === 'xy') {
        const p = args[f.key] as { x: number; y: number };
        out.push(label, number(p.x, (v) => { p.x = v; }, 0, 127), number(p.y, (v) => { p.y = v; }, 0, 127));
      } else if (f.type === 'pos') {
        const p = args[f.key] as Pos;
        out.push(label, select([['cell', T.posCell], ['near', T.posNear]], 'near' in p ? 'near' : 'cell',
          (v) => set(v === 'near' ? { near: s.units[0]?.pilot ?? 28, dir: 'at', by: 1 } : { x: 0, y: 0 })));
        if ('near' in p) out.push(who('pilot', p.near, (id) => { p.near = id; }),
          select(['at', 'up', 'right', 'down', 'left'].map((d): [string, string] => [d, T[`dir_${d}`]]), p.dir, (v) => { p.dir = v as never; }),
          p.dir === 'at' ? null : number(p.by, (v) => { p.by = v; }, 1, 63), p.dir === 'at' ? null : el('span', { class: 'sub' }, T.cells));
        else {
          const cell: Area = [p.x, p.y, p.x, p.y];
          out.push(el('span', { class: 'static' }, `(${p.x}, ${p.y})`), act(T.pickCell, () => { areaPick = { area: cell, back: tab, done: (a) => { p.x = a[0]; p.y = a[1]; } }; tab = 'units'; }, 'mini'));
        }
      }
    }
    return out.filter((n): n is Node => n !== null);
  }
  // The fields of a trigger after its kind.
  function whenFields(w: When | GroupWhen): Node[] {
    const s = stage(), groups = (all: boolean) => select([...(all ? [['-1', T.anyGroup] as [string, string]] : []), ...s.groups.map((gr): [string, string] => [String(gr.id), groupName(gr)])],
      String('group' in w ? w.group : -1), (v) => { (w as { group: number }).group = Number(v); });
    switch (w.kind) {
      case 'reach': return [...whoOrSide(w.who, w.side, (id, side) => { w.who = id; w.side = side; }), el('span', {}, T.into), ...areaFields(w.area)];
      case 'downed': return [sideSelect(w.side, (v) => { w.side = v; }), groups(true), el('span', {}, T.atLeast), number(w.atLeast, (v) => { w.atLeast = v; }, 1, 99), el('span', {}, T.unitsWord)];
      case 'group_gone': return [groups(false)];
      case 'turns_after': return [select(s.events.map((e): [string, string] => [e.id, e.name || e.id]), w.event, (v) => { w.event = v; }),
        el('span', {}, T.afterEvent), number(w.turns, (v) => { w.turns = v; }, 1, 99), el('span', {}, T.turnsLater), sideSelect(w.side, (v) => { w.side = v; })];
      case 'turn': return [number(w.turn, (v) => { w.turn = v; }, 1, 99), select([['player', T.sidePlayer], ['enemy', T.sideEnemy], ['third', T.sideThird]], w.side, (v) => { w.side = v as never; })];
      case 'defeated': return [who('pilot', w.who, (id) => { w.who = id; }), el('span', {}, T.by),
        select([['0', T.anyone], ...[...new Set(s.units.map((u) => u.pilot))].map((p): [string, string] => [String(p), pilotName(p)])], String(w.by ?? 0), (v) => { w.by = Number(v) || undefined; })];
      case 'hp_below': return [who('pilot', w.who, (id) => { w.who = id; }), number(w.percent, (v) => { w.percent = v; }, 1, 100), el('span', {}, '%')];
      case 'battle': return [who('pilot', w.a, (id) => { w.a = id; }), who('pilot', w.b, (id) => { w.b = id; }), select([['before', T.before], ['after', T.after]], w.moment, (v) => { w.moment = v as never; })];
      case 'remaining': return [sideSelect(w.side, (v) => { w.side = v; }), number(w.atMost, (v) => { w.atMost = v; }, 0, 99)];
      case 'persuade': return [who('pilot', w.by, (id) => { w.by = id; }), el('span', {}, '→'), who('pilot', w.target, (id) => { w.target = id; })];
      default: return [];
    }
  }
  const whenDefault = (kind: string, s: Stage): When => {
    const ours = s.units.find((u) => sideOf(s, u) === 0)?.pilot ?? 28, theirs = s.units.find((u) => sideOf(s, u) === 1)?.pilot ?? 298;
    const last = s.groups[s.groups.length - 1].id;
    if (kind === 'reach') return { kind, who: ours, side: 'player', area: [0, 0, 2, 2] };
    if (kind === 'downed') return { kind, side: 'player', group: -1, atLeast: 1 };
    if (kind === 'group_gone') return { kind, group: last };
    if (kind === 'turns_after') return { kind, event: s.events[0]?.id ?? '', turns: 2, side: 'player' };
    return kind === 'turn' ? { kind, turn: 3, side: 'enemy' } : kind === 'defeated' ? { kind, who: theirs } : kind === 'hp_below' ? { kind, who: theirs, percent: 30 }
      : kind === 'battle' ? { kind, a: ours, b: theirs, moment: 'before' } : kind === 'remaining' ? { kind, side: 'enemy', atMost: 3 } : kind === 'persuade' ? { kind, by: ours, target: theirs } : { kind: 'enemies_gone' };
  };
  const WHEN_KINDS = ['turn', 'defeated', 'hp_below', 'battle', 'enemies_gone', 'remaining', 'persuade', 'reach', 'downed', 'group_gone', 'turns_after'];

  // --- Conditions: one editor for an event's and a step's ---------------------------------------
  const COND_KINDS: Cond['kind'][] = ['turn', 'on_map', 'defeated', 'absent', 'hp', 'count', 'in_area', 'downed', 'fired', 'roster', 'route', 'choice', 'funds', 'var', 'level', 'opponent', 'we_struck'];
  function condDefault(kind: Cond['kind'], s: Stage, self?: GameEvent): Cond {
    const ours = s.units.find((u) => sideOf(s, u) === 0)?.pilot ?? 28, theirs = s.units.find((u) => sideOf(s, u) === 1)?.pilot ?? 298;
    switch (kind) {
      case 'turn': return { kind, from: 1, to: 5 };
      case 'on_map': case 'defeated': case 'absent': return { kind, who: theirs };
      case 'hp': return { kind, who: ours, below: 50 };
      case 'count': return { kind, side: 'enemy', atMost: 3 };
      case 'in_area': return { kind, who: ours, side: 'player', area: [0, 0, 2, 2] };
      case 'downed': return { kind, side: 'player', atLeast: 1 };
      case 'fired': return { kind, event: s.events.find((e) => e !== self)?.id ?? '' };
      case 'roster': return { kind, pilot: ours };
      case 'route': return { kind, route: project.route };
      case 'choice': return { kind, n: 1 };
      case 'funds': return { kind, atLeast: 10000 };
      case 'var': return { kind, n: project.vars?.[0]?.id ?? newVar() ?? 60, is: 1 };
      case 'level': return { kind, who: ours, of: theirs };
      case 'opponent': return { kind, who: ours, is: theirs };
      case 'we_struck': return { kind, value: true };
    }
  }
  function condFields(c: Cond, s: Stage, self?: GameEvent): Node[] {
    switch (c.kind) {
      case 'turn': return [number(c.from, (v) => { c.from = v; }, 1, 99), el('span', {}, '–'), number(c.to, (v) => { c.to = v; }, 1, 99), el('span', {}, T.turnWord)];
      case 'on_map': case 'defeated': case 'absent': return [who('pilot', c.who, (id) => { c.who = id; })];
      case 'hp': return [who('pilot', c.who, (id) => { c.who = id; }), el('span', {}, '<'), number(c.below, (v) => { c.below = v; }, 1, 100), el('span', {}, '%')];
      case 'count': return [sideSelect(c.side, (v) => { c.side = v; }), el('span', {}, '≤'), number(c.atMost, (v) => { c.atMost = v; }, 0, 99)];
      case 'in_area': return [...whoOrSide(c.who, c.side, (id, side) => { c.who = id; c.side = side; }), ...areaFields(c.area)];
      case 'downed': return [sideSelect(c.side, (v) => { c.side = v; }), el('span', {}, T.atLeast), number(c.atLeast, (v) => { c.atLeast = v; }, 1, 99), el('span', {}, T.unitsWord)];
      case 'fired': return [select(s.events.filter((e) => e !== self).map((e): [string, string] => [e.id, e.name || e.id]), c.event, (v) => { c.event = v; })];
      case 'roster': return [who('pilot', c.pilot, (id) => { c.pilot = id; })];
      case 'route': return [select([['0', T.route0], ['1', T.route1], ['2', T.route2], ['3', T.route3]], String(c.route), (v) => { c.route = Number(v); })];
      case 'choice': return [select([['1', '1'], ['2', '2'], ['3', '3']], String(c.n), (v) => { c.n = Number(v); })];
      case 'funds': return [el('span', {}, '≥'), number(c.atLeast, (v) => { c.atLeast = v; }, 0, 99999999)];
      case 'var': return [varSelect(c.n, (v) => { c.n = v; }), el('span', {}, '='), number(c.is, (v) => { c.is = v; }, 0, 3)];
      case 'level': return [who('pilot', c.who, (id) => { c.who = id; }), el('span', {}, '≥'), who('pilot', c.of, (id) => { c.of = id; })];
      case 'opponent': return [who('pilot', c.who, (id) => { c.who = id; }), el('span', {}, T.fights), who('pilot', c.is, (id) => { c.is = id; })];
      case 'we_struck': return [select([['1', T.struckYes], ['0', T.struckNo]], c.value ? '1' : '0', (v) => { c.value = v === '1'; })];
    }
  }
  function condEditor(set: Conds, s: Stage, self?: GameEvent, hint?: string): HTMLElement {
    return el('div', { class: 'conds' },
      set.list.length > 1 ? el('div', { class: 'segment' }, act(T.allOf, () => { set.any = false; }, set.any ? 'seg' : 'seg on'), act(T.oneOf, () => { set.any = true; }, set.any ? 'seg on' : 'seg')) : null,
      ...set.list.map((c, n) => el('div', { class: 'when cond' },
        el('label', { class: 'check' }, el('input', { type: 'checkbox', checked: !!c.not, onchange: (e: Event) => { c.not = (e.target as HTMLInputElement).checked || undefined; update(); } }), T.notWord),
        select(COND_KINDS.filter((k) => k !== 'fired' || s.events.some((e) => e !== self)).map((k): [string, string] => [k, T[`cond_${k}`]]), c.kind, (v) => { set.list[n] = condDefault(v as Cond['kind'], s, self); }),
        ...condFields(c, s, self), act('✕', () => { set.list.splice(n, 1); }, 'icon'))),
      el('div', { class: 'row' }, act(T.addCond, () => { set.list.push(condDefault('turn', s, self)); }, 'chip'), hint ? el('span', { class: 'hint' }, hint) : null));
  }
  // An event's conditions: the older "fired / not fired" pair becomes two of them.
  function eventConds(e: GameEvent): Conds {
    if (!e.conds) {
      e.conds = { any: false, list: [...e.fired.map((id): Cond => ({ kind: 'fired', event: id })), ...e.notFired.map((id): Cond => ({ kind: 'fired', event: id, not: true }))] };
      e.fired = []; e.notFired = [];
    }
    return e.conds;
  }
  // One battle line (level-editor.md §5.7): who says it, against whom, when, its text.
  function battleLineView(s: Stage, line: BattleLine): Node[] {
    const opponents: [string, string][] = [['0', T.anyone], ...[...new Set(s.units.map((u) => u.pilot))].filter((p) => p !== line.who).map((p): [string, string] => [String(p), pilotName(p)])];
    const area = el('textarea', { rows: 2, placeholder: T.linePlaceholder, onchange: (e: Event) => { line.text[language] = (e.target as HTMLTextAreaElement).value; update(); } }) as HTMLTextAreaElement;
    area.value = line.text[language] ?? '';
    const written = LOCALES.map(([code, label]) => `${label} ${line.text[code]?.trim() ? '✓' : '—'}`).join('　');
    return [el('div', { class: 'row' }, el('h2', {}, T.battleLine),
        act(T.removeLine, () => { s.battleLines = (s.battleLines ?? []).filter((b) => b !== line); eventAt = 'enter'; }, 'mini danger')),
      el('div', { class: 'when-box' },
        el('div', { class: 'when' }, el('span', { class: 'sub' }, T.quoteWho), who('pilot', line.who, (id) => { line.who = id; }),
          el('span', { class: 'sub' }, T.quoteAgainst), select(opponents, String(line.against), (v) => { line.against = Number(v); })),
        el('div', { class: 'when' }, el('span', { class: 'sub' }, T.quoteWhen), ...Object.keys(QUOTE_MOMENTS).map((m) => el('label', { class: 'check' },
          el('input', { type: 'checkbox', checked: line.when.includes(m), onchange: (e: Event) => {
            line.when = (e.target as HTMLInputElement).checked ? [...line.when, m] : line.when.filter((x) => x !== m); update(); } }), T[`moment_${m}`]))),
        el('span', { class: 'sub' }, T.andLabel), condEditor(line.conds ??= { any: false, list: [] }, s),
        el('label', { class: 'check' }, el('input', { type: 'checkbox', checked: line.once, onchange: (e: Event) => { line.once = (e.target as HTMLInputElement).checked; update(); } }), T.quoteOnce)),
      el('div', { class: 'row' }, el('span', { class: 'sub' }, T.lineLanguage), el('div', { class: 'segment' }, ...LOCALES.map(([code, label]) => act(label, () => { language = code; }, language === code ? 'seg on' : 'seg')))),
      el('div', { class: 'say' }, face(pilotOf.get(line.who)?.img), el('div', { class: 'say-body' }, el('span', { class: 'sub langs-done' }, written), area)),
      el('p', { class: 'hint' }, T.battleLineHint)];
  }
  function storyTab() {
    const s = stage();
    const quoteAt = eventAt.startsWith('q:') ? (s.battleLines ?? []).find((b) => `q:${b.id}` === eventAt) : undefined;
    if (!stepsOf(s, eventAt) && !quoteAt) eventAt = 'enter';
    const steps = stepsOf(s, eventAt) ?? [], event = s.events.find((e) => e.id === eventAt);
    const says = (list: Step[]) => list.filter((x) => x.kind === 'say').length;
    const item = (at: string, title: string, sub: string, late = false) => card(eventAt === at, () => { eventAt = at; stepAt = -1; update(); }, el('strong', {}, title), el('span', { class: late ? 'sub late' : 'sub' }, sub));
    const left = el('aside', { class: 'rail wide' }, el('div', { class: 'rail-head' }, T.events),
      el('div', { class: 'rail-part' }, T.partOpening), item('before', T.eventBefore, fill(T.lines, { n: says(s.before) })), item('enter', T.eventEnter, fill(T.lines, { n: says(s.enter) })),
      el('div', { class: 'rail-part' }, T.partBattle), ...s.events.map((e) => item(e.id, e.name || T.unnamed, whenText(e.when), true)),
      act(T.addEvent, () => { const id = freeName('event', s.events.map((e) => e.id)); s.events.push({ id, name: '', when: whenDefault('turn', s), once: true, fired: [], notFired: [], steps: [] }); eventAt = id; stepAt = -1; }, 'add'),
      el('div', { class: 'rail-part' }, T.partEnd), item('end', T.eventEnd, fill(T.lines, { n: says(s.end) })),
      el('div', { class: 'rail-part' }, T.partQuotes), ...(s.battleLines ?? []).map((b) => item(`q:${b.id}`, pilotName(b.who), b.when.map((m) => T[`moment_${m}`]).join('、') || '—', true)),
      act(T.addBattleLine, () => {
        const id = freeName('line', (s.battleLines ?? []).map((b) => b.id));
        (s.battleLines ??= []).push({ id, who: s.units.find((u) => sideOf(s, u) === 0)?.pilot ?? 28, against: 0, when: ['attack'], text: {}, once: false });
        eventAt = `q:${id}`;
      }, 'add'));
    if (quoteAt) return [left, el('main', { class: 'center' }, ...battleLineView(s, quoteAt)), el('aside', { class: 'inspector' }, el('p', { class: 'hint' }, T.battleLineNote))];
    const move = (from: number, to: number) => { steps.splice(to, 0, ...steps.splice(from, 1)); stepAt = to; };
    const box = el('div', { class: 'game-box' }), about = el('p', { class: 'note' });
    const rowNodes: HTMLElement[] = [];
    let depth = 0;
    // The flat markers' partners: the if or choose a step at `n` sits in (-1 none), the end
    // that closes the block opened at `n`, a choose's last `or`, which branch an `or` begins.
    const opens = (k: Step['kind']) => k === 'if' || k === 'choose';
    function opener(n: number): number {
      for (let i = n - 1, d = 0; i >= 0; --i) { const k = steps[i].kind; if (k === 'end') ++d; else if (opens(k)) { if (!d) return i; --d; } }
      return -1;
    }
    function closing(n: number): number {
      for (let i = n + 1, d = 0; i < steps.length; ++i) { const k = steps[i].kind; if (opens(k)) ++d; else if (k === 'end') { if (!d) return i; --d; } }
      return -1;
    }
    const sameLevel = (head: number, to: number) => steps.slice(head + 1, to).map((x, i) => [x, head + 1 + i] as const).filter(([x, i]) => x.kind === 'or' && opener(i) === head).map(([, i]) => i);
    const branchOf = (head: number, at: number) => sameLevel(head, at + 1).indexOf(at) + 2;
    const lastOr = (head: number, close: number) => sameLevel(head, close).at(-1) ?? -1;
    function preview() {
      const at = steps[stepAt]?.kind === 'say' ? steps[stepAt] as Say : steps.find((x) => x.kind === 'say') as Say | undefined;
      rowNodes.forEach((row, n) => row.classList.toggle('on', n === stepAt));
      if (!at) { box.replaceChildren(el('p', { class: 'sub' }, T.noLine)); about.textContent = ''; return; }
      const words = at.text[language]?.trim() || at.text['zh-Hans']?.trim() || at.text.ja?.trim() || at.text.en?.trim() || '';
      box.replaceChildren(el('div', { class: 'game-line' }, el('div', { class: 'game-text' }, el('span', { class: 'game-name' }, pilotName(at.speaker)), el('span', {}, words || T.linePlaceholder)), face(pilotOf.get(at.speaker)?.img)));
      about.textContent = [...words].length > 200 ? T.lineTooLong : fill(T.lineLength, { n: [...words].length });
    }
    steps.forEach((step, n) => {
      const body: Node[] = [];
      if (step.kind === 'say') {
        const written = LOCALES.map(([code, label]) => `${label} ${step.text[code]?.trim() ? '✓' : '—'}`).join('　');
        const area = el('textarea', { rows: 2, placeholder: T.linePlaceholder, onchange: (e: Event) => { step.text[language] = (e.target as HTMLTextAreaElement).value; update(); },
          onfocus: () => { if (stepAt !== n) { stepAt = n; preview(); } } }) as HTMLTextAreaElement;
        area.value = step.text[language] ?? '';
        const dark = step.silhouette && SILHOUETTES[step.speaker] !== undefined;
        const picture = face(pilotOf.get(step.speaker)?.img);
        if (dark && picture instanceof HTMLImageElement) picture.style.filter = 'brightness(0) opacity(.75)';
        body.push(el('div', { class: 'say' }, picture,
          el('div', { class: 'say-body' }, el('div', { class: 'row' }, who('pilot', step.speaker, (id) => { step.speaker = id; }),
            select([0, 1, 2, 3, 4, 5].map((m): [string, string] => [String(m), fill(T.boxMode, { n: m })]), String(step.mode), (v) => { step.mode = Number(v); }),
            SILHOUETTES[step.speaker] !== undefined ? el('label', { class: 'check', title: T.silhouetteHint }, el('input', { type: 'checkbox', checked: !!step.silhouette,
              onchange: (e: Event) => { step.silhouette = (e.target as HTMLInputElement).checked || undefined; update(); } }), T.silhouette) : null,
            el('span', { class: 'sub langs-done' }, written)), area)));
      } else if (step.kind === 'deploy') body.push(select(s.groups.map((gr): [string, string] => [String(gr.id), `${groupName(gr)}（${s.units.filter((u) => u.group === gr.id).length}）`]), String(step.group), (v) => { step.group = Number(v); }));
      else if (step.kind === 'leave' || step.kind === 'focus') body.push(who('pilot', step.who, (id) => { step.who = id; }));
      else if (step.kind === 'join') body.push(who('pilot', step.pilot, (id) => { step.pilot = id; }), who('unit', step.unit, (id) => { step.unit = id; }));
      else if (step.kind === 'bgm') body.push(songSelect(step.n, (v) => { step.n = v; }));
      else if (step.kind === 'wait') body.push(number(step.frames, (v) => { step.frames = v; }, 1, 32767), el('span', { class: 'sub' }, T.frames));
      else if (step.kind === 'fade') body.push(select([['in', T.fadeIn], ['out', T.fadeOut]], step.dir, (v) => { step.dir = v as never; }));
      else if (step.kind === 'if') body.push(condEditor(step.conds, s, event, T.stepIfHint));
      else if (step.kind === 'choose') {
        const close = closing(n);
        body.push(...step.options.map((o, k) => el('div', { class: 'row' }, el('span', { class: 'sub' }, fill(T.optionN, { n: k + 1 })),
          input(o[language] ?? '', (v) => { o[language] = v; }, { placeholder: LOCALES.find(([c]) => c === language)?.[1] ?? '', 'aria-label': fill(T.optionN, { n: k + 1 }) }))),
          el('div', { class: 'row' }, step.options.length < 3 && close >= 0 ? act(T.addThird, () => { step.options.push({}); steps.splice(close, 0, { kind: 'or' }); }, 'chip') : null,
            step.options.length > 2 && close >= 0 ? act(T.dropThird, () => {
              // The third branch goes with its text: the steps from the last `or` to the end.
              const last = lastOr(n, close); step.options.pop(); if (last >= 0) steps.splice(last, close - last);
            }, 'chip') : null),
          el('p', { class: 'hint' }, T.chooseHint),
          el('span', { class: 'sub' }, fill(T.whenPicked, { n: 1, text: step.options[0]?.[language] || step.options[0]?.['zh-Hans'] || step.options[0]?.ja || '…' })));
      } else if (step.kind === 'or') {
        const head = opener(n), k = head >= 0 ? branchOf(head, n) : 0, o = head >= 0 ? (steps[head] as Extract<Step, { kind: 'choose' }>).options[k - 1] : undefined;
        body.push(el('span', { class: 'sub' }, fill(T.whenPicked, { n: k, text: o?.[language] || o?.['zh-Hans'] || o?.ja || '…' })));
      }
      else if (step.kind === 'cmd') body.push(el('div', { class: 'when' }, ...commandFields(s, step.args, commandOf(step.name)?.fields ?? [])),
        T[`cmdHint_${step.name}`] ? el('p', { class: 'hint' }, T[`cmdHint_${step.name}`]) : null);
      else if (step.kind === 'raw') body.push(input(step.op, (v) => { step.op = v.trim().toUpperCase(); }, { class: 'op-in', spellcheck: 'false', 'aria-label': T.stepRaw }),
        input(step.args.join(' '), (v) => { step.args = v.split(/[\s,]+/).filter(Boolean).map(Number).filter(Number.isFinite); }, { placeholder: T.rawArgs, spellcheck: 'false', 'aria-label': T.rawArgs }));
      if (step.kind === 'else' || step.kind === 'end' || step.kind === 'or') depth = Math.max(0, depth - 1);
      const indent = depth;
      if (step.kind === 'if' || step.kind === 'else' || step.kind === 'choose' || step.kind === 'or') ++depth;
      const kindLabel = step.kind === 'cmd' ? T[`cmd_${step.name}`] ?? step.name : step.kind === 'end' && steps[opener(n)]?.kind === 'choose' ? T.stepEndChoose
        : T[STEP_KINDS.find(([k]) => k === step.kind)![1]];
      rowNodes.push(el('div', { class: `step-row k-${step.kind}`, style: indent ? `margin-left:${indent * 22}px` : '', onclick: () => { if (stepAt !== n) { stepAt = n; preview(); } } },
        el('span', { class: 'step-n' }, String(n + 1)), el('span', { class: 'step-kind' }, kindLabel), el('div', { class: 'step-body' }, ...body),
        el('span', { class: 'step-tools' }, button('↑', () => { move(n, n - 1); update(); }, 'icon', { 'aria-label': T.moveUp, disabled: n === 0 }),
          button('↓', () => { move(n, n + 1); update(); }, 'icon', { 'aria-label': T.moveDown, disabled: n === steps.length - 1 }),
          button('✕', () => { steps.splice(n, 1); stepAt = -1; update(); }, 'icon', { 'aria-label': T.remove }))));
    });
    const head: Node[] = [];
    if (event) {
      head.push(el('div', { class: 'row' }, input(event.name, (v) => { event.name = v.trim(); }, { class: 'title-in', placeholder: T.eventName, 'aria-label': T.eventName }),
        act(T.removeEvent, () => {
          s.events = s.events.filter((e) => e !== event);
          const keep = (c: Cond) => c.kind !== 'fired' || c.event !== event.id;
          for (const e of s.events) { e.fired = e.fired.filter((id) => id !== event.id); e.notFired = e.notFired.filter((id) => id !== event.id); if (e.conds) e.conds.list = e.conds.list.filter(keep);
            for (const x of e.steps) if (x.kind === 'if') x.conds.list = x.conds.list.filter(keep); }
          eventAt = 'enter';
        }, 'mini danger')),
        el('div', { class: 'when-box' }, el('span', { class: 'sub' }, T.whenLabel),
          el('div', { class: 'when' }, select(WHEN_KINDS.map((k): [string, string] => [k, T[`kind_${k}`]]), event.when.kind, (v) => { event.when = whenDefault(v, s); }), ...whenFields(event.when)),
          el('span', { class: 'sub' }, T.andLabel), condEditor(eventConds(event), s, event),
          el('label', { class: 'check' }, el('input', { type: 'checkbox', checked: event.once, onchange: (e: Event) => { event.once = (e.target as HTMLInputElement).checked; update(); } }), T.once)));
    } else head.push(el('h2', {}, eventAt === 'before' ? T.eventBefore : eventAt === 'enter' ? T.eventEnter : T.eventEnd),
      el('p', { class: 'note' }, eventAt === 'before' ? T.beforeNote : eventAt === 'enter' ? T.enterNote : T.endNote),
      eventAt === 'enter' ? null : field(T.placeLabel, placeSelect(eventAt === 'before' ? s.place ?? 0 : s.endPlace ?? 1,
        (v) => { if (eventAt === 'before') s.place = v; else s.endPlace = v; update(); }), T.placeHint));
    const center = el('main', { class: 'center' }, ...head,
      el('div', { class: 'row' }, el('span', { class: 'sub' }, T.lineLanguage), el('div', { class: 'segment' }, ...LOCALES.map(([code, label]) => act(label, () => { language = code; }, language === code ? 'seg on' : 'seg')))),
      ...rowNodes,
      el('div', { class: 'add-step' }, el('span', { class: 'sub' }, T.addStep), ...STEP_KINDS.filter(([k]) => event || (k !== 'win' && k !== 'lose'))
        .filter(([k]) => k !== 'else' && k !== 'end' && k !== 'or')
        .map(([k, label]) => act(T[label], () => {
          const at = stepAt >= 0 ? stepAt + 1 : steps.length;
          // An if comes with its else and end; anything else goes after the selected step.
          const added = k === 'if' ? [blank('if', s), { kind: 'else' } as Step, { kind: 'end' } as Step]
            : k === 'choose' ? [blank('choose', s), { kind: 'or' } as Step, { kind: 'end' } as Step] : [blank(k, s)];
          steps.splice(at, 0, ...added); stepAt = at;
        }, 'chip')),
        // The further commands, by kind, from one list.
        select([['', T.moreSteps], ...(['camera', 'show', 'unit', 'army', 'story', 'world'] as const).flatMap((g) =>
          COMMANDS.filter((c) => c.group === g).map((c): [string, string] => [c.name, `${T[`cmdGroup_${g}`]} · ${T[`cmd_${c.name}`] ?? c.name}`]))], '', (v) => {
          if (!v) return;
          const c = commandOf(v)!, at = stepAt >= 0 ? stepAt + 1 : steps.length;
          const args = defaults(c, s.units[0]?.pilot ?? 28, s.units[0]?.unit ?? 36, s.groups[s.groups.length - 1].id);
          for (const f of c.fields) if (f.type === 'var') args[f.key] = project.vars?.[0]?.id ?? newVar() ?? 60;
          // A weapon of the command's own unit to start with.
          for (const f of c.fields) if (f.type === 'weapon' && 'unit' in args) args[f.key] = original.weapons?.find((w) => w.unit === args.unit)?.id ?? args[f.key];
          steps.splice(at, 0, { kind: 'cmd', name: v, args });
          stepAt = at;
        })));
    preview();
    return [left, center, el('aside', { class: 'inspector' }, el('h3', {}, T.inGame), box, about, el('p', { class: 'hint' }, T.lineHint))];
  }

  // --- Rules ------------------------------------------------------------------------------------------
  function rulesTab() {
    const s = stage();
    // Three inputs, a language each, for one piece of the author's text.
    const three = (text: Record<string, string>, hint = '') => el('div', { class: 'grid3' }, ...LOCALES.map(([code, label]) =>
      input(text[code] ?? '', (v) => { text[code] = v; }, { placeholder: `${label}${hint}`, 'aria-label': label })));
    const rule = (label: (Node | null)[], remove: () => void) => el('div', { class: 'rule' }, el('div', { class: 'when' }, ...label), act('✕', remove, 'icon'));
    const within = (r: { within?: number }) => [el('span', { class: 'sub' }, T.within), number(r.within ?? 0, (v) => { r.within = v || undefined; }, 0, 99), el('span', { class: 'sub' }, T.withinHint)];
    const win = s.win.map((r, n) => rule([...(r.kind === 'enemies_gone' ? [el('strong', {}, T.whenGone)] : r.kind === 'defeated' ? [el('span', {}, T.ruleDefeat), who('pilot', r.who, (id) => { r.who = id; })]
      : r.kind === 'turn' ? [el('span', {}, T.ruleSurvive), number(r.turn, (v) => { r.turn = v; }, 2, 99), el('span', {}, T.turnWord)]
      : r.kind === 'reach' ? [select([['0', T.anyOfOurs], ['1', T.somebody]], r.who ? '1' : '0', (v) => { r.who = v === '1' ? s.units.find((u) => sideOf(s, u) === 0)?.pilot ?? 28 : 0; }),
          ...(r.who ? [who('pilot', r.who, (id) => { r.who = id; })] : []), el('span', {}, T.ruleReach), ...areaFields(r.area)]
      : r.kind === 'group_gone' ? [el('span', {}, T.ruleGroupGone), select(s.groups.map((gr): [string, string] => [String(gr.id), groupName(gr)]), String(r.group), (v) => { r.group = Number(v); })]
      : [el('span', {}, T.ruleRemaining), number(r.atMost, (v) => { r.atMost = v; }, 0, 99)]), ...(r.kind === 'turn' ? [] : within(r))], () => { s.win.splice(n, 1); }));
    const lose = s.lose.map((r, n) => rule(r.kind === 'defeated' ? [who('pilot', r.who, (id) => { r.who = id; }), el('span', {}, T.ruleLost)]
      : r.kind === 'reach' ? [el('span', {}, T.ruleEnemyReach), ...areaFields(r.area)]
      : r.kind === 'downed' ? [el('span', {}, T.ruleDowned), number(r.atLeast, (v) => { r.atLeast = v; }, 1, 99), el('span', {}, T.unitsWord)]
      : [el('span', {}, T.ruleTurnLimit), number(r.turn, (v) => { r.turn = v; }, 1, 98), el('span', {}, T.turnWord)], () => { s.lose.splice(n, 1); }));
    const leader = s.units.find((u) => sideOf(s, u) === 0)?.pilot ?? 28, boss = s.units.find((u) => sideOf(s, u) === 1)?.pilot ?? 298;
    const conditions = el('section', { class: 'panel' },
      el('h3', { class: 'good' }, T.winTitle, el('span', { class: 'sub' }, T.anyOf)), ...win,
      el('div', { class: 'row' }, s.win.some((r) => r.kind === 'enemies_gone') ? null : act(T.addGone, () => { s.win.push({ kind: 'enemies_gone' }); }, 'chip'), act(T.addDefeat, () => { s.win.push({ kind: 'defeated', who: boss }); }, 'chip'),
        act(T.addSurvive, () => { s.win.push({ kind: 'turn', turn: 6 }); }, 'chip'), act(T.addRemaining, () => { s.win.push({ kind: 'remaining', atMost: 3 }); }, 'chip'),
        act(T.addReach, () => { s.win.push({ kind: 'reach', who: 0, area: [0, 0, 2, 2] }); }, 'chip'),
        s.groups.length > 2 ? act(T.addGroupGone, () => { s.win.push({ kind: 'group_gone', group: s.groups[s.groups.length - 1].id }); }, 'chip') : null),
      el('h3', { class: 'bad' }, T.loseTitle, el('span', { class: 'sub' }, T.anyOfLose)), ...lose,
      el('div', { class: 'row' }, act(T.addLost, () => { s.lose.push({ kind: 'defeated', who: leader }); }, 'chip'), act(T.addTurnLimit, () => { s.lose.push({ kind: 'turn', turn: 12 }); }, 'chip'),
        act(T.addEnemyReach, () => { s.lose.push({ kind: 'reach', area: [0, 0, 2, 2] }); }, 'chip'), act(T.addDowned, () => { s.lose.push({ kind: 'downed', atLeast: 1 }); }, 'chip')),
      el('p', { class: 'hint' }, T.rulesHint));
    const groupKinds: [string, string][] = [['opening', T.whenOpening], ['turn', T.kind_turn], ['remaining', T.kind_remaining], ['defeated', T.kind_defeated], ['hp_below', T.kind_hp_below],
      ['reach', T.kind_reach], ['downed', T.kind_downed], ['group_gone', T.kind_group_gone], ['event', T.whenEvent], ['spot', T.whenSpot]];
    const arrivals = el('section', { class: 'panel' }, el('h3', {}, T.arrivals, el('span', { class: 'sub' }, T.arrivalsNote)),
      ...s.groups.map((gr) => { const count = s.units.filter((u) => u.group === gr.id).length;
        return el('div', { class: 'rule' }, el('span', { class: 'side-dot', style: `background:${SIDES[gr.side]}` }), el('strong', { class: 'rule-name' }, groupName(gr)),
          el('span', { class: count ? 'sub' : 'sub late' }, count ? fill(T.unitsCount, { n: count }) : T.noUnits),
          el('div', { class: 'when' }, select(groupKinds, gr.when.kind, (v) => { gr.when = v === 'opening' || v === 'event' || v === 'spot' ? { kind: v } : whenDefault(v, s) as GroupWhen; }), ...whenFields(gr.when))); }),
      el('p', { class: 'hint' }, T.arrivalsHint));
    const others = project.stages.filter((o) => o !== s).map((o): [string, string] => [o.key, stageLabel(o)]);
    const after = s.after;
    const afterBody: Node[] = [select([['ending', T.ending], ...(others.length ? [['next', T.afterNext] as [string, string]] : []), ...(others.length > 1 ? [['choice', T.afterChoice] as [string, string]] : [])], after.kind,
      (v) => { s.after = v === 'ending' ? { kind: 'ending' } : v === 'next' ? { kind: 'next', stage: others[0][0] } : { kind: 'choice', text: 18402, stages: [others[0][0], others[1][0]], own: [{}, {}] }; })];
    if (after.kind === 'next') afterBody.push(select(others, after.stage, (v) => { after.stage = v; }));
    if (after.kind === 'choice') {
      afterBody.push(el('label', { class: 'check' }, el('input', { type: 'checkbox', checked: !!after.own, onchange: (e: Event) => {
        after.own = (e.target as HTMLInputElement).checked ? after.stages.map(() => ({})) : undefined; update(); } }), T.choiceOwn));
      if (!after.own) afterBody.push(field(T.choiceText, number(after.text, (v) => { after.text = v; }, 0, 0xC7FF), T.choiceHint));
      after.stages.forEach((key, n) => {
        afterBody.push(field(fill(T.choiceOption, { n: n + 1 }), select(others, key, (v) => { after.stages[n] = v; })));
        if (after.own) afterBody.push(three(after.own[n] ??= {}, T.choiceOptionHint));
      });
      if (after.own && after.stages.length < 3 && others.length > after.stages.length)
        afterBody.push(act(T.addOption, () => { after.stages.push(others.find(([k]) => !after.stages.includes(k))![0]); after.own!.push({}); }, 'chip'));
    }
    // The 作戦目的 window: one of the original's winning and losing texts each.
    const shown = objectiveOf(s), texts = original.objectives ?? [];
    const objectiveSelect = (first: number, top: number, value: number, set: (n: number | undefined) => void, auto: boolean) =>
      select([['', T.objectiveAuto], ...texts.filter((o) => o.id >= first && o.id - first < top).map((o): [string, string] => [String(o.id - first), nameOf(o)])],
        auto ? '' : String(value), (v) => set(v === '' ? undefined : Number(v)));
    const own = s.objective?.own;
    const objective = own ? el('section', { class: 'panel' }, el('h3', {}, T.objectiveTitle, el('span', { class: 'sub' }, T.objectiveNote)),
      el('label', { class: 'check' }, el('input', { type: 'checkbox', checked: true, onchange: () => { s.objective = { ...s.objective, own: undefined }; update(); } }), T.objectiveOwn),
      field(T.objectiveWin, three(own.win)), field(T.objectiveLose, three(own.lose)), el('p', { class: 'hint' }, T.objectiveOwnHint))
    : el('section', { class: 'panel' }, el('h3', {}, T.objectiveTitle, el('span', { class: 'sub' }, T.objectiveNote)),
      el('label', { class: 'check' }, el('input', { type: 'checkbox', checked: false, onchange: () => {
        s.objective = { ...s.objective, own: { win: { [language]: nameOf(texts.find((o) => o.id === 5567 + shown.win)) }, lose: { [language]: nameOf(texts.find((o) => o.id === 5593 + shown.lose)) } } }; update(); } }), T.objectiveOwn),
      field(T.objectiveWin, objectiveSelect(5567, 26, shown.win, (n) => { s.objective = { ...s.objective, win: n }; }, s.objective?.win === undefined)),
      el('p', { class: 'note' }, nameOf(texts.find((o) => o.id === 5567 + shown.win))),
      field(T.objectiveLose, objectiveSelect(5593, 128, shown.lose, (n) => { s.objective = { ...s.objective, lose: n }; }, s.objective?.lose === undefined)),
      el('p', { class: 'note' }, nameOf(texts.find((o) => o.id === 5593 + shown.lose))),
      el('p', { class: 'hint' }, T.objectiveHint));
    const leads = el('section', { class: 'panel' }, el('h3', {}, T.afterTitle), ...afterBody, field(T.stageBgm, songSelect(s.bgm, (v) => { s.bgm = v; })));
    return [el('main', { class: 'center columns' }, conditions, arrivals, el('div', {}, objective, leads))];
  }

  // --- The frame: tabs, the stage, export ----------------------------------------------------
  const body = $('[data-body]'), tabsBox = $('[data-tabs]'), stageBox = $('[data-stage]'), overviewOnly = $('[data-overview-only]');
  // Undo: the project as it was before each change update() sees (the page's state, like the
  // tab, is not part of it). Kept in memory for this visit.
  const past: string[] = [], future: string[] = [];
  let last = '';
  const undoButton = $<HTMLButtonElement>('[data-undo]'), redoButton = $<HTMLButtonElement>('[data-redo]');
  function track() {
    const now = JSON.stringify(project);
    if (last && now !== last) { past.push(last); if (past.length > 200) past.shift(); future.length = 0; }
    last = now;
    undoButton.disabled = !past.length; redoButton.disabled = !future.length;
  }
  function history(back: boolean) {
    const from = back ? past : future, to = back ? future : past;
    if (!from.length) return;
    to.push(JSON.stringify(project));
    last = from.pop()!;
    const was: Project = JSON.parse(last);
    for (const k of Object.keys(project)) delete (project as Record<string, unknown>)[k];
    Object.assign(project, was);
    route.value = String(project.route);
    picked = -1; stepAt = -1; compare = null;
    if (!stepsOf(stage() ?? project.stages[0], eventAt)) eventAt = 'enter';
    update();
  }
  undoButton.addEventListener('click', () => history(true));
  redoButton.addEventListener('click', () => history(false));
  function update() {
    if (current >= project.stages.length) current = 0;
    tabsBox.replaceChildren(...(['overview', 'map', 'units', 'story', 'rules'] as Tab[]).map((t) => button(T[`tab_${t}`], () => { tab = t; compare = null; update(); }, tab === t ? 'tab on' : 'tab')));
    stageBox.replaceChildren(select(project.stages.map((s, n): [string, string] => [String(n), `${n + 1} · ${stageLabel(s)}`]), String(current),
      (v) => { current = Number(v); picked = -1; compare = null; eventAt = 'enter'; stepAt = -1; }, { 'aria-label': T.stages }));
    overviewOnly.hidden = tab !== 'overview';
    body.className = `editor-body t-${tab}`;
    body.replaceChildren(...(tab === 'overview' ? overview() : tab === 'map' ? mapTab() : tab === 'units' ? unitsTab() : tab === 'story' ? storyTab() : rulesTab()));
    draw();
    track();
    changed();
  }
  const route = $<HTMLSelectElement>('[data-route]');
  route.addEventListener('change', () => { project.route = Number(route.value); changed(); });
  for (const k of fields) fieldOf(k).addEventListener('change', changed);

  const status = $('[data-export-status]');
  async function compile() {
    const stages = Object.fromEntries(project.stages.map((s) => [s.key, stageSource(s)]));
    const used = project.maps.filter((m) => project.stages.some((s) => s.map === m.name));
    const files: Record<string, Uint8Array> = {};
    const json = (v: unknown) => strToU8(JSON.stringify(v) + '\n');
    const maps: Record<string, { like: number }> = {};
    for (const own of used) {
      const { surface, built } = build(own);
      maps[own.name] = { like: own.like };
      files[`campaign/maps/${own.name}/map.json`] = json(built.image);
      if (built.meta) {
        // The HD mode's: the picture as cut, and each of its pixels' palette index as a grey.
        const w = built.columns * 16, h = built.rows * 16, grey = new Uint8ClampedArray(w * h * 4);
        for (let p = 0; p < w * h; ++p) { grey[p * 4] = grey[p * 4 + 1] = grey[p * 4 + 2] = built.indices[p]; grey[p * 4 + 3] = 255; }
        const small = new OffscreenCanvas(w, h);
        small.getContext('2d')!.putImageData(new ImageData(grey, w, h), 0, 0);
        const index = new OffscreenCanvas(w * own.scale, h * own.scale), c = index.getContext('2d')!;
        c.imageSmoothingEnabled = false;
        c.drawImage(small, 0, 0, w * own.scale, h * own.scale);
        files[`campaign/maps/${own.name}/meta.json`] = json(built.meta);
        files[`campaign/maps/${own.name}/base.png`] = await bytesOf(await surface.convertToBlob({ type: 'image/png' }));
        files[`campaign/maps/${own.name}/index.png`] = await bytesOf(await index.convertToBlob({ type: 'image/png' }));
      }
    }
    return { stages, maps, files, json, used };
  }
  $('[data-export]').addEventListener('click', async () => {
    const made = form.manifest();
    if (typeof made === 'string') { say(status, made, true); tab = 'overview'; update(); return; }
    const hard = found().filter((p) => p.hard);
    if (hard.length) { say(status, hard.slice(0, 5).map(problemText).join('　'), true); return; }
    say(status, T.working);
    await new Promise((r) => setTimeout(r, 30));
    try {
      const { stages, maps, files, json, used } = await compile();
      const { campaign, story } = compileCampaign({ campaign: campaignSource(project, String(made.manifest.id), made.manifest.name, made.version), stages, maps });
      files['campaign/campaign.json'] = json(campaign);
      if (story) files['campaign/story.json'] = json(story);
      // The project rides along so the page can open the package again, with each map's
      // picture as it was brought in; the sources it wrote are there to read.
      files['campaign/source/editor.json'] = json({ schema: 'srw64.web-editor-project.v1', project: { ...project, maps: used } });
      for (const own of used) files[`campaign/source/maps/${own.name}/picture.png`] = await bytesOf(pictures.get(own.name)!.blob);
      for (const [key, source] of Object.entries(stages)) files[`campaign/source/stages/${key}.json`] = strToU8(JSON.stringify(source, null, 1) + '\n');
      (window as any).levelEditor.exported = { sizes: Object.fromEntries(Object.entries(files).map(([path, bytes]) => [path, bytes.length])), campaign, story };
      writePackage(made.manifest, made.version, files, status, T);
    } catch (error) { say(status, error instanceof CompileError ? error.message : fill(T.errRuntime, { detail: String(error) }), true); }
  });
  const opener = $<HTMLInputElement>('[data-import]'), importStatus = $('[data-import-status]');
  opener.addEventListener('change', async () => {
    const file = opener.files?.[0];
    opener.value = '';
    if (file) await openMod(file);
  });
  async function openMod(file: File) {
    try {
      const { mod, files } = await openPackage(file);
      const kept = readJson(files['campaign/source/editor.json']);
      if (kept?.schema !== 'srw64.web-editor-project.v1') throw new Error(T.errNotEditors);
      for (const m of kept.project.maps as OwnMapInfo[]) { const blob = new Blob([files[`campaign/source/maps/${m.name}/picture.png`] as BlobPart]); pictures.set(m.name, { bitmap: await createImageBitmap(blob), blob }); }
      for (const k of Object.keys(project)) delete (project as Record<string, unknown>)[k];
      Object.assign(project, kept.project);
      route.value = String(project.route);
      current = 0; picked = -1; tab = 'overview';
      form.fill(mod);
      say(importStatus, fill(T.imported, { id: mod.id }));
      say(status, fill(T.imported, { id: mod.id }));
      update();
    } catch (error) { say(importStatus, fill(T.errImport, { detail: String((error as Error).message ?? error) }), true); say(status, fill(T.errImport, { detail: String((error as Error).message ?? error) }), true); }
  }
  $('[data-new]').addEventListener('click', () => {
    if (!window.confirm(T.confirmNew)) return;
    project.route = 1; project.stages = [newStage('stage-1')]; project.maps = []; project.vars = []; current = 0; tab = 'overview'; route.value = '1'; update();
  });
  // The project file: the whole project as the page keeps it (every map and its picture, the
  // package form), saved whatever state it is in, unlike the package.
  const PROJECT = 'srw64.web-editor-project.v1';
  $('[data-save-project]').addEventListener('click', async () => {
    const files: Record<string, Uint8Array> = {
      'editor.json': strToU8(JSON.stringify({ schema: PROJECT, project, form: Object.fromEntries(fields.map((k) => [k, fieldOf(k).value])) }) + '\n') };
    for (const m of project.maps) { const p = pictures.get(m.name); if (p) files[`maps/${m.name}/picture.png`] = await bytesOf(p.blob); }
    const zip = zipSync(files, { level: 6 });
    const name = `${(fieldOf('id').value.trim() || 'campaign').replace(/[^\w.-]+/g, '_')}.srw64edit`;
    const url = URL.createObjectURL(new Blob([zip as BlobPart], { type: 'application/zip' }));
    const a = el('a', { href: url, download: name });
    document.body.append(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 60_000);
    say(status, fill(T.projectSaved, { file: name }));
  });
  const projectOpener = $<HTMLInputElement>('[data-open-project]');
  projectOpener.addEventListener('change', async () => {
    const file = projectOpener.files?.[0];
    projectOpener.value = '';
    if (!file) return;
    try {
      if (/\.srw64mod$/i.test(file.name)) { await openMod(file); return; }
      const files = unzipSync(new Uint8Array(await file.arrayBuffer()));
      const kept = readJson(files['editor.json']);
      if (kept?.schema !== PROJECT) throw new Error(T.errNotProject);
      for (const m of kept.project.maps as OwnMapInfo[]) {
        const bytes = files[`maps/${m.name}/picture.png`];
        if (bytes) { const blob = new Blob([bytes as BlobPart]); pictures.set(m.name, { bitmap: await createImageBitmap(blob), blob }); }
      }
      for (const k of Object.keys(project)) delete (project as Record<string, unknown>)[k];
      Object.assign(project, kept.project);
      project.maps = project.maps.filter((m) => pictures.has(m.name));
      for (const [k, v] of Object.entries<string>(kept.form ?? {})) if (fieldOf(k)) fieldOf(k).value = v;
      route.value = String(project.route);
      current = 0; picked = -1; tab = 'overview';
      say(status, fill(T.projectOpened, { file: file.name }));
      update();
    } catch (error) { say(status, fill(T.errImport, { detail: String((error as Error).message ?? error) }), true); }
  });

  await restore();
  route.value = String(project.route);
  // For checks from outside the page: the project, the sources it writes, a picture brought in.
  (window as any).levelEditor = { project, update, problems: found, sources: () => Object.fromEntries(project.stages.map((s) => [s.key, stageSource(s)])), importPicture, compile,
    go: (t: Tab) => { tab = t; update(); }, text: (bytes: Uint8Array) => strFromU8(bytes) };
  update();
}
