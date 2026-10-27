---
title: Name and portrait mods
summary: Rename the original protagonists, partners and unit, rename any pilot, unit or weapon, and replace pilot portraits. Only what is shown changes; nothing is written to saves.
section: mods
order: 4
updated: 2026-10-27
---

These mods change only the names and portraits the game **shows**: dialogue, speakers, status screens, the battle confirmation, the library and map labels follow. Saves keep the original names, so disabling the package brings everything back, and a save still loads anywhere, including in the original game.

Installing and managing packages: [MOD packages](../mods/). Needs 0.5.2 or later.

## What goes in the package

```text
yourname.heroes/
  mod.json
  dialogue/names.json          names
  art/portraits/133.png        portraits, named by pilot number
```

Either one alone is fine.

## Names (dialogue/names.json)

```json
{
  "schema": "srw64.mod-names.v1",
  "heroes": {
    "super-male": {
      "hero":    { "en": { "name": "Takuma", "surname": "Amano", "nickname": "Takuma" } },
      "partner": { "en": { "name": "Minato", "surname": "Hojo" } }
    }
  },
  "unit": { "en": "Daybreak" },
  "pilots":  { "133": { "en": { "name": "Shingo", "full": "Shingo Hojo" } } },
  "units":   { "184": { "en": "GoShogun Kai" } },
  "weapons": { "731": { "en": "Super Bazooka" } }
}
```

Every entry is written per language: `zh-Hans` (Simplified Chinese), `zh-Hant` (Traditional Chinese; the Simplified one when missing), `en`, `ja`. A language you leave out shows the usual name. When several packages rename the same thing, the one higher in the MOD list wins.

### Original protagonists, partners and unit (heroes, unit)

- `heroes` goes by the four routes: `super-male`, `super-female`, `real-male`, `real-female`, each with `hero` and `partner`.
- Each language takes `name` (required), `surname` and `nickname` (the name when missing). The full name is joined per language: a space in English, 「·」 in Chinese, 「・」 in Japanese.
- The original fills the protagonists' names into the dialogue, so the dialogue follows.
- With a unit name, the naming scene takes the "I don't like it" branch: the protagonist turns March Wind down and proposes the package's name. A scene already played does not replay.

### Other pilots, units, weapons (pilots, units, weapons)

- By number, from the [ID table](/en/mods/ids/). A pilot takes `name` (short name, required) and `full` (the short name when missing); units and weapons just take the name.
- The weapon menu's melee/ranged mark and P, B, MAP markers are added for you.
- The name itself changes, so speakers, status screens and the map follow; **the dialogue text that mentions the name does not**.

### Length

Names are not written to saves, so the original's cells do not bind them, but some places are tight: map labels and the battle name bars have fixed widths.

- A protagonist's name, surname or nickname past the original's cells (7 for name and surname, 5 for the nickname) or wider than our widest translated one, and a unit name past 10 cells, get a `SRW64_MOD_NAMES_WARNING` line in the log; the name is still used. The nickname is shown most; keep it near 5 cells.
- Hard limits: 16 characters for a protagonist's name, surname and nickname, 24 for the unit and every other name. Past them, or with an unknown field or language, **the whole file is left out** and the log has `SRW64_MOD_NAMES_PROBLEM`.
- No line breaks, ideographic spaces or `< > { }` in a name, and no spaces at either end.

In Japanese with the original images, the original screens draw with the game's own glyphs, which cannot draw a package's names; those names are drawn in this port's own font there, the text around them in the original glyphs.

## Portraits (art/portraits)

- The file is named by **pilot number** plus `.png`, e.g. `art/portraits/133.png`.
- Square, 96 to 2048 pixels, a multiple of 96 (96, 192, 384, 768…). It may have transparency; the picture behind shows through.
- Dialogue, battles, status screens, the battle confirmation and the library all use it, with the images set to original or HD.
- Pilots that use the same original portrait (other numbers of one character, say) all change together.
- A file not named by a pilot number with a portrait, or of the wrong size, is skipped with `SRW64_MOD_PORTRAITS_PROBLEM` in the log.

## Example

The example package "Renaming Example" is in [`config/recomp/mods/hero-names`](https://github.com/dyzz/srw64-recomp/tree/main/config/recomp/mods/hero-names): it renames the Super Robot route's male protagonist, his partner and the unit.

Numbers: [Number mods](../mod-data/).
