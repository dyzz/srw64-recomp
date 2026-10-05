// Online copy of the suggestions database: VACUUM INTO a dated file, check it opens
// and holds the same number of suggestions, keep the newest N days.
//   node api/backup.mjs <db> <backup dir> <days to keep>
import { DatabaseSync } from 'node:sqlite';
import { mkdirSync, readdirSync, rmSync, existsSync } from 'node:fs';
import { join } from 'node:path';

const [db, dir, keep = '30'] = process.argv.slice(2);
mkdirSync(dir, { recursive: true });
const stamp = new Date().toISOString().replace(/[:.]/g, '-');
const target = join(dir, `reviews-${stamp}.sqlite3`);
const src = new DatabaseSync(db, { readOnly: true });
const count = src.prepare('SELECT COUNT(*) AS n FROM suggestions').get().n;
src.exec(`VACUUM INTO '${target.replace(/'/g, "''")}'`);
src.close();
const copy = new DatabaseSync(target, { readOnly: true });
const ok = copy.prepare('PRAGMA integrity_check').get().integrity_check === 'ok' && copy.prepare('SELECT COUNT(*) AS n FROM suggestions').get().n === count;
copy.close();
if (!ok) { rmSync(target); throw new Error('backup check failed'); }
const files = readdirSync(dir).filter((f) => /^reviews-.*\.sqlite3$/.test(f)).sort();
for (const f of files.slice(0, Math.max(0, files.length - Number(keep)))) rmSync(join(dir, f));
console.log(`backup ${target}: ${count} suggestions; kept ${Math.min(files.length, Number(keep))}`);
