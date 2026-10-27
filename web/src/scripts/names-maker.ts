// The name and portrait MOD maker (pages/[lang]/mods/names.astro): dialogue/names.json with
// the host's rules (src/host/mod_names.hpp) and art/portraits/<pilot>.png squared and scaled
// to a multiple of 96 (native_portrait.cpp), made in the browser.
import { el, fill, jsonBytes, openPackage, packageForm, readJson, say, search, writePackage, type Text } from './mod-package';

type Names3 = { ja: string; zh: string; en: string };
type Hero = { route: string; role: 'hero' | 'partner'; ja: string[]; zh: string[]; en: string[] };
type Person = { id: number; name: Names3; full: Names3; portrait: string | null; image: number | null };
type Named = { id: number; name: Names3 };
type Kind = 'pilots' | 'units' | 'weapons';
type Picture = { png: Uint8Array; size: number; url: string };

// The package's languages and the page language's column of the data.
const LOCALES = [['zh-Hans', 'zh', '中文'], ['en', 'en', 'English'], ['ja', 'ja', '日本語']] as const;
const HERO_FIELDS = ['name', 'surname', 'nickname'] as const;
const LONGEST = 16, LONGEST_RECORD = 24;
// The original's cells, as half cells (mod_names.hpp width): name and surname 7, nickname 5, unit 10.
const CELLS: Record<string, number> = { name: 14, surname: 14, nickname: 10, unit: 20 };

const width = (s: string) => [...s].reduce((w, c) => { const cp = c.codePointAt(0)!; return w + (cp < 0x2e80 || (cp >= 0xff61 && cp <= 0xff9f) ? 1 : 2); }, 0);
const badChars = (s: string) => s !== s.trim() || /[\u0000-\u001f\u007f<>{}　]/.test(s);

