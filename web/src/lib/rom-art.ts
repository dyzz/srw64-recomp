// The original's battle pictures, read from the player's own ROM in the browser (it never
// leaves the machine and the site carries none of them): the unit maker draws weapon effects
// on its stage with them and shows a scene's palette to recolour. The same formats as
// src/native/app/rom_import_codec.hpp (resources), battle_art.hpp (atlases) and
// src/srw64_native/battle_graphics.py (scenes). No DOM, no imports, so tests/test_rom_art.py
// runs it under Node against the ROM.

export const ROM_SIZE = 0x2000000;
export const ROM_SHA256 = 'ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e';
const RESOURCES = 0x00a20bd0;          // the resource table (count, then offset/size pairs)
const BATTLE_SCENES = 0x11e3d0, BATTLE_SCENE_COUNT = 1053;   // (scene, atlas, palette) per row

// A ROM in any of the three byte orders, as big-endian .z64 bytes; null when it is not an N64 ROM.
export function toZ64(bytes: Uint8Array): Uint8Array | null {
  if (bytes.length < 4) return null;
  const head = (bytes[0] << 24 | bytes[1] << 16 | bytes[2] << 8 | bytes[3]) >>> 0;
  if (head === 0x80371240) return bytes;
  const out = new Uint8Array(bytes.length);
  if (head === 0x37804012) {        // .v64: bytes swapped in pairs
    for (let i = 0; i + 1 < bytes.length; i += 2) { out[i] = bytes[i + 1]; out[i + 1] = bytes[i]; }
    return out;
  }
  if (head === 0x40123780) {        // .n64: little-endian words
    for (let i = 0; i + 3 < bytes.length; i += 4) { out[i] = bytes[i + 3]; out[i + 1] = bytes[i + 2]; out[i + 2] = bytes[i + 1]; out[i + 3] = bytes[i]; }
    return out;
  }
  return null;
}

const be16 = (b: Uint8Array, p: number) => b[p] << 8 | b[p + 1];
const be32 = (b: Uint8Array, p: number) => (b[p] << 24 | b[p + 1] << 16 | b[p + 2] << 8 | b[p + 3]) >>> 0;
const s16 = (b: Uint8Array, p: number) => (be16(b, p) << 16) >> 16;

// The game's LZ (a 1024-byte ring starting at 0x3BE).
function lz(source: Uint8Array, size: number): Uint8Array {
  if (size > 16 * 1024 * 1024) throw new Error('resource too large');
  const ring = new Uint8Array(1024), out = new Uint8Array(size);
  let cursor = 0x3be, input = 0, flags = 0, n = 0;
  const next = () => { if (input >= source.length) throw new Error('truncated resource'); return source[input++]; };
  const emit = (v: number) => { out[n++] = v; ring[cursor] = v; cursor = (cursor + 1) & 1023; };
  while (n < size) {
    flags >>= 1;
    if (!(flags & 0x100)) flags = next() | 0xff00;
    if (flags & 1) emit(next());
    else {
      const low = next(), high = next(), start = low | ((high & 0xc0) << 2), length = (high & 0x3f) + 3;
      for (let i = 0; i < length && n < size; i++) emit(ring[(start + i) & 1023]);
    }
    flags &= 0xffff;
  }
  return out;
}

export interface Part { flip: boolean; s: number; t: number; w: number; h: number; x: number; y: number }
export interface Scene { steps: [number, number][]; frames: Part[][]; bounds: [number, number, number, number] }
export interface Atlas { width: number; height: number; indices: Uint8Array }
// A palette: its RGBA5551 entries (the first 256: CI8 indexes no further).
export type Palette = number[];

export class RomArt {
  private cache = new Map<number, Uint8Array>();
  readonly rom: Uint8Array;
  constructor(rom: Uint8Array) { this.rom = rom; }
  resource(id: number): Uint8Array {
    const hit = this.cache.get(id);
    if (hit) return hit;
    const count = be32(this.rom, RESOURCES);
    if (id < 0 || id >= count) throw new Error(`no resource ${id}`);
    const offset = RESOURCES + be32(this.rom, RESOURCES + 4 + id * 8), length = be32(this.rom, RESOURCES + 8 + id * 8);
    if (length < 4 || offset + length > this.rom.length) throw new Error(`resource ${id} outside the ROM`);
    const bytes = lz(this.rom.subarray(offset + 4, offset + length), be32(this.rom, offset));
    if (this.cache.size > 256) this.cache.clear();
    this.cache.set(id, bytes);
    return bytes;
  }
  // A battle scene row (the actors' "scene"): its resources.
  triple(row: number): [number, number, number] {
    if (!(row >= 0 && row < BATTLE_SCENE_COUNT)) throw new Error(`no battle scene ${row}`);
    const p = BATTLE_SCENES + row * 6;
    return [be16(this.rom, p), be16(this.rom, p + 2), be16(this.rom, p + 4)];
  }
  scene(id: number): Scene { return parseScene(this.resource(id)); }
  atlas(id: number): Atlas { return parseAtlas(this.resource(id)); }
  palette(id: number): Palette { return parsePalette(this.resource(id)); }
}

