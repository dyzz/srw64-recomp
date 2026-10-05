// /latest.json: what the game's update check reads (docs/design/website.md §5.3).
import { release } from '../data/release';

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
          en: `https://srw64.dreamquest.club/en/blog/v${release.version.replaceAll('.', '-')}/`,
          ja: `https://srw64.dreamquest.club/ja/blog/v${release.version.replaceAll('.', '-')}/`,
        },
        download: { zh: 'https://srw64.dreamquest.club/zh/install/', en: 'https://srw64.dreamquest.club/en/install/', ja: 'https://srw64.dreamquest.club/ja/install/' },
        github: release.github,
        quark: release.quark,
        files: release.files,
      },
      null,
      2,
    ),
    { headers: { 'Content-Type': 'application/json; charset=utf-8' } },
  );
