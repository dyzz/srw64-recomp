// The campaign, story and map compiler: the only one. The level editor page runs it in the
// browser (pages/[lang]/mods/editor.astro); the command line (tools/mods/story.py, stage.py,
// map_tool.py, pack.py) runs it with node through web/scripts (tools/mods/web_compiler.py).
// Rules go here only.
//
// This file: script instructions into words (script_debug.py assemble), with the instruction
// table web/scripts/export_editor.py writes from the layout lock.
import table from '../../data/script-ops.json' with { type: 'json' };

export class CompileError extends Error {}

export type Command = { op: number | string; args?: number[]; note?: string };
export type Line = { index: number; offset: number; opcode: string; family: string; name: string; operands: number[]; note?: string };
type Op = { family: string; words: number; name: string };

export const OPS = (table as { ops: Record<string, Op> }).ops;
const NO_HANDLER = new Set([0x3D76, 0x3D77, 0x3D78, 0x3D79]);
export const hex4 = (n: number) => n.toString(16).toUpperCase().padStart(4, '0');
export const isInt = (v: unknown): v is number => typeof v === 'number' && Number.isInteger(v);

// An event's words (its type, four parameters, the instructions, FFFF) and a listing that
// keeps every instruction's byte offset.
export function assemble(eventType: number, header: number[], commands: Command[]): { words: number[]; listing: Line[] } {
  if (!isInt(eventType) || eventType < 0 || eventType > 14 || header.length !== 4 || header.some((v) => !isInt(v) || v < 0 || v > 0xFFFF))
    throw new CompileError('Event header must be a type 0..14 and four 16-bit parameters');
  const words = [eventType, ...header];
  const listing: Line[] = [];
  commands.forEach((row, index) => {
    const opcode = typeof row.op === 'string' ? parseInt(row.op, 16) : row.op;
    const known = isInt(opcode) ? OPS[opcode.toString(16).padStart(4, '0')] : undefined;
    if (!known) throw new CompileError(`Unknown opcode ${isInt(opcode) ? hex4(opcode) : String(row.op)}`);
    if (NO_HANDLER.has(opcode)) throw new CompileError(`${hex4(opcode)} has no reachable handler`);
    const args = row.args ?? [];
    if (args.length !== known.words) throw new CompileError(`${hex4(opcode)} takes ${known.words} operand words, got ${args.length}`);
    if (args.some((a) => !isInt(a) || a < 0 || a > 0xFFFF)) throw new CompileError(`${hex4(opcode)} operands must be 16-bit`);
    listing.push({ index, offset: words.length * 2, opcode: hex4(opcode), family: known.family, name: known.name, operands: [...args], note: row.note ?? '' });
    words.push(opcode, ...args);
  });
  listing.push({ index: listing.length, offset: words.length * 2, opcode: 'FFFF', family: 'end', name: '事件结束', operands: [] });
  words.push(0xFFFF);
  return { words, listing };
}
