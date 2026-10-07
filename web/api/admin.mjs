#!/usr/bin/env node
// Work through suggestions from the command line (maintainers).
//
//   node web/api/admin.mjs list [pending|questioned|processed|dismissed]
//   node web/api/admin.mjs dismiss|question|process <reason> <id> [id…]
//   node web/api/admin.mjs restore <id> [id…]
//   node web/api/admin.mjs hide|unhide <id> [id…]      words off / back on the public lists
//   node web/api/admin.mjs ban|unban <id> [id…]        the authors of these suggestions
//
// Uses ~/.config/srw64/site-admin-token (written by web/deploy/deploy.sh) and
// https://srw64.dreamquest.club, or SRW64_API_BASE / SRW64_ADMIN_TOKEN.
import { readFileSync } from 'node:fs';
import { homedir } from 'node:os';
import { join } from 'node:path';

const BASE = process.env.SRW64_API_BASE || 'https://srw64.dreamquest.club';
const TOKEN = process.env.SRW64_ADMIN_TOKEN || readFileSync(join(homedir(), '.config/srw64/site-admin-token'), 'utf8').trim();
const [cmd, ...args] = process.argv.slice(2);
const call = async (path, init = {}) => {
  const r = await fetch(`${BASE}/api/${path}`, { ...init, headers: { Authorization: `Bearer ${TOKEN}`, 'Content-Type': 'application/json', Origin: BASE } });
  const data = await r.json();
  if (!r.ok) throw new Error(data.error || r.status);
  return data;
};
if (cmd === 'list') {
  const { items } = await call(`admin/suggestions${args[0] ? `?status=${args[0]}` : ''}`);
  for (const x of items) {
    console.log(`#${x.id} [${x.status}]${x.hidden ? ' [hidden]' : ''} ${x.target_type} ${x.target_id} ${x.locale} ${x.kind} — ${x.author.name} (participant ${x.participant_id}${x.banned ? ', banned' : ''})`);
    console.log(`   now: ${x.current.replace(/\n/g, ' / ')}`);
    if (x.proposed) console.log(`   new: ${x.proposed.replace(/\n/g, ' / ')}`);
    if (x.reason) console.log(`   why: ${x.reason}`);
    if (x.question_response) console.log(`   answer: ${x.question_response}`);
  }
} else if (['dismiss', 'question', 'process'].includes(cmd)) {
  const [reason, ...ids] = args;
  console.log(await call('admin/suggestions', { method: 'POST', body: JSON.stringify({ action: cmd, reason, ids: ids.map(Number) }) }));
} else if (['hide', 'unhide', 'ban', 'unban'].includes(cmd)) {
  console.log(await call('admin/suggestions', { method: 'POST', body: JSON.stringify({ action: cmd, ids: args.map(Number) }) }));
} else if (cmd === 'restore') {
  console.log(await call('admin/suggestions', { method: 'POST', body: JSON.stringify({ action: 'restore', ids: args.map(Number) }) }));
} else {
  console.log('usage: admin.mjs list [status] | dismiss|question|process <reason> <ids…> | restore|hide|unhide|ban|unban <ids…>');
  process.exit(1);
}
