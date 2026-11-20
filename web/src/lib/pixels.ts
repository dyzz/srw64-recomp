// A package picture's pixels the way the game keeps them (tools/mods/sprite_pixels.py): alpha
// cut at half, at most `colors` colours by median cut without dithering, each channel as
// RGBA5551 has it. The caller scales (a canvas draws premultiplied, so transparent edges do not
// bleed in). No DOM, so tests/test_unit_maker.py runs it under Node.

// A channel as RGBA5551 keeps it, back in eight bits.
export const five = (v: number) => Math.floor(((v >> 3) * 255) / 31);

type Box = { colors: number[]; counts: number[] };   // packed 0xRRGGBB, how many pixels each

function range(box: Box, shift: number) {
  let low = 255, high = 0;
  for (const c of box.colors) { const v = (c >> shift) & 255; if (v < low) low = v; if (v > high) high = v; }
  return high - low;
}
// The widest channel's weighted median splits the box in two.
function split(box: Box): [Box, Box] | null {
  if (box.colors.length < 2) return null;
  const shift = [16, 8, 0].reduce((best, s) => (range(box, s) > range(box, best) ? s : best), 16);
  const order = box.colors.map((c, i) => i).sort((a, b) => ((box.colors[a] >> shift) & 255) - ((box.colors[b] >> shift) & 255));
  const total = box.counts.reduce((a, b) => a + b, 0);
  let seen = 0, cut = 1;
  for (let k = 0; k < order.length - 1; k++) { seen += box.counts[order[k]]; if (seen * 2 >= total) { cut = k + 1; break; } cut = k + 1; }
  const part = (from: number, to: number): Box => ({ colors: order.slice(from, to).map((i) => box.colors[i]), counts: order.slice(from, to).map((i) => box.counts[i]) });
  return [part(0, cut), part(cut, order.length)];
}

// `rgba` (width × height × 4) reduced in place; returns the number of opaque colours left.
export function reduce(rgba: Uint8ClampedArray | Uint8Array, colors: number): number {
  const histogram = new Map<number, number>();
  for (let i = 0; i < rgba.length; i += 4) {
    if (rgba[i + 3] < 128) continue;
    const c = (rgba[i] << 16) | (rgba[i + 1] << 8) | rgba[i + 2];
    histogram.set(c, (histogram.get(c) ?? 0) + 1);
  }
  const boxes: Box[] = [{ colors: [...histogram.keys()], counts: [...histogram.values()] }];
  while (boxes.length < colors) {
    // The box with the most pixels that can still split.
    let best = -1, most = -1;
    boxes.forEach((b, i) => { const n = b.counts.reduce((a, c) => a + c, 0); if (b.colors.length > 1 && n > most) { most = n; best = i; } });
    if (best < 0) break;
    const parts = split(boxes[best]);
    if (!parts) break;
    boxes.splice(best, 1, ...parts);
  }
  const mapped = new Map<number, number>();
  for (const b of boxes) {
    const n = b.counts.reduce((a, c) => a + c, 0) || 1;
    let r = 0, g = 0, bl = 0;
    b.colors.forEach((c, i) => { r += ((c >> 16) & 255) * b.counts[i]; g += ((c >> 8) & 255) * b.counts[i]; bl += (c & 255) * b.counts[i]; });
    const mean = (five(Math.round(r / n)) << 16) | (five(Math.round(g / n)) << 8) | five(Math.round(bl / n));
    for (const c of b.colors) mapped.set(c, mean);
  }
  const left = new Set<number>();
  for (let i = 0; i < rgba.length; i += 4) {
    if (rgba[i + 3] < 128) { rgba[i] = rgba[i + 1] = rgba[i + 2] = rgba[i + 3] = 0; continue; }
    const m = mapped.get((rgba[i] << 16) | (rgba[i + 1] << 8) | rgba[i + 2])!;
    rgba[i] = (m >> 16) & 255; rgba[i + 1] = (m >> 8) & 255; rgba[i + 2] = m & 255; rgba[i + 3] = 255;
    left.add(m);
  }
  return left.size;
}

// Where a pose's origin goes by default: the middle under its feet, the centre of the opaque
// pixels of its lowest rows and two pixels above the lowest (the original's poses stand on
// y 0 with a little below it).
export function foot(rgba: Uint8ClampedArray | Uint8Array, width: number, height: number): [number, number] {
  let bottom = -1;
  for (let y = height - 1; y >= 0 && bottom < 0; y--) for (let x = 0; x < width; x++) if (rgba[(y * width + x) * 4 + 3] >= 128) { bottom = y; break; }
  if (bottom < 0) return [Math.floor(width / 2), height - 1];
  let low = width, high = -1;
  for (let y = Math.max(0, bottom - 8); y <= bottom; y++)
    for (let x = 0; x < width; x++) if (rgba[(y * width + x) * 4 + 3] >= 128) { low = Math.min(low, x); high = Math.max(high, x); }
  return [Math.round((low + high) / 2), Math.max(0, bottom - 2)];
}
