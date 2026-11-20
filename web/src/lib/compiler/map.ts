// A campaign's own tactical map from one painted picture and a terrain grid (tools/mods/
// map_tool.py): what the original draws (a layout, a 512 x 512 atlas of at most 1,023
// 16 x 16 tiles, 256 colours) and what the HD mode needs beside the picture itself (the
// palette index of every pixel, the reference palette). The format and the rules are the
// command line's; the colours are not bit for bit its own (it quantizes with Pillow, this
// with a median cut over 15-bit colours), which tests/test_web_compiler.py allows for: it
// reads this file's output back with the reader of the original's maps.
import { CompileError, isInt } from './assemble.ts';

export const CELL = 16, FRAME = 2, TILES = 1023;
const MIN_COLUMNS = 20, MIN_ROWS = 15, MAX_CELLS = 128, MAX_WIDTH = 4240, MAX_HEIGHT = 3184;
const FLIP_H = 0x4000, FLIP_V = 0x8000;

// The original map a new one stands in for (web/public/gen/editor.json, export_editor.py).
export type Template = { map: number; layout: number; atlas: number; palette: number; palette_hex: string; cycles: [number, number][] };
export type MapSource = { name: string; like: number; scale: number; terrain: number[][] };
export type BuiltMap = {
  image: Record<string, unknown>;      // srw64.map-image.v1 (map.json)
  meta: Record<string, unknown> | null; // srw64.hd-map-runtime.v0 (meta.json), at scale 2 and up
  columns: number; rows: number;
  indices: Uint8Array;                 // the picture at the game's size, a palette index a pixel
  preview: Uint8ClampedArray;          // RGBA, as the original mode draws it
};

const toHex = (bytes: ArrayLike<number>) => { let out = ''; for (let i = 0; i < bytes.length; ++i) out += bytes[i].toString(16).toUpperCase().padStart(2, '0'); return out; };
const fromHex = (hex: string) => Uint8Array.from({ length: hex.length / 2 }, (_, i) => parseInt(hex.substr(i * 2, 2), 16));
const up = (v5: number) => Math.round(v5 * 255 / 31);

// The size in cells of a picture `scale` times the game's pixels, or what is wrong with it.
export function gridOf(width: number, height: number, scale: number): { columns: number; rows: number } {
  if (!isInt(scale) || scale < 1 || scale > 8) throw new CompileError('scale 是 1–8 的整数（底图是游戏像素的几倍）');
  const step = CELL * scale;
  if (width % step || height % step) throw new CompileError(`底图 ${width}×${height} 不是 ${step} 的整数倍（一格 16 像素 × scale ${scale}）`);
  const columns = width / step, rows = height / step;
  if (!(columns >= MIN_COLUMNS && columns <= MAX_CELLS && rows >= MIN_ROWS && rows <= MAX_CELLS) || columns * CELL > MAX_WIDTH || rows * CELL > MAX_HEIGHT)
    throw new CompileError(`地图 ${columns}×${rows} 格：要在 ${MIN_COLUMNS}×${MIN_ROWS} 到 ${MAX_CELLS}×${MAX_CELLS} 格之间`);
  return { columns, rows };
}

function terrainGrid(rows: unknown, columns: number, count: number): number[][] {
  if (!Array.isArray(rows) || rows.length !== count || rows.some((row) => !Array.isArray(row) || row.length !== columns))
    throw new CompileError(`terrain 要是 ${count} 行、每行 ${columns} 个地形号（和底图的格数一致）`);
  return (rows as unknown[][]).map((row, y) => row.map((value, x) => {
    if (x < FRAME || y < FRAME || x >= columns - FRAME || y >= count - FRAME) return 99;
    if (!isInt(value) || value < 0 || value > 98) throw new CompileError(`terrain 第 ${y + 1} 行第 ${x + 1} 格：地形号要是 0–98（63 是墙）`);
    return value;
  }));
}

// The layout's number of the atlas tile at column index % 32, row index // 32 (800945D4).
const tileCode = (index: number) => { const x = index % 32, y = index >> 5; return (x % 8) * 2 | (y % 8) * 2 << 4 | (x >> 3) << 8 | (y >> 3) << 10; };

