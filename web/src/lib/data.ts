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
export type SceneMeta = { scene: number; title: L3; episode: Episode; count: number; next: number[]; previous: number[] };
export type Cand = { route: string; who: L3; face: number | null };
export type Line =
  | { t: 'route'; marker: string; label: L3; routes: string[] | null }
  | { t: 'say'; id: number; ja: string[]; zh: string[]; en: string[]; side: number; who: L3; face?: number | null; route?: string; cands?: Cand[] }
  | { t: 'choice'; id: number; ja: string[]; zh: string[]; en: string[] };
export type Event = { phase: string; label: L3; trigger?: L3; lines: Line[] };
export type Scene = SceneMeta & { events: Event[] };

export const storyIndex = () => read<{ scenes: SceneMeta[] }>('story/index.json').scenes;
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
export type Unit = {
  id: number; series: number | null; model: string | null; name: L3; upgrade_cap: number;
  hp: number; en: number; move: number; mobility: number; armor: number; limit: number; size: string;
  movement_types: L3[]; terrain: Record<string, string>; abilities: L3[]; shield: boolean; part_slots: number;
  repair_cost: number; weapons: Weapon[]; image?: { hd?: string };
};
export type Person = {
  id: number; series: number | null; name: L3; full_name: L3; enemy: boolean; role: string | null; no_battle: boolean;
  stats: { lv1: Record<string, number>; lv99: Record<string, number> } | null; terrain: Record<string, string> | null;
  double_move_level: number | null; spirits: { level: number; id: number; name: L3; cost: number }[];
  skills: { name: L3; levels: { level: number; rank: number; label: L3 }[] }[];
  love: { partner_id: number; mutual: boolean }[];
  portrait?: { image: number; palette: number; hd: string | null; silhouette_rgb: number[] | null };
};
export type Library = {
  series: { id: number | null; name: L3 }[]; labels: Record<string, L3>;
  upgrade_types: unknown; units: Unit[]; people: Person[];
};
export const library = () => read<Library>('library.json');

export const portraitUrl = (p?: { image: number; hd: string | null; silhouette_rgb: number[] | null } | null) =>
  p?.hd ? `/gen/portraits/${p.image}${p.silhouette_rgb ? '-s' : ''}.webp` : null;
