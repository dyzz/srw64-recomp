// The level editor's further story steps (level-editor.md §5.8): one table says, for each
// action of the stage source (story.ts Builder.stage and the older ones), the fields the page
// asks for and how they become the source. The page draws the form from it
// (scripts/level-editor.ts); the model writes the action from it (editor-model.ts).

export type Field =
  | { key: string; type: 'pilot' | 'unit' | 'group' | 'pos' | 'bool' | 'map' | 'xy' | 'area' | 'place' | 'battle' }
  | { key: string; type: 'number'; min: number; max: number; step?: number }
  | { key: string; type: 'select'; options: [string, string][] };   // [value, label key]
export type Command = { name: string; group: 'camera' | 'unit' | 'army' | 'world' | 'story' | 'show'; fields: Field[];
  // The source's value: an object of the fields (default), the one field alone, or true.
  shape?: 'object' | 'scalar' | 'flag' };

// A position as the page keeps it: a cell, or relative to where a pilot stands.
export type Pos = { x: number; y: number } | { near: number; dir: 'at' | 'up' | 'right' | 'down' | 'left'; by: number };
export const posSource = (p: Pos) => 'near' in p ? { near: p.near, dir: p.dir, ...(p.dir === 'at' ? {} : { by: p.by }) } : [p.x, p.y];

const sides: [string, string][] = [['player', 'sidePlayer'], ['enemy', 'sideEnemy'], ['third', 'sideThird']];
export const COMMANDS: Command[] = [
  { name: 'camera', group: 'camera', fields: [{ key: 'to', type: 'pos' }, { key: 'wait', type: 'bool' }] },
  { name: 'camera_follows', group: 'camera', fields: [{ key: 'value', type: 'bool' }], shape: 'scalar' },
  { name: 'camera_back', group: 'camera', fields: [], shape: 'flag' },
  { name: 'effect', group: 'show', fields: [{ key: 'id', type: 'select', options: [['0', 'effect0'], ['1', 'effect1'], ['11', 'effect11']] }, { key: 'at', type: 'pos' }] },
  { name: 'green_area', group: 'show', fields: [{ key: 'value', type: 'area' }], shape: 'scalar' },
  { name: 'shake', group: 'show', fields: [{ key: 'value', type: 'number', min: 1, max: 20 }], shape: 'scalar' },
  { name: 'sound', group: 'show', fields: [{ key: 'value', type: 'number', min: 0, max: 0xFFFF }], shape: 'scalar' },
  { name: 'powerup_scene', group: 'show', fields: [{ key: 'value', type: 'number', min: 0, max: 10 }], shape: 'scalar' },
  { name: 'battle_scene', group: 'show', fields: [{ key: 'a', type: 'battle' }, { key: 'b', type: 'battle' }] },
  { name: 'map_weapon', group: 'show', fields: [{ key: 'who', type: 'pilot' }, { key: 'unit', type: 'unit' }, { key: 'weapon', type: 'number', min: 0, max: 1328 }, { key: 'dir', type: 'number', min: 0, max: 3 }] },
  { name: 'move', group: 'unit', fields: [{ key: 'who', type: 'pilot' }, { key: 'to', type: 'pos' }] },
  { name: 'destroy', group: 'unit', fields: [{ key: 'value', type: 'pilot' }], shape: 'scalar' },
  { name: 'destroy_group', group: 'unit', fields: [{ key: 'value', type: 'group' }], shape: 'scalar' },
  { name: 'leave_group', group: 'unit', fields: [{ key: 'value', type: 'group' }], shape: 'scalar' },
  { name: 'change_side', group: 'unit', fields: [{ key: 'who', type: 'pilot' }, { key: 'to', type: 'select', options: sides }] },
  { name: 'can_act', group: 'unit', fields: [{ key: 'who', type: 'pilot' }, { key: 'value', type: 'bool' }] },
  { name: 'transform', group: 'unit', fields: [{ key: 'who', type: 'pilot' }, { key: 'from', type: 'unit' }, { key: 'to', type: 'unit' }] },
  { name: 'combine', group: 'unit', fields: [{ key: 'who', type: 'pilot' }, { key: 'join', type: 'bool' }] },
  { name: 'combined_form', group: 'unit', fields: [{ key: 'family', type: 'select', options: [['dancouga', 'familyDancouga'], ['combattler', 'familyCombattler']] }, { key: 'join', type: 'bool' }] },
  { name: 'godmars', group: 'unit', fields: [{ key: 'value', type: 'select', options: [['4', 'godmars4'], ['0', 'godmars0'], ['1', 'godmars1'], ['3', 'godmars3']] }], shape: 'scalar' },
  { name: 'off_map', group: 'unit', fields: [{ key: 'value', type: 'pilot' }], shape: 'scalar' },
  { name: 'hand_over', group: 'unit', fields: [{ key: 'value', type: 'pilot' }], shape: 'scalar' },
  { name: 'land', group: 'unit', fields: [{ key: 'value', type: 'pilot' }], shape: 'scalar' },
  { name: 'launch', group: 'unit', fields: [{ key: 'value', type: 'pilot' }], shape: 'scalar' },
  { name: 'formation', group: 'unit', fields: [{ key: 'at', type: 'xy' }, { key: 'per_row', type: 'number', min: 1, max: 30 }] },
  { name: 'sortie', group: 'army', fields: [{ key: 'group', type: 'group' }, { key: 'max', type: 'number', min: 0, max: 15 }] },
  { name: 'forbid', group: 'army', fields: [{ key: 'value', type: 'pilot' }], shape: 'scalar' },
  { name: 'remove', group: 'army', fields: [{ key: 'pilot', type: 'pilot' }, { key: 'unit', type: 'unit' }] },
  { name: 'upgrade', group: 'army', fields: [{ key: 'unit', type: 'unit' }, { key: 'level', type: 'number', min: 0, max: 15 }] },
  { name: 'replace', group: 'army', fields: [{ key: 'pilot', type: 'pilot' }, { key: 'unit', type: 'unit' }, { key: 'with', type: 'unit' }] },
  { name: 'remove_unit', group: 'army', fields: [{ key: 'pilot', type: 'pilot' }, { key: 'unit', type: 'unit' }] },
  { name: 'squad_mark', group: 'army', fields: [{ key: 'pilot', type: 'pilot' }, { key: 'unit', type: 'unit' }, { key: 'on', type: 'bool' }] },
  { name: 'squad_split', group: 'army', fields: [{ key: 'value', type: 'select', options: [['a', 'squadA'], ['b', 'squadB']] }], shape: 'scalar' },
  { name: 'squad_clear', group: 'army', fields: [], shape: 'flag' },
  { name: 'unlock', group: 'army', fields: [{ key: 'value', type: 'number', min: 0, max: 1328 }], shape: 'scalar' },
  { name: 'copilot', group: 'army', fields: [{ key: 'pilot', type: 'pilot' }, { key: 'add', type: 'pilot' }] },
  { name: 'funds', group: 'army', fields: [{ key: 'value', type: 'number', min: 1000, max: 999000, step: 1000 }], shape: 'scalar' },
  { name: 'morale_down', group: 'army', fields: [], shape: 'flag' },
  { name: 'set', group: 'story', fields: [{ key: 'var', type: 'number', min: 0, max: 199 }, { key: 'value', type: 'number', min: 0, max: 3 }] },
  { name: 'bump', group: 'story', fields: [{ key: 'var', type: 'number', min: 0, max: 199 }, { key: 'by', type: 'select', options: [['1', 'bumpUp'], ['-1', 'bumpDown']] }] },
  { name: 'all_lost_defeat', group: 'story', fields: [{ key: 'value', type: 'bool' }], shape: 'scalar' },
  { name: 'player_phase', group: 'story', fields: [], shape: 'flag' },
  { name: 'swap_identity', group: 'story', fields: [{ key: 'a', type: 'pilot' }, { key: 'b', type: 'pilot' }] },
  { name: 'world_map', group: 'world', fields: [{ key: 'value', type: 'place' }], shape: 'scalar' },
  { name: 'travel', group: 'world', fields: [{ key: 'value', type: 'place' }], shape: 'scalar' },
  { name: 'vehicle', group: 'world', fields: [{ key: 'value', type: 'number', min: 0, max: 14 }], shape: 'scalar' },
  { name: 'change_map', group: 'world', fields: [{ key: 'map', type: 'map' }, { key: 'at', type: 'xy' }] },
];
export const commandOf = (name: string) => COMMANDS.find((c) => c.name === name);

