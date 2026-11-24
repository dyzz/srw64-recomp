// A stage's image for the game's own per-scene buffers (mini_stage.py compile_stage): events
// at 8019B400 (at most 0x1A00 bytes, 63 of them), the deployment block at 80199400 (at
// most 0x2000 bytes). A stage that copies an original event or starts a record from an
// original one (copy_from, template) needs those records, extracted from the ROM: the
// command line hands them over (Catalog, tools/mods/stage.py); the page has none.
import { assemble, CompileError, hex4, isInt, OPS, type Command, type Line } from './assemble.ts';

export const EVENT_BLOCK = 0x8019B400, EVENT_LIMIT = 0x1A00, AUX_BLOCK = 0x80199400, AUX_LIMIT = 0x2000;
export const MAX_EVENTS = 63, RECORD_BYTES = 28, TERMINATOR = 999;
// The 14-halfword deployment record: name, offset, size.
const RECORD_FIELDS: [string, number, number][] = [['group', 0, 2], ['x', 2, 2], ['y', 4, 2], ['actor', 6, 2], ['byte8', 8, 1],
  ['level_offset', 9, 1], ['unit', 10, 2], ['upgrade', 12, 2], ['raw14', 14, 2], ['raw16', 16, 2], ['raw18', 18, 2],
  ['faction', 20, 2], ['behavior', 22, 2], ['extra', 24, 2], ['raw26', 26, 2]];
const SCOPE = 'Per-scene event and deployment buffers at registration, plus optional one-time unit resources at initial map idle; ROM and other scenes are untouched.';

// Original records by key, as raw hex (assets/original-data/records: stage_events,
// stage_deployments, stage_auxiliary).
export type Catalog = Partial<Record<'stage_events' | 'stage_deployments' | 'stage_auxiliary', Record<string, string>>>;
const record = (catalog: Catalog | undefined, collection: keyof Catalog, key: string, what: string): number[] => {
  if (!catalog) throw new CompileError(`这里不能用 ${what}（要读原版记录）：${what === 'template' ? '直接写各个字段' : collection === 'stage_events' ? '把事件写出来' : '把单位一个个摆出来'}`);
  const hex = catalog[collection]?.[key];
  if (typeof hex !== 'string') throw new CompileError(`Unknown catalog record ${key}`);
  return Array.from({ length: hex.length / 2 }, (_, i) => parseInt(hex.substr(i * 2, 2), 16));
};

export type RawEvent = { name?: string; type?: number; header?: number[]; commands?: Command[]; copy_from?: string };
export type StageEvent = { index: number; name: string; type: number; header: number[]; offset: number; size: number; pointer: number; listing: Line[] };
export type StageImage = {
  schema: string; name: string; map: number | null; slot: number | null; event_block: number; aux_block: number;
  route: number | null; story_vars: Record<string, number>; last_choice: number | null; initial_resources: unknown[];
  events: StageEvent[]; events_hex: string; aux_hex: string; deployments: Record<string, number>[]; scope: string; custom_map?: string;
};
export type StageDocument = {
  name?: string; map?: number | null; slot?: number | null; events?: RawEvent[]; deployments?: Record<string, unknown>[];
  deployments_from?: string; initial_resources?: unknown; route?: unknown; story_vars?: unknown; last_choice?: unknown;
};

const toHex = (bytes: number[] | Uint8Array) => Array.from(bytes, (b) => b.toString(16).toUpperCase().padStart(2, '0')).join('');

