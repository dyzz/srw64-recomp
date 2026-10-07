#!/usr/bin/env node
// Work through suggestions from the command line (maintainers).
//
//   node web/api/admin.mjs list [pending|questioned|processed|dismissed]
//   node web/api/admin.mjs dismiss|question|process <reason> <id> [id…]
//   node web/api/admin.mjs restore <id> [id…]
//   node web/api/admin.mjs hide|unhide <id> [id…]      words off / back on the public lists
//   node web/api/admin.mjs ban|unban <id> [id…]        these suggestions' authors and addresses (30 days)
//
// The admin API is not public (Nginx answers 404 to /api/admin/): this opens an ssh
// tunnel to the API on the server itself (127.0.0.1:3064), on the host deploy.sh uses
// (SRW64_DEPLOY_HOST or ~/.config/srw64/site-deploy-host), and closes it when done. The
// token is ~/.config/srw64/site-admin-token (written by web/deploy/deploy.sh) or
// SRW64_ADMIN_TOKEN. SRW64_API_BASE (e.g. http://127.0.0.1:3064, a local API) skips the tunnel.
import { spawn } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { createServer, connect } from 'node:net';
import { homedir } from 'node:os';
import { join } from 'node:path';

const config = (name) => { try { return readFileSync(join(homedir(), '.config/srw64', name), 'utf8').trim(); } catch { return ''; } };
const TOKEN = process.env.SRW64_ADMIN_TOKEN || config('site-admin-token');
if (!TOKEN) { console.error('no admin token: SRW64_ADMIN_TOKEN or ~/.config/srw64/site-admin-token'); process.exit(1); }

// A free local port for the tunnel.
const freePort = () => new Promise((resolve) => { const s = createServer().listen(0, '127.0.0.1', () => { const { port } = s.address(); s.close(() => resolve(port)); }); });
const reachable = (port) => new Promise((resolve) => { const c = connect(port, '127.0.0.1', () => { c.end(); resolve(true); }); c.on('error', () => resolve(false)); });

let tunnel = null;
let BASE = process.env.SRW64_API_BASE;
if (!BASE) {
  const host = process.env.SRW64_DEPLOY_HOST || config('site-deploy-host');
  if (!host) { console.error('no host: SRW64_DEPLOY_HOST or ~/.config/srw64/site-deploy-host'); process.exit(1); }
  const port = await freePort();
  // Its own connection, not a shared ControlMaster one: a master would outlive this process,
  // and the tunnel with it.
  tunnel = spawn('ssh', ['-N', '-o', 'ExitOnForwardFailure=yes', '-o', 'ControlMaster=no', '-o', 'ControlPath=none',
    '-L', `${port}:127.0.0.1:3064`, host], { stdio: ['ignore', 'ignore', 'inherit'] });
  for (let i = 0; i < 100 && !(await reachable(port)); i++) {
    if (tunnel.exitCode !== null) { console.error('the ssh tunnel ended'); process.exit(1); }
    await new Promise((r) => setTimeout(r, 100));
  }
  BASE = `http://127.0.0.1:${port}`;
}

// No Origin header: the token is the credential here, not a browser's cookie.
const call = async (path, init = {}) => {
  const r = await fetch(`${BASE}/api/${path}`, { ...init, headers: { Authorization: `Bearer ${TOKEN}`, 'Content-Type': 'application/json' } });
  const data = await r.json();
  if (!r.ok) throw new Error(data.error || r.status);
  return data;
};

const [cmd, ...args] = process.argv.slice(2);
try {
  if (cmd === 'list') {
    const { items } = await call(`admin/suggestions${args[0] ? `?status=${args[0]}` : ''}`);
    for (const x of items) {
      console.log(`#${x.id} [${x.status}]${x.hidden ? ' [hidden]' : ''} ${x.target_type} ${x.target_id} ${x.locale} ${x.kind} — ${x.author.name} (participant ${x.participant_id}${x.banned ? ', banned' : ''}${x.ip_banned ? ', address banned' : ''})`);
      console.log(`   now: ${x.current.replace(/\n/g, ' / ')}`);
      if (x.proposed) console.log(`   new: ${x.proposed.replace(/\n/g, ' / ')}`);
      if (x.reason) console.log(`   why: ${x.reason}`);
      if (x.question_response) console.log(`   answer: ${x.question_response}`);
    }
  } else if (['dismiss', 'question', 'process'].includes(cmd)) {
    const [reason, ...ids] = args;
    console.log(await call('admin/suggestions', { method: 'POST', body: JSON.stringify({ action: cmd, reason, ids: ids.map(Number) }) }));
  } else if (['hide', 'unhide', 'ban', 'unban', 'restore'].includes(cmd)) {
    console.log(await call('admin/suggestions', { method: 'POST', body: JSON.stringify({ action: cmd, ids: args.map(Number) }) }));
  } else {
    console.log('usage: admin.mjs list [status] | dismiss|question|process <reason> <ids…> | restore|hide|unhide|ban|unban <ids…>');
    process.exitCode = 1;
  }
} catch (e) {
  console.error(e.message);
  process.exitCode = 1;
} finally {
  tunnel?.kill();
}
