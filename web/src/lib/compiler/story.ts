// Story events (docs/design/mod-packages.md §7, level-editor.md §5.3): when an event fires, what must hold and what it does,
// into what the host loads (srw64.story-image.v1): the trigger and conditions the host
// checks, the instructions for the original script engine, the new lines.
import { assemble, CompileError, isInt, type Command } from './assemble.ts';
import { ALL_KEYS, area, downed, newTests, test as condition, who as onMap, type Refs, type Test } from './conditions.ts';

export const TEXT = 0xFFFE;       // a line's place in the words until the host numbers it
export const NUMBER = 0xFFFD;     // a new unit's or weapon's place, numbered by the host
export const CHECK = 0xFFFC;      // a step's condition the host decides: 0 or 1 (Builder.checks)
const NAME = /^([a-z0-9][a-z0-9._-]*)?@[a-z0-9._-]{1,40}$/;
const SIDES: Record<string, number> = { player: 1, enemy: 2, third: 3, any: 4 };
const LONGEST = 200;
const LOCALES = new Set(['ja', 'zh-Hans', 'zh-Hant', 'en', 'ko']);
export const FLAG = /^[a-z0-9_-]{1,32}$/;

type Obj = Record<string, any>;
// kind: a line (the default), a choice's options ("<BR>" between them) or a stage's own objective.
export type Line = { speaker: number | string; lines: Record<string, string>; kind?: 'choice' | 'objective' | 'battle' };
// The original's silhouettes: a character's portrait in the dark palette 609, named 「???」,
// actors 317-353 (assets/original-data/records/actors.jsonl, by the portrait's pixels).
export const SILHOUETTES: Record<number, number> = { 165: 317, 162: 318, 156: 319, 174: 320, 141: 321, 143: 322, 240: 323, 36: 324, 56: 325,
  103: 326, 98: 327, 18: 328, 3: 329, 13: 330, 12: 331, 8: 332, 11: 333, 10: 334, 32: 335, 34: 336, 33: 337, 35: 338, 219: 339, 199: 340,
  93: 341, 14: 342, 245: 343, 142: 344, 191: 349, 90: 350, 161: 351, 286: 352, 38: 353 };
// When a battle quote is drawn (80222B14's context, docs/data/battle-quotes.md).
export const QUOTE_MOMENTS: Record<string, number> = { attack: 0, defeated: 1, heavy: 2, damaged: 3, light: 4, evade: 5, no_effect: 6, no_ammo: 7, out_of_range: 8 };
const QUOTE_LONGEST = 60;
const CHOICE_LONGEST = 24;   // code points an option may have: one row of the choice window
export type StoryEvent = Obj;
export type StoryImage = { schema: string; events: StoryEvent[]; texts: Line[]; stage_info?: Obj[]; campaign?: string; patches?: Obj[] };

const isObj = (v: unknown): v is Obj => !!v && typeof v === 'object' && !Array.isArray(v);
const int = (value: unknown, name: string, low: number, high: number): number => {
  if (!isInt(value) || value < low || value > high) throw new CompileError(`${name} 要是 ${low}–${high} 的整数`);
  return value;
};
const side = (value: unknown, name: string, fallback = 'any') => {
  const key = value === undefined || value === null ? fallback : value;
  if (typeof key !== 'string' || !(key in SIDES)) throw new CompileError(`${name} 只能是 ${Object.keys(SIDES).join(', ')}`);
  return SIDES[key];
};
const named = (value: unknown): value is string => typeof value === 'string' && NAME.test(value);
const unit = (value: unknown, name: string) => named(value) ? value : int(value, name, 0, 362);
const pilot = (value: unknown, name: string, top = 360) => named(value) ? value : int(value, name, 0, top);
const weapon = (value: unknown, name: string) => named(value) ? value : int(value, name, 0, 1328);
const codePoints = (s: string) => [...s].length;

function roster(value: unknown, name: string): Obj {
  if (!isObj(value) || !Object.keys(value).length || Object.keys(value).some((k) => k !== 'pilots' && k !== 'units'))
    throw new CompileError(`${name} 写 {"pilots": [人物号], "units": [机体号或 @新机体]}`);
  const out: Obj = {};
  for (const kind of ['pilots', 'units']) if (kind in value)
    out[kind] = (value[kind] as unknown[]).map((n) => kind === 'pilots' ? int(n, `${name}.${kind}`, 0, 360) : unit(n, `${name}.${kind}`));
  return out;
}
function byLocale(value: unknown, name: string): Obj {
  if (!isObj(value) || !('ja' in value)) throw new CompileError(`${name} 写成 {"ja": …, "zh-Hans": …, "en": …}，至少有 ja`);
  for (const [locale, words] of Object.entries(value)) {
    if (!LOCALES.has(locale)) throw new CompileError(`${name}：不认识的语言 ${locale}`);
    if (typeof words !== 'string' || !words.trim() || codePoints(words) > LONGEST) throw new CompileError(`${name}.${locale}：为空或超过 ${LONGEST} 字`);
  }
  return { ...value };
}
function stageInfo(rows: unknown): Obj[] {
  if (!Array.isArray(rows)) throw new CompileError('stage_info 要是数组');
  return rows.map((row, n) => {
    const where = `stage_info[${n}]`;
    if (!isObj(row) || Object.keys(row).some((k) => !['scene', 'title', 'text', 'done'].includes(k))) throw new CompileError(`${where} 只能写 scene、title、text、done`);
    const entry: Obj = { scene: int(row.scene, `${where}.scene`, 0, 255), title: byLocale(row.title, `${where}.title`), text: byLocale(row.text, `${where}.text`) };
    if ('done' in row) entry.done = roster(row.done, `${where}.done`);
    return entry;
  });
}
function flags(value: unknown, name: string): string[] {
  if (!Array.isArray(value) || !value.every((f) => typeof f === 'string' && FLAG.test(f))) throw new CompileError(`${name} 是标记名的数组：小写字母、数字、_、-，最多 32 个`);
  return [...value];
}

