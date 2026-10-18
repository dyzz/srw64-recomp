// A file under public/ with its content hash in the query (/media/x.webp?v=1a2b3c4d), so a
// picture replaced under the same name is fetched again: /media is cached for a week.
import { createHash } from 'node:crypto';
import { existsSync, readFileSync } from 'node:fs';
import { join } from 'node:path';

const PUBLIC = join(process.cwd(), 'public');
const seen = new Map<string, string>();
export function asset(path: string): string {
  let url = seen.get(path);
  if (!url) {
    const file = join(PUBLIC, path);
    url = existsSync(file) ? `${path}?v=${createHash('sha256').update(readFileSync(file)).digest('hex').slice(0, 8)}` : path;
    seen.set(path, url);
  }
  return url;
}
