// SRW64 site API: story search, anonymous identities and translation suggestions.
// Plain Node (22.13+ for node:sqlite), no dependencies. Nginx proxies /api/ here.
// Modeled on the review store of our SRW Z community site: same four states, server-side targets, admin by token.
//
//   SRW64_DATA=web/.data SRW64_DB=/srv/srw64/data/reviews.sqlite3 \
//   SRW64_ADMIN_TOKEN=… node web/api/server.mjs
//
// Environment:
//   SRW64_API_HOST / SRW64_API_PORT   listen address (127.0.0.1:3064)
//   SRW64_DATA     exported data: story/search.json, story/index.json, library.json
//   SRW64_DB       SQLite file (created if missing)
//   SRW64_ADMIN_TOKEN  Bearer token for /api/admin/*; admin is off without it
//   SRW64_ORIGINS  comma-separated origins allowed to write (default the site and localhost)
//   SRW64_SECURE_COOKIE=1  mark the session cookie Secure (production, behind HTTPS)
//
// A suggestion targets a dialogue line (`base:t00_NNNNN`) or a Library name
// (`unit:N`, `person:N`, `weapon:N`) in one translation locale (zh-Hans, en). Its
// state is derived: dismissed or questioned by the maintainer; processed when the
// maintainer says so or when the live translation no longer equals the text the
// suggestion was made against (adopted when it now equals the proposal); else pending.
// The site never becomes the translation's master copy: accepted changes go into
// content/ in the repository and reach the site with the next data export.

