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

/** Library people (p) and units (u) shown on each secret's card, by library id. */
export const SECRET_ART: Record<string, { p?: number[]; u?: number[] }> = {
  'secret-katz': { p: [31], u: [35, 306] },
  'secret-aisha': { p: [32], u: [37] },
  'secret-elrich': { p: [29], u: [327] },
  'secret-reese': { p: [30], u: [328] },
  'secret-aina': { p: [51], u: [84] },
  'secret-allenby': { p: [9], u: [6] },
  'secret-garalia': { p: [185], u: [250] },
  'secret-sheila-elle': { p: [180, 179], u: [243, 244] },
  'secret-endless-waltz': { u: [121, 129, 131, 125, 116] },
  'secret-emma': { p: [53], u: [56] },
  'secret-erika': { p: [199] },
  'secret-naida': { p: [116], u: [163] },
  'secret-kirika': { p: [115] },
  'secret-apolly-roberto': { p: [236, 237], u: [60] },
  'secret-gale': { p: [206], u: [274] },
  'secret-rx-trio': { u: [55, 54, 58] },
  'secret-four': { p: [56] },
  'secret-ginrei': { p: [154], u: [208] },
  'secret-rosamia': { p: [59] },
  'secret-hilde': { p: [86], u: [134] },
  'secret-mk3-methuss': { u: [57, 67] },
  'secret-minerva-x': { u: [264] },
  'secret-mp-great': { u: [157] },
  'secret-gato': { p: [52], u: [75] },
  'secret-fa-hyakushiki': { u: [295] },
  'secret-zechs-epyon': { p: [101], u: [123] },
  'secret-todd-silky': { p: [186, 182] },
  'secret-rose': { p: [140], u: [188] },
  'secret-puru': { p: [54], u: [77] },
  'secret-puru-two': { p: [57] },
  'secret-mashymre-chara': { p: [58, 65], u: [78, 94] },
  'secret-neue-ziel': { u: [79] },
  'secret-schwarz': { p: [3], u: [362] },
  'secret-mp-nu': { u: [294, 296] },
  'secret-tallgeese3': { u: [136] },
  'secret-qubeley-color': { u: [77] },
  'secret-kyral': { p: [10], u: [17] },
  'secret-quess': { p: [55], u: [80] },
};
