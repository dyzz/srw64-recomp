// The offline single-file guide (guide/srw64-flow-guide.html, tools/content/build_guide.py)
// as a download beside the guide pages.
import { readFileSync } from 'node:fs';
import { join } from 'node:path';

export const GET = () =>
  new Response(readFileSync(join(process.cwd(), '..', 'guide', 'srw64-flow-guide.html')), {
    headers: { 'Content-Type': 'text/html; charset=utf-8' },
  });
