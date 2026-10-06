// /llms.txt and /llms-full.txt (https://llmstxt.org): what the project is and where its technical pages are,
// for AI agents. The technical pages come from the docs collection, so a new page is listed by itself.
import { getCollection } from 'astro:content';

export const SITE = 'https://srw64.dreamquest.club';
const LANGS = [
  { lang: 'en', label: 'English' },
  { lang: 'zh', label: '简体中文' },
  { lang: 'ja', label: '日本語' },
] as const;

export async function docsFor(lang: string) {
  return (await getCollection('docs', (d) => d.id.startsWith(`${lang}/`))).sort((a, b) => a.data.order - b.data.order);
}

export const INTRO = `# MARCHWIND 64

> Marchwind 64 is an unofficial native recompilation of Super Robot Wars 64 (Nintendo 64, 1999) for Windows, macOS, Linux, Steam Deck and Android, with full English and Chinese translations, widescreen and an optional HD art pack. It needs the player's own Japanese ROM (Rev 0); the project distributes no game data.

The running game can be driven by AI agents over MCP (Model Context Protocol) once its debug interface is on: read its state, take screenshots, press keys, work the menus, read and write memory, and load mini stages (small custom scenarios written in JSON) to reproduce battles, screens and script commands. Source code: https://github.com/dyzz/srw64-recomp (GPL-3.0-or-later; the game itself is not covered).
`;

export async function llmsIndex() {
  const out = [INTRO];
  for (const { lang, label } of LANGS) {
    const docs = await docsFor(lang);
    out.push(`## Technical docs (${label})\n`);
    for (const d of docs) out.push(`- [${d.data.title}](${SITE}/${lang}/docs/${d.id.split('/')[1]}.md): ${d.data.summary}`);
    out.push('');
  }
  out.push(`## Optional

- [Install](${SITE}/en/install/): downloads and setup for each platform
- [FAQ](${SITE}/en/faq/)
- [Story](${SITE}/en/story/): the full script by stage, Japanese original beside the translation
- [Library](${SITE}/en/library/): units and characters with their game data
- [Full English technical docs in one file](${SITE}/llms-full.txt)
- [Source repository](https://github.com/dyzz/srw64-recomp)
`);
  return out.join('\n');
}

export async function llmsFull() {
  const out = [INTRO];
  for (const d of await docsFor('en')) {
    out.push(`\n---\n\n# ${d.data.title}\n\nSource: ${SITE}/en/docs/${d.id.split('/')[1]}/\n\n> ${d.data.summary}\n\n${(d.body ?? '').trim()}\n`);
  }
  return out.join('\n');
}
