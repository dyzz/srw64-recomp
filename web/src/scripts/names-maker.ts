// The name and portrait MOD maker (pages/[lang]/mods/names.astro): dialogue/names.json with
// the host's rules (src/host/mod_names.hpp) and art/portraits/<pilot>.png squared and scaled
// to a multiple of 96 (native_portrait.cpp), made in the browser. Map icons have their own
// maker (icon-maker.ts); a package's icons are kept as they are.
import { dataUrl, frame, framer, type Picture } from './framer';
import { el, fill, jsonBytes, openPackage, packageForm, readJson, say, writePackage, type Text } from './mod-package';
import { picker, type Pickable } from './picker';

type Names3 = { ja: string; zh: string; en: string };
type Hero = { route: string; role: 'hero' | 'partner'; ja: string[]; zh: string[]; en: string[]; face: string | null };
type Person = { id: number; name: Names3; full: Names3; portrait: string | null; image: number | null };
type Face = { id: number; name: Names3; portrait: string | null };
type Named = { id: number; name: Names3 };
type Kind = 'pilots' | 'units' | 'weapons';

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
  const { text, heroes, routes, unitDefault, people, portraitPeople, pickFaces, units, weapons, pick, locale, lang } = JSON.parse(payload.textContent!) as {
    text: Text; heroes: Hero[]; routes: Record<string, string>; unitDefault: Names3; people: Person[]; portraitPeople: Face[]; pickFaces: Pickable[]; units: Named[];
    weapons: (Named & { units: number[] })[]; pick: { units: Pickable[]; pilots: Pickable[]; series: { id: number | null; name: string }[] };
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
        // Without a nickname of its own the package's name is the nickname: the box says so.
        const boxes = HERO_FIELDS.map((f, n) => input(now[f] ?? '', h[col][n], (v) => {
          const e = { ...(heroNames[p][loc] ?? {}) };
          if (v) e[f] = v; else delete e[f];
          if (Object.keys(e).length) heroNames[p][loc] = e; else delete heroNames[p][loc];
          boxes[2].placeholder = heroNames[p][loc]?.name || h[col][2];
          check();
        }));
        boxes[2].placeholder = now.name || h[col][2];
        rows.push(el('span', { class: 'lang' }, label), ...boxes, warn);
        check();
      }
      return el('div', { class: 'card' },
        el('h3', {}, h.face ? el('img', { class: 'icon', src: h.face, alt: '', loading: 'lazy' }) : null, `${routes[h.route]} · ${text[h.role]}`, el('span', { class: 'dim' }, h[lang][0] + ({ zh: '·', en: ' ', ja: '・' })[lang] + h[lang][1])),
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

  // --- Other names: pilots and units from the grid, weapons through the unit carrying them --
  let kind: Kind = 'pilots';
  const pickBox = root.querySelector<HTMLElement>('[data-pick]')!;
  const weaponList = root.querySelector<HTMLElement>('[data-weapon-list]')!;
  const recordBox = root.querySelector<HTMLElement>('[data-records]')!;
  const kindLabel: Record<Kind, string> = { pilots: text.pilot, units: text.unit, weapons: text.weapon };
  const imgOf = (k: Kind, id: number) => (k === 'units' ? pick.units : k === 'pilots' ? pick.pilots : []).find((x) => x.id === id)?.img ?? null;
  const icon = (k: Kind, id: number) => { const src = imgOf(k, id); return src ? el('img', { class: 'icon', src, alt: '', loading: 'lazy' }) : null; };
  const unitWeapons = new Map<number, number[]>();
  for (const w of weapons) for (const u of w.units) unitWeapons.set(u, [...(unitWeapons.get(u) ?? []), w.id]);
  const pickerText = { allSeries: text.allSeries, search: text.search, none: text.none };
  const add = (k: Kind, id: number) => { if (!records[k].has(id)) records[k].set(id, {}); renderRecords(); refresh(); };
  let weaponUnit: number | null = null;
  const pickers = {
    pilots: picker(pick.pilots, pick.series, lang, pickerText, (id) => add('pilots', id), { badge: (id) => records.pilots.has(id) ? '✓' : null }),
    units: picker(pick.units, pick.series, lang, pickerText, (id) => add('units', id), { badge: (id) => records.units.has(id) ? '✓' : null }),
    weapons: picker(pick.units, pick.series, lang, pickerText, (id) => { weaponUnit = id; renderWeaponList(); },
      { badge: (u) => (unitWeapons.get(u) ?? []).some((w) => records.weapons.has(w)) ? '✓' : null }),
  };
  function refresh() { for (const p of Object.values(pickers)) p.refresh(); renderWeaponList(); }
  function renderWeaponList() {
    if (kind !== 'weapons') { weaponList.replaceChildren(); return; }
    if (weaponUnit === null) { weaponList.replaceChildren(el('p', { class: 'hint' }, text.pickUnitFirst)); return; }
    const u = byId.units.get(weaponUnit);
    weaponList.replaceChildren(el('div', { class: 'weapon-list' },
      el('h3', {}, icon('units', weaponUnit), ' ', fill(text.weaponsOf, { unit: u ? nameOf(u.name) : String(weaponUnit) })),
      el('ul', { class: 'results' }, ...(unitWeapons.get(weaponUnit) ?? []).map((wid) =>
        el('li', {}, el('button', { type: 'button', onclick: () => add('weapons', wid) },
          el('span', { class: 'num' }, String(wid)), nameOf(byId.weapons.get(wid)!.name), records.weapons.has(wid) ? ' ✓' : ''))))));
  }
  function showKind(k: Kind) {
    kind = k;
    for (const o of root!.querySelectorAll<HTMLElement>('[data-kind]')) o.classList.toggle('on', o.dataset.kind === k);
    pickBox.replaceChildren(pickers[k].node);
    renderWeaponList();
  }
  for (const b of root.querySelectorAll<HTMLButtonElement>('[data-kind]')) b.addEventListener('click', () => showKind(b.dataset.kind as Kind));
  showKind('pilots');
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
          el('h3', {}, icon(k, id), `${kindLabel[k]} `, el('span', { class: 'num' }, String(id)), nameOf(e.name),
            el('button', { type: 'button', class: 'mini', onclick: () => { records[k].delete(id); renderRecords(); refresh(); } }, text.remove)),
          el('div', { class: full ? 'langs two' : 'langs one' }, ...rows)));
      }
    recordBox.replaceChildren(...cards);
  }

  // --- Portraits ---------------------------------------------------------------------------
  const shown = new Set<number>();
  const portraitBox = root.querySelector<HTMLElement>('[data-portraits]')!;
  const facePicker = picker(pickFaces, pick.series, lang, pickerText, (id) => { shown.add(id); renderPortraits(); },
    { badge: (id) => pictures.has(id) ? '✓' : null });
  root.querySelector<HTMLElement>('[data-portrait-pick]')!.replaceChildren(facePicker.node);

  // The framed square as a PNG, a multiple of 96 up to 768 (at least 96).
  async function encode(pic: Picture) {
    const { side, x, y } = frame(pic);
    const size = Math.min(768, Math.max(96, Math.floor(side / 96) * 96));
    const canvas = document.createElement('canvas');
    canvas.width = canvas.height = size;
    const g = canvas.getContext('2d')!;
    g.imageSmoothingQuality = 'high';
    g.drawImage(pic.source, x, y, side, side, 0, 0, size, size);
    const blob: Blob = await new Promise((resolve, reject) => canvas.toBlob((b) => b ? resolve(b) : reject(new Error('png')), 'image/png'));
    pic.png = new Uint8Array(await blob.arrayBuffer());
    pic.size = size;
    pic.url = dataUrl(pic.png);
  }
  async function load(blob: Blob): Promise<Picture> {
    const source = await createImageBitmap(blob);
    return { source, cx: source.width / 2, cy: source.height / 2, zoom: 1, png: new Uint8Array(), size: 0, url: '', framing: true };
  }
  function renderPortraits() {
    for (const id of pictures.keys()) shown.add(id);
    portraitBox.replaceChildren(...[...shown].map((id) => {
      const p = portraitPeople.find((x) => x.id === id)!, pic = pictures.get(id);
      const file = el('input', { type: 'file', accept: 'image/*', hidden: true }) as HTMLInputElement;
      file.addEventListener('change', async () => {
        const f = file.files?.[0];
        if (!f) return;
        pictures.set(id, await load(f));
        renderPortraits(); facePicker.refresh();
      });
      const done = () => { renderPortraits(); facePicker.refresh(); };
      const head = el('h3', {}, p.portrait ? el('img', { class: 'icon', src: p.portrait, alt: '', loading: 'lazy' }) : null, el('span', { class: 'num' }, String(id)), nameOf(p.name));
      const remove = el('button', { type: 'button', class: 'mini', onclick: () => { pictures.delete(id); shown.delete(id); done(); } }, text.remove);
      const choose = el('label', { class: 'mini' }, pic ? text.otherPicture : text.choosePicture, file);
      if (pic?.framing) return el('div', { class: 'card' }, head, framer(pic, done, text, encode), el('div', {}, choose, ' ', remove));
      return el('div', { class: 'card' }, head,
        el('div', { class: 'faces' },
          p.portrait ? el('img', { src: p.portrait, alt: '', loading: 'lazy' }) : el('span', { class: 'hint' }, '—'),
          el('span', { class: 'arrow' }, '→'),
          pic ? el('img', { src: pic.url, alt: '' }) : el('span', { class: 'hint' }, '?')),
        pic ? el('span', { class: 'hint' }, `${pic.size} × ${pic.size} PNG`) : null,
        el('div', {}, pic ? el('button', { type: 'button', class: 'mini', onclick: () => { pic.framing = true; renderPortraits(); } }, text.reframe) : null,
          pic ? ' ' : null, choose, ' ', remove));
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
    for (const [id, pic] of pictures) if (pic.framing) problems.push(fill(text.errFraming, { who: `${id} ${nameOf(portraitPeople.find((x) => x.id === id)!.name)}` }));
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
        } else if (face && portraitPeople.some((p) => p.id === Number(face[1]))) {
          // Framed already: the whole square, kept as it is until the frame is moved.
          const source = await createImageBitmap(new Blob([bytes as BlobPart], { type: 'image/png' }));
          pictures.set(Number(face[1]), { source, cx: source.width / 2, cy: source.height / 2, zoom: 1, png: bytes, size: source.width, url: dataUrl(bytes), framing: false });
        } else kept[path] = bytes;
      }
      renderHeroes(); renderUnit(); renderRecords(); renderPortraits(); refresh(); facePicker.refresh();
      say(importStatus, fill(text.imported, { id: mod.id ?? file.name }));
    } catch (error) {
      say(importStatus, fill(text.errImport, { detail: String((error as Error).message ?? error) }), true);
    }
  });

  renderHeroes(); renderUnit(); renderRecords(); renderPortraits();
  addEventListener('beforeunload', (e) => { if (pictures.size || Object.keys(unitName).length || heroNames.some((h) => Object.keys(h).length)) e.preventDefault(); });
}
