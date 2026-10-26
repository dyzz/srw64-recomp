---
title: Number mods
summary: Change the terrain ranks (air, land, sea, space) of units and pilots, by unit and pilot number. With how battles combine them and when changes take effect.
section: mods
order: 3
updated: 2026-10-26
---

A number mod changes values of the units and pilots themselves. Everything that reads them follows: battle results, the status screens, the library.

**For now only terrain ranks can be changed.** HP, armor, weapons and the other values are not open yet; a package that carries them is ignored for those.

Installing and managing packages: [Mod packages](../mods/). A version after 0.5.0 is needed.

## What goes in the package

```text
yourname.space/
  mod.json
  data/
    units.json      units, by unit number
    pilots.json     pilots, by pilot number
```

Either file alone is fine. `mod.json` is written as for any package.

## How to write them

```json
{
  "262": { "terrain": { "space": "A" } },
  "184": { "terrain": { "sea": "A", "land": "B" } }
}
```

- The keys at the top are **numbers**: unit numbers in `units.json`, pilot numbers in `pilots.json`, in decimal without leading zeros. Look them up in the [unit and pilot ID table](/en/mods/ids/).
- `terrain` holds only the terrains you change: `air`, `land`, `sea`, `space`. A value is `A`, `B`, `C`, `D` or `-`. Terrains you leave out stay as in the original.
- When several packages change the same thing, the one higher in the mod manager’s list wins.

The rules are strict: an unknown field, terrain or rank, or a number out of range, is an error, and **a file with an error is not used at all**. The game’s log then has a `SRW64_MOD_DATA_PROBLEM` line naming the package, the file and the reason.

## How battles count them

A unit’s side of the multiplier **adds the unit’s rank and the pilot’s rank**, then grades the sum:

| Rank | A | B | C | D | - |
| --- | --- | --- | --- | --- | --- |
| Points | 4 | 3 | 2 | 1 | 0 |

| Sum | 8 or more | 6–7 | 4–5 | 1–3 | 0 |
| --- | --- | --- | --- | --- | --- |
| Counts as | A (120%) | B (100%) | C (80%) | D (60%) | - (0%) |

The attacker’s power and the defender’s armor are each multiplied by their own. So **when a unit is already A, only its pilot can make it count as A**. GoShogun (184) is A in space and Shingo (133) is B: 7 together, so in the original it only counts as B in space. With Shingo at A in space, it is A.

A weapon’s terrain rank is the weapon’s alone and multiplies once more. Weapons are numbered too, but cannot be changed yet.

## When changes take effect

- No restart is needed after installing, turning on or turning off a package.
- **Loading a save between stages** rebuilds every unit and pilot with the new values, so existing saves load with them.
- At other times a unit follows the next time the game recomputes it (upgrades, changing pilots, fitting parts…), a pilot when leveling up. Turning a package on or off in the middle of a stage waits for those, so do it outside stages.
- The library shows the changed values.

## Shared ability records

Some pilots share one ability record, mostly other numbers of the same character (Domon’s 4, 273 and 274, for instance). Changing one changes all of them; the ID table lists them under “Shares its record with”.

Entries with the same name are different numbers: another form, an enemy copy or a story-only copy. Check their original ranks before you change one.

## Sample

[`space-a`](https://github.com/dyzz/srw64-recomp/tree/main/config/recomp/mods/space-a) raises space to A for the 20 pilots and 21 units of the main cast below A there: the Mazinger team, Combattler V, Getter-3, GoShogun, Giant Robo, the Dunbine cast, Guncannon, Guntank and more.

### Let an AI do it

The repository’s [`docs/guide/data-mods/`](https://github.com/dyzz/srw64-recomp/tree/main/docs/guide/data-mods) folder holds the format and the unit and pilot tables as CSV. Give the whole folder to an AI assistant and have it write `units.json` and `pilots.json` to your wishes.
