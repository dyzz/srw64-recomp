// Stage page: the translation shown and the Japanese on or off (remembered per site
// language), search within the stage, "only lines with suggestions", "only lines changed
// since the release" with the old and new text compared, and the table of contents
// following the scroll.

const KEY = 'srw64-story';

function load(lang: string): Record<string, string> {
  try { return JSON.parse(localStorage.getItem(`${KEY}-${lang}`) || '{}'); } catch { return {}; }
}
function save(lang: string, v: Record<string, string>) {
  try { localStorage.setItem(`${KEY}-${lang}`, JSON.stringify(v)); } catch { /* private mode */ }
}

// Old against new, by characters (by words for English): equal runs plain, removed in
// <del>, added in <ins>. A rewrite with little in common shows both texts whole.
function tokens(text: string, words: boolean): string[] {
  return words ? text.match(/\s+|[\p{L}\p{N}'’]+|./gu) ?? [] : Array.from(text);
}
function escapeHtml(text: string) {
  return text.replace(/[&<>]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;' })[c]!);
}
export function diffHtml(before: string, after: string, words: boolean): string {
  const a = tokens(before, words), b = tokens(after, words);
  if (a.length * b.length > 250000) return `<span class="old">${escapeHtml(before)}</span><span class="new">${escapeHtml(after)}</span>`;
  // Longest common subsequence, table from the end.
  const w = b.length + 1, table = new Uint16Array((a.length + 1) * w);
  for (let i = a.length - 1; i >= 0; i--)
    for (let j = b.length - 1; j >= 0; j--)
      table[i * w + j] = a[i] === b[j] ? table[(i + 1) * w + j + 1] + 1 : Math.max(table[(i + 1) * w + j], table[i * w + j + 1]);
  const common = table[0];
  if (common < 0.3 * Math.max(before.length, after.length) / (words ? 4 : 1) && before && after)
    return `<span class="old">${escapeHtml(before)}</span><span class="new">${escapeHtml(after)}</span>`;
  let html = '', run = '', kind = '';
  const flush = () => { if (run) html += kind ? `<${kind}>${escapeHtml(run)}</${kind}>` : escapeHtml(run); run = ''; };
  const put = (k: string, s: string) => { if (k !== kind) { flush(); kind = k; } run += s; };
  let i = 0, j = 0;
  while (i < a.length || j < b.length) {
    if (i < a.length && j < b.length && a[i] === b[j]) { put('', a[i]); i++; j++; }
    // Where both keep the same common length, the removal goes first: old, then new.
    else if (j < b.length && (i === a.length || table[i * w + j + 1] > table[(i + 1) * w + j])) { put('ins', b[j]); j++; }
    else { put('del', a[i]); i++; }
  }
  flush();
  return html;
}
// The text of a block of paragraphs or list items, one per line.
const blockText = (el: Element) => [...el.querySelectorAll('p, li')].map((p) => p.textContent ?? '').join('\n');

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
    if (control === 'tr') filter?.();
  }
  let filter: (() => void) | undefined;
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
  // --- Lines changed since the latest game release: the comparison opens on demand, and
  // "only changed" keeps the lines changed in the translation shown, compared.
  const onlyChanged = root.querySelector<HTMLButtonElement>('[data-only-changed]');
  const changedCount = root.querySelector<HTMLElement>('[data-changed-count]');
  function compare(box: HTMLDetailsElement) {
    const out = box.querySelector<HTMLElement>('[data-diff]')!;
    if (out.dataset.done) return;
    const was = box.querySelector<HTMLTemplateElement>('template[data-was]')!.content;
    const now = box.parentElement!.querySelector('[data-now]')!;
    const before = [...was.querySelectorAll('p')].map((p) => p.textContent ?? '').join('\n');
    out.innerHTML = diffHtml(before, blockText(now), box.dataset.upd === 'en');
    out.dataset.done = '1';
  }
  root.querySelectorAll<HTMLDetailsElement>('details.upd').forEach((d) => d.addEventListener('toggle', () => { if (d.open) compare(d); }));
  const changedIn = (l: HTMLElement) => (l.dataset.changed ?? '').split(' ').includes(root!.dataset.tr ?? '');
  filter = function () {
    const onReviewed = only?.getAttribute('aria-pressed') === 'true';
    const onChanged = onlyChanged?.getAttribute('aria-pressed') === 'true';
    for (const l of lines)
      l.classList.toggle('filtered', (onReviewed && !l.querySelector('[data-suggest][data-count]')) || (onChanged && !changedIn(l)));
    if (onChanged) root!.querySelectorAll<HTMLDetailsElement>(`.line:not(.filtered) details.upd[data-upd="${root!.dataset.tr}"]`).forEach((d) => { d.open = true; });
    if (changedCount) changedCount.textContent = `(${lines.filter(changedIn).length})`;
    root!.querySelectorAll<HTMLElement>('.node').forEach((n) => { n.hidden = (onReviewed || onChanged) && !n.querySelector('.line:not(.filtered)'); });
    if (input.value.trim()) find();
  };
  const toggle = (b: HTMLButtonElement) => { b.setAttribute('aria-pressed', String(b.getAttribute('aria-pressed') !== 'true')); filter!(); };
  only?.addEventListener('click', () => toggle(only));
  onlyChanged?.addEventListener('click', () => toggle(onlyChanged));
  if (onlyChanged && new URLSearchParams(location.search).has('changed')) toggle(onlyChanged);
  else filter();
  root.addEventListener('suggestions', (ev) => {
    const { items, online } = (ev as CustomEvent).detail;
    if (stat) stat.textContent = online ? String(items.length) : '–';
    if (onlyCount) onlyCount.textContent = `(${root!.querySelectorAll('.line [data-suggest][data-count]').length})`;
    filter!();
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
