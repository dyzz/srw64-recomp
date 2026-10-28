// The map icon MOD maker (pages/[lang]/mods/icons.astro): art/icons/<unit>.png pixel art of
// 16-128 (native_icon.cpp) and art/icons.json for the pictures that do not look the way the
// original icon does, made in the browser. Names, portraits and anything else in an opened
// package are kept as they are.
import { dataUrl, frame, framer, type Picture } from './framer';
import { el, fill, jsonBytes, openPackage, packageForm, readJson, say, writePackage, type Text } from './mod-package';
import { picker, type Pickable } from './picker';

type Names3 = { ja: string; zh: string; en: string };
type Way = 'left' | 'right' | 'front';
type Unit = { id: number; name: Names3; icon: number; facing: Way; pose: string | null };
// One replaced icon: the unit it was picked for, its picture and the way that picture looks.
type Entry = { unit: number; pic: Picture | null; way: Way };

const ICON = 128, ICON_SIZES = [16, 32, 64, 128];
// The three ways the game draws an icon (unit_icon_art.hpp marked; the third party is left
// out): the player's, looking left; the enemy's, looking right with a bright and a dark ring;
// acted, the player's grey at 85%.
const SIDES = [{ left: true, palette: '' }, { left: false, palette: '-1011' }, { left: true, palette: '-1013' }];
const RING = [[222, 123, 123], [156, 25, 25]];

