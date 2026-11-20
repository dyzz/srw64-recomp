// Conditions (docs/design/level-editor.md §5.3): what an event's `if` and a step's `if` may
// test, compiled into the tree the host evaluates (story_mod.hpp, Test). The same words serve
// both places; an event's `if` keeps its older keys in the event's own fields as well, so an
// older game still reads them.
import { CompileError, isInt } from './assemble.ts';

type Obj = Record<string, any>;
export type Test = Obj;
const isObj = (v: unknown): v is Obj => !!v && typeof v === 'object' && !Array.isArray(v);
const int = (value: unknown, name: string, low: number, high: number): number => {
  if (!isInt(value) || value < low || value > high) throw new CompileError(`${name} 要是 ${low}–${high} 的整数`);
  return value;
};
const NAME = /^([a-z0-9][a-z0-9._-]*)?@[a-z0-9._-]{1,40}$/;
const FLAG = /^[a-z0-9_-]{1,32}$/;
// The sides as the map's roster numbers them: 0 ours, 1 enemy, 2 third.
export const MAP_SIDES: Record<string, number> = { player: 0, enemy: 1, third: 2 };
export const mapSide = (value: unknown, name: string): number => {
  if (typeof value !== 'string' || !(value in MAP_SIDES)) throw new CompileError(`${name} 只能是 player、enemy、third`);
  return MAP_SIDES[value];
};
const pilot = (value: unknown, name: string) => int(value, name, 0, 360);
const pilots = (value: unknown, name: string): number[] => {
  const list = Array.isArray(value) ? value : [value];
  if (!list.length) throw new CompileError(`${name} 至少写一个人物号`);
  return list.map((n) => pilot(n, name));
};
const unitRef = (value: unknown, name: string) => typeof value === 'string' && NAME.test(value) ? value : int(value, name, 0, 362);

// An area on the map: [x0, y0, x1, y1], the tiles of the deployment records, both corners in it.
export function area(value: unknown, name: string): number[] {
  if (!Array.isArray(value) || value.length !== 4) throw new CompileError(`${name} 写 [x0, y0, x1, y1]（两个角的格，含边）`);
  const [x0, y0, x1, y1] = value.map((v, k) => int(v, `${name}[${k}]`, 0, 127));
  return [Math.min(x0, x1), Math.min(y0, y1), Math.max(x0, x1), Math.max(y0, y1)];
}
// Who on the map: a pilot ({"who": 18}) or any unit of a side ({"side": "player"}).
export function who(value: Obj, name: string): { pilot: number } | { side: number } {
  const hasWho = 'who' in value, hasSide = 'side' in value;
  if (hasWho === hasSide) throw new CompileError(`${name} 写 who（人物号）或 side（player、enemy、third）其中一个`);
  return hasWho ? { pilot: pilot(value.who, `${name}.who`) } : { side: mapSide(value.side, `${name}.side`) };
}

// Keys an event's `if` has always had; the host keeps reading them from the event's fields.
export const OLD_KEYS = ['vars', 'fired', 'not_fired', 'kills', 'kills_below', 'roster', 'not_roster', 'flags', 'not_flags'];
export const NEW_KEYS = ['turn', 'on_map', 'defeated', 'absent', 'hp', 'count', 'in_area', 'route', 'choice', 'funds', 'downed', 'level', 'opponent', 'all', 'any', 'not'];
export const ALL_KEYS = [...OLD_KEYS, ...NEW_KEYS];

export type Refs = { fired: string[] };   // the ids a condition names (checked by the caller)

