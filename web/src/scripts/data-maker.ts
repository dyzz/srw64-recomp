// The number MOD maker (pages/[lang]/mods/data.astro): edits against the original values
// (data/data-mods.json, export_data_tables.py), written as data/units.json, pilots.json and
// weapons.json the host reads (src/host/mod_data.hpp). Only changed fields are written.
import data from '../data/data-mods.json';
import { el, fill, jsonBytes, openPackage, packageForm, readJson, say, search, writePackage, type Text } from './mod-package';
import { picker, type Pickable } from './picker';

type Kind = 'units' | 'pilots' | 'weapons';
type Field = { key: string; label: string; low: number; high: number; step?: number };
type Labels = {
  fields: Record<Kind, Field[]>;
  abilities: { key: string; label: string }[];
  skills: { key: string; label: string; row: number }[];
  spirits: { key: string; label: string }[];
  sizes: string[];
};
type Entry = Record<string, any> & { id: number; name: { ja: string; zh: string; en: string } };

const FILES: Record<Kind, string> = { units: 'data/units.json', pilots: 'data/pilots.json', weapons: 'data/weapons.json' };
const CHANNELS = ['air', 'land', 'sea', 'space'];
const RANKS = ['A', 'B', 'C', 'D', '-'];
const FIRST_ROW = ['newtype', 'enhanced', 'holy_warrior', 'esp', 'potential'];
// The original status screen's abilities box holds about three (native-ability-screens.md).
const ABILITIES_SHOWN = 3;

