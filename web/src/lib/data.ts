// Build-time access to the exported game data in web/.data (export_story.py,
// export_library.py). Read with fs because the files are large and not in git.
import { readFileSync, existsSync } from 'node:fs';
import { join } from 'node:path';
import type { Lang } from '../i18n';

const DATA = join(process.cwd(), '.data');
const cache = new Map<string, unknown>();

function read<T>(path: string): T {
  if (!cache.has(path)) {
    const file = join(DATA, path);
    if (!existsSync(file)) {
      throw new Error(`${file} is missing: run web/scripts/export_story.py and export_library.py first`);
    }
    cache.set(path, JSON.parse(readFileSync(file, 'utf8')));
  }
  return cache.get(path) as T;
}

export type L3 = { ja: string; zh: string; en: string };
export type Episode = { stage: string; section: L3; lanes: L3[]; routes: string[] } | null;
// changed: lines whose zh / en text differs from the latest game release (StorySource.baseline).
export type SceneMeta = { scene: number; title: L3; episode: Episode; count: number; changed?: { zh: number; en: number }; next: number[]; previous: number[] };
// What the exported translations are (web/scripts/export_story.py): the release they are
// compared with, the last commit that changed them, uncommitted edits in the export.
export type StorySource = { baseline: string; commit: string; date: string; dirty: boolean };
// was: the release's text of each language that reads differently now ([] if it had none).
type Was = { was?: { zh?: string[]; en?: string[] } };
export type Cand = { route: string; who: L3; face: number | null };
export type Line =
  | { t: 'route'; marker: string; label: L3; routes: string[] | null }
  | ({ t: 'say'; id: number; ja: string[]; zh: string[]; en: string[]; side: number; who: L3; face?: number | null; route?: string; cands?: Cand[] } & Was)
  | ({ t: 'choice'; id: number; ja: string[]; zh: string[]; en: string[] } & Was);
export type Event = { phase: string; label: L3; trigger?: L3; lines: Line[] };
export type Scene = SceneMeta & { events: Event[] };

export const storyIndex = () => read<{ scenes: SceneMeta[] }>('story/index.json').scenes;
export const storySource = () => read<{ source?: StorySource }>('story/index.json').source ?? null;

// --- Translation history (export_history.py) ----------------------------------
// The commits since the latest game release that changed translations, newest first;
// each commit's changed lines grouped by story scene, battle speaker, other file, terms.
export type HistoryCommit = { commit: string; date: string; subject: string; counts: { zh: number; en: number }; scenes: { zh: number[]; en: number[] }; terms: number };
export type HistoryLine = { key: string; id: number | null; who?: string; ja: string; was: { zh?: string; en?: string }; now: { zh?: string; en?: string } };
export type HistoryGroup = { kind: 'story' | 'battle' | 'other' | 'terms'; group: string; title?: L3 | null; lines: HistoryLine[] };
export const historyIndex = () =>
  existsSync(join(DATA, 'history/index.json')) ? read<{ baseline: string; commits: HistoryCommit[] }>('history/index.json') : null;
export const historyCommit = (commit: string) => read<HistoryCommit & { groups: HistoryGroup[] }>(`history/${commit}.json`);
export const scene = (n: number) => read<Scene>(`story/scenes/${String(n).padStart(4, '0')}.json`);

/** Scenes grouped the way the guide groups episodes; scenes outside the campaign last. */
export function storyGroups(lang: Lang) {
  const groups: { key: string; title: string; scenes: SceneMeta[] }[] = [];
  const outside = { zh: '正篇之外：废案、预留与特殊场景', en: 'Outside the campaign: cut, reserved and special scenes', ja: '本編以外：没シナリオ・予備・特殊な場面' };
  for (const s of storyIndex()) {
    const title = s.episode ? s.episode.section[lang] : outside[lang];
    const key = s.episode ? s.episode.section.zh : 'outside';
    let g = groups.find((x) => x.key === key);
    if (!g) groups.push((g = { key, title, scenes: [] }));
    g.scenes.push(s);
  }
  return groups;
}

// --- Library -----------------------------------------------------------------
export type Weapon = {
  id: number; name: L3; markers: string[]; power: number; max_power: number; upgrade_type: number;
  range: [number, number]; hit: number; crit: number; ammo: number | null; en: number | null; morale: number | null;
  terrain: Record<string, string>; unlock_at_full_upgrade: unknown; combination: boolean;
};
// A text of a Library entry that reads differently from the latest game release
// (export_library.py): what kind of field, the Japanese, and old / new per language changed.
export type TextChange = { kind: string; ja: string; was: { zh?: string; en?: string }; now: { zh?: string; en?: string } };
type Changes = { changes?: TextChange[]; changed?: { zh: number; en: number } };
export type Unit = Changes & {
  id: number; series: number | null; model: string | null; name: L3; upgrade_cap: number;
  hp: number; en: number; move: number; mobility: number; armor: number; limit: number; size: string;
  movement_types: L3[]; terrain: Record<string, string>; abilities: L3[]; shield: boolean; part_slots: number;
  repair_cost: number; weapons: Weapon[]; image?: { hd?: string };
};
export type Person = Changes & {
  id: number; series: number | null; name: L3; full_name: L3; enemy: boolean; role: string | null; no_battle: boolean;
  stats: { lv1: Record<string, number>; lv99: Record<string, number> } | null; terrain: Record<string, string> | null;
  double_move_level: number | null; spirits: { level: number; id: number; name: L3; cost: number }[];
  skills: { name: L3; levels: { level: number; rank: number; label: L3 }[] }[];
  love: { partner_id: number; mutual: boolean }[];
  portrait?: { image: number; palette: number; hd: string | null; silhouette_rgb: number[] | null };
};
export type Library = {
  series: { id: number | null; name: L3 }[]; labels: Record<string, L3>;
  upgrade_types: unknown; units: Unit[]; people: Person[]; baseline?: string;
};
export const library = () => read<Library>('library.json');

export const portraitUrl = (p?: { image: number; hd: string | null; silhouette_rgb: number[] | null } | null) =>
  p?.hd ? `/gen/portraits/${p.image}${p.silhouette_rgb ? '-s' : ''}.webp` : null;
