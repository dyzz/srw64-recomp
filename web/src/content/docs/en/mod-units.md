---
title: New unit, character and weapon mods
summary: "Add units, weapons and characters the original does not have: units that transform or combine on the map, combination attacks, and poses, battle pictures, portraits, battle lines, voices and battle songs of their own. Everything new gets numbers of its own and takes no original's place."
section: mods
order: 3.5
updated: 2026-11-26
---

These mods add units, weapons and characters **the original does not have**. They get numbers of their own, next to the whole original cast, and replace no one. You start from a copy of an original unit or character, then change its name, numbers, weapons, pictures and lines.

A package can add:

- **units that transform**, each form with its own weapons and map icon;
- **units that combine on the map**: they combine by the recipe the package writes, can separate again and can sortie combined. Upgrading the combined unit does not wipe out the parts' own upgrades;
- **new weapons**, **combination attacks** among them: partners, EN and morale are checked as in the original, and the weapon page, the library and the Battle Viewer list the partners;
- **new characters** with their own portraits, **battle lines and voices** (for the usual battle moments, or for a given weapon or opponent) and their own **battle songs**;
- **new units and characters on the enemy side**, brought in by the story;
- **poses, battle pictures and cut-ins of their own**: the stand pose and the scenes in an attack, one picture for the HD images and one for the original images.

New units and characters show up in the library and the Battle Viewer, with their own pictures and series.

Needs 0.5.4 or later. Installing and managing packages: [MOD packages](../mods/).

## Using the unit maker

The website's [unit maker](/en/mods/units/) writes such a package in the browser; **no picture or recording is uploaded anywhere**:

1. Copy an original character, unit or weapon, and change its names in four languages, its numbers, weapons, forms, crew and combination attack.
2. Frame a portrait from a picture of your own; give the unit a pose, battle pictures with cut-ins and a map icon, and the browser reduces them to the colours the game uses.
3. Write battle lines in four languages, each with a voice you can trim on its waveform; add battle songs if you like.
4. Put weapon animations together from the game's own effects: move each actor, borrow actors from any original weapon, add cut-ins.
5. Write when the unit joins and the package's own reinforcements; export a `.srw64mod`, or a Thunderstore package.

The maker has ready templates to start from. A switch on the About page (off by default) lets the maker send the package straight to a running game to try in the Battle Viewer; the game asks you before it takes it.

For now the maker offers only common generic weapons to copy, borrow animations from and give to a unit (beam rifle, beam saber, Vulcan, machine gun, missiles, bazooka, beam cannon, cannon, punches and kicks, ramming, axe, saber), plus the weapons of the unit it was copied from; other weapons will follow. For a more complex package, write it by hand in the format below, or hand it to an AI assistant.

## What goes in the package

```text
yourname.rival/
  mod.json
  data/new_units.json            new units, weapons, characters and battle pictures (required)
  data/units.json, pilots.json, weapons.json
                                 optional: change their values after copying (as in number mods)
  dialogue/names.json            names (as in name mods)
  dialogue/battle_quotes.json    optional: new characters' own battle lines
  art/…                          poses, battle pictures, portraits, map icons
  audio/…                        optional: voices, battle songs
  story/…                        optional: when they join, enemy arrivals (story mods)
```

## new_units.json

```json
{
  "schema": "srw64.new-units.v2",
  "scenes":  { "cannon-pose": [2560, 1716, 2118] },
  "weapons": { "cannon": { "copy": 875, "inherits_from": 875,
                           "animation": { "camera": 1, "hit": 59, "action": 84, "defender": 125, "sounds": [219],
                                          "actors": [ { "scene": "@cannon-pose", "h4": 1, "h5": 255, "behavior": 359 } ] } } },
  "units":   { "final": { "copy": 224, "pose": [2324, 1724, 2016], "icon": 978,
                          "weapons": [870, 871, "@cannon"], "inherits_from": 224,
                          "crew": [171, 168, 170, 169, 166] } }
}
```

New units, weapons, characters and battle scenes are **written by name**, and the game gives them numbers: units 400–998, weapons 1400–2399, characters 400–511, scenes after the original's. None of the original numbers is taken.

