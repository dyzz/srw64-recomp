---
title: Language package MODs
summary: Add a new reading language to the game (Korean, French…), or replace an existing language's UI text, names and dialogue. Anything left untranslated shows a fallback language you choose.
section: mods
order: 6
updated: 2026-11-03
---

The game comes with four reading languages: Japanese, Simplified Chinese, Traditional Chinese and English. A language package adds another: install it, turn it on and **restart the game**, and it shows up in the language menu in the settings and on the language key (F7). UI text, names and dialogue the package does not translate show the fallback language you choose; where that has nothing either, the Japanese original shows.

A package can also change an existing language only, for example with a different English translation.

Installing and managing packages is covered in [MOD packages: installing and making](../mods/). Requires version 0.5.3 or later.

## What goes in the package

```text
yourname.korean/
  mod.json
  dialogue/ko/language.json      the language's declaration (required for a new language)
  dialogue/ko/terms.json         UI text and names
  dialogue/ko/story/…            story dialogue
  dialogue/ko/battle/…           battle dialogue
  dialogue/ko/fonts/…            fonts (when ours lack the script)
  art/ko/scene-images/…          pictures replaced for this language (optional)
  art/ko/plates/…                world map name plates (optional)
```

`ko` is the language code; the folder name must be the same as the one in `language.json`. Put in only what you use: dialogue files you leave untranslated and pictures you do not replace can stay out.