// The host's trigger name and its parameters (story_mod.hpp, Event::a/b/c).
export function trigger(when: unknown): [string, number[]] {
  if (!isObj(when) || Object.keys(when).length !== 1) throw new CompileError('when 要写且只写一种时机');
  const [kind, value] = Object.entries(when)[0];
  if (kind === 'turn') {
    if (!isObj(value) || Object.keys(value).some((k) => k !== 'turn' && k !== 'side')) throw new CompileError('turn 写 {"turn": 画面回合, "side": player|enemy|third}');
    return ['turn', [int(value.turn, 'turn', 1, 99) - 1, side(value.side, 'side', 'player')]];
  }
  if (kind === 'defeated') {
    if (!isObj(value)) return ['defeated', [int(value, 'defeated', 0, 360)]];
    if (Object.keys(value).some((k) => k !== 'who' && k !== 'by')) throw new CompileError('defeated 写人物号，或 {"who": 人物号, "by": 击破者}');
    return ['defeated', [int(value.who, 'defeated.who', 0, 360), 'by' in value ? int(value.by, 'defeated.by', 1, 360) : 0]];
  }
  if (kind === 'downed') return ['downed', downed(value, 'downed').downed];
  if (kind === 'turns_after') {   // the original's delayed count (type 1, 3D52): N turns after an event ran
    if (!isObj(value) || Object.keys(value).some((k) => !['event', 'turns', 'side'].includes(k)) || typeof value.event !== 'string')
      throw new CompileError('turns_after 写 {"event": 事件 id, "turns": 回合数, "side": player|enemy|third}');
    return ['turns_after', [int(value.turns, 'turns_after.turns', 1, 99), side(value.side, 'side', 'player')]];
  }
  if (kind === 'group_gone') return ['group_gone', [int(value, 'group_gone', 0, 99)]];
  if (kind === 'reach') {
    if (!isObj(value) || Object.keys(value).some((k) => !['who', 'side', 'area'].includes(k))) throw new CompileError('reach 写 {"who": 人物号 或 "side": 阵营, "area": [x0, y0, x1, y1]}');
    const w = onMap(value, 'reach');
    return ['reach', ['pilot' in w ? w.pilot : -1, 'side' in w ? w.side : -1, ...area(value.area, 'reach.area')]];
  }
  if (kind === 'hp_below') return ['hp_below', [int(value?.who, 'who', 0, 360), int(value?.percent, 'percent', 1, 100)]];
  if (kind === 'battle') {
    const between = value?.between ?? [];
    if (!Array.isArray(between) || between.length !== 2) throw new CompileError('battle.between 写两个人物号（0 = 任意）');
    const moment = value.moment ?? 'after';
    if (moment !== 'after' && moment !== 'before') throw new CompileError('battle.moment 只能是 after 或 before');
    return [`battle_${moment}`, [int(between[0], 'between', 0, 360), int(between[1], 'between', 0, 360)]];
  }
  if (kind === 'enemies_gone') {
    const v = value === true ? {} : value;
    const by = v?.by_turn;
    return ['enemies_gone', [side(v?.side, 'side'), by === undefined || by === null ? 254 : int(by, 'by_turn', 1, 99) - 1]];
  }
  if (kind === 'remaining') {
    const which = value?.side ?? 'enemy';
    if (!['enemy', 'third', 'player'].includes(which)) throw new CompileError('remaining.side 只能是 enemy、third 或 player');
    return ['remaining', [which === 'third' ? 1 : which === 'player' ? 2 : 0, int(value?.at_most, 'at_most', 0, 99), side(value?.phase, 'phase')]];
  }
  if (kind === 'persuade') return ['persuade', [int(value?.by, 'by', 0, 360), int(value?.target, 'target', 0, 360)]];
  if (kind === 'after') {
    if (!['opening', 'setup', 'end'].includes(value)) throw new CompileError('after 只能是 opening、setup 或 end');
    return [`after_${value}`, []];
  }
  throw new CompileError(`不认识的时机 ${kind}`);
}

type Operand = number | [string, number | string];

// The commands of one event and the lines they show.
export class Builder {
  commands: Command[] = [];
  refs: number[] = [];                              // command indexes whose argument is a line
  refOperand: Record<number, number> = {};          // ...when it is not their first operand
  like: [number, number][] = [];                    // (command index, pilot): a join's level, set by the host
  crew: Obj[] = [];
  builds: (number | string)[] = [];
  named: [number, number, string, string][] = [];   // (command, operand, name, kind)
  checks: [number, Test][] = [];                    // (command index whose operand is CHECK, its condition)
  refs_fired: Refs = { fired: [] };                 // event ids the steps' conditions name
  prefix: (id: string) => string = (id) => id;      // a campaign stage's own event ids
  texts: Line[];
  stages: Record<string, number> | null;            // a campaign's stages (key -> scene); null in the main story