import { createServer } from 'node:http';
import { DatabaseSync } from 'node:sqlite';
import { readFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { createHash, randomBytes, timingSafeEqual } from 'node:crypto';

const HOST = process.env.SRW64_API_HOST || '127.0.0.1';
const PORT = Number(process.env.SRW64_API_PORT || 3064);
const DATA = process.env.SRW64_DATA || join(process.cwd(), '.data');
const DB_PATH = process.env.SRW64_DB || join(DATA, 'reviews.sqlite3');
const ADMIN_TOKEN = process.env.SRW64_ADMIN_TOKEN || '';
const ORIGINS = (process.env.SRW64_ORIGINS || 'https://srw64.dreamquest.club,http://localhost:4321,http://127.0.0.1:4321,http://localhost:59151,http://127.0.0.1:59151').split(',');
const SECURE = process.env.SRW64_SECURE_COOKIE === '1';
const COOKIE = 'srw64_session';
const LOCALES = new Set(['zh-Hans', 'en']);
const KINDS = new Set(['mistranslation', 'awkward', 'typo', 'naming', 'other']);
const MAX_BODY = 64 * 1024;
// Every list answers with at most the most recent 500 suggestions.
const MAX_LIST = 500;

// ---------------------------------------------------------------- game data
const json = (p) => JSON.parse(readFileSync(join(DATA, p), 'utf8'));
const searchData = json('story/search.json');
const storyIndex = json('story/index.json').scenes;
const library = json('library.json');
const sceneTitle = new Map(storyIndex.map((s) => [s.scene, s.title]));
const lines = new Map(); // id -> row
for (const r of searchData.rows) {
  const [id, scene, whoZh, whoEn, whoJa, ja, zh, en] = r;
  lines.set(id, { id, scene, who: { zh: whoZh, en: whoEn, ja: whoJa }, ja, zh, en, low: { ja: ja.toLowerCase(), zh: zh.toLowerCase(), en: en.toLowerCase() } });
}
const terms = new Map(); // `unit:55` -> {ja, zh, en}
for (const u of library.units) {
  terms.set(`unit:${u.id}`, u.name);
  for (const w of u.weapons) terms.set(`weapon:${w.id}`, w.name);
}
for (const p of library.people) {
  terms.set(`person:${p.id}`, p.name);
  if (p.full_name.ja !== p.name.ja) terms.set(`person-full:${p.id}`, p.full_name);
}
// Names shared across the Library are keyed once: spirits by id, skills and abilities
// by their Japanese name.
for (const p of library.people) {
  for (const x of p.spirits) terms.set(`spirit:${x.id}`, x.name);
  for (const x of p.skills) terms.set(`skill:${x.name.ja}`, x.name);
}
for (const u of library.units) for (const a of u.abilities) terms.set(`ability:${a.ja}`, a);
for (const x of library.series) if (x.id != null) terms.set(`series:${x.id}`, x.name);
// Identities borrow a character's name and portrait, as on the SRW Z site. Anyone named
// with an HD portrait can be picked (one entry per name, not the four heroes and their
// partners, whose names the player sets); a random draw takes an ally. A nickname gets a
// nameless soldier's face.
const seenName = new Set();
const pickable = library.people
  .filter((p) => p.enemy !== null && p.portrait?.hd && !p.portrait.silhouette_rgb && p.name.zh
    && !(p.id >= 25 && p.id <= 32) && !/[(（]/.test(p.name.ja))
  .filter((p) => !seenName.has(p.name.zh) && seenName.add(p.name.zh));
const pickableById = new Map(pickable.map((p) => [p.id, p]));
const pilots = pickable.filter((p) => p.enemy === false);
const seenFace = new Set();
const soldierFaces = library.people
  .filter((p) => p.enemy === null && p.portrait?.hd && !p.portrait.silhouette_rgb)
  .map((p) => p.portrait.image).filter((n) => !seenFace.has(n) && seenFace.add(n));
const randomOf = (list) => list[randomBytes(4).readUInt32BE() % list.length];
const seriesOrder = new Map(library.series.map((x, i) => [x.id, i]));

const L = (locale) => (locale === 'zh-Hans' ? 'zh' : locale);
function target(type, id, locale) {
  const l = L(locale);
  if (type === 'dialogue') {
    const m = /^base:t00_(\d{5})$/.exec(id);
    const row = m && lines.get(Number(m[1]));
    return row ? { source: row.ja, current: row[l], scene: row.scene } : null;
  }
  if (type === 'term') {
    const name = terms.get(id);
    return name ? { source: name.ja, current: name[l] } : null;
  }
  return null;
}

// ---------------------------------------------------------------- database
mkdirSync(dirname(DB_PATH), { recursive: true });
const db = new DatabaseSync(DB_PATH);
db.exec(`
  PRAGMA journal_mode = WAL;
  PRAGMA foreign_keys = ON;
  CREATE TABLE IF NOT EXISTS participants (
    id INTEGER PRIMARY KEY, mode TEXT NOT NULL, pilot_id INTEGER, custom_name TEXT,
    custom_avatar INTEGER, created_at TEXT NOT NULL, updated_at TEXT NOT NULL);
  CREATE TABLE IF NOT EXISTS sessions (
    token_hash TEXT PRIMARY KEY, participant_id INTEGER NOT NULL REFERENCES participants(id),
    created_at TEXT NOT NULL, expires_at TEXT NOT NULL);
  CREATE TABLE IF NOT EXISTS suggestions (
    id INTEGER PRIMARY KEY, participant_id INTEGER NOT NULL REFERENCES participants(id),
    request_id TEXT NOT NULL, target_type TEXT NOT NULL, target_id TEXT NOT NULL, locale TEXT NOT NULL,
    kind TEXT NOT NULL, source_text TEXT NOT NULL, current_text TEXT NOT NULL,
    proposed TEXT NOT NULL DEFAULT '', reason TEXT NOT NULL DEFAULT '', context_json TEXT NOT NULL DEFAULT '{}',
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL,
    admin_status TEXT NOT NULL DEFAULT '', admin_reason TEXT NOT NULL DEFAULT '', admin_at TEXT,
    question_response TEXT NOT NULL DEFAULT '', question_at TEXT,
    UNIQUE (participant_id, request_id));
  CREATE INDEX IF NOT EXISTS suggestions_target ON suggestions (target_type, target_id, locale);
  CREATE TABLE IF NOT EXISTS admin_log (
    id INTEGER PRIMARY KEY, suggestion_id INTEGER NOT NULL, action TEXT NOT NULL, reason TEXT NOT NULL, at TEXT NOT NULL);
`);
// Databases made before nicknames had faces.
if (!db.prepare('PRAGMA table_info(participants)').all().some((c) => c.name === 'custom_avatar')) {
  db.exec('ALTER TABLE participants ADD COLUMN custom_avatar INTEGER');
}
const now = () => new Date().toISOString();
const sha = (s) => createHash('sha256').update(s).digest('hex');

// ---------------------------------------------------------------- identity
function cookies(req) {
  const out = {};
  for (const part of (req.headers.cookie || '').split(';')) {
    const i = part.indexOf('=');
    if (i > 0) out[part.slice(0, i).trim()] = decodeURIComponent(part.slice(i + 1).trim());
  }
  return out;
}
function participantOf(req) {
  const token = cookies(req)[COOKIE];
  if (!token) return null;
  const row = db.prepare(`SELECT p.* FROM sessions s JOIN participants p ON p.id = s.participant_id
                          WHERE s.token_hash = ? AND s.expires_at > ?`).get(sha(token), now());
  return row || null;
}
function newParticipant(res) {
  const pilot = randomOf(pilots);
  const t = now();
  const { lastInsertRowid } = db.prepare('INSERT INTO participants (mode, pilot_id, created_at, updated_at) VALUES (?, ?, ?, ?)').run('pilot', pilot.id, t, t);
  const token = randomBytes(32).toString('base64url');
  const expires = new Date(Date.now() + 365 * 86400e3).toISOString();
  db.prepare('INSERT INTO sessions (token_hash, participant_id, created_at, expires_at) VALUES (?, ?, ?, ?)').run(sha(token), lastInsertRowid, t, expires);
  res.setHeader('Set-Cookie', `${COOKIE}=${token}; Path=/; HttpOnly; SameSite=Lax; Max-Age=${365 * 86400}${SECURE ? '; Secure' : ''}`);
  return db.prepare('SELECT * FROM participants WHERE id = ?').get(lastInsertRowid);
}
function display(p, lang) {
  if (!p) return { name: '', avatar: null };
  if (p.mode === 'custom' && p.custom_name) {
    return { name: p.custom_name, avatar: p.custom_avatar != null ? `/gen/portraits/${p.custom_avatar}.webp` : null };
  }
  const pilot = library.people.find((x) => x.id === p.pilot_id);
  return { name: pilot ? pilot.name[lang] || pilot.name.zh : '?', avatar: pilot ? `/gen/portraits/${pilot.portrait.image}.webp` : null };
}

// ---------------------------------------------------------------- suggestions
function status(row) {
  const live = target(row.target_type, row.target_id, row.locale)?.current;
  if (row.admin_status === 'dismissed') return { status: 'dismissed' };
  if (row.admin_status === 'questioned') return { status: 'questioned' };
  if (live != null && live !== row.current_text) return { status: 'processed', adopted: !!row.proposed && live === row.proposed, live };
  if (row.admin_status === 'processed') return { status: 'processed', adopted: false, live };
  return { status: 'pending' };
}
function view(row, me, lang) {
  const owner = db.prepare('SELECT * FROM participants WHERE id = ?').get(row.participant_id);
  const st = status(row);
  const context = JSON.parse(row.context_json || '{}');
  if (context.scene != null) context.scene_title = sceneTitle.get(context.scene)?.[lang] ?? null;
  return {
    id: row.id, target_type: row.target_type, target_id: row.target_id, locale: row.locale, kind: row.kind,
    source: row.source_text, current: row.current_text, proposed: row.proposed, reason: row.reason,
    context, created_at: row.created_at, updated_at: row.updated_at,
    author: display(owner, lang), mine: !!me && me.id === row.participant_id,
    ...st, result: st.status === 'processed' ? (st.adopted ? 'adopted' : 'final') : st.status,
    admin_reason: row.admin_reason, admin_at: row.admin_at, question_response: row.question_response,
  };
}
const clean = (s, max) => String(s ?? '').replace(/\r\n?/g, '\n').trim().slice(0, max);

// ---------------------------------------------------------------- http
const hits = new Map();
function limited(req) {
  const ip = req.headers['ali-cdn-real-ip'] || req.headers['x-real-ip'] || req.socket.remoteAddress;
  const minute = Math.floor(Date.now() / 60000);
  const key = `${ip}:${minute}`;
  const n = (hits.get(key) || 0) + 1;
  hits.set(key, n);
  if (hits.size > 5000) for (const k of hits.keys()) if (!k.endsWith(`:${minute}`)) hits.delete(k);
  return n > 10;
}
function send(res, code, body, headers = {}) {
  res.writeHead(code, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store', ...headers });
  res.end(JSON.stringify(body));
}
async function readBody(req) {
  let size = 0;
  const chunks = [];
  for await (const c of req) {
    size += c.length;
    if (size > MAX_BODY) throw Object.assign(new Error('body too large'), { code: 413 });
    chunks.push(c);
  }
  return chunks.length ? JSON.parse(Buffer.concat(chunks).toString('utf8')) : {};
}
function admin(req) {
  const m = /^Bearer (.+)$/.exec(req.headers.authorization || '');
  if (!ADMIN_TOKEN || !m) return false;
  const a = Buffer.from(m[1]), b = Buffer.from(ADMIN_TOKEN);
  return a.length === b.length && timingSafeEqual(a, b);
}

function search(q, lang) {
  const needle = q.toLowerCase();
  const results = [];
  const order = lang === 'ja' ? ['ja', 'en', 'zh'] : [lang, 'ja', lang === 'zh' ? 'en' : 'zh'];
  for (const row of lines.values()) {
    if (!order.some((l) => row.low[l].includes(needle))) continue;
    results.push({ id: row.id, scene: row.scene, scene_title: sceneTitle.get(row.scene)?.[lang] ?? '', who: row.who[lang], text: row[lang === 'ja' ? 'ja' : lang].replace(/\n/g, lang === 'en' ? ' ' : ''), ja: lang === 'ja' ? undefined : row.ja.replace(/\n/g, '') });
    if (results.length > 100) break;
  }
  return { results: results.slice(0, 100), truncated: results.length > 100 };
}

const routes = {
  'GET /api/health': () => [200, { ok: true, lines: lines.size, terms: terms.size }],

  'GET /api/story/search': (req, url) => {
    const q = clean(url.searchParams.get('q'), 80);
    const lang = ['zh', 'en', 'ja'].includes(url.searchParams.get('lang')) ? url.searchParams.get('lang') : 'zh';
    return q ? [200, search(q, lang)] : [200, { results: [], truncated: false }];
  },

  'GET /api/me': (req, url) => {
    const me = participantOf(req);
    const lang = url.searchParams.get('lang') || 'zh';
    return [200, { participant: me ? { ...display(me, lang), mode: me.mode, pilot_id: me.mode === 'pilot' ? me.pilot_id : null } : null }];
  },

  // Everyone an identity can borrow, in the Library's series order.
  'GET /api/me/pilots': (req, url) => {
    const lang = url.searchParams.get('lang') || 'zh';
    const list = [...pickable].sort((a, b) => (seriesOrder.get(a.series) ?? 999) - (seriesOrder.get(b.series) ?? 999) || a.id - b.id);
    const series = library.series.map((x) => ({ id: x.id, name: x.name[lang] || x.name.zh }));
    return [200, { series, pilots: list.map((p) => ({ id: p.id, series: p.series, name: p.name[lang] || p.name.zh, ja: p.name.ja, avatar: `/gen/portraits/${p.portrait.image}.webp` })) }];
  },

  // A nickname ({mode:'custom', name}), a chosen character ({mode:'pilot', pilot_id}) or a
  // random ally ({mode:'pilot'}). Creates the identity when the browser has none yet.
  'POST /api/me': async (req, url, res) => {
    const body = await readBody(req);
    let me = participantOf(req) || newParticipant(res);
    if (body.mode === 'custom') {
      const name = clean(body.name, 20);
      if (!name) return [400, { error: 'name required' }];
      // The soldier's face stays while the nickname changes.
      const face = me.mode === 'custom' && me.custom_avatar != null ? me.custom_avatar : randomOf(soldierFaces);
      db.prepare('UPDATE participants SET mode = ?, custom_name = ?, custom_avatar = ?, updated_at = ? WHERE id = ?').run('custom', name, face, now(), me.id);
    } else {
      let pilot;
      if (body.pilot_id != null) {
        pilot = pickableById.get(Number(body.pilot_id));
        if (!pilot) return [400, { error: 'unknown pilot' }];
      } else {
        do pilot = randomOf(pilots); while (pilots.length > 1 && me.mode === 'pilot' && pilot.id === me.pilot_id);
      }
      db.prepare('UPDATE participants SET mode = ?, pilot_id = ?, updated_at = ? WHERE id = ?').run('pilot', pilot.id, now(), me.id);
    }
    me = db.prepare('SELECT * FROM participants WHERE id = ?').get(me.id);
    return [200, { participant: { ...display(me, url.searchParams.get('lang') || 'zh'), mode: me.mode, pilot_id: me.mode === 'pilot' ? me.pilot_id : null } }];
  },

  // ?scene=N (one story page), ?target_type=&target_id= (one target),
  // or the review lists: ?status=pending|questioned|processed|dismissed&mine=1&locale=&offset=&limit=
  'GET /api/suggestions': (req, url) => {
    const me = participantOf(req);
    const lang = url.searchParams.get('lang') || 'zh';
    const p = url.searchParams;
    let rows;
    // The latest MAX_LIST rows, oldest first on a page, newest first on the lists.
    const recent = (where, args) => db.prepare(`SELECT * FROM (SELECT * FROM suggestions WHERE ${where} ORDER BY id DESC LIMIT ${MAX_LIST}) ORDER BY id`).all(...args);
    if (p.get('scene')) {
      rows = recent(`target_type = 'dialogue' AND json_extract(context_json, '$.scene') = ?`, [Number(p.get('scene'))]);
    } else if (p.get('target_id')) {
      rows = recent('target_type = ? AND target_id = ?', [p.get('target_type') || 'dialogue', p.get('target_id')]);
    } else if (p.get('target_prefix') != null) {
      rows = recent('target_type = ? AND target_id LIKE ?', [p.get('target_type') || 'term', `${p.get('target_prefix')}%`]);
    } else {
      const where = ['1'], args = [];
      if (p.get('mine') === '1') { where.push('participant_id = ?'); args.push(me ? me.id : -1); }
      if (LOCALES.has(p.get('locale'))) { where.push('locale = ?'); args.push(p.get('locale')); }
      rows = recent(where.join(' AND '), args).reverse();
    }
    let items = rows.map((r) => view(r, me, lang));
    const counts = { pending: 0, questioned: 0, processed: 0, dismissed: 0 };
    for (const it of items) counts[it.status]++;
    if (p.get('status')) items = items.filter((it) => it.status === p.get('status'));
    const offset = Math.max(0, Number(p.get('offset') || 0));
    const limit = Math.min(MAX_LIST, Math.max(1, Number(p.get('limit') || MAX_LIST)));
    return [200, { items: items.slice(offset, offset + limit), total: items.length, counts }];
  },

  'POST /api/suggestions': async (req, url, res) => {
    if (limited(req)) return [429, { error: 'too many requests' }];
    const b = await readBody(req);
    const type = b.target_type === 'term' ? 'term' : 'dialogue';
    const locale = LOCALES.has(b.locale) ? b.locale : null;
    if (!locale) return [400, { error: 'locale must be zh-Hans or en' }];
    const tgt = target(type, String(b.target_id || ''), locale);
    if (!tgt) return [404, { error: 'unknown target' }];
    const proposed = clean(b.proposed, 2000);
    const reason = clean(b.reason, 500);
    if (!proposed && !reason) return [400, { error: 'proposed or reason required' }];
    if (proposed && proposed === tgt.current) return [400, { error: 'proposal equals the current text' }];
    const kind = KINDS.has(b.kind) ? b.kind : 'other';
    const requestId = clean(b.request_id, 64) || randomBytes(8).toString('hex');
    const me = participantOf(req) || newParticipant(res);
    const ctx = { scene: tgt.scene ?? (Number.isInteger(b.context?.scene) ? b.context.scene : undefined), href: clean(b.context?.href, 300) || undefined, speaker: clean(b.context?.speaker, 60) || undefined };
    const t = now();
    const existing = db.prepare('SELECT * FROM suggestions WHERE participant_id = ? AND request_id = ?').get(me.id, requestId);
    if (existing) return [200, { item: view(existing, me, url.searchParams.get('lang') || 'zh') }];
    const { lastInsertRowid } = db.prepare(`INSERT INTO suggestions
      (participant_id, request_id, target_type, target_id, locale, kind, source_text, current_text, proposed, reason, context_json, created_at, updated_at)
      VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`).run(me.id, requestId, type, String(b.target_id), locale, kind, tgt.source, tgt.current, proposed, reason, JSON.stringify(ctx), t, t);
    return [201, { item: view(db.prepare('SELECT * FROM suggestions WHERE id = ?').get(lastInsertRowid), me, url.searchParams.get('lang') || 'zh') }];
  },

  // Edit your own suggestion while it is pending, or answer the maintainer's question.
  'PATCH /api/suggestions': async (req, url) => {
    if (limited(req)) return [429, { error: 'too many requests' }];
    const b = await readBody(req);
    const me = participantOf(req);
    const row = me && db.prepare('SELECT * FROM suggestions WHERE id = ? AND participant_id = ?').get(Number(b.id), me.id);
    if (!row) return [404, { error: 'not found' }];
    const st = status(row).status;
    if (b.answer != null) {
      if (st !== 'questioned') return [409, { error: 'no open question' }];
      db.prepare('UPDATE suggestions SET question_response = ?, question_at = ?, updated_at = ? WHERE id = ?').run(clean(b.answer, 1000), now(), now(), row.id);
    } else {
      if (st !== 'pending') return [409, { error: 'only pending suggestions can be edited' }];
      const proposed = clean(b.proposed ?? row.proposed, 2000);
      const reason = clean(b.reason ?? row.reason, 500);
      if (!proposed && !reason) return [400, { error: 'proposed or reason required' }];
      db.prepare('UPDATE suggestions SET proposed = ?, reason = ?, kind = ?, updated_at = ? WHERE id = ?')
        .run(proposed, reason, KINDS.has(b.kind) ? b.kind : row.kind, now(), row.id);
    }
    return [200, { item: view(db.prepare('SELECT * FROM suggestions WHERE id = ?').get(row.id), me, url.searchParams.get('lang') || 'zh') }];
  },

  'DELETE /api/suggestions': (req, url) => {
    const me = participantOf(req);
    const id = Number(url.searchParams.get('id'));
    const row = me && db.prepare('SELECT * FROM suggestions WHERE id = ? AND participant_id = ?').get(id, me.id);
    if (!row) return [404, { error: 'not found' }];
    if (status(row).status === 'processed') return [409, { error: 'processed suggestions stay' }];
    db.prepare('DELETE FROM suggestions WHERE id = ?').run(id);
    return [200, { ok: true }];
  },

  // Maintainer: {action: dismiss|question|process|restore, ids: [..], reason}.
  'POST /api/admin/suggestions': async (req) => {
    if (!admin(req)) return [401, { error: 'admin token required' }];
    const b = await readBody(req);
    const ids = (Array.isArray(b.ids) ? b.ids : []).map(Number).filter(Number.isInteger).slice(0, 200);
    const reason = clean(b.reason, 1000);
    const state = { dismiss: 'dismissed', question: 'questioned', process: 'processed', restore: '' }[b.action];
    if (state === undefined || !ids.length) return [400, { error: 'action and ids required' }];
    if (b.action !== 'restore' && !reason) return [400, { error: 'reason required' }];
    const t = now();
    let changed = 0;
    db.exec('BEGIN');
    try {
      for (const id of ids) {
        changed += db.prepare('UPDATE suggestions SET admin_status = ?, admin_reason = ?, admin_at = ?, updated_at = ? WHERE id = ?').run(state, reason, t, t, id).changes;
        db.prepare('INSERT INTO admin_log (suggestion_id, action, reason, at) VALUES (?, ?, ?, ?)').run(id, b.action, reason, t);
      }
      db.exec('COMMIT');
    } catch (e) { db.exec('ROLLBACK'); throw e; }
    return [200, { changed }];
  },

  // Maintainer export: every suggestion with its derived state, for working through.
  'GET /api/admin/suggestions': (req, url) => {
    if (!admin(req)) return [401, { error: 'admin token required' }];
    let items = db.prepare(`SELECT * FROM suggestions ORDER BY id DESC LIMIT ${MAX_LIST}`).all().reverse().map((r) => view(r, null, 'zh'));
    const st = url.searchParams.get('status');
    if (st) items = items.filter((x) => x.status === st);
    return [200, { items }];
  },
};

createServer(async (req, res) => {
  const url = new URL(req.url, 'http://local');
  const handler = routes[`${req.method} ${url.pathname.replace(/\/$/, '')}`];
  if (!handler) return send(res, 404, { error: 'not found' });
  if (req.method !== 'GET') {
    const origin = req.headers.origin;
    if (origin && !ORIGINS.includes(origin)) return send(res, 403, { error: 'origin not allowed' });
  }
  try {
    const [code, body] = await handler(req, url, res);
    send(res, code, body);
  } catch (e) {
    if (e.code === 413) return send(res, 413, { error: e.message });
    if (e instanceof SyntaxError) return send(res, 400, { error: 'invalid JSON' });
    console.error(e);
    send(res, 500, { error: 'internal error' });
  }
}).listen(PORT, HOST, () => console.log(`srw64 api on ${HOST}:${PORT} (${lines.size} lines, ${terms.size} terms)`));