export function initIconMaker() {
  const root = document.querySelector<HTMLElement>('[data-icon-maker]');
  const payload = document.getElementById('icons-data');
  if (!root || !payload) return;
  const { text, units, pick, locale, lang } = JSON.parse(payload.textContent!) as {
    text: Text; units: Unit[]; pick: { units: Pickable[]; series: { id: number | null; name: string }[] }; locale: string; lang: 'zh' | 'en' | 'ja';
  };
  const form = packageForm(root, text, locale, text.game);
  const nameOf = (n: Names3) => n[lang] || n.ja;
  const unitById = new Map(units.map((u) => [u.id, u]));
  const byIcon = new Map<number, Unit[]>();
  for (const u of units) byIcon.set(u.icon, [...(byIcon.get(u.icon) ?? []), u]);
  // Keyed by icon resource: units that share an icon share the picture (native_icon.cpp), filed
  // under the unit it was picked for.
  const entries = new Map<number, Entry>();
  let kept: Record<string, Uint8Array> = {};
  let keptFacing: Record<string, string> = {};

  const box = root.querySelector<HTMLElement>('[data-icons]')!;
  const iconPicker = picker(pick.units, pick.series, lang, { allSeries: text.allSeries, search: text.search, none: text.none },
    (id) => { const u = unitById.get(id)!; if (!entries.has(u.icon)) entries.set(u.icon, { unit: id, pic: null, way: u.facing }); render(); },
    { badge: (id) => entries.get(unitById.get(id)?.icon ?? -1)?.pic ? '✓' : null });
  root.querySelector<HTMLElement>('[data-icon-pick]')!.replaceChildren(iconPicker.node);

  // A picture of the sizes the game takes goes in as it is; anything else is framed first.
  async function loadIcon(blob: Blob, bytes?: Uint8Array): Promise<Picture> {
    const source = await createImageBitmap(blob);
    const pic: Picture = { source, cx: source.width / 2, cy: source.height / 2, zoom: 1, png: new Uint8Array(), size: 0, url: '', framing: true };
    if (blob.type === 'image/png' && source.width === source.height && ICON_SIZES.includes(source.width)) {
      pic.png = bytes ?? new Uint8Array(await blob.arrayBuffer());
      pic.size = source.width; pic.url = dataUrl(pic.png); pic.framing = false;
    }
    return pic;
  }
  // The framed square at 128: pixels repeated when enlarging, averaged when shrinking.
  async function encodeIcon(pic: Picture) {
    const { side, x, y } = frame(pic);
    const canvas = document.createElement('canvas');
    canvas.width = canvas.height = ICON;
    const g = canvas.getContext('2d')!;
    g.imageSmoothingEnabled = side > ICON;
    g.imageSmoothingQuality = 'high';
    g.drawImage(pic.source, x, y, side, side, 0, 0, ICON, ICON);
    const blob: Blob = await new Promise((resolve, reject) => canvas.toBlob((b) => b ? resolve(b) : reject(new Error('png')), 'image/png'));
    pic.png = new Uint8Array(await blob.arrayBuffer());
    pic.size = ICON;
    pic.url = dataUrl(pic.png);
  }

  // One side's look of a picture: drawn at 128, marked when `mark` (the HD pack's and the
  // player's pictures; the original has a palette per side instead), and turned the way the
  // game turns it (unit_icon_art.hpp face): a picture looking the other way from its side is
  // mirrored, one facing front never.
  function look(src: string, side: number, way: Way, mark: boolean) {
    const canvas = el('canvas', { width: ICON, height: ICON }) as HTMLCanvasElement;
    const image = new Image();
    image.onload = () => {
      const work = document.createElement('canvas');
      work.width = work.height = ICON;
      const m = work.getContext('2d')!;
      m.imageSmoothingEnabled = false;
      m.drawImage(image, 0, 0, ICON, ICON);
      if (mark && side > 0) {
        const data = m.getImageData(0, 0, ICON, ICON), p = data.data;
        if (side === 2)
          for (let i = 0; i < p.length; i += 4) p[i] = p[i + 1] = p[i + 2] = Math.min(255, Math.floor((p[i] * 0.299 + p[i + 1] * 0.587 + p[i + 2] * 0.114) * 0.85));
        else
          for (const tone of RING) {
            const solid = (x: number, y: number) => x >= 0 && y >= 0 && x < ICON && y < ICON && p[(y * ICON + x) * 4 + 3] > 0;
            const ring: number[] = [];
            for (let y = 0; y < ICON; y++)
              for (let x = 0; x < ICON; x++)
                if (!solid(x, y) && (solid(x - 1, y) || solid(x + 1, y) || solid(x, y - 1) || solid(x, y + 1))) ring.push((y * ICON + x) * 4);
            for (const i of ring) { p[i] = tone[0]; p[i + 1] = tone[1]; p[i + 2] = tone[2]; p[i + 3] = 255; }
          }
        m.putImageData(data, 0, 0);
      }
      const g = canvas.getContext('2d')!;
      g.imageSmoothingEnabled = false;
      if (way !== 'front' && (way === 'left') !== SIDES[side].left) { g.translate(ICON, 0); g.scale(-1, 1); }
      g.drawImage(work, 0, 0);
    };
    image.onerror = () => canvas.closest('.icon-row')?.remove();
    image.src = src;
    return canvas;
  }
  const names = text.sides.split('|');
  const row = (label: string, make: (side: number) => HTMLCanvasElement) =>
    el('div', { class: 'icon-row' }, el('span', { class: 'row-label' }, label),
      ...SIDES.map((_, side) => el('figure', {}, make(side), el('figcaption', {}, names[side]))));

  function render() {
    box.replaceChildren(...[...entries].map(([icon, entry]) => {
      const u = unitById.get(entry.unit)!, pic = entry.pic;
      const others = byIcon.get(icon)!.filter((o) => o.id !== u.id);
      const file = el('input', { type: 'file', accept: 'image/*', hidden: true }) as HTMLInputElement;
      file.addEventListener('change', async () => {
        const f = file.files?.[0];
        if (!f) return;
        entry.pic = await loadIcon(f);
        render(); iconPicker.refresh();
      });
      const done = () => { render(); iconPicker.refresh(); };
      const original = { left: text.facingLeft, right: text.facingRight, front: text.facingFront }[u.facing];
      const head = el('h3', {}, u.pose ? el('img', { class: 'pose', src: u.pose, alt: '' }) : null, el('span', { class: 'num' }, String(u.id)), nameOf(u.name));
      const notes = [el('p', { class: 'hint' }, fill(text.iconFacing, { facing: original })),
        others.length ? el('p', { class: 'hint' }, fill(text.iconShared, { units: others.map((o) => `${o.id} ${nameOf(o.name)}`).join(lang === 'en' ? ', ' : '、') })) : null];
      const remove = el('button', { type: 'button', class: 'mini', onclick: () => { entries.delete(icon); done(); } }, text.remove);
      const choose = el('label', { class: 'mini' }, pic ? text.otherPicture : text.choosePicture, file);
      if (pic?.framing) return el('div', { class: 'card' }, head, ...notes, framer(pic, done, text, encodeIcon, false, text.iconFrameHint), el('div', {}, choose, ' ', remove));
      const way = el('select', {}, ...(['left', 'right', 'front'] as Way[]).map((w) =>
        el('option', { value: w, selected: w === entry.way }, text[{ left: 'wayLeft', right: 'wayRight', front: 'wayFront' }[w]]))) as HTMLSelectElement;
      way.addEventListener('change', () => { entry.way = way.value as Way; render(); });
      return el('div', { class: 'card' }, head, ...notes,
        el('div', { class: 'icon-rows' },
          row(text.rowOriginal, (side) => look(`/gen/icons/${icon}${SIDES[side].palette}.png`, side, u.facing, false)),
          row(text.rowHd, (side) => look(`/gen/icons-hd/${icon}.png`, side, u.facing, true)),
          pic ? row(text.rowYours, (side) => look(pic.url, side, entry.way, true)) : null),
        pic ? el('label', { class: 'icon-facing' }, text.myFacing, way) : null,
        pic ? el('p', { class: 'hint' }, text.facingNote) : null,
        pic ? el('span', { class: 'hint' }, pic.size === pic.source.width ? fill(text.iconAsIs, { size: pic.size }) : `${pic.size} × ${pic.size} PNG`) : null,
        el('div', {}, pic ? el('button', { type: 'button', class: 'mini', onclick: () => { pic.framing = true; render(); } }, text.reframe) : null,
          pic ? ' ' : null, choose, ' ', remove));
    }));
  }

  const exportStatus = root.querySelector<HTMLElement>('[data-export-status]')!;
  root.querySelector('[data-export]')!.addEventListener('click', () => {
    const m = form.manifest();
    if (typeof m === 'string') return say(exportStatus, m, true);
    const ready = [...entries.values()].filter((e) => e.pic);
    const problems = ready.filter((e) => e.pic!.framing).map((e) => fill(text.errIconFraming, { who: `${e.unit} ${nameOf(unitById.get(e.unit)!.name)}` }));
    if (problems.length) return say(exportStatus, problems.join('；'), true);
    if (!ready.length && !Object.keys(kept).length) return say(exportStatus, text.errEmpty, true);
    const out: Record<string, Uint8Array> = { ...kept };
    // Only the pictures that look another way than the original icon need saying.
    const facing: Record<string, string> = {};
    for (const [unit, way] of Object.entries(keptFacing)) if (!ready.some((e) => unitById.get(e.unit)!.icon === unitById.get(Number(unit))?.icon)) facing[unit] = way;
    for (const e of ready) {
      out[`art/icons/${e.unit}.png`] = e.pic!.png;
      if (e.way !== unitById.get(e.unit)!.facing) facing[String(e.unit)] = e.way;
    }
    if (Object.keys(facing).length) out['art/icons.json'] = jsonBytes({ schema: 'srw64.icons.v1', facing });
    writePackage(m.manifest, m.version, out, exportStatus, text);
  });

  const importStatus = root.querySelector<HTMLElement>('[data-import-status]')!;
  root.querySelector<HTMLInputElement>('[data-import]')!.addEventListener('change', async (ev) => {
    const input = ev.target as HTMLInputElement, file = input.files?.[0];
    input.value = '';
    if (!file) return;
    try {
      const { mod, files } = await openPackage(file);
      form.fill(mod);
      entries.clear(); kept = {}; keptFacing = {};
      const said: Record<string, string> = readJson(files['art/icons.json'])?.facing ?? {};
      for (const [path, bytes] of Object.entries(files)) {
        const iconFile = /^art\/icons\/(\d{1,3})\.png$/i.exec(path);
        if (path === 'mod.json' || path === 'art/icons.json') continue;
        if (iconFile && unitById.has(Number(iconFile[1]))) {
          const u = unitById.get(Number(iconFile[1]))!;
          const way = (['left', 'right', 'front'] as Way[]).find((w) => w === said[String(u.id)]) ?? u.facing;
          entries.set(u.icon, { unit: u.id, pic: await loadIcon(new Blob([bytes as BlobPart], { type: 'image/png' }), bytes), way });
        } else kept[path] = bytes;
      }
      // A way said for a unit without its picture here stays as it was.
      for (const [unit, way] of Object.entries(said)) if (!entries.has(unitById.get(Number(unit))?.icon ?? -1)) keptFacing[unit] = way;
      render(); iconPicker.refresh();
      say(importStatus, fill(text.imported, { id: mod.id ?? file.name }));
    } catch (error) {
      say(importStatus, fill(text.errImport, { detail: String((error as Error).message ?? error) }), true);
    }
  });

  render();
  addEventListener('beforeunload', (e) => { if (entries.size) e.preventDefault(); });
}