  constructor(texts: Line[], stages: Record<string, number> | null = null) { this.texts = texts; this.stages = stages; }

  op(opcode: number, ...args: Operand[]): void {
    const values = args.map((a, k) => {
      if (!Array.isArray(a)) return a;
      const [kind, value] = a;
      if (named(value)) { this.named.push([this.commands.length, k, value, kind]); return NUMBER; }
      return value as number;
    });
    this.commands.push({ op: opcode, args: values });
  }

  line(speaker: unknown, text: unknown): number {
    const who = pilot(speaker, 'say');
    if (!isObj(text) || !Object.keys(text).length) throw new CompileError('text 写成 {"ja": …, "zh-Hans": …, "en": …}');
    if (!('ja' in text)) throw new CompileError('每句台词都要有 ja（原版界面和缺译时用它）');
    for (const [locale, words] of Object.entries(text)) {
      if (!LOCALES.has(locale)) throw new CompileError(`不认识的语言 ${locale}`);
      if (typeof words !== 'string' || !words.trim()) throw new CompileError(`${locale} 的台词是空的`);
      if (codePoints(words) > LONGEST) throw new CompileError(`${locale} 的台词超过 ${LONGEST} 字，拆成几句`);
      if (words.includes('<STOP>') || words.includes('<END>')) throw new CompileError('台词里不写 <STOP>／<END>：一句一个 say，阅读器自己分页');
    }
    this.texts.push({ speaker: who, lines: { ...text } });
    return this.texts.length - 1;
  }

  // A map position (800A38DC): [x, y] is absolute (x << 8 | y); {"near": pilot, "dir":
  // "at"|"up"|"right"|"down"|"left", "by": cells} is relative to where that pilot stands,
  // which 3D54 remembers first (codes 0x40-0x44).
  position(value: unknown, name: string): number {
    if (Array.isArray(value) && value.length === 2) return int(value[0], `${name}[0]`, 0, 127) << 8 | int(value[1], `${name}[1]`, 0, 127);
    if (isObj(value) && 'near' in value) {
      const dirs = ['at', 'up', 'right', 'down', 'left'];
      const dir = value.dir ?? 'at';
      if (!dirs.includes(dir) || Object.keys(value).some((k) => !['near', 'dir', 'by'].includes(k))) throw new CompileError(`${name} 写 [x, y] 或 {"near": 人物号, "dir": ${dirs.join('|')}, "by": 格数}`);
      this.op(0x3D54, int(value.near, `${name}.near`, 0, 360));
      return (0x40 + dirs.indexOf(dir)) << 8 | (dir === 'at' ? 0 : int(value.by ?? 1, `${name}.by`, 1, 63));
    }
    throw new CompileError(`${name} 写 [x, y] 或 {"near": 人物号, "dir": …, "by": 格数}`);
  }

