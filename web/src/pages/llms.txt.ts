// /llms.txt: the site in brief for AI agents and AI search (llmstxt.org): what Marchwind 64 is,
// the current release, and where to download, install and read more, in English with each
// page's other languages named. Written from the release data and the blog and docs
// collections, so it follows each deploy. The mini stage reference for agents that drive the
// game is its own file, /docs/mini-stage/llms.txt.
import { getCollection } from 'astro:content';
import { allFiles, hd, published, release, PLATFORM_LABEL, type Platform } from '../data/release';
import { newestFirst } from '../lib/blog';

const SITE = 'https://srw64.dreamquest.club';

export async function GET() {
  const posts = (await getCollection('blog', (p) => p.id.startsWith('en/') && !p.data.draft))
    .sort((a, b) => newestFirst({ data: { ...a.data, pinned: false } }, { data: { ...b.data, pinned: false } }))
    .slice(0, 5);
  const docs = (await getCollection('docs', (d) => d.id.startsWith('en/')))
    .sort((a, b) => a.data.order - b.data.order);
  const slug = (id: string) => id.split('/')[1];
  const files = allFiles.map((f) => `- ${PLATFORM_LABEL[f.platform as Platform] ?? f.platform}: ${f.url}`);
  const text = `# MARCHWIND 64 (Marchwind64)

> Marchwind 64 is an unofficial, free, open-source native recompilation of Super Robot Wars 64 (スーパーロボット大戦64, Nintendo 64, 1999). It runs the game natively on Windows, macOS, Linux, Steam Deck, Android and Nintendo Switch, with full English, Simplified Chinese and Traditional Chinese translations besides the original Japanese, widescreen, and an optional HD art pack. Players must supply their own Japanese ROM; the site does not provide ROMs. It is a fan project, not affiliated with Bandai Namco or BANPRESTO.

${published ? `- Current version: ${release.version} (${release.date}), release notes ${SITE}/en/blog/v${release.version.replaceAll('.', '-')}/` : '- A release is being prepared.'}
- HD art pack: version ${hd.version}, optional, a separate download (the Switch has its own pack)
- Source code (GPL-3.0-or-later): https://github.com/dyzz/srw64-recomp
- Every page exists in four languages: ${SITE}/en/… (English), /zh/… (Simplified Chinese), /tw/… (Traditional Chinese), /ja/… (Japanese)

## Download and install

- [Installation](${SITE}/en/install/): downloads for each platform, where the ROM goes, installing the HD pack
- [FAQ](${SITE}/en/faq/): the ROM, saves, controls, languages, differences from the original, reporting bugs
- [Release notes](${SITE}/en/releases/): every version's changes
- [Android compatibility](${SITE}/en/android/): tested phones and GPUs
- Mainland China mirrors: Quark ${release.quark} and Baidu ${release.baidu}
${files.join('\n')}
- Machine-readable newest release: ${SITE}/latest.json

## The game

- [Walkthrough](${SITE}/en/guide/): routes, branches, hidden units and items
- [Story](${SITE}/en/story/): the full script, translated, by scene
- [Library](${SITE}/en/library/): units and characters with their data
- [Mods](${SITE}/en/mods/): mod packages to download and web tools to make them

## Docs

${docs.map((d) => `- [${d.data.title}](${SITE}/en/docs/${slug(d.id)}/): ${d.data.summary} Markdown: ${SITE}/en/docs/${slug(d.id)}.md`).join('\n')}
- Mini stages for AI agents: ${SITE}/docs/mini-stage/llms.txt

## News

${posts.map((p) => `- ${p.data.date.toISOString().slice(0, 10)} [${p.data.title}](${SITE}/en/blog/${slug(p.id)}/)`).join('\n')}
- All posts: ${SITE}/en/blog/

## Optional

- Sitemap of every page: ${SITE}/sitemap.xml
`;
  return new Response(text, { headers: { 'Content-Type': 'text/plain; charset=utf-8' } });
}
