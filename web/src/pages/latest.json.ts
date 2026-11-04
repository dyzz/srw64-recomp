// /latest.json: what the game's update check reads (docs/design/website.md §5.3,
// docs/native/update-check.md). `hd` is the newest HD pack, which has versions of its own.
// `text`: the newest text update list, the whole text by short hash, for every release's game
// (docs/design/text-updates.md); written by tools/release/build_text_update.py.
import { allFiles, hd, published, release } from '../data/release';
import textUpdate from '../data/text-update.json';

const LANGS = ['zh', 'tw', 'en', 'ja', 'ko'] as const;
const install = (lang: string) => `https://srw64.dreamquest.club/${lang}/install/`;
const byLang = (url: (lang: string) => string) => Object.fromEntries(LANGS.map((l) => [l, url(l)]));

export const GET = () =>
  new Response(
    JSON.stringify(
      {
        schema: 'srw64.latest.v1',
        version: release.version,
        tag: release.tag,
        commit: release.commit,
        date: release.date,
        notes: byLang((l) => `https://srw64.dreamquest.club/${l}/blog/v${release.version.replaceAll('.', '-')}/`),
        download: byLang(install),
        github: release.github,
        quark: release.quark,
        baidu: release.baidu,
        files: allFiles,
        // A pack from before HD versions has none, and is offered to nobody.
        // Taken down (published false): no files and no HD pack to offer.
        ...(published && hd.version && {
          hd: {
            version: hd.version,
            download: byLang((l) => `${install(l)}#hd`),
            github: hd.github,
            name: hd.name,
            bytes: hd.bytes,
            sha256: hd.sha256,
            url: hd.url,
          },
        }),
        ...(published && 'revision' in textUpdate && { text: textUpdate }),
      },
      null,
      2,
    ),
    { headers: { 'Content-Type': 'application/json; charset=utf-8' } },
  );
