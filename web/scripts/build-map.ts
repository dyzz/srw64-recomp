// The page's map tool (src/lib/compiler/map.ts) from the command line, for
// tests/test_web_compiler.py: a picture as raw RGBA in, the map image out, as JSON.
//
//   node web/scripts/build-map.ts <map.json> <picture.rgba> <width> <height> <templates.json> [name]
//
// templates.json: {"<map number>": template} as web/public/gen/editor.json's maps carry one.
// Prints {"image": …, "meta": …, "indices_hex": …} or {"error": "…"}: indices are the picture
// at the game's size, a palette index a pixel (the HD mode's index.png, tools/mods/map_tool.py).
import { readFileSync } from 'node:fs';
import { buildMap } from '../src/lib/compiler/map.ts';

const [source, picture, width, height, templates, name = 'map'] = process.argv.slice(2);
try {
  const document = JSON.parse(readFileSync(source, 'utf8'));
  const like = JSON.parse(readFileSync(templates, 'utf8'))[String(document.like)];
  const rgba = new Uint8ClampedArray(readFileSync(picture));
  const built = buildMap({ name, like: document.like, scale: document.scale ?? 4, terrain: document.terrain }, like, rgba, Number(width), Number(height));
  console.log(JSON.stringify({ image: built.image, meta: built.meta, indices_hex: Buffer.from(built.indices).toString('hex') }));
} catch (error) {
  console.log(JSON.stringify({ error: (error as Error).message }));
}