// The picture reduced by `scale` (the mean of every scale x scale block), as RGB.
function shrink(rgba: Uint8ClampedArray, width: number, height: number, scale: number): Uint8Array {
  const w = width / scale, h = height / scale, out = new Uint8Array(w * h * 3), area = scale * scale;
  for (let y = 0; y < h; ++y) for (let x = 0; x < w; ++x) {
    let r = 0, g = 0, b = 0;
    for (let dy = 0; dy < scale; ++dy) {
      let at = ((y * scale + dy) * width + x * scale) * 4;
      for (let dx = 0; dx < scale; ++dx, at += 4) { r += rgba[at]; g += rgba[at + 1]; b += rgba[at + 2]; }
    }
    const to = (y * w + x) * 3;
    out[to] = Math.round(r / area); out[to + 1] = Math.round(g / area); out[to + 2] = Math.round(b / area);
  }
  return out;
}

// At most `most` colours for the picture, in 5 bits a channel (as the palette keeps them),
// by median cut over the colours it uses; and each pixel's colour among them.
function quantize(rgb: Uint8Array, most: number): { colours: number[][]; pixels: Uint8Array } {
  const histogram = new Uint32Array(32768);
  const bin = new Uint16Array(rgb.length / 3);
  for (let i = 0, p = 0; i < rgb.length; i += 3, ++p) {
    const key = (rgb[i] * 31 + 127) / 255 << 10 | (rgb[i + 1] * 31 + 127) / 255 << 5 | (rgb[i + 2] * 31 + 127) / 255 | 0;
    bin[p] = key; histogram[key]++;
  }
  const used: number[] = [];
  for (let key = 0; key < 32768; ++key) if (histogram[key]) used.push(key);
  const channel = (key: number, c: number) => key >> (10 - 5 * c) & 31;
  type Box = { keys: number[]; axis: number; range: number };
  const measure = (keys: number[]): Box => {
    let axis = 0, range = -1;
    for (let c = 0; c < 3; ++c) {
      let low = 31, high = 0;
      for (const key of keys) { const v = channel(key, c); if (v < low) low = v; if (v > high) high = v; }
      if (high - low > range) { range = high - low; axis = c; }
    }
    return { keys, axis, range };
  };
  const boxes = [measure(used)];
  while (boxes.length < most) {
    let pick = -1, score = 0;
    boxes.forEach((box, n) => {
      if (box.keys.length < 2) return;
      let weight = 0;
      for (const key of box.keys) weight += histogram[key];
      const s = (box.range + 1) * Math.sqrt(weight);
      if (s > score) { score = s; pick = n; }
    });
    if (pick < 0) break;
    const box = boxes[pick];
    box.keys.sort((a, b) => channel(a, box.axis) - channel(b, box.axis) || a - b);
    let total = 0;
    for (const key of box.keys) total += histogram[key];
    let half = 0, cut = 0;
    while (cut < box.keys.length - 1 && half + histogram[box.keys[cut]] <= total / 2) half += histogram[box.keys[cut++]];
    cut = Math.max(1, cut);
    boxes.splice(pick, 1, measure(box.keys.slice(0, cut)), measure(box.keys.slice(cut)));
  }
  const colours = boxes.map((box) => {
    const sum = [0, 0, 0];
    let weight = 0;
    for (const key of box.keys) { const n = histogram[key]; weight += n; for (let c = 0; c < 3; ++c) sum[c] += channel(key, c) * n; }
    return sum.map((v) => Math.round(v / weight));
  });
  const nearest = new Int16Array(32768).fill(-1);
  boxes.forEach((box, n) => { for (const key of box.keys) nearest[key] = n; });
  const pixels = new Uint8Array(bin.length);
  for (let p = 0; p < bin.length; ++p) pixels[p] = nearest[bin[p]];
  return { colours, pixels };
}

const keyOf = (bytes: Uint8Array) => String.fromCharCode(...bytes);
// The tile as each flag draws it: as is, mirrored, upside down, both.
function flips(tile: Uint8Array): [number, Uint8Array][] {
  const mirrored = new Uint8Array(256), down = new Uint8Array(256), both = new Uint8Array(256);
  for (let y = 0; y < CELL; ++y) for (let x = 0; x < CELL; ++x) {
    const v = tile[y * CELL + x];
    mirrored[y * CELL + CELL - 1 - x] = v; down[(CELL - 1 - y) * CELL + x] = v; both[(CELL - 1 - y) * CELL + CELL - 1 - x] = v;
  }
  return [[0, tile], [FLIP_H, mirrored], [FLIP_V, down], [FLIP_H | FLIP_V, both]];
}