export function initDataMaker() {
  const root = document.querySelector<HTMLElement>('[data-maker]');
  const payload = document.getElementById('maker-data');
  if (!root || !payload) return;
  const { text, labels, pick, locale, lang } = JSON.parse(payload.textContent!) as {
    text: Text & { terrainNames: string[] }; labels: Labels; locale: string; lang: 'zh' | 'en' | 'ja';
    pick: { units: Pickable[]; pilots: Pickable[]; series: { id: number | null; name: string }[] };
  };
  const lists = data as unknown as Record<Kind, Entry[]>;
  const byId = Object.fromEntries((['units', 'pilots', 'weapons'] as Kind[]).map((k) => [k, new Map(lists[k].map((e) => [e.id, e]))])) as Record<Kind, Map<number, Entry>>;
  const nameOf = (e: Entry) => e.name[lang] || e.name.ja;
  const form = packageForm(root, text, locale, text.game);

  // Changes by kind and number: field -> value, as the file writes it (terrain by channel,
  // skills by key with null for none).
  const edits: Record<Kind, Map<number, Record<string, any>>> = { units: new Map(), pilots: new Map(), weapons: new Map() };
  let kind: Kind = 'units';
  let current: number | null = null;
  let kept: Record<string, Uint8Array> = {};

  const originalOf = (k: Kind, id: number, key: string): any => {
    const e = byId[k].get(id)!;
    if (key.startsWith('terrain.')) return e.terrain[CHANNELS.indexOf(key.slice(8))];
    if (key.startsWith('skill.')) return (e.skills[key.slice(6)] ?? []).join(', ');
    return e[key];
  };
  // Lists compare by content: abilities as a set, spirits in order.
  const same = (key: string, a: any, b: any) =>
    key === 'abilities' ? [...a].sort().join(' ') === [...b].sort().join(' ') : key === 'spirits' ? JSON.stringify(a) === JSON.stringify(b) : a === b;
  const edit = (k: Kind, id: number) => edits[k].get(id) ?? {};
  const valueOf = (k: Kind, id: number, key: string) => key in edit(k, id) ? edit(k, id)[key] : originalOf(k, id, key);
  function set(k: Kind, id: number, key: string, value: any) {
    const e = { ...edit(k, id) };
    if (same(key, value, originalOf(k, id, key))) delete e[key]; else e[key] = value;
    if (Object.keys(e).length) edits[k].set(id, e); else edits[k].delete(id);
    renderEdited();
    refreshBadges();
  }
  // Pickers redraw their ✓ marks after a change settles.
  let badgeTimer = 0;
  function refreshBadges() { clearTimeout(badgeTimer); badgeTimer = window.setTimeout(() => { for (const p of Object.values(pickers)) p.refresh(); renderWeaponList(); }, 400); }

  // --- Picking: units and pilots from the grid, weapons through the unit carrying them ----
  const pickBox = root.querySelector<HTMLElement>('[data-pick]')!;
  const weaponList = root.querySelector<HTMLElement>('[data-weapon-list]')!;
  const results = root.querySelector<HTMLElement>('[data-results]')!;
  const find = root.querySelector<HTMLInputElement>('[data-find]')!;
  const editor = root.querySelector<HTMLElement>('[data-editor]')!;
  const imgOf = (k: Kind, id: number) => (k === 'units' ? pick.units : k === 'pilots' ? pick.pilots : []).find((x) => x.id === id)?.img ?? null;
  const icon = (k: Kind, id: number) => { const src = imgOf(k, id); return src ? el('img', { class: 'icon', src, alt: '', loading: 'lazy' }) : null; };
  const unitWeapons = new Map<number, number[]>();
  for (const w of lists.weapons) for (const u of (w.units as number[])) unitWeapons.set(u, [...(unitWeapons.get(u) ?? []), w.id]);
  const changedCount = (k: Kind) => (id: number) => edits[k].has(id) ? '✓' : null;
  const pickerText = { allSeries: text.allSeries, search: text.search, none: text.none };
  let weaponUnit: number | null = null;
  const pickers = {
    units: picker(pick.units, pick.series, lang, pickerText, (id) => { current = id; renderEditor(); }, { badge: changedCount('units') }),
    pilots: picker(pick.pilots, pick.series, lang, pickerText, (id) => { current = id; renderEditor(); }, { badge: changedCount('pilots') }),
    weapons: picker(pick.units, pick.series, lang, pickerText, (id) => { weaponUnit = id; current = null; renderWeaponList(); renderEditor(); },
      { badge: (u) => (unitWeapons.get(u) ?? []).some((w) => edits.weapons.has(w)) ? '✓' : null }),
  };
  function openWeapon(id: number) { current = id; renderWeaponList(); renderEditor(); }
  function renderWeaponList() {
    if (kind !== 'weapons' || weaponUnit === null) { weaponList.replaceChildren(kind === 'weapons' ? el('p', { class: 'hint' }, text.pickUnitFirst) : ''); return; }
    const u = byId.units.get(weaponUnit);
    weaponList.replaceChildren(el('div', { class: 'weapon-list' },
      el('h3', {}, icon('units', weaponUnit), ' ', fill(text.weaponsOf, { unit: u ? nameOf(u) : String(weaponUnit) })),
      el('ul', { class: 'results' }, ...(unitWeapons.get(weaponUnit) ?? []).map((wid) => {
        const w = byId.weapons.get(wid)!;
        return el('li', {}, el('button', { type: 'button', class: wid === current ? 'on' : '', onclick: () => openWeapon(wid) },
          el('span', { class: 'num' }, String(wid)), nameOf(w), el('span', { class: 'stat' }, `${w.power}`), edits.weapons.has(wid) ? ' ✓' : ''));
      }))));
  }
  function showKind(k: Kind) {
    kind = k;
    for (const o of root!.querySelectorAll<HTMLElement>('[data-kind]')) o.classList.toggle('on', o.dataset.kind === k);
    pickBox.replaceChildren(pickers[k].node);
    find.hidden = k !== 'weapons'; results.replaceChildren(); find.value = '';
    renderWeaponList();
  }
  function renderResults() {
    const found = search(lists.weapons, find.value);
    results.replaceChildren(...found.map((e) => el('li', {}, el('button', { type: 'button', onclick: () => {
      weaponUnit = (e.units as number[])[0] ?? null; pickers.weapons.choose(weaponUnit); openWeapon(e.id);
    } }, el('span', { class: 'num' }, String(e.id)), nameOf(e)))));
    if (find.value.trim() && !found.length) results.replaceChildren(el('li', { class: 'hint' }, text.none));
  }
  find.addEventListener('input', renderResults);
  for (const b of root.querySelectorAll<HTMLButtonElement>('[data-kind]'))
    b.addEventListener('click', () => { current = kind === b.dataset.kind ? current : null; showKind(b.dataset.kind as Kind); renderEditor(); });

  function numberField(f: Field, id: number) {
    const was = originalOf(kind, id, f.key), now = valueOf(kind, id, f.key);
    const input = el('input', { type: 'number', min: f.low, max: f.high, step: f.step ?? 1, value: now }) as HTMLInputElement;
    const wrap = el('label', { class: `field${now !== was ? ' changed' : ''}` }, f.label, input, el('span', { class: 'was' }, `${text.original} ${was}`));
    input.addEventListener('change', () => {
      const v = input.value.trim() === '' ? was : Math.round(Number(input.value));
      set(kind, id, f.key, Number.isFinite(v) ? v : was);
      wrap.classList.toggle('changed', valueOf(kind, id, f.key) !== was);
    });
    return wrap;
  }
  function selectField(label: string, key: string, options: string[], id: number) {
    const was = originalOf(kind, id, key), now = valueOf(kind, id, key);
    const select = el('select', {}, ...options.map((o) => el('option', { value: o }, o))) as HTMLSelectElement;
    select.value = now;
    const wrap = el('label', { class: `field${now !== was ? ' changed' : ''}` }, label, select, el('span', { class: 'was' }, `${text.original} ${was || '—'}`));
    select.addEventListener('change', () => { set(kind, id, key, select.value); wrap.classList.toggle('changed', select.value !== was); });
    return wrap;
  }
  function check(label: string, key: string, id: number) {
    const was = originalOf(kind, id, key), input = el('input', { type: 'checkbox', checked: valueOf(kind, id, key) }) as HTMLInputElement;
    const wrap = el('label', { class: `check${input.checked !== was ? ' changed' : ''}` }, input, label);
    input.addEventListener('change', () => { set(kind, id, key, input.checked); wrap.classList.toggle('changed', input.checked !== was); });
    return wrap;
  }

  function renderEditor() {
    if (current === null || !byId[kind].has(current)) { editor.replaceChildren(); return; }
    const id = current, e = byId[kind].get(id)!;
    const parts: (Node | null)[] = [];
    if (kind === 'pilots' && e.shared?.length)
      parts.push(el('p', { class: 'hint' }, text.shared + e.shared.map((s: number) => `${s} ${byId.pilots.get(s) ? nameOf(byId.pilots.get(s)!) : ''}`).join(lang === 'en' ? ', ' : '、')));
    if (kind === 'weapons' && e.units?.length > 1)
      parts.push(el('p', { class: 'hint' }, text.carriers + e.units.map((u: number) => `${u} ${byId.units.get(u) ? nameOf(byId.units.get(u)!) : ''}`).join(lang === 'en' ? ', ' : '、')));
    if (kind === 'pilots') parts.push(el('p', { class: 'hint' }, text.stats1));
    parts.push(el('div', { class: 'grid' }, ...labels.fields[kind].map((f) => numberField(f, id)),
      kind === 'units' ? selectField(text.size, 'size', labels.sizes, id) : null));
    parts.push(el('div', { class: 'sub' }, text.terrain),
      el('div', { class: 'grid' }, ...CHANNELS.map((c, n) => selectField(text.terrainNames[n], `terrain.${c}`, RANKS, id))));
    if (kind === 'units') {
      parts.push(el('div', { class: 'sub' }, text.equipment), el('div', { class: 'checks' }, check(text.shield, 'shield', id), check(text.sword, 'sword', id)));
      const abil = new Set<string>(valueOf(kind, id, 'abilities'));
      parts.push(el('div', { class: 'sub' }, text.abilities), el('div', { class: 'checks' }, ...labels.abilities.map((a) => {
        const input = el('input', { type: 'checkbox', checked: abil.has(a.key) }) as HTMLInputElement;
        const had = e.abilities.includes(a.key);
        const wrap = el('label', { class: `check${input.checked !== had ? ' changed' : ''}` }, input, a.label);
        input.addEventListener('change', () => {
          const now = new Set<string>(valueOf(kind, id, 'abilities'));
          if (input.checked) now.add(a.key); else now.delete(a.key);
          // One HP regen rate at a time (801FA544 reads one of the two).
          if (input.checked && a.key.startsWith('hp_regen_')) now.delete(a.key === 'hp_regen_10' ? 'hp_regen_20' : 'hp_regen_10');
          set(kind, id, 'abilities', [...now]);
          renderEditor();
        });
        return wrap;
      })), el('p', { class: 'hint' }, text.abilityRules),
      abil.size + (valueOf(kind, id, 'shield') ? 1 : 0) > ABILITIES_SHOWN ? el('p', { class: 'warn' }, text.abilityMany) : null);
      const own = unitWeapons.get(id) ?? [];
      if (own.length) parts.push(el('div', { class: 'sub' }, text.unitWeapons), el('ul', { class: 'results' }, ...own.map((wid) =>
        el('li', {}, el('button', { type: 'button', onclick: () => { weaponUnit = id; showKind('weapons'); pickers.weapons.choose(id); openWeapon(wid); } },
          el('span', { class: 'num' }, String(wid)), nameOf(byId.weapons.get(wid)!), edits.weapons.has(wid) ? ' ✓' : '')))));
    }
    if (kind === 'pilots') {
      parts.push(el('div', { class: 'sub' }, text.skills), el('p', { class: 'hint' }, text.skillRow),
        el('div', { class: 'grid' }, ...labels.skills.map((s) => {
          const key = `skill.${s.key}`, was = originalOf(kind, id, key);
          const input = el('input', { value: valueOf(kind, id, key), placeholder: '1, 5, 10', autocomplete: 'off' }) as HTMLInputElement;
          const wrap = el('label', { class: `field${input.value !== was ? ' changed' : ''}` }, s.label, input, el('span', { class: 'was' }, `${text.original} ${was || '—'}`));
          input.addEventListener('change', () => {
            const v = input.value.split(/[,，、\s]+/).filter(Boolean).join(', ');
            input.value = v; set(kind, id, key, v); wrap.classList.toggle('changed', v !== was);
          });
          return wrap;
        })));
      const rows = el('div', { class: 'grid wide' });
      const draw = () => {
        const list: [string, number][] = valueOf(kind, id, 'spirits');
        rows.replaceChildren(...Array.from({ length: 6 }, (_, n) => {
          const [key, level] = list[n] ?? ['', 0];
          const select = el('select', {}, el('option', { value: '' }, '—'), ...labels.spirits.map((s) => el('option', { value: s.key }, s.label))) as HTMLSelectElement;
          select.value = key;
          const lv = el('input', { type: 'number', min: 1, max: 99, value: key ? level : '', placeholder: text.level }) as HTMLInputElement;
          const commit = () => {
            const next: [string, number][] = [];
            for (const row of rows.querySelectorAll('.spirit-row')) {
              const s = row.querySelector('select')!.value, l = Number(row.querySelector('input')!.value);
              if (s) next.push([s, Number.isFinite(l) && l > 0 ? Math.round(l) : 1]);
            }
            set(kind, id, 'spirits', next);
          };
          select.addEventListener('change', commit); lv.addEventListener('change', commit);
          return el('div', { class: 'spirit-row' }, select, lv);
        }));
      };
      draw();
      const was = e.spirits.map(([k, l]: [string, number]) => `${labels.spirits.find((s) => s.key === k)?.label ?? k} ${l}`).join(lang === 'en' ? ', ' : '、');
      parts.push(el('div', { class: 'sub' }, text.spirits), rows, el('p', { class: 'hint' }, `${text.original}：${was || '—'}`));
    }
    parts.push(el('div', {}, el('button', { type: 'button', class: 'mini', onclick: () => { edits[kind].delete(id); renderEdited(); renderEditor(); } }, text.reset)));
    editor.replaceChildren(el('div', { class: 'card' },
      el('h3', {}, icon(kind, id), el('span', { class: 'num' }, String(id)), nameOf(e), lang !== 'ja' && e.name.ja !== nameOf(e) ? el('span', { class: 'ja' }, e.name.ja) : null),
      ...parts));
  }

  // --- What is changed ------------------------------------------------------------------
  const editedBox = root.querySelector<HTMLElement>('[data-edited]')!;
  const kindLabel: Record<Kind, string> = { units: text.unit, pilots: text.pilot, weapons: text.weapon };
  function renderEdited() {
    const rows: HTMLElement[] = [];
    for (const k of ['units', 'pilots', 'weapons'] as Kind[])
      for (const [id, e] of edits[k]) {
        const entry = byId[k].get(id)!;
        rows.push(el('div', { class: 'edited-row' },
          el('span', {}, icon(k, id), ` ${kindLabel[k]} `, el('span', { class: 'num' }, String(id)), nameOf(entry)),
          el('span', { class: 'what' }, Object.keys(e).map((key) => key.replace(/^(terrain|skill)\./, '')).join(' · ')),
          el('button', { type: 'button', class: 'mini', onclick: () => {
            if (k === 'weapons') { weaponUnit = (entry.units as number[])[0] ?? null; pickers.weapons.choose(weaponUnit); }
            showKind(k); pickers[k].choose(k === 'weapons' ? weaponUnit : id); current = id; renderWeaponList(); renderEditor();
            editor.scrollIntoView({ behavior: 'smooth', block: 'start' }); } }, '✎'),
          el('button', { type: 'button', class: 'mini', onclick: () => { edits[k].delete(id); renderEdited(); if (kind === k && current === id) renderEditor(); } }, text.remove)));
      }
    editedBox.replaceChildren(...(rows.length ? rows : [el('p', { class: 'hint' }, text.editedNone)]));
  }
  renderEdited();
  showKind('units');

  // --- The files --------------------------------------------------------------------------
  const levels = (s: string) => s.split(/[,，、\s]+/).filter(Boolean).map(Number);
  function files(): { files: Record<string, unknown>; problems: string[] } {
    const out: Record<string, unknown> = {}, problems: string[] = [];
    for (const k of ['units', 'pilots', 'weapons'] as Kind[]) {
      const doc: Record<string, Record<string, unknown>> = {};
      for (const [id, e] of edits[k]) {
        const row: Record<string, any> = {}, entry = byId[k].get(id)!, where = `${kindLabel[k]} ${id} ${nameOf(entry)}`;
        for (const [key, value] of Object.entries(e)) {
          if (key.startsWith('terrain.')) (row.terrain ??= {})[key.slice(8)] = value;
          else if (key.startsWith('skill.')) {
            const list = levels(value);
            if (list.some((l) => !Number.isInteger(l) || l < 1 || l > 99) || list.length > 9 || list.some((l, n) => n && l < list[n - 1]))
              problems.push(`${where}: ${fill(text.skillBad, { skill: labels.skills.find((s) => `skill.${s.key}` === key)?.label ?? key })}`);
            (row.skills ??= {})[key.slice(6)] = list.length ? list : null;
          } else row[key] = value;
          const f = labels.fields[k].find((x) => x.key === key);
          if (f && (value < f.low || value > f.high)) problems.push(`${where}: ${fill(text.outOfRange, { field: f.label, low: f.low, high: f.high })}`);
          if (f?.step && value % f.step) problems.push(`${where}: ${text.power100}`);
        }
        if (k === 'pilots') {
          const firstRow = FIRST_ROW.filter((s) => levels(valueOf(k, id, `skill.${s}`)).length);
          // The host gives a first-row skill the row alone (mod_data.hpp): the original one
          // has to be cleared here, so it is written as null.
          if (firstRow.length > 1) problems.push(`${where}: ${text.skillOne}`);
          if (row.spirits) {
            const l = (row.spirits as [string, number][]).map(([, x]) => x);
            if (l.some((x, n) => x < 1 || x > 99 || (n && x < l[n - 1]))) problems.push(`${where}: ${text.spiritBad}`);
          }
        }
        if (k === 'weapons' && valueOf(k, id, 'range_min') > valueOf(k, id, 'range_max')) problems.push(`${where}: ${text.rangeOrder}`);
        doc[String(id)] = row;
      }
      if (Object.keys(doc).length) out[FILES[k]] = doc;
    }
    return { files: out, problems };
  }

  const exportStatus = root.querySelector<HTMLElement>('[data-export-status]')!;
  root.querySelector('[data-export]')!.addEventListener('click', () => {
    const m = form.manifest();
    if (typeof m === 'string') return say(exportStatus, m, true);
    const { files: docs, problems } = files();
    if (problems.length) return say(exportStatus, text.fixFirst + problems.join('；'), true);
    if (!Object.keys(docs).length && !Object.keys(kept).length) return say(exportStatus, text.errEmpty, true);
    const out: Record<string, Uint8Array> = { ...kept };
    for (const [path, doc] of Object.entries(docs)) out[path] = jsonBytes(doc);
    writePackage(m.manifest, m.version, out, exportStatus, text);
  });

  // --- Opening a package ------------------------------------------------------------------
  const importStatus = root.querySelector<HTMLElement>('[data-import-status]')!;
  root.querySelector<HTMLInputElement>('[data-import]')!.addEventListener('change', async (ev) => {
    const input = ev.target as HTMLInputElement, file = input.files?.[0];
    input.value = '';
    if (!file) return;
    try {
      const { mod, files: inside } = await openPackage(file);
      form.fill(mod);
      for (const k of ['units', 'pilots', 'weapons'] as Kind[]) edits[k].clear();
      kept = {};
      for (const [path, bytes] of Object.entries(inside)) {
        const k = (Object.keys(FILES) as Kind[]).find((x) => FILES[x] === path);
        if (path === 'mod.json') continue;
        if (!k) { kept[path] = bytes; continue; }
        for (const [key, row] of Object.entries(readJson(bytes) as Record<string, any>)) {
          const id = Number(key);
          if (!byId[k].has(id)) continue;
          for (const [field, value] of Object.entries(row)) {
            if (field === 'terrain') for (const [c, r] of Object.entries(value as object)) set(k, id, `terrain.${c}`, r);
            else if (field === 'skills') for (const [s, l] of Object.entries(value as object)) set(k, id, `skill.${s}`, Array.isArray(l) ? l.join(', ') : '');
            else set(k, id, field, value);
          }
        }
      }
      renderEdited(); renderEditor();
      say(importStatus, fill(text.imported, { id: mod.id ?? file.name }));
    } catch (error) {
      say(importStatus, fill(text.errImport, { detail: String((error as Error).message ?? error) }), true);
    }
  });
  addEventListener('beforeunload', (e) => { if (edits.units.size || edits.pilots.size || edits.weapons.size) e.preventDefault(); });
}