  // The stage and presentation commands (level-editor.md §5.8): true when `kind` is one.
  stage(kind: string, value: any): boolean {
    const who = (v: unknown, name: string) => int(v, name, 0, 360);
    const obj = (keys: string[], hint: string) => {
      if (!isObj(value) || Object.keys(value).some((k) => !keys.includes(k))) throw new CompileError(`${kind} 写 ${hint}`);
      return value;
    };
    const SIDE: Record<string, number> = { player: 0, enemy: 1, third: 2 };
    switch (kind) {
      case 'camera': {   // 3D35: scroll there; "wait" blocks until it arrives
        const v = isObj(value) && 'to' in value ? obj(['to', 'wait'], '{"to": 位置, "wait": true}') : { to: value };
        this.op(0x3D35, this.position(v.to, 'camera'), v.wait ? 0 : 1);
        return true;
      }
      case 'camera_follows': if (typeof value !== 'boolean') throw new CompileError('camera_follows 写 true/false'); this.op(0x3D66, value ? 0 : 1); return true;
      case 'camera_back': this.op(0x3D68); return true;
      case 'effect': {   // 3D37: 0 big explosion, 1 hit sparks, 11 shield ring, ...
        const v = obj(['id', 'at'], '{"id": 特效编号, "at": 位置}');
        this.op(0x3D37, int(v.id, 'effect.id', 0, 255), this.position(v.at, 'effect.at'));
        return true;
      }
      case 'shake': this.op(0x3D36, int(value, 'shake', 1, 20)); return true;
      case 'destroy': this.op(0x3D4F, who(value, 'destroy')); return true;
      case 'destroy_group': this.op(0x3D4F, 500 + int(value, 'destroy_group', 0, 99)); return true;
      case 'move': {   // 3D3C: a pilot's unit walks to a cell
        const v = obj(['who', 'to'], '{"who": 人物号, "to": 位置}');
        const at = this.position(v.to, 'move.to');
        this.op(0x3D3C, who(v.who, 'move.who'), at);
        return true;
      }
      case 'change_side': {
        const v = obj(['who', 'to'], '{"who": 人物号, "to": "player"|"enemy"|"third"}');
        if (!(v.to in SIDE)) throw new CompileError('change_side.to 只能是 player、enemy、third');
        this.op(0x3D58, who(v.who, 'change_side.who'), SIDE[v.to]);
        return true;
      }
      case 'can_act': {
        const v = obj(['who', 'value'], '{"who": 人物号, "value": true/false}');
        if (typeof v.value !== 'boolean') throw new CompileError('can_act.value 写 true/false');
        this.op(0x3D69, who(v.who, 'can_act.who'), v.value ? 1 : 0);
        return true;
      }
      case 'upgrade': {
        const v = obj(['unit', 'level'], '{"unit": 机体号, "level": 段数}');
        this.op(0x3D6C, ['unit', unit(v.unit, 'upgrade.unit')], int(v.level, 'upgrade.level', 0, 15));
        return true;
      }
      case 'funds': {
        const n = int(value, 'funds', 1000, 999000);
        if (n % 1000) throw new CompileError('funds 是 1000 的倍数（原版按千加）');
        this.op(0x3D5B, n / 1000);
        return true;
      }
      case 'bump': {   // 3E0E / 3E0F: a variable one up or down, round the four values
        const v = isObj(value) ? obj(['var', 'by'], '{"var": 变量号, "by": 1 或 -1}') : { var: value, by: 1 };
        if (v.by !== 1 && v.by !== -1) throw new CompileError('bump.by 只能是 1 或 -1');
        this.op(v.by === 1 ? 0x3E0E : 0x3E0F, int(v.var, 'bump.var', 0, 199));
        return true;
      }
      case 'morale_down': if (value !== true) throw new CompileError('morale_down 写 true'); this.op(0x3D5F, 0); return true;
      case 'transform': {
        const v = obj(['who', 'from', 'to'], '{"who": 人物号, "from": 机体号, "to": 机体号}');
        this.op(0x3D50, who(v.who, 'transform.who'), ['unit', unit(v.from, 'transform.from')], ['unit', unit(v.to, 'transform.to')]);
        return true;
      }
      case 'combine': {
        const v = obj(['who', 'join'], '{"who": 人物号, "join": true/false}');
        this.op(0x3D5C, who(v.who, 'combine.who'), v.join ? 1 : 0);
        return true;
      }
      case 'combined_form': {   // 3D5D: 0 ダンクーガ, 1 コン・バトラーV
        const v = obj(['family', 'join'], '{"family": "dancouga"|"combattler", "join": true/false}');
        if (!['dancouga', 'combattler'].includes(v.family)) throw new CompileError('combined_form.family 只能是 dancouga 或 combattler');
        this.op(0x3D5D, v.join ? 1 : 0, v.family === 'dancouga' ? 0 : 1);
        return true;
      }
      case 'battle_scene': {   // 3D49: two entries of the scripted battle table 800C9A18
        if (!Array.isArray(value) || value.length !== 2) throw new CompileError('battle_scene 写 [表项 A, 表项 B]');
        this.op(0x3D49, int(value[0], 'battle_scene', 0, 999), int(value[1], 'battle_scene', 0, 999));
        return true;
      }
      case 'map_weapon': {
        const v = obj(['who', 'unit', 'weapon', 'dir'], '{"who": 人物号, "unit": 机体号, "weapon": MAP 兵器号, "dir": 方向}');
        this.op(0x3D51, who(v.who, 'map_weapon.who'), ['unit', unit(v.unit, 'map_weapon.unit')], ['weapon', weapon(v.weapon, 'map_weapon.weapon')], int(v.dir ?? 0, 'map_weapon.dir', 0, 0xFFFF), 0);
        return true;
      }
      case 'formation': {
        const v = obj(['at', 'per_row'], '{"at": [x, y], "per_row": 每行台数}');
        if (!Array.isArray(v.at) || v.at.length !== 2) throw new CompileError('formation.at 写 [x, y]');
        this.op(0x3D60, int(v.at[0], 'formation.at', 0, 127), int(v.at[1], 'formation.at', 0, 127), int(v.per_row ?? 3, 'formation.per_row', 1, 30));
        return true;
      }
      case 'powerup_scene': this.op(0x3D67, int(value, 'powerup_scene', 0, 10)); return true;
      case 'godmars': this.op(0x3D6A, int(value, 'godmars', 0, 4)); return true;
      case 'launch': this.op(0x3D75, value === 'all' ? 999 : who(value, 'launch')); return true;
      case 'all_lost_defeat': if (typeof value !== 'boolean') throw new CompileError('all_lost_defeat 写 true/false'); this.op(0x3D61, value ? 0 : 1); return true;
      case 'player_phase': if (value !== true) throw new CompileError('player_phase 写 true'); this.op(0x3D6D, 0); return true;
      case 'swap_identity': {
        if (!Array.isArray(value) || value.length !== 2) throw new CompileError('swap_identity 写 [原人物号, 新人物号]');
        this.op(0x3D62, who(value[0], 'swap_identity'), who(value[1], 'swap_identity'));
        return true;
      }
      case 'land': this.op(0x3D63, who(value, 'land')); return true;
      case 'copilot': {
        const v = obj(['pilot', 'add'], '{"pilot": 主驾驶, "add": 同乘人物}');
        this.op(0x3D70, who(v.pilot, 'copilot.pilot'), who(v.add, 'copilot.add'));
        return true;
      }
      // 3D6E: 8020AB14 on the pilot's roster unit; the original writes it after a scripted
      // defeat (3D4F) and before a pilot leaves the army (3D5A …,4000).
      case 'off_map': this.op(0x3D6E, who(value, 'off_map')); return true;
      // 3D64's general branch (80212668): the unit on the map whose main pilot this is drops
      // them from its crew (+0x34 less one, +0x38… moved up), so its first co-pilot takes over,
      // and the pilot's record is freed (seen on Windows: 28 gone, 24 flying Manami's unit).
      // Its 500,500 form is the original's one fixed story case.
      case 'hand_over': this.op(0x3D64, who(value, 'hand_over'), 0); return true;
      case 'squad_mark': {   // 3D6B: the squad mark (pilot +0 bit 7, unit +0x0C bit 6)
        const v = obj(['pilot', 'unit', 'on'], '{"pilot": 人物号, "unit": 机体号, "on": true/false}');
        if (typeof v.on !== 'boolean') throw new CompileError('squad_mark.on 写 true/false');
        this.op(0x3D6B, who(v.pilot, 'squad_mark.pilot'), ['unit', unit(v.unit, 'squad_mark.unit')], v.on ? 1 : 0);
        return true;
      }
      case 'squad_split': {   // 3D73: mark the original's list A (D_800D06A8) or B (D_800D06E4)
        if (value !== 'a' && value !== 'b') throw new CompileError('squad_split 写 "a" 或 "b"（原版的两份分队名单）');
        this.op(0x3D73, value === 'a' ? 1 : 0);
        return true;
      }
      case 'squad_clear': if (value !== true) throw new CompileError('squad_clear 写 true'); this.op(0x3D74); return true;
      case 'world_map': this.op(0x3D32, int(value, 'world_map', 0, 255)); return true;
      case 'travel': this.op(0x3D33, int(value, 'travel', 0, 255)); return true;
      case 'vehicle': this.op(0x3D72, int(value, 'vehicle', 0, 14)); return true;
      case 'change_map': {   // 3D34: an original map in the middle of a stage
        const v = obj(['map', 'at'], '{"map": 原版地图号, "at": [x, y]}');
        if (!Array.isArray(v.at) || v.at.length !== 2) throw new CompileError('change_map.at 写 [x, y]');
        this.op(0x3D34, int(v.map, 'change_map.map', 0, 157), int(v.at[0], 'change_map.at', 0, 127), int(v.at[1], 'change_map.at', 0, 127), 1);
        return true;
      }
      case 'sortie': {   // 3D3D: the player picks who goes out, placed from the group's first record
        const v = obj(['group', 'max'], '{"group": 出击位置的组, "max": 台数（1–15）|"all"（全部自动）|"ship"（选母舰）}');
        const max = v.max === 'all' ? 200 : v.max === 'ship' ? 0 : int(v.max, 'sortie.max', 1, 15);
        this.op(0x3D3D, 0, 0, 0, max, int(v.group, 'sortie.group', 0, 99));
        return true;
      }
      case 'green_area': {   // 3D56: blinks a green rectangle; x*10 + width, y*10 + height (each at most 9)
        if (!Array.isArray(value) || value.length !== 4) throw new CompileError('green_area 写 [x0, y0, x1, y1]');
        const [x0, y0, x1, y1] = value.map((v: unknown) => int(v, 'green_area', 0, 127));
        const w = Math.abs(x1 - x0) + 1, h = Math.abs(y1 - y0) + 1;
        if (w > 9 || h > 9) throw new CompileError('green_area 宽高各最多 9 格（原版的编码只有一位）');
        this.op(0x3D56, Math.min(x0, x1) * 10 + w, Math.min(y0, y1) * 10 + h, 60);
        return true;
      }
      case 'forbid': {   // 3D59: kept out of the next sortie choice; a unit as {"unit": n}
        const n = isObj(value) ? 500 + int(obj(['unit'], '人物号或 {"unit": 机体号}').unit, 'forbid.unit', 0, 362) : who(value, 'forbid');
        this.op(0x3D59, n);
        return true;
      }
    }
    return false;
  }