// More kinds of tile than the atlas holds: the kinds are grouped by colour (k-means over
// each tile's 4 x 4 block means), each group drawn with its member nearest the middle.
function mergeTiles(kinds: string[], colours: number[][], most: number): Map<string, string> {
  const dims = 48, n = kinds.length, points = new Float32Array(n * dims);
  kinds.forEach((kind, k) => {
    for (let by = 0; by < 4; ++by) for (let bx = 0; bx < 4; ++bx) for (let y = 0; y < 4; ++y) for (let x = 0; x < 4; ++x) {
      const colour = colours[kind.charCodeAt((by * 4 + y) * CELL + bx * 4 + x)];
      for (let c = 0; c < 3; ++c) points[k * dims + (by * 4 + bx) * 3 + c] += colour[c] / 16;
    }
  });
  const centres = new Float32Array(most * dims);
  for (let k = 0; k < most; ++k) centres.set(points.subarray(Math.floor(k * n / most) * dims, Math.floor(k * n / most) * dims + dims), k * dims);
  const group = new Int32Array(n), distance = new Float32Array(n);
  const assign = () => {
    for (let p = 0; p < n; ++p) {
      let best = 0, least = Infinity;
      for (let k = 0; k < most; ++k) {
        let d = 0;
        for (let i = 0; i < dims && d < least; ++i) { const v = points[p * dims + i] - centres[k * dims + i]; d += v * v; }
        if (d < least) { least = d; best = k; }
      }
      group[p] = best; distance[p] = least;
    }
  };
  for (let round = 0; round < 12; ++round) {
    assign();
    const sums = new Float32Array(most * dims), counts = new Int32Array(most);
    for (let p = 0; p < n; ++p) { counts[group[p]]++; for (let i = 0; i < dims; ++i) sums[group[p] * dims + i] += points[p * dims + i]; }
    for (let k = 0; k < most; ++k) if (counts[k]) for (let i = 0; i < dims; ++i) centres[k * dims + i] = sums[k * dims + i] / counts[k];
  }
  assign();
  const middle = new Int32Array(most).fill(-1);
  for (let p = 0; p < n; ++p) if (middle[group[p]] < 0 || distance[p] < distance[middle[group[p]]]) middle[group[p]] = p;
  return new Map(kinds.map((kind, p) => [kind, kinds[middle[group[p]]]]));
}