function encodeRecord(spec: Record<string, unknown>, catalog?: Catalog): number[] {
  const record = 'template' in spec ? record_(catalog, String(spec.template)) : new Array<number>(RECORD_BYTES).fill(0);
  const names = new Set(RECORD_FIELDS.map(([name]) => name));
  for (const [name, offset, size] of RECORD_FIELDS) {
    if (!(name in spec)) continue;
    const value = Math.trunc(Number(spec[name]));
    if (!(value >= 0 && value < 2 ** (8 * size))) throw new CompileError(`Deployment field ${name} = ${value} does not fit ${size} byte(s)`);
    if (size === 1) record[offset] = value; else { record[offset] = value >> 8; record[offset + 1] = value & 0xFF; }
  }
  const unknown = Object.keys(spec).filter((k) => !names.has(k) && k !== 'note' && k !== 'template').sort();
  if (unknown.length) throw new CompileError(`Unknown deployment fields ['${unknown.join("', '")}']`);
  return record;
}
function record_(catalog: Catalog | undefined, key: string): number[] {
  const bytes = record(catalog, 'stage_deployments', key, 'template');
  if (bytes.length !== RECORD_BYTES) throw new CompileError(`Template ${key} is not one 28-byte record`);
  return bytes;
}
// Records up to (not taking) the first whose group halfword is 999.
function parseBlock(raw: number[]): number[][] {
  const records: number[][] = [];
  for (let at = 0; at + 1 < raw.length; at += RECORD_BYTES) {
    if ((raw[at] << 8 | raw[at + 1]) === TERMINATOR) break;
    if (at + RECORD_BYTES > raw.length) throw new CompileError('Deployment block ends inside a record');
    records.push(raw.slice(at, at + RECORD_BYTES));
  }
  return records;
}
// An original event's words as they are, to its terminator: the instruction stream walked
// (FFFF also occurs as an operand, and the record's trailing bytes are not instructions).
function copiedEvent(raw: number[], key: string): { words: number[]; listing: Line[] } {
  const words = Array.from({ length: raw.length >> 1 }, (_, i) => raw[i * 2] << 8 | raw[i * 2 + 1]);
  if (words.length < 6 || words[0] > 14) throw new CompileError(`${key} is not one event`);
  const listing: Line[] = [];
  for (let at = 5; at < words.length;) {
    const op = words[at];
    if (op === 0xFFFF) { listing.push({ index: listing.length, offset: at * 2, opcode: 'FFFF', family: 'end', name: '事件结束', operands: [] }); return { words: words.slice(0, at + 1), listing }; }
    const known = OPS[hex4(op).toLowerCase()];
    if (!known) throw new CompileError(`${key}: unknown opcode ${hex4(op)} at ${at * 2}`);
    listing.push({ index: listing.length, offset: at * 2, opcode: hex4(op), family: known.family, name: known.name, operands: words.slice(at + 1, at + 1 + known.words) });
    at += 1 + known.words;
  }
  throw new CompileError(`${key} does not end in a terminator`);
}
const decodeRecord = (record: number[]) => Object.fromEntries(RECORD_FIELDS.map(([name, offset, size]) =>
  [name, size === 1 ? record[offset] : record[offset] << 8 | record[offset + 1]]));