// One key of a condition into its tests (several for a list).
function one(key: string, value: any, refs: Refs, prefix: (id: string) => string): Test[] {
  switch (key) {
    case 'vars': {
      if (!isObj(value) || !Object.keys(value).length) throw new CompileError('vars 写 {"变量号": 0–3}');
      return Object.entries(value).map(([k, v]) => ({ var: [int(Number(k), 'vars 变量', 0, 199), int(v, 'vars 值', 0, 3)] }));
    }
    case 'kills': case 'kills_below': {
      if (!isObj(value) || !Object.keys(value).length) throw new CompileError(`${key} 写 {"人物号": 击坠数}`);
      return Object.entries(value).map(([k, v]) => {
        const p = pilot(Number(k), `${key} 人物`), n = int(v, `${key} 击坠数`, 1, 9999);
        return { kills: key === 'kills' ? [p, n, 65535] : [p, 0, n - 1] };
      });
    }
    case 'fired': case 'not_fired': {
      if (!Array.isArray(value) || !value.every((id) => typeof id === 'string' && id)) throw new CompileError(`${key} 是事件 id 的数组`);
      return value.map((id: string) => { refs.fired.push(prefix(id)); const t = { fired: prefix(id) }; return key === 'fired' ? t : { not: t }; });
    }
    case 'roster': case 'not_roster': {
      if (!isObj(value) || !Object.keys(value).length || Object.keys(value).some((k) => k !== 'pilots' && k !== 'units'))
        throw new CompileError(`${key} 写 {"pilots": [人物号], "units": [机体号或 @新机体]}`);
      const t = { roster: { pilots: (value.pilots ?? []).map((n: unknown) => pilot(n, `${key}.pilots`)), units: (value.units ?? []).map((n: unknown) => unitRef(n, `${key}.units`)) } };
      return [key === 'roster' ? t : { not: { any: [...t.roster.pilots.map((p: number) => ({ roster: { pilots: [p], units: [] } })), ...t.roster.units.map((u: unknown) => ({ roster: { pilots: [], units: [u] } }))] } }];
    }
    case 'flags': case 'not_flags': {
      if (!Array.isArray(value) || !value.every((f) => typeof f === 'string' && FLAG.test(f))) throw new CompileError(`${key} 是标记名的数组：小写字母、数字、_、-，最多 32 个`);
      return value.map((f: string) => key === 'flags' ? { flag: f } : { not: { flag: f } });
    }
    case 'turn': {
      if (!isObj(value) || !Object.keys(value).length || Object.keys(value).some((k) => k !== 'from' && k !== 'to'))
        throw new CompileError('turn 写 {"from": 回合, "to": 回合}（可只写一个）');
      const from = 'from' in value ? int(value.from, 'turn.from', 1, 99) : 1, to = 'to' in value ? int(value.to, 'turn.to', 1, 99) : 99;
      if (from > to) throw new CompileError('turn.from 不能大于 turn.to');
      return [{ turn: [from - 1, to - 1] }];
    }
    case 'on_map': case 'defeated': case 'absent':
      return pilots(value, key).map((p) => ({ status: [p, key] }));
    case 'hp': {
      if (!isObj(value) || Object.keys(value).sort().join(',') !== 'below,who') throw new CompileError('hp 写 {"who": 人物号, "below": 百分比}');
      return [{ hp: [pilot(value.who, 'hp.who'), int(value.below, 'hp.below', 1, 100)] }];
    }
    case 'count': {
      if (!isObj(value) || Object.keys(value).some((k) => !['side', 'at_most', 'at_least'].includes(k)) || !('at_most' in value || 'at_least' in value))
        throw new CompileError('count 写 {"side": player|enemy|third, "at_most": n, "at_least": n}（至少写一个界）');
      return [{ count: [mapSide(value.side, 'count.side'), 'at_least' in value ? int(value.at_least, 'count.at_least', 0, 99) : 0,
        'at_most' in value ? int(value.at_most, 'count.at_most', 0, 99) : 99] }];
    }
    case 'in_area': {
      if (!isObj(value) || Object.keys(value).some((k) => !['who', 'side', 'area'].includes(k))) throw new CompileError('in_area 写 {"who": 人物号 或 "side": 阵营, "area": [x0, y0, x1, y1]}');
      const w = who(value, 'in_area');
      return [{ in_area: ['pilot' in w ? w.pilot : -1, 'side' in w ? w.side : -1, ...area(value.area, 'in_area.area')] }];
    }
    case 'route': {
      const list = Array.isArray(value) ? value : [value];
      if (!list.length) throw new CompileError('route 写主角路线 0–3（可以是数组）');
      return [{ route: list.map((r) => int(r, 'route', 0, 3)) }];
    }
    case 'choice': return [{ choice: int(value, 'choice', 1, 3) }];
    case 'funds': {
      if (!isObj(value) || Object.keys(value).some((k) => k !== 'at_least' && k !== 'below') || !Object.keys(value).length)
        throw new CompileError('funds 写 {"at_least": 金额} 或 {"below": 金额}');
      return [{ funds: ['at_least' in value ? int(value.at_least, 'funds.at_least', 0, 99999999) : 0, 'below' in value ? int(value.below, 'funds.below', 1, 99999999) - 1 : 99999999] }];
    }
    case 'downed': return [downed(value, 'downed')];
    case 'level': {   // 3E06: one pilot's level at least another's
      if (!isObj(value) || Object.keys(value).sort().join(',') !== 'at_least_of,who') throw new CompileError('level 写 {"who": 人物号, "at_least_of": 人物号}');
      return [{ level: [pilot(value.who, 'level.who'), pilot(value.at_least_of, 'level.at_least_of')] }];
    }
    case 'opponent': {   // 3E16: in the battle now, `of` fights `is`
      if (!isObj(value) || Object.keys(value).sort().join(',') !== 'is,of') throw new CompileError('opponent 写 {"of": 人物号, "is": 人物号}');
      return [{ opponent: [pilot(value.of, 'opponent.of'), pilot(value.is, 'opponent.is')] }];
    }
    case 'any': {
      if (!Array.isArray(value) || value.length < 2) throw new CompileError('any 写两个以上的条件对象，满足其一即可');
      return [{ any: value.map((c, n) => test(c, refs, prefix, `any[${n}]`)) }];
    }
    case 'all': {
      if (!Array.isArray(value) || !value.length) throw new CompileError('all 写条件对象的数组，全部满足');
      return value.map((c, n) => test(c, refs, prefix, `all[${n}]`));
    }
    case 'not': return [{ not: test(value, refs, prefix, 'not') }];
  }
  throw new CompileError(`if 不认识 ${key}；能写 ${[...ALL_KEYS].sort().join('、')}`);
}

