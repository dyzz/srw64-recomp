// Stage page: the translation shown and the Japanese on or off (remembered per site
// language), search within the stage, "only lines with suggestions", and the table of
// contents following the scroll.

const KEY = 'srw64-story';

function load(lang: string): Record<string, string> {
  try { return JSON.parse(localStorage.getItem(`${KEY}-${lang}`) || '{}'); } catch { return {}; }
}
function save(lang: string, v: Record<string, string>) {
  try { localStorage.setItem(`${KEY}-${lang}`, JSON.stringify(v)); } catch { /* private mode */ }
}

export function initStory() {
  const root = document.querySelector<HTMLElement>('[data-story]');
  if (!root) return;
  const lang = root.dataset.lang!;
  const saved = load(lang);
  const lines = [...root.querySelectorAll<HTMLElement>('.line')];

  // --- View controls.
  function set(control: string, value: string) {
    root!.querySelectorAll<HTMLButtonElement>(`[data-control="${control}"] button`).forEach((b) => {
      b.setAttribute('aria-pressed', String(b.dataset.value === value));
    });
    root!.dataset[control] = value;
    saved[control] = value;
    save(lang, saved);
  }
  root.querySelectorAll<HTMLElement>('[data-control]').forEach((group) => {
    const control = group.dataset.control!;
    const buttons = [...group.querySelectorAll<HTMLButtonElement>('button')];
    buttons.forEach((b) => b.addEventListener('click', () => set(control, b.dataset.value!)));
    const initial = buttons.find((b) => b.dataset.value === saved[control]) ?? buttons.find((b) => b.getAttribute('aria-pressed') === 'true') ?? buttons[0];
    set(control, initial.dataset.value!);
  });

  // --- Search within the stage: every visible text of a line, names included.
  const input = root.querySelector<HTMLInputElement>('[data-find]')!;
  const hitsOut = root.querySelector<HTMLElement>('[data-hits]')!;
  let hits: HTMLElement[] = [];
  let at = -1;
  const text = (line: HTMLElement) => (line.dataset.text ??= (line.querySelector('.body')?.textContent || '').toLowerCase());
  function go(step: number) {
    if (!hits.length) return;
    hits[at]?.classList.remove('hit-on');
    at = (at + step + hits.length) % hits.length;
    hits[at].classList.add('hit-on');
    hits[at].scrollIntoView({ block: 'center' });
    hitsOut.textContent = `${at + 1}/${hits.length}`;
  }
  function find() {
    hits.forEach((h) => h.classList.remove('hit-on'));
    const q = input.value.trim().toLowerCase();
    hits = q ? lines.filter((l) => !l.classList.contains('filtered') && text(l).includes(q)) : [];
    at = -1;
    hitsOut.textContent = q ? (hits.length ? `0/${hits.length}` : '0') : '';
    if (hits.length) go(1);
  }
  let timer = 0;
  input.addEventListener('input', () => { clearTimeout(timer); timer = window.setTimeout(find, 200); });
  input.addEventListener('keydown', (ev) => { if (ev.key === 'Enter') { ev.preventDefault(); go(ev.shiftKey ? -1 : 1); } });
  root.querySelectorAll<HTMLButtonElement>('[data-hit]').forEach((b) => b.addEventListener('click', () => go(Number(b.dataset.hit))));

  // --- Suggestions: the count in the header and the "only reviewed" filter.
  // Absent on the Japanese pages, which take no suggestions.
  const only = root.querySelector<HTMLButtonElement>('[data-only]');
  const onlyCount = root.querySelector<HTMLElement>('[data-only-count]');
  const stat = root.querySelector<HTMLElement>('[data-stat-suggestions]');
  function filter() {
    const on = only?.getAttribute('aria-pressed') === 'true';
    for (const l of lines) l.classList.toggle('filtered', on && !l.querySelector('[data-suggest][data-count]'));
    root!.querySelectorAll<HTMLElement>('.node').forEach((n) => { n.hidden = on && !n.querySelector('.line:not(.filtered)'); });
    if (input.value.trim()) find();
  }
  only?.addEventListener('click', () => { only.setAttribute('aria-pressed', String(only.getAttribute('aria-pressed') !== 'true')); filter(); });
  root.addEventListener('suggestions', (ev) => {
    const { items, online } = (ev as CustomEvent).detail;
    if (stat) stat.textContent = online ? String(items.length) : '–';
    if (onlyCount) onlyCount.textContent = `(${root!.querySelectorAll('.line [data-suggest][data-count]').length})`;
    filter();
  });

  // --- Table of contents follows the scroll; the phone drop-down jumps.
  const links = new Map([...root.querySelectorAll<HTMLAnchorElement>('[data-toc]')].map((a) => [a.dataset.toc!, a]));
  const seen = new Map<string, boolean>();
  const io = new IntersectionObserver((entries) => {
    for (const e of entries) seen.set(e.target.id, e.isIntersecting);
    const first = [...links.keys()].reverse().find((id) => seen.get(id));
    links.forEach((a, id) => a.classList.toggle('on', id === first));
  }, { rootMargin: '-140px 0px -55% 0px' });
  links.forEach((_, id) => { const el = document.getElementById(id); if (el) io.observe(el); });
  root.querySelector<HTMLSelectElement>('[data-jump]')?.addEventListener('change', (ev) => {
    const id = (ev.target as HTMLSelectElement).value;
    if (id) document.getElementById(id)?.scrollIntoView({ block: 'start' });
  });

  const target = location.hash && document.getElementById(location.hash.slice(1));
  if (target?.classList.contains('line')) target.scrollIntoView({ block: 'center' });
}