// A new step's values: the first of the stage's own people and units, sensible numbers.
export function defaults(c: Command, pilot: number, unit: number, group: number): Record<string, unknown> {
  const out: Record<string, unknown> = {};
  for (const f of c.fields) out[f.key] = f.type === 'pilot' ? pilot : f.type === 'unit' ? unit : f.type === 'group' ? group : f.type === 'bool' ? true
    : f.type === 'area' ? [0, 0, 2, 2] : f.type === 'pos' ? { x: 0, y: 0 } : f.type === 'xy' ? { x: 0, y: 0 } : f.type === 'map' ? 20 : f.type === 'place' ? 0 : f.type === 'battle' ? (f.key === 'b' ? 16 : 15) : f.type === 'select' ? f.options[0][0] : f.type === 'number' ? Math.max(f.min, f.step ?? 1) : 0;
  if (c.name === 'sortie') out.max = 15;
  return out;
}

// The source's action for a command step.
export function commandSource(name: string, args: Record<string, unknown>): Record<string, unknown> {
  const c = commandOf(name);
  if (!c) throw new Error(`no command ${name}`);
  const value = (f: Field): unknown => {
    const v = args[f.key];
    if (f.type === 'pos') return posSource(v as Pos);
    if (f.type === 'xy') { const p = v as { x: number; y: number }; return [p.x, p.y]; }
    if (f.type === 'select') return typeof v === 'string' && /^-?\d+$/.test(v) ? Number(v) : v;   // a numbered choice
    return v;
  };
  if (c.shape === 'flag') return { [name]: true };
  if (c.shape === 'scalar') return { [name]: value(c.fields[0]) };
  if (name === 'battle_scene') return { battle_scene: [args.a, args.b] };
  if (name === 'swap_identity') return { swap_identity: [args.a, args.b] };
  if (name === 'set') return { set: { [String(args.var)]: args.value } };
  if (name === 'sortie') { const max = Number(args.max); return { sortie: { group: args.group, max: max === 0 ? 'ship' : max } }; }
  return { [name]: Object.fromEntries(c.fields.map((f) => [f.key, value(f)])) };
}
