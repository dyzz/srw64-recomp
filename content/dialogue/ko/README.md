# Korean translation

Korean is built into the game as a fifth reading language, standing on English: whatever
Korean leaves out shows the English text. These files now contain the community Korean
translation, aligned with the filenames in `content/dialogue/en/`. The UI and system
text live in `content/locales/ko.json`. English-only credits remain on English fallback.

| What | Where | Format |
| --- | --- | --- |
| Dialogue | this folder, with the same file names and folders as `content/dialogue/en/` | [docs/guide/dialogue-text.md](../../../docs/guide/dialogue-text.md): keep each `@` line, the `>` lines of the Japanese original and the same number of `---` page breaks |
| Interface labels | `"ui"` in `content/locales/ko.json` | `{"label key": "text"}`, the keys of `content/locales/en.json`, the same `{marks}` |
| Names, terms and system text (records `base:t00_00000`–`base:t00_05643`) | `"entries"` in `content/locales/ko.json` | `{"key": "base:t00_00000", "source_sha256": "…", "target": "평원<END>"}`, with `source_sha256` copied from the row of the same key in `content/locales/en.json`, and the original's control codes (`<BR>`, `<STOP>`, `<END>`) |
| Title logo | `content/art/ko/scene-images/title-logo.png` | a PNG of the whole logo, 4:1 like ours (1792×448), transparent around the letters |

Leave out what is not translated yet rather than copying the English: it shows English
anyway, and a copy would hide later English corrections. Keep the other fields of `ko.json`
(`schema`, `locale`, `fallback`, `font`, `display_name`) as they are.

In the pull request, please say under which licence the translation and the logo may be
shipped with the game (for example CC BY-NC 4.0), how you would like to be credited, and
which fonts you would like, with where each one comes from.

## Korean translation and title-logo attribution

The original Korean localization contributions (translated dialogue, Korean interface strings,
Korean system/entry terms, and the original Korean title-logo artwork contributed for the
Korean release) are credited to **테오 (jung5441)**: https://github.com/jung5441.

To the extent owned by this contributor, these original Korean translation and title-logo
contributions are licensed under **Creative Commons Attribution 4.0 International (CC BY 4.0)**:
https://creativecommons.org/licenses/by/4.0/

The contributor permits the Marchwind64 project to use, modify, maintain and redistribute
these contributions in official versions (including commercial distribution). Please give
appropriate credit, link to this license and indicate whether changes were made.

This notice does not license underlying original game text/artwork, characters, trademarks,
fonts, other third-party assets or the game's code. Those rights and their licenses remain
separate.