  actions(rows: unknown): void {
    if (!Array.isArray(rows)) throw new CompileError('do／then／else 要是数组');
    for (const row of rows) this.action(row);
  }

  action(row: unknown): void {
    if (!isObj(row)) throw new CompileError('每个动作是一个对象');
    if ('op' in row) { this.commands.push({ op: row.op, args: row.args ?? [] }); return; }
    if ('say' in row) {
      const mode = int(row.mode ?? 0, 'mode', 0, 5);
      if (Object.keys(row).some((k) => !['say', 'text', 'mode', 'silhouette'].includes(k))) throw new CompileError('say 只写 say、text、mode、silhouette');
      let speaker = row.say;
      if (row.silhouette !== undefined && row.silhouette !== false) {
        if (row.silhouette !== true) throw new CompileError('silhouette 写 true');
        if (!isInt(speaker) || !(speaker in SILHOUETTES)) throw new CompileError(`人物 ${speaker} 原版没有剪影头像；有剪影的：${Object.keys(SILHOUETTES).join(', ')}`);
        speaker = SILHOUETTES[speaker];
      }
      const index = this.line(speaker, row.text);
      this.refs.push(this.commands.length);
      this.op(0x3D3E + mode, index);
      return;
    }
    if ('if' in row) {
      const cond = row.if;
      if (Object.keys(row).some((k) => !['if', 'then', 'else'].includes(k))) throw new CompileError('如果写 {"if": 条件, "then": [...], "else": [...]}');
      const otherwise: unknown[] = row.else ?? [];
      const has = Array.isArray(otherwise) ? otherwise.length > 0 : !!otherwise;
      if (!isObj(cond) || Object.keys(cond).sort().join(',') !== 'is,var') {
        // Any other condition: the host writes 0 or 1 into ACC before the step runs.
        this.checks.push([this.commands.length, condition(cond, this.refs_fired, this.prefix)]);
        this.op(0x3E14, CHECK);
        this.op(0x3E08, 1);
        this.actions(row.then ?? []);
        if (has) this.op(0x3E14, 1);
        this.op(0x3E1D);
        if (has) { this.op(0x3E08, 0); this.actions(otherwise); this.op(0x3E1D); }
        return;
      }
      this.op(0x3E14, 0);
      this.op(0x3E03, int(cond.var, 'var', 0, 199), int(cond.is, 'is', 0, 3), has ? 1 : 0);
      this.actions(row.then ?? []);
      if (has) this.op(0x3E14, 1);
      this.op(0x3E1D);
      if (has) { this.op(0x3E08, 0); this.actions(otherwise); this.op(0x3E1D); }
      return;
    }
    if ('line' in row) {
      if (Object.keys(row).some((k) => k !== 'line' && k !== 'mode')) throw new CompileError('line 写 {"line": 原版文本号, "mode": 0–5}');
      this.op(0x3D3E + int(row.mode ?? 0, 'mode', 0, 5), int(row.line, 'line', 0, 0xC7FF));
      return;
    }
    if ('choose' in row) {
      const value = row.choose;
      if (!isObj(value) || Object.keys(value).sort().join(',') !== 'options,text' || !Array.isArray(value.options) || value.options.length < 2 || value.options.length > 3)
        throw new CompileError('choose 写 {"text": 原版选项文本号 或 {"ja": [选项…], …}, "options": [[动作…], [动作…]]}，2–3 项');
      if (isObj(value.text)) {
        // New text: the options by locale, one row each; the record is the host's (0xC800 up).
        const lines: Record<string, string> = {};
        for (const [locale, list] of Object.entries(value.text)) {
          if (!LOCALES.has(locale)) throw new CompileError(`choose.text：不认识的语言 ${locale}`);
          if (!Array.isArray(list) || list.length !== value.options.length || !list.every((o) => typeof o === 'string' && o.trim() && !o.includes('<') && codePoints(o) <= CHOICE_LONGEST))
            throw new CompileError(`choose.text.${locale} 要写 ${value.options.length} 个选项文字（和 options 一样多），每个不超过 ${CHOICE_LONGEST} 字`);
          lines[locale] = list.map((o: string) => o.trim()).join('<BR>');
        }
        if (!('ja' in lines)) throw new CompileError('choose.text 要有 ja（原版界面和缺译时用它）');
        this.texts.push({ speaker: 0, lines, kind: 'choice' });
        this.refs.push(this.commands.length); this.refOperand[this.commands.length] = 2;
        this.op(0x3D44, 0, value.options.length, this.texts.length - 1);
      } else this.op(0x3D44, 0, value.options.length, int(value.text, 'choose.text', 0, 0xC7FF));
      value.options.forEach((block: unknown, n: number) => { this.op(0x3E10 + n); this.actions(block); this.op(0x3E1D); });
      return;
    }
    const keys = Object.keys(row);
    if (keys.length !== 1) throw new CompileError(`一个动作只写一件事：['${[...keys].sort().join("', '")}']`);
    const kind = keys[0], value = row[kind];
    if (['win', 'lose', 'next', 'ending', 'to_map'].includes(kind)) {
      if (this.stages === null) throw new CompileError(`${kind} 只能写在战役关卡里`);
      if (kind === 'next') {
        if (typeof value !== 'string' || !(value in this.stages)) throw new CompileError(`next：战役里没有关卡 ${typeof value === 'string' ? `'${value}'` : String(value)}`);
        this.op(0x3D4B, this.stages[value]);
      } else if (value !== true) throw new CompileError(`${kind} 写 true`);
      else this.op(({ win: 0x3D4A, lose: 0x3D4C, ending: 0x3D71, to_map: 0x3D4D } as Record<string, number>)[kind]);
    } else if (kind === 'set') {
      if (!isObj(value) || !Object.keys(value).length) throw new CompileError('set 写 {"变量号": 值}');
      for (const [key, x] of Object.entries(value)) this.op(0x3E13, int(Number(key), 'set 变量', 0, 199), int(x, 'set 值', 0, 3));
    } else if (kind === 'join') {
      const level = value?.level ?? { offset: 0 };
      if (!isObj(level) || Object.keys(level).length !== 1 || !['offset', 'like'].includes(Object.keys(level)[0])) throw new CompileError('join.level 写 {"offset": n} 或 {"like": 人物号}');
      let offset = 0;
      if ('offset' in level) offset = int(level.offset, 'level.offset', -98, 98);
      else this.like.push([this.commands.length, int(level.like, 'level.like', 0, 360)]);
      this.op(0x3D5A, ['pilot', pilot(value?.pilot, 'pilot')], offset & 0xFFFF, ['unit', unit(value?.unit, 'unit')], 500);
    } else if (kind === 'remove') {
      this.op(0x3D5A, ['pilot', pilot(value?.pilot, 'pilot')], 0, ['unit', unit(value?.unit, 'unit')], 4000);
    } else if (kind === 'replace') {
      if (!isObj(value) || Object.keys(value).sort().join(',') !== 'pilot,unit,with') throw new CompileError('replace 写 {"pilot": 人物号, "unit": 旧机体, "with": 新机体}');
      this.op(0x3D5A, ['pilot', pilot(value.pilot, 'pilot')], 0, ['unit', unit(value.with, 'with')], ['unit', unit(value.unit, 'unit')]);
    } else if (kind === 'remove_unit') {
      if (!isObj(value) || Object.keys(value).sort().join(',') !== 'pilot,unit') throw new CompileError('remove_unit 写 {"pilot": 人物号, "unit": 机体号}');
      this.op(0x3D5A, int(value.pilot, 'pilot', 0, 359), 0, ['unit', unit(value.unit, 'unit')], 3000);
    } else if (kind === 'unlock') this.op(0x3D6F, ['weapon', weapon(value, 'unlock')]);
    else if (kind === 'crew') {
      if (!isObj(value) || Object.keys(value).sort().join(',') !== 'pilots,unit' || !Array.isArray(value.pilots) || value.pilots.length < 1 || value.pilots.length > 5)
        throw new CompileError('crew 写 {"unit": 机体号, "pilots": [1–5 个人物号，第一个是主驾驶]}');
      this.crew.push({ unit: unit(value.unit, 'unit'), pilots: value.pilots.map((x: unknown) => int(x, 'crew 人物', 0, 360)) });
    } else if (kind === 'build') this.builds.push(unit(value, 'build'));
    else if (this.stage(kind, value)) return;
    else if (kind === 'leave') this.op(0x3D46, int(value, 'leave', 0, 360), 1);
    else if (kind === 'leave_group') this.op(0x3D46, 500 + int(value, 'leave_group', 0, 99), 1);
    else if (kind === 'deploy') this.op(0x3D45, int(value, 'deploy', 0, 99));
    else if (kind === 'focus') this.op(0x3D55, int(value, 'focus', 0, 360), 30);
    else if (kind === 'bgm') this.op(0x3D3A, int(value, 'bgm', 0, 255));
    else if (kind === 'sound') this.op(0x3D39, int(value, 'sound', 0, 0xFFFF));
    else if (kind === 'fade') {
      if (value !== 'out' && value !== 'in') throw new CompileError('fade 只能是 out 或 in');
      this.op(0x3D3B, value === 'out' ? 0 : 1);
    } else if (kind === 'wait') this.op(0x3D38, int(value, 'wait', 1, 0x7FFF));
    else if (kind === 'close') this.op(value !== 'yield' ? 0x3D47 : 0x3D4E);
    else throw new CompileError(`不认识的动作 ${kind}`);
  }
}