// Units of a side, a group or a pilot defeated in the stage so far: at least this many.
export function downed(value: unknown, name: string): Test {
  if (!isObj(value) || Object.keys(value).some((k) => !['side', 'group', 'who', 'at_least'].includes(k)) || !['side', 'group', 'who'].some((k) => k in value))
    throw new CompileError(`${name} 写 {"side": 阵营, "group": 组号, "who": 人物号, "at_least": 台数}（side、group、who 至少一个）`);
  return { downed: ['side' in value ? mapSide(value.side, `${name}.side`) : -1, 'group' in value ? int(value.group, `${name}.group`, 0, 99) : -1,
    'who' in value ? pilot(value.who, `${name}.who`) : -1, 'at_least' in value ? int(value.at_least, `${name}.at_least`, 1, 99) : 1] };
}

// A whole condition object (all its keys must hold) into one test.
export function test(value: unknown, refs: Refs, prefix: (id: string) => string = (id) => id, name = 'if'): Test {
  if (!isObj(value) || !Object.keys(value).length) throw new CompileError(`${name} 要是一个条件对象`);
  const parts = Object.entries(value).flatMap(([k, v]) => one(k, v, refs, prefix));
  return parts.length === 1 ? parts[0] : { all: parts };
}
// An event's `if`: only the keys that are new, the older ones going to their fields.
export function newTests(value: Obj, refs: Refs, prefix: (id: string) => string = (id) => id): Test | null {
  const fresh = Object.fromEntries(Object.entries(value).filter(([k]) => !OLD_KEYS.includes(k)));
  return Object.keys(fresh).length ? test(fresh, refs, prefix) : null;
}
