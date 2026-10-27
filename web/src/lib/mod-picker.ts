// Build-time data for the MOD makers' picker (scripts/picker.ts): units and pilots with their
// series and picture, in the Library's order (by series), then any the Library leaves out.
import { tl, type Lang } from '../i18n';
import { library, portraitUrl } from './data';
import data from '../data/data-mods.json';

type Names = { ja: string; zh: string; en: string };
export type Pickable = { id: number; name: Names; series: number | null; img: string | null };

export function pickables(lang: Lang, unitIds: number[], pilotIds: number[]) {
  const lib = library();
  const names = new Map<number, Names>(data.units.map((u) => [u.id, u.name]));
  const pilotNames = new Map<number, Names>(data.pilots.map((p) => [p.id, p.name]));
  const order = <T extends { id: number }>(ids: number[], known: T[], make: (id: number, x?: T) => Pickable | null) => {
    const byId = new Map(known.map((x) => [x.id, x]));
    const out: Pickable[] = [];
    for (const x of known) if (ids.includes(x.id)) { const p = make(x.id, x); if (p) out.push(p); }
    for (const id of ids) if (!byId.has(id)) { const p = make(id); if (p) out.push(p); }
    return out;
  };
  const units = order(unitIds, lib.units, (id, u) => {
    const name = u?.name ?? names.get(id);
    return name ? { id, name, series: u?.series ?? null, img: u ? `/gen/units/${id}.webp` : null } : null;
  });
  const pilots = order(pilotIds, lib.people, (id, p) => {
    const name = p?.name ?? pilotNames.get(id);
    return name ? { id, name, series: p?.series ?? null, img: portraitUrl(p?.portrait) } : null;
  });
  const series = [...lib.series.map((s) => ({ id: s.id, name: s.name[tl(lang)] ?? s.name.ja })), { id: null, name: ({ zh: '其他', en: 'Other', ja: 'その他' })[tl(lang)] }];
  return { units, pilots, series };
}