export function compileStage(document: StageDocument, catalog?: Catalog): StageImage {
  const events: StageEvent[] = [];
  const blob: number[] = [];
  (document.events ?? []).forEach((event, index) => {
    let type: number, assembled: { words: number[]; listing: Line[] };
    if ('copy_from' in event) {
      const key = String(event.copy_from);
      assembled = copiedEvent(record(catalog, 'stage_events', key, 'copy_from'), key);
      // A header override keeps every command of the original and changes only when it runs.
      if (event.header) {
        const header = event.header.map((v) => Math.trunc(Number(v)));
        if (header.length !== 4 || header.some((v) => !(v >= 0 && v <= 0xFFFF))) throw new CompileError('A header override is four 16-bit parameters');
        assembled.words = [assembled.words[0], ...header, ...assembled.words.slice(5)];
      }
      type = assembled.words[0];
    } else {
      type = Math.trunc(Number(event.type));
      assembled = assemble(type, (event.header ?? [0, 0, 0, 0]).map((v) => Math.trunc(Number(v))), event.commands ?? []);
    }
    const offset = blob.length;
    for (const word of assembled.words) blob.push(word >> 8, word & 0xFF);
    const size = assembled.words.length * 2;
    while (blob.length % 4) blob.push(0);
    events.push({ index, name: event.name ?? `event-${index}`, type, header: assembled.words.slice(1, 5), offset, size, pointer: EVENT_BLOCK + offset, listing: assembled.listing });
  });
  if (events.length > MAX_EVENTS) throw new CompileError(`At most ${MAX_EVENTS} events fit the pointer table`);
  if (blob.length > EVENT_LIMIT) throw new CompileError(`Events take ${blob.length} bytes; the per-scene buffer holds ${EVENT_LIMIT}`);
  const records = [...('deployments_from' in document ? parseBlock(record(catalog, 'stage_auxiliary', String(document.deployments_from), 'copy_from')) : []),
    ...(document.deployments ?? []).map((spec) => encodeRecord(spec, catalog))];
  const aux = [...records.flat(), TERMINATOR >> 8, TERMINATOR & 0xFF, 0, 0];
  if (aux.length > AUX_LIMIT) throw new CompileError(`Deployment block takes ${aux.length} bytes; the buffer holds ${AUX_LIMIT}`);
  const map = document.map ?? null, slot = document.slot ?? null;
  if (map !== null && !(Math.trunc(Number(map)) >= 0 && Math.trunc(Number(map)) <= 255)) throw new CompileError('map must be a map index 0..255');
  if (slot !== null && !(Math.trunc(Number(slot)) >= 0 && Math.trunc(Number(slot)) <= 255)) throw new CompileError('slot must be a scene index 0..255');
  const resources = document.initial_resources ?? [];
  if (!Array.isArray(resources) || resources.length > 90) throw new CompileError('initial_resources must contain at most 90 slot overrides');
  const seen = new Set<string>();
  for (const row of resources as Record<string, unknown>[]) {
    const keys = row && typeof row === 'object' && !Array.isArray(row) ? Object.keys(row).sort().join(',') : '';
    if (keys !== 'en_percent,hp_percent,side,slot') throw new CompileError('initial_resources requires side, slot, hp_percent and en_percent');
    const { side, slot: place, hp_percent: hp, en_percent: en } = row as Record<string, number>;
    if (![side, place, hp, en].every(isInt) || !(side >= 0 && side <= 2 && place >= 0 && place < 30 && hp >= 1 && hp <= 100 && en >= 0 && en <= 100))
      throw new CompileError('initial_resources values are out of range');
    if (seen.has(`${side},${place}`)) throw new CompileError('initial_resources contains a duplicate slot');
    seen.add(`${side},${place}`);
  }
  const route = document.route ?? null;
  if (route !== null && !(isInt(route) && route >= 0 && route <= 3)) throw new CompileError('route must be 0 (Brad), 1 (Manami), 2 (Ark) or 3 (Selain)');
  const vars = document.story_vars ?? {};
  if (!vars || typeof vars !== 'object' || Array.isArray(vars) || Object.entries(vars as Record<string, unknown>).some(([k, v]) =>
    !/^\d+$/.test(k) || Number(k) >= 200 || !isInt(v) || v < 0 || v > 3)) throw new CompileError('story_vars maps variable indices 0..199 to values 0..3');
  const choice = document.last_choice ?? null;
  if (choice !== null && !(isInt(choice) && choice >= 1 && choice <= 8)) throw new CompileError('last_choice is the option taken, 1 to 8');
  return {
    schema: 'srw64.mini-stage-image.v1', name: document.name ?? 'mini-stage', map: map === null ? null : Math.trunc(Number(map)),
    slot: slot === null ? null : Math.trunc(Number(slot)), event_block: EVENT_BLOCK, aux_block: AUX_BLOCK,
    route: route as number | null, story_vars: Object.fromEntries(Object.entries(vars as Record<string, number>).map(([k, v]) => [String(Number(k)), v])),
    last_choice: choice as number | null, initial_resources: resources, events, events_hex: toHex(blob), aux_hex: toHex(aux),
    deployments: records.map(decodeRecord), scope: SCOPE,
  };
}

// Scenes a campaign stage must not borrow (src/host/campaign.hpp): the "（前）" scenes and 132
// shorten the intermission menu, 109-122 are the Link Battler stages.
const SHORTENED = new Set([38, 48, 68, 73, 79, 81, 83, 86, 88, 93, 95, 96, 104, 132]);
export const borrowable = (scene: number) => scene >= 0 && scene <= 142 && !(scene >= 109 && scene <= 122) && !SHORTENED.has(scene);
// The 3D4B targets in a stage image's events (500 returns to the saved scene).
export const nextScenes = (image: StageImage) => image.events.flatMap((event) => event.listing
  .filter((row) => row.opcode === '3D4B' && row.operands.length).map((row) => row.operands[0]));
export { hex4 };
