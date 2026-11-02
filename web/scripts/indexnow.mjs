#!/usr/bin/env node
// Tell search engines which pages changed, through IndexNow (Bing, Yandex, Seznam, Naver and
// the others that share it; ChatGPT search and Copilot read Bing's index). deploy.sh runs it
// once the CDN serves the new release.
//
//   node web/scripts/indexnow.mjs web/dist          the pages whose content changed
//   node web/scripts/indexnow.mjs web/dist --all    every page in the sitemap
//
// The pages are the build's sitemap.xml plus /llms.txt. What each looked like at the last
// accepted submission is kept in web/.data/indexnow.json (a hash per address), so the first
// run sends everything and later runs only what changed. The key is the file
// public/<key>.txt, which the engines fetch to check the submission is the site's own.
import { createHash } from 'node:crypto';
import { existsSync, readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

const SITE = 'https://srw64.dreamquest.club';
const WEB = fileURLToPath(new URL('..', import.meta.url));
const STATE = join(WEB, '.data/indexnow.json');
const dist = process.argv[2] ?? join(WEB, 'dist');
const all = process.argv.includes('--all');

const key = readdirSync(join(WEB, 'public')).find((f) => /^[0-9a-f]{32}\.txt$/.test(f))?.slice(0, -4);
if (!key) throw new Error('no IndexNow key file (public/<32 hex>.txt)');

const urls = [...readFileSync(join(dist, 'sitemap.xml'), 'utf8').matchAll(/<loc>([^<]+)<\/loc>/g)].map((m) => m[1]);
urls.push(`${SITE}/llms.txt`);
const fileOf = (url) => {
  const path = decodeURI(url.slice(SITE.length));
  return join(dist, path.endsWith('/') ? `${path}index.html` : path);
};
const hashes = Object.fromEntries(urls.filter((u) => existsSync(fileOf(u)))
  .map((u) => [u, createHash('sha256').update(readFileSync(fileOf(u))).digest('hex').slice(0, 16)]));
const before = !all && existsSync(STATE) ? JSON.parse(readFileSync(STATE, 'utf8')) : {};
const changed = Object.keys(hashes).filter((u) => before[u] !== hashes[u]);
if (!changed.length) { console.log('indexnow: nothing changed'); process.exit(0); }

for (let i = 0; i < changed.length; i += 10000) {
  const res = await fetch('https://api.indexnow.org/indexnow', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json; charset=utf-8' },
    body: JSON.stringify({ host: new URL(SITE).host, key, keyLocation: `${SITE}/${key}.txt`, urlList: changed.slice(i, i + 10000) }),
  });
  // 200 accepted, 202 accepted while the key is still being checked.
  if (res.status !== 200 && res.status !== 202) {
    console.error(`indexnow: HTTP ${res.status} ${(await res.text()).slice(0, 200)}`);
    process.exit(1);
  }
}
writeFileSync(STATE, JSON.stringify(hashes));
console.log(`indexnow: ${changed.length} page(s) sent`);