// `rgba`: the whole picture, width x height, `source.scale` times the game's pixels.
export function buildMap(source: MapSource, like: Template | undefined, rgba: Uint8ClampedArray, width: number, height: number): BuiltMap {
  if (!like) throw new CompileError(`like ${source.like}：这张原版地图不能当模板（带殖民地动画）；换一张`);
  const { scale } = source;
  const { columns, rows } = gridOf(width, height, scale);
  const grid = terrainGrid(source.terrain, columns, rows);
  const w = columns * CELL, h = rows * CELL;
  const small = scale === 1 ? Uint8Array.from({ length: w * h * 3 }, (_, i) => rgba[(i / 3 | 0) * 4 + i % 3]) : shrink(rgba, width, height, scale);

  // Palette indices a new map's pixels may use: not 0 (transparent in the ROM's map
  // palettes), not what the template's cycles rewrite.
  const cycled = new Set(like.cycles.flatMap(([first, count]) => Array.from({ length: count }, (_, i) => first + i)));
  const free = Array.from({ length: 255 }, (_, i) => i + 1).filter((i) => !cycled.has(i));
  const { colours: found, pixels } = quantize(small, free.length);
  const palette = fromHex(like.palette_hex);
  found.forEach(([r, g, b], slot) => { const value = r << 11 | g << 6 | b << 1 | 1; palette[8 + free[slot] * 2] = value >> 8; palette[9 + free[slot] * 2] = value & 0xFF; });
  const colours = Array.from({ length: 256 }, (_, i) => { const v = palette[8 + i * 2] << 8 | palette[9 + i * 2]; return [up(v >> 11 & 31), up(v >> 6 & 31), up(v >> 1 & 31)]; });
  const indices = new Uint8Array(w * h);
  for (let p = 0; p < indices.length; ++p) indices[p] = free[pixels[p]];

  // Cells to tiles: a tile and its three flips are one kind.
  const kind = new Map<string, [string, number]>();     // a cell's pixels -> (its kind's, the flag that draws it)
  const cells: string[] = [];
  for (let y = 0; y < rows; ++y) for (let x = 0; x < columns; ++x) {
    const cell = new Uint8Array(256);
    for (let r = 0; r < CELL; ++r) cell.set(indices.subarray((y * CELL + r) * w + x * CELL, (y * CELL + r) * w + (x + 1) * CELL), r * CELL);
    const key = keyOf(cell);
    cells.push(key);
    if (!kind.has(key)) {
      let best: [string, number] = [key, 0];
      for (const [flag, bytes] of flips(cell)) { const k = keyOf(bytes); if (k < best[0]) best = [k, flag]; }
      kind.set(key, best);
    }
  }
  const kinds = [...new Set([...kind.values()].map(([canonical]) => canonical))].sort();
  const drawn = kinds.length > TILES ? mergeTiles(kinds, colours, TILES) : new Map(kinds.map((k) => [k, k]));
  const atlasTiles = [...new Set(drawn.values())].sort();
  const number = new Map(atlasTiles.map((tile, index) => [tile, tileCode(index + 1)]));   // tile 0 is not drawn

  const atlas = new Uint8Array(8 + 512 * 512).fill(free[0]);
  atlas.set([0, 8, 2, 0, 2, 0, 0, 0]);
  atlasTiles.forEach((tile, n) => {
    const index = n + 1, ax = index % 32 * CELL, ay = (index >> 5) * CELL;
    for (let r = 0; r < CELL; ++r) for (let c = 0; c < CELL; ++c) atlas[8 + (ay + r) * 512 + ax + c] = tile.charCodeAt(r * CELL + c);
  });
  const placements = new Map<number, [number, number, number][]>();
  const records: number[] = [];
  const shown = new Uint8Array(w * h);
  cells.forEach((cell, at) => {
    const x = at % columns, y = at / columns | 0;
    const [canonical, flag] = kind.get(cell)!;
    const tile = drawn.get(canonical)!, code = number.get(tile)!;
    const first = flag | grid[y][x];
    records.push(first >> 8, first & 0xFF, code >> 8, code & 0xFF);
    if (!placements.has(code)) placements.set(code, []);
    placements.get(code)!.push([flag, x * CELL, y * CELL]);
    const bytes = Uint8Array.from(tile, (ch) => ch.charCodeAt(0));
    const pixelsOf = flips(bytes).find(([f]) => f === flag)![1];
    for (let r = 0; r < CELL; ++r) shown.set(pixelsOf.subarray(r * CELL, (r + 1) * CELL), (y * CELL + r) * w + x * CELL);
  });
  const groups = [...placements.keys()].sort((a, b) => a - b);
  const listsAt = 8 + records.length + groups.length * 6;
  const table: number[] = [], lists: number[] = [];
  const push16 = (into: number[], ...values: number[]) => { for (const v of values) into.push(v >> 8 & 0xFF, v & 0xFF); };
  for (const tile of groups) {
    push16(table, tile, placements.get(tile)!.length, listsAt + lists.length);
    for (const placement of placements.get(tile)!) push16(lists, ...placement);
  }
  const header: number[] = [];
  push16(header, 7, groups.length, columns * 2, rows * 2);
  const layout = [...header, ...records, ...table, ...lists];
  if (layout.length > 0xFFFF) throw new CompileError(`地图太大：布局 ${layout.length} 字节，放置表的偏移只有 16 位`);

  const preview = new Uint8ClampedArray(w * h * 4);
  let error = 0;
  for (let p = 0; p < shown.length; ++p) {
    const colour = colours[shown[p]];
    preview[p * 4] = colour[0]; preview[p * 4 + 1] = colour[1]; preview[p * 4 + 2] = colour[2]; preview[p * 4 + 3] = 255;
    for (let c = 0; c < 3; ++c) { const d = colour[c] - small[p * 3 + c]; error += d * d; }
  }
  error /= shown.length * 3;
  const image = { schema: 'srw64.map-image.v1', name: source.name, like: like.map, replaces: { layout: like.layout, atlas: like.atlas, palette: like.palette },
    columns, rows, scale, tiles: atlasTiles.length, kinds: kinds.length, psnr: error ? Math.round(100 * Math.log10(255 * 255 / error)) / 10 : null,
    layout_hex: toHex(layout), atlas_hex: toHex(atlas), palette_hex: toHex(palette), hd: scale > 1 };
  const meta = scale > 1 ? { schema: 'srw64.hd-map-runtime.v0', layout: like.layout, atlas: like.atlas, palette: like.palette, width: w, height: h, scale,
    base: 'base.png', reference_palette: colours.map((c, i) => [...c, i === 0 ? 0 : 255]) } : null;
  return { image, meta, columns, rows, indices, preview };
}
