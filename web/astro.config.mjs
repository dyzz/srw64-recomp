// @ts-check
import { defineConfig } from 'astro/config';

export default defineConfig({
  site: 'https://srw64.dreamquest.club',
  // 'always' for the built site; the dev server must let /api/* through to its proxy.
  trailingSlash: process.argv.includes('dev') ? 'ignore' : 'always',
  build: { format: 'directory' },
  // Nginx serves dist/ as-is; nothing is rendered on demand.
  output: 'static',
  devToolbar: { enabled: false },
  // In development the site API (web/api/server.mjs) runs beside the dev server.
  vite: { server: { proxy: { '/api': 'http://127.0.0.1:3064' } } },
});
