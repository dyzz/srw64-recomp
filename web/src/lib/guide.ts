// The player guide's data (guide/data/<language>/, in git) for the guide pages.
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import type { Lang } from '../i18n';

const DIR = join(process.cwd(), '..', 'guide', 'data');
const FOLDER: Record<Lang, string> = { zh: 'zh-Hans', en: 'en', ja: 'ja' };
const cache = new Map<string, any>();
function load(lang: Lang, name: string) {
  const key = `${lang}/${name}`;
  if (!cache.has(key)) cache.set(key, JSON.parse(readFileSync(join(DIR, FOLDER[lang], `${name}.json`), 'utf8')));
  return cache.get(key);
}
export const progression = (lang: Lang) => load(lang, 'progression');
export const hiddenElements = (lang: Lang) => load(lang, 'hidden-elements');
export const reference = (lang: Lang) => load(lang, 'reference');

/** Tabs in the offline guide's order: flow chart, secrets, then the reference tabs. */
export function guideTabs(lang: Lang, ui: { flow: string; hidden: string }) {
  return [
    { id: 'flow', label: ui.flow, href: `/${lang}/guide/` },
    { id: 'hidden-elements', label: ui.hidden, href: `/${lang}/guide/hidden-elements/` },
    ...reference(lang).tabs.map((t: any) => ({ id: t.id, label: t.label, href: `/${lang}/guide/${t.id}/` })),
  ];
}

const TAGS: Record<string, string> = { '{≠}': 'diff', '{+}': 'new', '{?}': 'unverified' };
const esc = (s: string) => s.replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]!);

/** Escaped text with the {≠} {+} {?} markers turned into badges at the end. */
export function inline(text: unknown, tags: Record<string, string>): string {
  let s = String(text ?? '');
  const found = Object.entries(TAGS).filter(([m]) => s.includes(m)).map(([, k]) => k);
  for (const m of Object.keys(TAGS)) s = s.split(m).join('');
  return esc(s.trim()) + found.map((k) => `<span class="gtag ${k}">${esc(tags[k])}</span>`).join('');
}

export function isNumber(cell: unknown): boolean {
  let s = String(cell);
  for (const m of Object.keys(TAGS)) s = s.split(m).join('');
  s = s.replace(/,/g, '').replace(/\+/g, '').replace(/%/g, '').replace(/−/g, '-').trim();
  return s !== '' && !Number.isNaN(Number(s));
}