export function parseScene(data: Uint8Array): Scene {
  const count = data[0], mode = data[1];
  if (mode > 2) throw new Error('unknown scene mode');
  const steps: [number, number][] = [];
  for (let i = 0; i < count; i++) steps.push([data[2 + i * 2], data[3 + i * 2]]);
  if (data[2 + count * 2] !== 0xff) throw new Error('scene steps not terminated');
  const table = 2 + count * 2 + 2, first = be16(data, table);
  if (first <= table || (first - table) % 2) throw new Error('bad scene frame table');
  const frames: Part[][] = [];
  let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
  for (let f = 0; f < (first - table) / 2; f++) {
    const parts: Part[] = [];
    for (let p = be16(data, table + f * 2); p + 16 <= data.length; p += 16) {
      const flags = be16(data, p);
      if (flags & 0x8000) break;
      const part = { flip: !!(flags & 0x10), s: be16(data, p + 2), t: be16(data, p + 4), w: data[p + 6], h: data[p + 7], x: s16(data, p + 8), y: s16(data, p + 10) };
      if (!part.w || !part.h) continue;
      parts.push(part);
      x0 = Math.min(x0, part.x); y0 = Math.min(y0, part.y); x1 = Math.max(x1, part.x + part.w); y1 = Math.max(y1, part.y + part.h);
    }
    frames.push(parts);
  }
  const bounds: [number, number, number, number] = Number.isFinite(x0) ? [x0, y0, x1, y1] : [0, 0, 1, 1];
  return { steps, frames, bounds };
}

export function parseAtlas(image: Uint8Array): Atlas {
  const kind = be16(image, 0), width = be16(image, 2), height = be16(image, 4);
  const ci4 = kind === 5 || kind === 14;
  if (!ci4 && ![6, 7, 8, 15].includes(kind) || !width || !height || width > 2048 || height > 2048) throw new Error('not a battle atlas');
  const indices = new Uint8Array(width * height);
  for (let i = 0; i < width * height; i++) {
    const packed = image[8 + (ci4 ? i >> 1 : i)];
    indices[i] = ci4 ? (i & 1 ? packed & 15 : packed >> 4) : packed;
  }
  return { width, height, indices };
}

export function parsePalette(data: Uint8Array): Palette {
  if (data.length < 8 || be16(data, 0) !== 3) throw new Error('not a palette');
  const count = Math.min(256, be16(data, 2) >> 1, (data.length - 8) >> 1);
  return Array.from({ length: count }, (_, i) => be16(data, 8 + i * 2));
}

// RGBA5551 <-> "#rrggbb" (the package's colours; src/host/mod_units.hpp recolor keeps each
// entry's transparency bit).
export const opaque = (c: number) => (c & 1) === 1;
export function toHex(c: number): string {
  const ch = (v: number) => Math.round(v * 255 / 31).toString(16).padStart(2, '0');
  return `#${ch(c >> 11 & 31)}${ch(c >> 6 & 31)}${ch(c >> 1 & 31)}`;
}
export function fromHex(hex: string, was: number): number {
  const v = parseInt(hex.slice(1), 16);
  return ((v >> 19 & 31) << 11) | ((v >> 11 & 31) << 6) | ((v >> 3 & 31) << 1) | (was & 1);
}
// The palette with a package's changes ({"index": "#rrggbb"}).
export function recoloured(palette: Palette, colors: Record<string, string> | undefined): Palette {
  const out = [...palette];
  for (const [k, hex] of Object.entries(colors ?? {})) {
    const i = Number(k);
    if (Number.isInteger(i) && i >= 0 && i < out.length && /^#[0-9a-f]{6}$/i.test(hex)) out[i] = fromHex(hex, out[i]);
  }
  return out;
}

// The frame a scene shows first (its first step that is not blank).
export const firstFrame = (scene: Scene) => {
  const step = scene.steps.find(([f]) => f !== 0xff);
  return step && step[0] < scene.frames.length ? step[0] : 0;
};

// The palette entries a scene's frames show, with how many texels show each.
export function usedEntries(scene: Scene, atlas: Atlas): Map<number, number> {
  const used = new Map<number, number>();
  for (const frame of scene.frames) for (const p of frame)
    for (let y = 0; y < p.h; y++) for (let x = 0; x < p.w; x++) {
      const sx = p.s + x, sy = p.t + y;
      if (sx >= atlas.width || sy >= atlas.height) continue;
      const i = atlas.indices[sy * atlas.width + sx];
      used.set(i, (used.get(i) ?? 0) + 1);
    }
  return used;
}

// One frame on the scene's canvas (its bounds), RGBA8; transparent entries leave earlier parts.
export function renderFrame(scene: Scene, atlas: Atlas, palette: Palette, frame: number): { width: number; height: number; rgba: Uint8ClampedArray } {
  const [x0, y0, x1, y1] = scene.bounds, width = x1 - x0, height = y1 - y0;
  const rgba = new Uint8ClampedArray(width * height * 4);
  const ch = (v: number) => (v * 255 + 15) / 31 | 0;
  for (const p of scene.frames[frame] ?? []) {
    for (let y = 0; y < p.h; y++) for (let x = 0; x < p.w; x++) {
      const sx = p.s + (p.flip ? p.w - 1 - x : x), sy = p.t + y;
      if (sx >= atlas.width || sy >= atlas.height) continue;
      const c = palette[atlas.indices[sy * atlas.width + sx]];
      if (c === undefined || !(c & 1)) continue;
      const at = ((p.y - y0 + y) * width + p.x - x0 + x) * 4;
      rgba[at] = ch(c >> 11 & 31); rgba[at + 1] = ch(c >> 6 & 31); rgba[at + 2] = ch(c >> 1 & 31); rgba[at + 3] = 255;
    }
  }
  return { width, height, rgba };
}
