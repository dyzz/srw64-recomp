// /latest.json: what the game's update check reads (docs/design/website.md §5.3,
// docs/native/update-check.md). `hd` is the newest HD pack, which has versions of its own.
// `text`: per release, the text that changed since it (docs/design/text-updates.md), written
// by tools/release/build_text_pack.py.
import { allFiles, hd, published, release } from '../data/release';
import textPacks from '../data/text-packs.json';

const install = (lang: string) => `https://srw64.dreamquest.club/${lang}/install/`;

export const GET = () =>
  new Response(
    JSON.stringify(
      {
        schema: 'srw64.latest.v1',
        version: release.version,
        tag: release.tag,
        commit: release.commit,
        date: release.date,
        notes: {
          zh: `https://srw64.dreamquest.club/zh/blog/v${release.version.replaceAll('.', '-')}/`,
          tw: `https://srw64.dreamquest.club/tw/blog/v${release.version.replaceAll('.', '-')}/`,
          en: `https://srw64.dreamquest.club/en/blog/v${release.version.replaceAll('.', '-')}/`,
          ja: `https://srw64.dreamquest.club/ja/blog/v${release.version.replaceAll('.', '-')}/`,
        },
        download: { zh: install('zh'), tw: install('tw'), en: install('en'), ja: install('ja') },
        github: release.github,
        quark: release.quark,
        baidu: release.baidu,
        files: allFiles,
        // A pack from before HD versions has none, and is offered to nobody.
        // Taken down (published false): no files and no HD pack to offer.
        ...(published && hd.version && {
          hd: {
            version: hd.version,
            download: { zh: `${install('zh')}#hd`, tw: `${install('tw')}#hd`, en: `${install('en')}#hd`, ja: `${install('ja')}#hd` },
            github: hd.github,
            name: hd.name,
            bytes: hd.bytes,
            sha256: hd.sha256,
            url: hd.url,
          },
        }),
        ...(published && Object.keys(textPacks).length > 0 && { text: textPacks }),
      },
      null,
      2,
    ),
    { headers: { 'Content-Type': 'application/json; charset=utf-8' } },
  );
