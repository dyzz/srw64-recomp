// A browsable picker for the MOD makers (styles/mod-maker.css): units or pilots as cards with their picture, filtered
// by series and by name or number, in the game's own order. Picking calls back with the number.
import { el } from './mod-package';

export type Pickable = { id: number; name: { ja: string; zh: string; en: string }; series: number | null; img: string | null };
export type PickerText = { allSeries: string; search: string; none: string };

export function picker(items: Pickable[], series: { id: number | null; name: string }[], lang: 'zh' | 'en' | 'ja', text: PickerText,
  onPick: (id: number) => void, options: { badge?: (id: number) => string | null } = {}) {
  let chosen: number | null = null;
  const nameOf = (x: Pickable) => x.name[lang] || x.name.ja;
  const select = el('select', {}, el('option', { value: '' }, text.allSeries),
    ...series.filter((s) => items.some((x) => x.series === s.id)).map((s) => el('option', { value: String(s.id) }, s.name))) as HTMLSelectElement;
  const find = el('input', { type: 'search', placeholder: text.search, autocomplete: 'off', spellcheck: 'false' }) as HTMLInputElement;
  const grid = el('div', { class: 'pick-grid' });
  const card = (x: Pickable) => {
    const badge = options.badge?.(x.id);
    const b = el('button', { type: 'button', class: `pick${x.id === chosen ? ' on' : ''}`, 'data-id': x.id, title: `${x.id} ${x.name.ja}` },
      x.img ? el('img', { src: x.img, alt: '', loading: 'lazy', width: 56, height: 56 }) : el('span', { class: 'pick-blank' }),
      el('span', { class: 'pick-name' }, nameOf(x)), el('span', { class: 'pick-num' }, String(x.id)),
      badge ? el('span', { class: 'pick-badge' }, badge) : null);
    b.addEventListener('click', () => { chosen = x.id; for (const o of grid.querySelectorAll('.pick')) o.classList.toggle('on', o === b); onPick(x.id); });
    return b;
  };
  function render() {
    const s = select.value, q = find.value.trim().toLowerCase();
    const shown = items.filter((x) => (s === '' || String(x.series) === s) &&
      (!q || (/^\d+$/.test(q) ? String(x.id).startsWith(q) : `${x.name.ja} ${x.name.zh} ${x.name.en}`.toLowerCase().includes(q))));
    grid.replaceChildren(...(shown.length ? shown.map(card) : [el('p', { class: 'hint' }, text.none)]));
  }
  select.addEventListener('change', render);
  find.addEventListener('input', render);
  render();
  return {
    node: el('div', { class: 'picker' }, el('div', { class: 'pick-bar' }, select, find), grid),
    refresh: render,
    choose(id: number | null) { chosen = id; for (const o of grid.querySelectorAll<HTMLElement>('.pick')) o.classList.toggle('on', Number(o.dataset.id) === id); },
  };
}