export function initNamesMaker() {
  const root = document.querySelector<HTMLElement>('[data-names]');
  const payload = document.getElementById('names-data');
  if (!root || !payload) return;
  const { text, heroes, routes, unitDefault, people, units, weapons, locale, lang } = JSON.parse(payload.textContent!) as {
    text: Text; heroes: Hero[]; routes: Record<string, string>; unitDefault: Names3; people: Person[]; units: Named[]; weapons: Named[];
    locale: string; lang: 'zh' | 'en' | 'ja';
  };
  const form = packageForm(root, text, locale, text.game);
  const lists: Record<Kind, Named[]> = { pilots: people, units, weapons };
  const byId = Object.fromEntries((Object.keys(lists) as Kind[]).map((k) => [k, new Map(lists[k].map((e) => [e.id, e]))])) as Record<Kind, Map<number, Named>>;
  const nameOf = (n: Names3) => n[lang] || n.ja;

  // The widest usual name of each field over the eight and the three languages (half cells).
  const widest: Record<string, number> = {};
  HERO_FIELDS.forEach((f, n) => { widest[f] = Math.max(...heroes.flatMap((h) => [h.ja[n], h.zh[n], h.en[n]].map(width))); });
  widest.unit = Math.max(...Object.values(unitDefault).map(width));

  // --- State --------------------------------------------------------------------------
  // heroes: person -> package locale -> field -> text; unit: locale -> text;
  // records: kind -> number -> locale -> { name, full } (units and weapons use name only).
  const heroNames = heroes.map(() => ({} as Record<string, Record<string, string>>));
  const unitName: Record<string, string> = {};
  const records: Record<Kind, Map<number, Record<string, { name: string; full: string }>>> = { pilots: new Map(), units: new Map(), weapons: new Map() };
  const pictures = new Map<number, Picture>();
  let kept: Record<string, Uint8Array> = {};
  let keptNames: Record<string, unknown> = {};

  const input = (value: string, placeholder: string, onchange: (v: string) => void) => {
    const box = el('input', { value, placeholder, autocomplete: 'off', spellcheck: 'false' }) as HTMLInputElement;
    box.addEventListener('change', () => { box.value = box.value.trim(); onchange(box.value); });
    return box;
  };
  const warning = (value: string, field: string) =>
    value && (width(value) > CELLS[field] || width(value) > widest[field]) ? text.warnWide : '';

  // --- The eight and the unit -----------------------------------------------------------
  const heroBox = root.querySelector<HTMLElement>('[data-heroes]')!;
  function renderHeroes() {
    heroBox.replaceChildren(...heroes.map((h, p) => {
      const rows: Node[] = [el('span', {}), ...HERO_FIELDS.map((f) => el('span', { class: 'head' }, text[f === 'name' ? 'given' : f]))];
      for (const [loc, col, label] of LOCALES) {
        const now = heroNames[p][loc] ?? {};
        const warn = el('span', { class: 'warn' });
        const check = () => { warn.textContent = HERO_FIELDS.map((f) => warning(heroNames[p][loc]?.[f] ?? '', f)).find(Boolean) ?? ''; };
        rows.push(el('span', { class: 'lang' }, label), ...HERO_FIELDS.map((f, n) => input(now[f] ?? '', h[col][n], (v) => {
          const e = { ...(heroNames[p][loc] ?? {}) };
          if (v) e[f] = v; else delete e[f];
          if (Object.keys(e).length) heroNames[p][loc] = e; else delete heroNames[p][loc];
          check();
        })), warn);
        check();
      }
      return el('div', { class: 'card' },
        el('h3', {}, `${routes[h.route]} · ${text[h.role]}`, el('span', { class: 'dim' }, h[lang][0] + ({ zh: '·', en: ' ', ja: '・' })[lang] + h[lang][1])),
        el('div', { class: 'langs' }, ...rows));
    }));
  }
  const unitBox = root.querySelector<HTMLElement>('[data-unit]')!;
  function renderUnit() {
    const warn = el('span', { class: 'warn' });
    const check = () => { warn.textContent = LOCALES.map(([loc]) => warning(unitName[loc] ?? '', 'unit')).find(Boolean) ?? ''; };
    unitBox.replaceChildren(el('div', { class: 'card' }, el('div', { class: 'langs' },
      ...LOCALES.flatMap(([loc, col, label]) => [el('span', { class: 'lang' }, label), input(unitName[loc] ?? '', unitDefault[col], (v) => {
        if (v) unitName[loc] = v; else delete unitName[loc];
        check();
      }), el('span', {}), el('span', {})]), warn)));
    check();
  }

  // --- Other names --------------------------------------------------------------------------
  let kind: Kind = 'pilots';
  const find = root.querySelector<HTMLInputElement>('[data-find]')!;
  const results = root.querySelector<HTMLElement>('[data-results]')!;
  const recordBox = root.querySelector<HTMLElement>('[data-records]')!;
  const kindLabel: Record<Kind, string> = { pilots: text.pilot, units: text.unit, weapons: text.weapon };
  function renderResults() {
    const found = search(lists[kind], find.value);
    results.replaceChildren(...found.map((e) => el('li', {}, el('button', { type: 'button', onclick: () => {
      if (!records[kind].has(e.id)) records[kind].set(e.id, {});
      find.value = ''; renderResults(); renderRecords();
    } }, el('span', { class: 'num' }, String(e.id)), nameOf(e.name)))));
    if (find.value.trim() && !found.length) results.replaceChildren(el('li', { class: 'hint' }, text.none));
  }
  find.addEventListener('input', renderResults);
  for (const b of root.querySelectorAll<HTMLButtonElement>('[data-kind]'))
    b.addEventListener('click', () => {
      kind = b.dataset.kind as Kind;
      for (const o of root.querySelectorAll('[data-kind]')) o.classList.toggle('on', o === b);
      renderResults();
    });
  function renderRecords() {
    const cards: HTMLElement[] = [];
    for (const k of ['pilots', 'units', 'weapons'] as Kind[])
      for (const [id, byLocale] of records[k]) {
        const e = byId[k].get(id)!, full = k === 'pilots' ? (e as Person).full : null;
        const rows: Node[] = [el('span', {}), el('span', { class: 'head' }, k === 'pilots' ? text.shortName : text.given)];
        if (full) rows.push(el('span', { class: 'head' }, text.fullName));
        for (const [loc, col, label] of LOCALES) {
          const now = byLocale[loc] ?? { name: '', full: '' };
          const update = (field: 'name' | 'full', v: string) => {
            const next = { ...(byLocale[loc] ?? { name: '', full: '' }), [field]: v };
            if (next.name || next.full) byLocale[loc] = next; else delete byLocale[loc];
          };
          rows.push(el('span', { class: 'lang' }, label), input(now.name, e.name[col], (v) => update('name', v)));
          if (full) rows.push(input(now.full, full[col], (v) => update('full', v)));
        }
        cards.push(el('div', { class: 'card' },
          el('h3', {}, `${kindLabel[k]} `, el('span', { class: 'num' }, String(id)), nameOf(e.name),
            el('button', { type: 'button', class: 'mini', onclick: () => { records[k].delete(id); renderRecords(); } }, text.remove)),
          el('div', { class: full ? 'langs two' : 'langs one' }, ...rows)));
      }
    recordBox.replaceChildren(...cards);
  }

  // --- Portraits ---------------------------------------------------------------------------
  const withFace = people.filter((p) => p.image !== null);
  const portraitFind = root.querySelector<HTMLInputElement>('[data-portrait-find]')!;
  const portraitResults = root.querySelector<HTMLElement>('[data-portrait-results]')!;
  const portraitBox = root.querySelector<HTMLElement>('[data-portraits]')!;
  const shown = new Set<number>();
  portraitFind.addEventListener('input', () => {
    const found = search(withFace, portraitFind.value);
    portraitResults.replaceChildren(...found.map((p) => el('li', {}, el('button', { type: 'button', onclick: () => {
      shown.add(p.id); portraitFind.value = ''; portraitResults.replaceChildren(); renderPortraits();
    } }, el('span', { class: 'num' }, String(p.id)), nameOf(p.name)))));
    if (portraitFind.value.trim() && !found.length) portraitResults.replaceChildren(el('li', { class: 'hint' }, text.none));
  });

  // Previews as data: URLs (the site's policy allows no blob: images).
  const dataUrl = (png: Uint8Array) => { let s = ''; for (let i = 0; i < png.length; i += 0x8000) s += String.fromCharCode(...png.subarray(i, i + 0x8000)); return `data:image/png;base64,${btoa(s)}`; };
  // Centre square, scaled to a multiple of 96 up to 768 (at least 96).
  async function square(file: Blob): Promise<Picture> {
    const bitmap = await createImageBitmap(file);
    const side = Math.min(bitmap.width, bitmap.height);
    const size = Math.min(768, Math.max(96, Math.floor(side / 96) * 96));
    const canvas = document.createElement('canvas');
    canvas.width = canvas.height = size;
    const g = canvas.getContext('2d')!;
    g.imageSmoothingQuality = 'high';
    g.drawImage(bitmap, (bitmap.width - side) / 2, (bitmap.height - side) / 2, side, side, 0, 0, size, size);
    const blob: Blob = await new Promise((resolve, reject) => canvas.toBlob((b) => b ? resolve(b) : reject(new Error('png')), 'image/png'));
    const png = new Uint8Array(await blob.arrayBuffer());
    return { png, size, url: dataUrl(png) };
  }
  function renderPortraits() {
    for (const id of pictures.keys()) shown.add(id);
    portraitBox.replaceChildren(...[...shown].map((id) => {
      const p = people.find((x) => x.id === id)!, pic = pictures.get(id);
      const file = el('input', { type: 'file', accept: 'image/*', hidden: true }) as HTMLInputElement;
      file.addEventListener('change', async () => {
        const f = file.files?.[0];
        if (!f) return;
        pictures.set(id, await square(f));
        renderPortraits();
      });
      return el('div', { class: 'card' },
        el('h3', {}, el('span', { class: 'num' }, String(id)), nameOf(p.name)),
        el('div', { class: 'faces' },
          p.portrait ? el('img', { src: p.portrait, alt: '', loading: 'lazy' }) : el('span', { class: 'hint' }, '—'),
          el('span', { class: 'arrow' }, '→'),
          pic ? el('img', { src: pic.url, alt: '' }) : el('span', { class: 'hint' }, '?')),
        pic ? el('span', { class: 'hint' }, `${pic.size} × ${pic.size} PNG`) : null,
        el('div', {}, el('label', { class: 'mini' }, text.choosePicture, file), ' ',
          el('button', { type: 'button', class: 'mini', onclick: () => {
            pictures.delete(id); shown.delete(id); renderPortraits();
          } }, text.remove)));
    }));
  }

  // --- names.json ----------------------------------------------------------------------------
  function namesJson(): { doc: Record<string, unknown> | null; problems: string[] } {
    const problems: string[] = [];
    const checkText = (v: string, what: string, longest: number) => {
      if (badChars(v)) problems.push(fill(text.errChars, { what }));
      if ([...v].length > longest) problems.push(fill(text.errLong, { what, n: longest }));
    };
    const doc: Record<string, any> = { ...keptNames, schema: 'srw64.mod-names.v1' };
    delete doc.heroes; delete doc.unit; delete doc.pilots; delete doc.units; delete doc.weapons;
    heroes.forEach((h, p) => {
      for (const [loc, fields] of Object.entries(heroNames[p])) {
        const what = `${routes[h.route]} ${text[h.role]} ${loc}`;
        if (!fields.name) { problems.push(fill(text.errNeedName, { what })); continue; }
        for (const [f, v] of Object.entries(fields)) checkText(v, `${what} ${text[f === 'name' ? 'given' : f]}`, LONGEST);
        ((doc.heroes ??= {})[h.route] ??= {})[h.role] ??= {};
        doc.heroes[h.route][h.role][loc] = fields;
      }
    });
    for (const [loc, v] of Object.entries(unitName)) { checkText(v, `${text.unitName} ${loc}`, LONGEST_RECORD); (doc.unit ??= {})[loc] = v; }
    for (const k of ['pilots', 'units', 'weapons'] as Kind[])
      for (const [id, byLocale] of records[k])
        for (const [loc, { name, full }] of Object.entries(byLocale)) {
          const what = `${kindLabel[k]} ${id} ${loc}`;
          if (!name) { if (full) problems.push(fill(text.errNeedName, { what })); continue; }
          checkText(name, what, LONGEST_RECORD);
          if (full) checkText(full, what, LONGEST_RECORD);
          ((doc[k] ??= {})[String(id)] ??= {})[loc] = k === 'pilots' ? (full ? { name, full } : { name }) : name;
        }
    const any = ['heroes', 'unit', 'pilots', 'units', 'weapons'].some((k) => k in doc);
    return { doc: any ? doc : null, problems };
  }

  const exportStatus = root.querySelector<HTMLElement>('[data-export-status]')!;
  root.querySelector('[data-export]')!.addEventListener('click', () => {
    const m = form.manifest();
    if (typeof m === 'string') return say(exportStatus, m, true);
    const { doc, problems } = namesJson();
    if (problems.length) return say(exportStatus, text.fixFirst + problems.join('；'), true);
    if (!doc && !pictures.size && !Object.keys(kept).length) return say(exportStatus, text.errEmpty, true);
    const out: Record<string, Uint8Array> = { ...kept };
    if (doc) out['dialogue/names.json'] = jsonBytes(doc);
    for (const [id, pic] of pictures) out[`art/portraits/${id}.png`] = pic.png;
    writePackage(m.manifest, m.version, out, exportStatus, text);
  });

  // --- Opening a package ------------------------------------------------------------------
  const importStatus = root.querySelector<HTMLElement>('[data-import-status]')!;
  root.querySelector<HTMLInputElement>('[data-import]')!.addEventListener('change', async (ev) => {
    const box = ev.target as HTMLInputElement, file = box.files?.[0];
    box.value = '';
    if (!file) return;
    try {
      const { mod, files } = await openPackage(file);
      form.fill(mod);
      heroNames.forEach((h) => { for (const k of Object.keys(h)) delete h[k]; });
      for (const k of Object.keys(unitName)) delete unitName[k];
      for (const k of Object.keys(records) as Kind[]) records[k].clear();
      pictures.clear(); shown.clear(); kept = {}; keptNames = {};
      for (const [path, bytes] of Object.entries(files)) {
        const face = /^art\/portraits\/(\d{1,3})\.png$/i.exec(path);
        if (path === 'mod.json') continue;
        if (path === 'dialogue/names.json') {
          const doc = readJson(bytes) ?? {};
          keptNames = doc;
          heroes.forEach((h, p) => { for (const [loc, f] of Object.entries(doc.heroes?.[h.route]?.[h.role] ?? {})) heroNames[p][loc] = { ...(f as object) } as Record<string, string>; });
          Object.assign(unitName, doc.unit ?? {});
          for (const k of ['pilots', 'units', 'weapons'] as Kind[])
            for (const [id, byLocale] of Object.entries(doc[k] ?? {})) {
              if (!byId[k].has(Number(id))) continue;
              const row: Record<string, { name: string; full: string }> = {};
              for (const [loc, v] of Object.entries(byLocale as object))
                row[loc] = typeof v === 'string' ? { name: v, full: '' } : { name: (v as any).name ?? '', full: (v as any).full ?? '' };
              records[k].set(Number(id), row);
            }
        } else if (face && people.some((p) => p.id === Number(face[1]))) {
          const blob = new Blob([bytes as BlobPart], { type: 'image/png' });
          const bitmap = await createImageBitmap(blob);
          pictures.set(Number(face[1]), { png: bytes, size: bitmap.width, url: dataUrl(bytes) });
        } else kept[path] = bytes;
      }
      renderHeroes(); renderUnit(); renderRecords(); renderPortraits();
      say(importStatus, fill(text.imported, { id: mod.id ?? file.name }));
    } catch (error) {
      say(importStatus, fill(text.errImport, { detail: String((error as Error).message ?? error) }), true);
    }
  });

  renderHeroes(); renderUnit(); renderRecords(); renderPortraits();
  addEventListener('beforeunload', (e) => { if (pictures.size || Object.keys(unitName).length || heroNames.some((h) => Object.keys(h).length)) e.preventDefault(); });
}
