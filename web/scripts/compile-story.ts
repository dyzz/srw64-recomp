// The story compiler (src/lib/compiler/story.ts) from the command line, for tools/mods/story.py
// and pack.py: a srw64.story.v1 file in, its srw64.story-image.v1 out, as JSON.
//
//   node web/scripts/compile-story.ts <story.json> [<lines.json>]
//
// lines.json: the package's lines files (story.py load_lines), key -> {locale: text}.
//
// Prints the image or {"error": "…"}.
import { readFileSync } from 'node:fs';
import { compileStory } from '../src/lib/compiler/story.ts';

try {
  const lines = process.argv[3] ? JSON.parse(readFileSync(process.argv[3], 'utf8')) : {};
  console.log(JSON.stringify(compileStory(JSON.parse(readFileSync(process.argv[2], 'utf8')), null, undefined, lines)));
} catch (error) {
  console.log(JSON.stringify({ error: (error as Error).message }));
}