const counts = (value: unknown, name: string, what: string): Record<string, number> => Object.fromEntries(Object.entries((value ?? {}) as Obj)
  .map(([k, v]) => [String(int(Number(k), `${name} 人物`, 0, 360)), int(v, `${name} ${what}`, 1, 9999)]));

// Triggers an event may not repeat on: they keep holding once they hold.
const ONCE = new Set(['downed', 'group_gone', 'reach']);

// `prefix` names an event's own stage's events (a campaign: "stage.id").
export function compileStory(document: Obj, stages: Record<string, number> | null = null,
  prefixOf: (eventId: string) => (id: string) => string = () => (id) => id): StoryImage {
  if (!isObj(document) || document.schema !== 'srw64.story.v1') throw new CompileError('schema 要是 srw64.story.v1');
  const extra = Object.keys(document).filter((k) => !['schema', 'events', 'stage_info', 'battle_lines'].includes(k)).sort();
  if (extra.length) throw new CompileError(`不认识的字段 ['${extra.join("', '")}']`);
  const texts: Line[] = [];
  const events: StoryEvent[] = [];
  const ids = new Set<string>();
  const used: Refs = { fired: [] };
  for (const row of (document.events ?? []) as Obj[]) {
    const where = isObj(row) ? row.id ?? '?' : '?';
    try {
      const unknown = Object.keys(row).filter((k) => !['id', 'scene', 'when', 'if', 'repeat', 'set_flags', 'do', 'note'].includes(k)).sort();
      if (unknown.length) throw new CompileError(`不认识的字段 ['${unknown.join("', '")}']`);
      if (typeof row.id !== 'string' || !row.id) throw new CompileError('每个事件要有 id');
      if (ids.has(row.id)) throw new CompileError('id 重复');
      ids.add(row.id);
      const [name, params] = trigger(row.when);
      const cond = row.if ?? {};
      if (!isObj(cond) || Object.keys(cond).some((k) => !ALL_KEYS.includes(k))) throw new CompileError('if 只能写 ' + [...ALL_KEYS].sort().join('、'));
      const prefix = prefixOf(row.id);
      const fresh = newTests(cond, used, prefix);
      const variables = Object.fromEntries(Object.entries((cond.vars ?? {}) as Obj).map(([k, v]) => [String(int(Number(k), 'vars 变量', 0, 199)), int(v, 'vars 值', 0, 3)]));
      const kills = counts(cond.kills, 'kills', '击坠数'), killsBelow = counts(cond.kills_below, 'kills_below', '击坠数');
      const repeat = row.repeat ?? false;
      if (typeof repeat !== 'boolean' || (repeat && name.startsWith('after_'))) throw new CompileError('repeat 是 true/false，开场／配置／结束之后的事件不能重复');
      if (repeat && ONCE.has(name)) throw new CompileError(`${name} 一旦成立就一直成立，不能 repeat`);
      const builder = new Builder(texts, stages);
      builder.prefix = prefix; builder.refs_fired = used;
      builder.actions(row.do ?? []);
      const assembled = assemble(12, [0, 0, 0, 0], builder.commands);
      const words = assembled.words.slice(5);
      const offsets = assembled.listing.map((entry) => entry.offset / 2 - 5);
      const levels = builder.like.map(([command, who]) => [offsets[command] + 2, who]);
      const numbers = builder.named.map(([command, operand, value, kind]) => [offsets[command] + 1 + operand, value, kind]);
      const refs = builder.refs.map((command) => { const at = offsets[command] + 1 + (builder.refOperand[command] ?? 0); const ref = [at, words[at]]; words[at] = TEXT; return ref; });
      const event: StoryEvent = { id: row.id, scene: int(row.scene, 'scene', 0, 255), trigger: name, params, vars: variables, kills, kills_below: killsBelow };
      if (name === 'turns_after') { event.after = prefix(row.when.turns_after.event); used.fired.push(event.after); }
      for (const k of ['roster', 'not_roster']) if (k in cond) event[k] = roster(cond[k], k);
      for (const k of ['flags', 'not_flags']) if (k in cond) event[k] = flags(cond[k], k);
      Object.assign(event, { set_flags: flags(row.set_flags ?? [], 'set_flags'), fired: [...(cond.fired ?? [])], not_fired: [...(cond.not_fired ?? [])], repeat,
        words, texts: refs, levels, crew: builder.crew });
      if (builder.builds.length) event.builds = builder.builds;
      if (numbers.length) event.numbers = numbers;
      if (fresh) event.test = fresh;
      if (builder.checks.length) event.checks = builder.checks.map(([command, t]) => { const at = offsets[command] + 1; words[at] = CHECK; return [at, t]; });
      event.listing = assembled.listing.map((e) => `${e.opcode} ${e.operands.join(',')}`.trimEnd());
      events.push(event);
    } catch (error) {
      throw new CompileError(`事件 ${where}：${(error as Error).message}`);
    }
  }
  for (const event of events) for (const other of [...event.fired, ...event.not_fired])
    if (!ids.has(other)) throw new CompileError(`事件 ${event.id}：fired／not_fired 引用了不存在的事件 ${other}`);
  const quotes = battleLines(document.battle_lines ?? [], texts, used, prefixOf);
  for (const other of used.fired) if (!ids.has(other)) throw new CompileError(`条件里的 fired 引用了不存在的事件 ${other}`);
  const info = 'stage_info' in document ? stageInfo(document.stage_info) : [];
  return { schema: 'srw64.story-image.v1', events, texts, ...(quotes.length ? { quotes } : {}), ...(info.length ? { stage_info: info } : {}) };
}

