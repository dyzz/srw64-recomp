// @ts-check
import { execFileSync } from 'node:child_process';
import { readFileSync, writeFileSync } from 'node:fs';
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

// sitemap.xml from the pages the build wrote: every HTML page (not the 404, not the JSON and
// text endpoints, not a page that only redirects), each with its other-language versions as hreflang alternates, the
// language picker at the root as x-default. robots.txt (public/) points search engines at it.
const SITE = 'https://srw64.dreamquest.club';
const LANG_TAGS = { zh: 'zh-Hans', tw: 'zh-Hant', en: 'en', ja: 'ja', ko: 'ko' };  // as LANGS in src/i18n.ts
const sitemap = {
  name: 'srw64-sitemap',
  hooks: {
    'astro:build:done': ({ pages, dir }) => {
      const redirects = (p) => readFileSync(new URL(`.${p}index.html`, dir), 'utf8').slice(0, 300).includes('http-equiv="refresh"');
      const paths = new Set(pages.map((p) => `/${p.pathname}`).filter((p) => p.endsWith('/') && p !== '/404/' && !redirects(p)));
      const esc = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;');
      const urls = [...paths].sort().map((path) => {
        const m = path.match(/^\/(zh|tw|en|ja|ko)(\/.*)$/);
        const alts = m
          ? Object.entries(LANG_TAGS).filter(([l]) => paths.has(`/${l}${m[2]}`))
              .map(([l, tag]) => `<xhtml:link rel="alternate" hreflang="${tag}" href="${esc(SITE + `/${l}${m[2]}`)}"/>`)
              .concat(`<xhtml:link rel="alternate" hreflang="x-default" href="${SITE}/"/>`)
          : [];
        return `<url><loc>${esc(SITE + path)}</loc>${alts.join('')}</url>`;
      });
      writeFileSync(new URL('sitemap.xml', dir),
        `<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9" xmlns:xhtml="http://www.w3.org/1999/xhtml">\n${urls.join('\n')}\n</urlset>\n`);
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
  integrations: [zhHant, sitemap],
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
