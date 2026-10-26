---
title: "Mod packages: installing and making them"
summary: What a mod package is, how to install and manage one, and how to write mod.json when you make your own. Music replacement and terrain ranks are what take effect for now.
section: mods
order: 1
updated: 2026-10-26
---

A mod package is a folder, or that folder zipped into one `.srw64mod` file. It holds data only, never code. When a package is enabled, the game uses its contents in place of the matching parts of the original; wherever no package says otherwise, everything is the original.

**For now music and terrain ranks take effect**: replacing songs and choosing which song a pilot or unit fights to, as described in [Music mods](../mod-music/), and changing the terrain ranks of units and pilots, as described in [Number mods](../mod-data/). Additional campaigns, art, dialogue, voices and other numbers will follow; the package format already has a place for them.

Mods need a version newer than 0.5.0.

## Installing

1. Open the mod manager: click “MOD” in the bottom-right corner of the title screen, or open the settings and click “MOD” on the General page.
2. **Drag the `.srw64mod` file** (a `.zip` or a package folder works too) **into the game window**. The game installs it in the background, says so at the top of the screen and opens the Installed page.
3. Or click “Open folder” and put the package file straight into the MOD folder; it is installed the next time you open the mod manager.

Installing a new version of a package replaces the old one. The old version, and any package you remove, goes into `.trash` inside the MOD folder rather than being deleted.

Mods cannot be installed on Android yet.

## Managing

The Installed page lists each package’s name, version, authors, contents and status. You can turn a package on or off, or remove it (removal asks you to confirm).

- **A package higher in the list overrides the ones below it**: if two packages replace the same song, the upper one’s is used.
- A package that depends on another is placed above it automatically. If a dependency is missing, too old or turned off, the page says so and the package stays off for now.
- If a package needs a newer version of the game, the page says that too.

## Making a package

The easiest way to make a music package is the website’s [BGM MOD maker](/en/mods/bgm/), which writes the files below for you. Read on if you want to make one by hand, or to know what is inside.

### What a package holds

```text
mod.json        the package's details (required)
audio/          music and voices
campaign/       additional campaigns
art/            art
dialogue/       dialogue and languages
data/           numbers
```

Include only the folders you use. To publish, zip the whole folder and change the extension to `.srw64mod`. `mod.json` can sit at the top of the archive or inside its single top-level folder.

### mod.json

```json
{
  "schema": "srw64.mod.v1",
  "id": "yourname.bgm",
  "version": "1.0.0",
  "name": { "zh-Hans": "我的 BGM", "en": "My BGM", "ja": "マイ BGM" },
  "description": { "en": "New battle themes." },
  "authors": ["yourname"],
  "license": "CC-BY-4.0"
}
```

| Field | Meaning |
| --- | --- |
| `schema` | Always `srw64.mod.v1` |
| `id` | The package’s unique name: letters, digits and `.` `_` `-`, up to 64 characters. “author.name” is a good pattern. **Don’t change it once released** |
| `version` | The version, written like `1.0.0` |
| `name` | The name shown: one string, or one per language (`zh-Hans`, `en`, `ja`) |
| `description` | Optional; a description, written like `name` |
| `authors` | Optional; a list of authors |
| `license` | The license, which must allow passing the package on: `CC-BY-4.0`, `CC-BY-SA-4.0`, `CC-BY-NC-4.0`, `CC-BY-NC-SA-4.0`, `CC0-1.0`, `MIT` or `GPL-3.0-or-later` |
| `game` | Optional; the game version needed, such as `">=0.5.1"` |
| `requires` | Optional; packages this one needs, such as `{ "someone.base": ">=1.0" }` |
| `optional` | Optional; packages used together with this one when present, written like `requires` |
| `conflicts` | Optional; packages that cannot be on at the same time, such as `{ "someone.old": "*" }` |

A version requirement is `*` (any version) or a few space-separated conditions, such as `">=1.0 <2"`. Fields the game does not know are treated as errors, so don’t add your own.

### Checking

`tools/mods/validate.py` in the [source repository](https://github.com/dyzz/srw64-recomp) checks a package by the same rules as the game and lists every problem:

```bash
python3 tools/mods/validate.py yourname.bgm-1.0.0.srw64mod
```

## Copyright

Everything in a package must be yours to pass on. A package made from material such as the original anime songs is for your own use only; don’t publish it, and we won’t list such packages.