The repository has a complete example: the [Korean stage 1 trial package](https://github.com/dyzz/srw64-recomp/tree/main/config/recomp/mods/lang.ko), with the story and battle lines of the Real-robot male route's first stage and every UI text in Korean, the rest in English.

## Making one

The tools are in the [source repository](https://github.com/dyzz/srw64-recomp) and need Python 3.

**1. Export a starting point from one of our languages**

```bash
python3 tools/mods/language_template.py --locale ko --name 한국어 --fallback en --id yourname.korean -o korean-source
```

It writes all the text of the fallback (English here) into `korean-source` in the layout above: every UI text and name in `terms.json`, every dialogue file under `dialogue/ko/`, and `mod.json` and `language.json` filled in. `--id` is the package's unique name, written as "author.name"; do not change it after publishing.

**2. Translate**

Replace the fallback's text in `terms.json` and the dialogue files with your language, by the rules in the sections below. Entries you do not translate can stay or go: where the package has nothing, the game shows the fallback. Translating only part is still a working package; the example translates only the first stage.

**3. Fonts and names**

When our fonts lack the script, put the font files in `dialogue/ko/fonts/` and name them in `language.json`'s `fonts`. Fill in the authors and the licence in `mod.json` (see [MOD packages](../mods/)).

**4. Check and pack**

```bash
python3 tools/mods/validate.py korean-source
```

```bash
python3 tools/mods/pack.py korean-source -o build/mods
```

`validate.py` checks the files' structure, the language code, the fonts and every field by the game's rules; `pack.py` makes `yourname.korean-0.1.0.srw64mod` and checks it again. Whether control codes and dialogue pages match the original can only be checked with the ROM, in the game: look for `SRW64_MOD_LANGUAGE_PROBLEM` in the log, and `dialogue/dialogue-report.txt` in the user folder lists every dialogue record with a problem.

**5. Install and pick the language**

Install it like any MOD (drag it onto the game window on a computer; on Android put it in `Documents/Marchwind64/mods` or use Install from file on the MOD page), **restart the game**, then pick the language in the settings' language menu or with the language key (F7). The choice is remembered: the next start is in that language.

## language.json

```json
{
  "schema": "srw64.language.v1",
  "locale": "ko",
  "name": "한국어",
  "fallback": "en",
  "fonts": { "text": ["fonts/MyGothic.ttf"], "title": "fonts/MyBrush.ttf" }
}
```

| Field | Meaning |
| --- | --- |
| `locale` | The language code, written like `ko`, `fr`, `pt-BR` or `es-419`. Not `ja`, `zh-Hans`, `zh-Hant` or `en` |
| `name` | The name in the language menu, written in the language itself, at most 24 characters |
| `fallback` | Optional: what shows where the package has no text: `ja`, `zh-Hans`, `zh-Hant` or `en`. Default `en` |
| `fonts.text` | Optional: fonts for the text and the UI, at most 4, in front of the fallback's fonts |
| `fonts.title` | Optional: the font of the chapter title cards |

**Typesetting follows the fallback.** A language that falls back to English keeps spaces between words and breaks lines at spaces; one that falls back to Chinese or Japanese is set as those are. Pick `en` for languages that separate words with spaces, such as Korean or French.

**Fonts** go in `fonts/` only, `.ttf` or `.otf`, up to 32 MB each. Our fonts have the Latin letters, so French or Spanish need none of their own. Korean, Thai and other scripts our fonts lack need a font file, or their letters do not show. The font's licence must allow passing it on with the package.

## terms.json: UI text and names

```json
{
  "schema": "srw64.language-terms.v1",
  "ui": { "title_start": "시작" },
  "entries": { "base:t00_00000": "평지<END>" }
}
```

- `ui`: UI text, by the same keys as the game's own languages (the template has them all). Keep marks such as `{A}` (a button) and `{n}` (a number) as they are; a label that loses or adds one is not used.
- `entries`: names of characters, units and weapons, terrain, abilities, system messages, records 0–5643. Control codes such as `<BR>` (new line), `<STOP>` (new page) and `<END>` (end) must match the original. An entry is a string or `{"text": "…", "ja": "…"}`; `ja` is the Japanese original for your reference, which the game does not read (the template fills it in when the ROM's text export is there).
- Weapon menu names (the `entries` that start with 格 or 射) keep that first character: the game shows it as the melee or shooting icon.
- Entries you leave out show the fallback's.

## Dialogue

Story and battle dialogue and the text pages (opening, ending) are plain text in the same format as the files players edit dialogue with; see [the dialogue text format](https://github.com/dyzz/srw64-recomp/blob/main/docs/guide/dialogue-text.md) in the repository. The files can go anywhere under `dialogue/ko/`, one record per block:

```text
@17410 로렌스
“아가씨, 차가 준비되었습니다. 오늘은 러시안 티입니다.
---
어디로 가져다 드릴까요?”
```

Pages (`---`), the number of options and name placeholders such as `{HeroName}` must match the original; a record that does not is left out and shows the fallback's. A player's own dialogue folder can have a `ko/` too, over the package's.

## Pictures per language

While reading in the language, these pictures come from the package:

- `art/ko/scene-images/<file>`: scene pictures, such as the title screen's logo `art/ko/scene-images/title-logo.png`. The file names are the HD pack's; the HD pack must be installed.
- `art/ko/plates/<number>.png`: the name plates of ships and places on the world map. The number is our plate's file name without `.plate.ja.png` (`5590`, `5599.6`), and the size must be the same as ours. Plates you leave out come from the HD pack in that language when it has them (the HD pack carries Korean plates from its next version), else the fallback's.

## The language in other packages

With a language package installed, other MODs can have text in its language too: the name and description in `mod.json`, a campaign's name and chapter titles, name MODs, song titles. Whatever they leave out shows by the fallback.

## Notes

- Turning a language package on or off, or updating it, takes effect after restarting the game. If the language in use is turned off or removed, the next start goes back to the system's language.
- Korean (`ko`) breaks lines only at spaces and punctuation, never inside a word.
- Languages that need a dictionary to break lines (Thai, Lao, Khmer, Burmese) only break at spaces; right-to-left scripts (Arabic, Hebrew) are not supported yet.
- A language package with problems does not show up in the language menu; the reasons are in the game's log (`SRW64_MOD_LANGUAGE_PROBLEM`). Check the package with `tools/mods/validate.py` before publishing it.