- Names use lowercase letters, digits and `-` `_` `.` only. The package's other files name its new things as `@name`: `"@final": {"hp": 8000}` in `data/units.json`, `"@final"` under `units` in `dialogue/names.json`, the map icon `art/icons/@final.png`, and `"with": "@final"` or `"unlock": "@cannon"` in a story mod. Another package's things are `package-id@name`, with that package in `requires` in `mod.json`.
- The numbers are worked out from "package id@name", so the same packages give the same numbers on any machine, installed in any order. Only when two names come out at the same number does one move up by one. Beside the save, the game records which package and name each number belongs to: if a package installed later moves a number, loading changes it to the current one; with the package turned off, loading such a save goes back to the title and says which package is needed.

### Units (units)

| Field | Meaning |
| --- | --- |
| `copy` | The unit copied (an original unit number with a battle record of its own, or another new unit). Numbers, weapons and pictures all start from it. Copying drops Transform, Separate and Combine |
| `pose` | The stand pose: the original's three resource numbers `[scene, atlas, palette]`, or the package's `"@picture"` (see [Your own battle pictures](#your-own-battle-pictures)) |
| `icon` | The map icon: an original icon number; with `art/icons/@name.png` in the package, that picture is used |
| `weapons` | The weapon list: original weapon numbers and `@new weapon` |
| `inherits_from` | When the story changes a unit into this one (a story mod's `replace`), the upgrade steps move over from this old unit |
| `crew` | 1–5 character numbers, the first being the main pilot. When the unit is registered to the main pilot, the others become its crew |
| `combine` | Combined from parts, below |
| `forms` | Transforming, below |
| `work` | The series written in the library details, an original series number 0–24; without it, the copied unit's. The library's model line goes in `models` in `dialogue/names.json` (up to 48 characters) |

Units and characters added by packages sit in a last "MOD" group in the library. There is no limit on the number of new units.

**Combining**: `"combine": {"parts": [2–5 part units], "over": [original combined unit]}`. The order of the parts is the order of the crew (each part's main pilot). On the map, when the original combined unit's (`over`) Combine is possible and the other parts (Black Wing, for example) are next to it and have not acted, combining gives this new unit; Separate gives back every part. This unit is built by a story mod's `build` ([Story mods](../mod-story/)) and never sorties by itself at the start of a stage.

**Transforming**: written on the base form, `"forms": ["@wr"]` lists 1–2 other forms (new units in the same file), each with its own `weapons`, `pose` and `icon` as usual. The forms' weapons make one shared list, and each weapon can only be used in the forms that list it; the whole family shares upgrade steps, weapon steps and crew, as in the original. The map menu offers Transform (with three forms, a list opens), and the base form is what sorties at the start of a stage. A unit can be in only one transforming family, and original units cannot join one.

### Weapons (weapons)

- `copy` copies an original weapon; `inherits_from` says which weapon of the old unit this new weapon takes its upgrade steps from (weapons the old unit already had move over as they are).
- `animation` is the battle animation: camera, hit effect, action, the defender's reaction, sounds and actors (an actor's `scene` can be the package's `@scene`). This part is easiest to put together in the unit maker.
- When `data/weapons.json` changes a copied weapon's melee/ranged, after-move or beam, the 格／射, P and B around the menu name follow.

**Combination attacks**: `"combination": {"units": ["@z", 228], "parts": [196, 899], "multiplier": 1.5}`. `units` are the 2–5 units taking part (original numbers or `@name`), and `parts` gives each one's component weapon in the same order; the power is the average of their current power times `multiplier`. Optionally `pilots: [[pilot, pilot], …]`: the main pilots must make up one of these sets. Any unit taking part that has the weapon in its list can lead. Everyone taking part must be on the map, connected (adjacent in eight directions), with enough EN and morale of their own. Firing it takes the weapon's EN from each unit and no ammo, so when copying a weapon that uses ammo, give it `en` and `"ammo": -1` in `data/weapons.json`. With a partner missing, the map's weapon page shows it in grey and says who is missing. As in the original, a combination attack cannot be upgraded. Remember to name it under `weapons` in `dialogue/names.json`, or it shows the copied weapon's name.

### Characters (pilots)

```json
"pilots": { "kai": { "copy": 166, "voice": 166,
                     "portrait": { "image": "art/kai.png", "hd": "art/kai-hd.png" } } }
```

| Field | Meaning |
| --- | --- |
| `copy` | The original character copied (it must have an ability record): stats, skills, spirits, portrait and battle song come from it |
| `voice` | Whose lines it speaks in battle; without it, `copy`'s. When the package gives it battle lines of its own, the moments not written still use this character's |
| `portrait` | The package's portrait: `image` is a 96×96 pixel picture with at most 63 colours, `hd` a square HD picture whose side is a multiple of 96 (optional). Or two original resource numbers `[picture, palette]`. Without it, the copied character's portrait |
| `love` | Optional, a love/friendship bonus: `[{"with": 166, "mutual": true}]`; within 2 squares, attacks +30% and damage taken −30%. `mutual` gives it to the other one too |
| `combo_as` | Optional: the original character it counts as in a combination attack's pilot condition |

All packages together can add up to 112 new characters. Change stats, skills and spirits under `"@kai"` in `data/pilots.json`, the name under `pilots."@kai"` in `dialogue/names.json`, and have it join with a story mod's `{"join": {"pilot": "@kai", "unit": 9}}`.

The repository has a tool that scales `image` down from a large picture:

```bash
python3 tools/mods/sprite_pixels.py big.png kai.png --size 96 96 --colors 63
```

## Battle lines for new characters

```json
{
  "schema": "srw64.battle-quotes.v1",
  "pilots": {
    "@kai": {
      "attack":  [{"ja": "「カイ、行くぞ!」", "zh-Hans": "“凯，上了！”", "en": "\"Kai, here I come!\"", "voice": "kai-attack.ogg"},
                  {"ja": "「これでも食らえ!」", "en": "\"Take this!\""}],
      "evade":   [{"ja": "「見切った!」", "en": "\"I saw that coming!\""}],
      "special": [
        {"weapon": 49, "lines": [{"ja": "「全力の一撃だ!」", "en": "\"Everything I've got!\""}]},
        {"opponent": 298, "situation": "evade", "lines": [{"ja": "「そんな攻撃が当たるか!」", "en": "\"An attack like that won't hit me!\""}]}
      ]
    }
  }
}
```

This goes in `dialogue/battle_quotes.json`. Only a package's new characters can have lines here (`@name`, or another package's `package-id@name`); the original characters' lines cannot be changed.

- Nine moments, and as in the original each time one line of the set is said at random: `attack` attacking, `shot_down` shot down, `heavy` heavy damage (under 30% HP left), `medium` medium damage (30–90%), `light` light damage (90% or more), `evade` evading, `no_damage` no effect (barriers, armor), `no_ammo` no counterattack for want of ammo or EN, `out_of_range` no counterattack, out of range. **Moments not written use the lines of the `voice` character**, and the box shows that character's name and portrait too.
- Each line is an object: the line in each language (`ja` is required, up to 42 characters, exactly one page of three rows; other languages up to 200 characters, laid out by the reader to the box's width), and optionally `voice`, the name of an `.ogg` file in `audio/voice/battle/` played as the line shows (Ogg Vorbis only; see [Music mods](../mod-music/#battle-voices)).
- `special` holds lines with conditions; when the condition holds, it is always said:
  - `"weapon": weapon`: when attacking with this weapon (an original weapon number or `@new weapon`). Weapons of the same name count as one, and a new weapon counts as the one it copies. With `"sometimes": true` it is often pushed out by the ordinary attack lines, like some of the original's weapon lines.
  - `"opponent": original character`: when the opponent is this character, with `situation` one of the nine moments (`attack` by default).
  - Each character has at most 119 conditional lines in all; when several hold at once, one is picked at random.
- The game numbers the lines (at most 2048 for all packages together), and saves do not record them; with the package turned off, the borrowed lines are back as they were.

A new character's battle song goes in the package's `audio/music.json`: `"battle": {"pilot": {"@kai": "@kai-theme"}}`, with the song in `audio/music/kai-theme.ogg`; see [Music mods](../mod-music/).

## Your own battle pictures

A new unit's pose, the scenes in its attacks and its cut-ins can be PNGs in the package, written as `sprites` in `new_units.json`:

```json
"sprites": {
  "stand":  { "image": "art/battle/stand.png", "origin": [59, 90], "hd": "art/battle/stand-hd.png" },
  "cutin":  { "image": "art/battle/cutin.png", "origin": [56, 48], "mirror": false, "ticks": [22], "hd": "art/battle/cutin-hd.png" }
},
"scenes": { "cutin": "@cutin" },
"units":  { "final": { "pose": "@stand" } }
```

- `image` is a pixel picture at the size the game draws it (a pose is about 128×96), with at most 127 colours; anything half transparent or more is transparent. `python3 tools/mods/sprite_pixels.py big.png small.png --size 128 96` scales a large picture down and reduces its colours.
- `hd` is an HD picture, one per frame: the HD images draw it, the original images draw `image`. The upgrade and ability pages and the library use the two as well.
- `origin` is the pixel in the picture that sits on the scene's origin; for a pose, the middle under the feet.
- `mirror` is `true` by default: the unit and what is in its attacks are flipped for each side. Write `false` for a cut-in and anything else that should not flip.
- For several frames write `"frames": [...]`, with `"ticks"` (beats per frame) and `"loop"` (the frame to go back to at the end).
- `"around": [before, after]`: some weapon effects change to "the next scene" (a beam, for example, changes to the beam's body and then its tail); put your picture on such an actor and you need this. The unit maker's “Picture” writes it for you. A few weapons (about 95, such as attacks several characters make together) work out their animation from scene numbers, so their actors cannot take a picture of your own (a cut-in can be added). The maker locks “Picture” for them, and the game does not load a package that does it.
- The pose `pose` and the scene table `scenes` take `@picture`, or the original's three resource numbers as before.

A package that borrows original pictures and relies on the HD pack to replace them writes `"requires": {"srw64.hd": ">=1.3"}` in `mod.json`. Without the HD pack, or with the images set to original, the whole package is not loaded and the MOD page shows “Needs the HD images”; switching the images in the game takes a restart to count.

## Having them join, and giving them to the enemy

New units and characters do not show up by themselves once installed; a story says when they join ([Story mods](../mod-story/)):

- `{"join": {"pilot": "@kai", "unit": "@rz"}}`: the character and unit join the player;
- `{"replace": {"pilot": 171, "unit": 224, "with": "@final"}}`: the old unit on the roster becomes the new one, with the upgrade steps moved over by `inherits_from`;
- `{"build": "@final"}`: builds a combined unit (a new unit with `combine`) for the player;
- `{"unlock": "@cannon"}`: unlocks a new weapon the story unlocks;
- a story file's `deployments`: enemies and third parties can use the package's new units and characters too.

The deployment records of [campaigns](../mod-campaign/) can name `@name` too.

## Examples

- "Final Dancouga": [`config/recomp/mods/final-dancouga`](https://github.com/dyzz/srw64-recomp/tree/main/config/recomp/mods/final-dancouga). A new unit copied from the original Final Dancouga, with a crew of five and its own pose, cut-in and map icon. On the map Dancouga and Black Wing combine into it; it can separate and combine again, and can sortie combined. Download it from the [MOD page](/en/mods/#srw64.final-dancouga).
- "Kurtz and Super Earthgain": [`config/recomp/mods/super-earthgain`](https://github.com/dyzz/srw64-recomp/tree/main/config/recomp/mods/super-earthgain). Clear "Time to Settle It" on Brad's route and Kurtz joins, and Earthgain and Virose combine into Super Earthgain (Brad as main pilot, Kurtz as sub pilot), which keeps Earthgain's upgrades. It sorties combined; separated on the map, the two have the combination attack Shorai Tenryu Mametsujin, and can combine again when adjacent. Download it from the [MOD page](/en/mods/#srw64.super-earthgain).

### Let an AI write it

The repository's [`docs/guide/data-mods/`](https://github.com/dyzz/srw64-recomp/tree/main/docs/guide/data-mods) folder has the full format (the section on new units and new weapons; the guide is in Chinese) and the unit, pilot and weapon tables as CSV; which table each field goes into and how the game reads it is in [`docs/design/new-unit-ids.md`](https://github.com/dyzz/srw64-recomp/blob/main/docs/design/new-unit-ids.md). Hand them to an AI assistant along with the example packages and ask for what you have in mind. Check the result with `python3 tools/mods/validate.py package.srw64mod`.
