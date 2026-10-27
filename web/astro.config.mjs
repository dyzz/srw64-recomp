// @ts-check
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { defineConfig } from 'astro/config';

// The Traditional Chinese pages are built from the Simplified text; the build's last step
// converts their words with the game's converter (scripts/zh_hant_pages.py).
const zhHant = {
  name: 'srw64-zh-hant',
  hooks: {
    'astro:build:done': ({ dir }) => {
      const python = process.env.SRW64_PYTHON ?? fileURLToPath(new URL('../.venv/bin/python', import.meta.url));
      execFileSync(python, [fileURLToPath(new URL('./scripts/zh_hant_pages.py', import.meta.url)), fileURLToPath(dir)], { stdio: 'inherit' });
    },
  },
};

export default defineConfig({
  site: 'https://srw64.dreamquest.club',
  // 'always' for the built site; the dev server must let /api/* through to its proxy.
  trailingSlash: process.argv.includes('dev') ? 'ignore' : 'always',
  build: { format: 'directory' },
  // Nginx serves dist/ as-is; nothing is rendered on demand.
  output: 'static',
  devToolbar: { enabled: false },
  integrations: [zhHant],
  // A Content-Security-Policy <meta> on every page: scripts only from this site or inline
  // ones Astro has hashed, so an injected script or a javascript: link cannot run; styles
  // from this site, hashed <style> blocks and style="" attributes; nothing from elsewhere.
  // Framing and the rest are headers (deploy/nginx-srw64-headers.conf).
  security: {
    csp: {
      scriptDirective: { resources: ["'self'"] },
      styleDirective: { resources: ["'self'", { resource: "'unsafe-inline'", kind: 'attribute' }] },
      directives: ["default-src 'self'", "img-src 'self' data:", "font-src 'self'", "connect-src 'self'",
        "media-src 'self'", "object-src 'none'", "base-uri 'self'", "form-action 'self'"],
    },
  },
  // In development the site API (web/api/server.mjs) runs beside the dev server.
  vite: { server: { proxy: { '/api': 'http://127.0.0.1:3064' } } },
});