// Battle lines: in a stage's battles, `who` says `text` at these moments (against `against`,
// any opponent without it), in place of the line the original would draw; the host swaps it
// in when it draws a generic one (80222B14). `if` as an event's; `once` only the first time.
function battleLines(rows: unknown, texts: Line[], used: Refs, prefixOf: (eventId: string) => (id: string) => string): Obj[] {
  if (!Array.isArray(rows)) throw new CompileError('battle_lines 要是数组');
  const ids = new Set<string>();
  return rows.map((row, n) => {
    const where = `battle_lines[${n}]`;
    try {
      if (!isObj(row)) throw new CompileError('要是对象');
      const unknown = Object.keys(row).filter((k) => !['id', 'scene', 'who', 'against', 'when', 'text', 'if', 'once', 'note'].includes(k)).sort();
      if (unknown.length) throw new CompileError(`不认识的字段 ['${unknown.join("', '")}']`);
      if (typeof row.id !== 'string' || !row.id || ids.has(row.id)) throw new CompileError('每条要有不重复的 id');
      ids.add(row.id);
      const moments = (Array.isArray(row.when) ? row.when : [row.when]).map((m: unknown) => {
        if (typeof m !== 'string' || !(m in QUOTE_MOMENTS)) throw new CompileError(`when 只能是 ${Object.keys(QUOTE_MOMENTS).join('、')}（可以是数组）`);
        return QUOTE_MOMENTS[m];
      });
      const who = int(row.who, 'who', 0, 360);
      const text = row.text;
      if (!isObj(text) || !('ja' in text)) throw new CompileError('text 写成 {"ja": …, "zh-Hans": …}，至少有 ja');
      for (const [locale, words] of Object.entries(text)) {
        if (!LOCALES.has(locale)) throw new CompileError(`不认识的语言 ${locale}`);
        if (typeof words !== 'string' || !words.trim() || codePoints(words) > QUOTE_LONGEST || words.includes('<')) throw new CompileError(`text.${locale} 为空或超过 ${QUOTE_LONGEST} 字`);
      }
      texts.push({ speaker: who, lines: { ...text }, kind: 'battle' });
      const once = row.once ?? false;
      if (typeof once !== 'boolean') throw new CompileError('once 是 true/false');
      const quote: Obj = { id: row.id, scene: int(row.scene, 'scene', 0, 255), who, against: row.against === undefined ? 0 : int(row.against, 'against', 1, 360),
        moments: [...new Set(moments)].sort(), text: texts.length - 1, once };
      if (row.if !== undefined) quote.test = condition(row.if, used, prefixOf(row.id));
      return quote;
    } catch (error) {
      throw new CompileError(`战斗台词 ${isObj(row) && row.id ? row.id : where}：${(error as Error).message}`);
    }
  });
}
