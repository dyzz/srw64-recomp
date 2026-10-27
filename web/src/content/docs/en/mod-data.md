---
title: Number mods
summary: Change the values of units, pilots and weapons — HP, armor, stats, terrain ranks, special abilities, skills, spirits, weapon power and range — by number. With how battles combine terrain and when changes take effect.
section: mods
order: 3
updated: 2026-10-27
---

A number mod changes values of the units, pilots and weapons themselves. Everything that reads them follows: battle results, the status screens, the library.

Installing and managing packages: [MOD packages](../mods/). Terrain ranks work from 0.5.1; the other fields need 0.5.2 or later.

## What goes in the package

```text
yourname.balance/
  mod.json
  data/
    units.json      units, by unit number
    pilots.json     pilots, by pilot number
    weapons.json    weapons, by weapon number
```

Any of the three files will do. `mod.json` is written as for any package.

## How to write them

```json
{
  "184": { "hp": 5200, "armor": 1300, "terrain": { "sea": "A" } },
  "262": { "terrain": { "space": "A" }, "abilities": ["i_field"] }
}
```

- The top-level keys are **numbers**, plain decimals without leading zeros: unit numbers in `units.json`, pilot numbers in `pilots.json`, weapon numbers in `weapons.json`. Look numbers and original values up in the [ID table](/en/mods/ids/).
- Under each number write only the fields you change; the rest stay as they are.
- When several packages change the same thing, the one higher in the MOD list wins.

The rules are strict: an unknown field, a value out of range or a number out of range is an error, and **one error leaves the whole file out**. The game log then has a `SRW64_MOD_DATA_PROBLEM` line naming the package, the file and the reason.

### Units (units.json)

| Field | Meaning | Range |
| --- | --- | --- |
| `hp`, `en` | max HP, EN | HP 1–65535, EN 0–65535 |
| `move` | movement | 1–30 |
| `mobility`, `armor`, `limit` | mobility, armor, limit | 0–65535 |
| `repair_cost` | repair cost | 0–65535 |
| `upgrade_cap` | upgrade limit | 0–15 |
| `terrain` | terrain ranks, see below | |
| `size` | size | `SS`, `S`, `M`, `L`, `LL` |
| `shield`, `sword` | carries a shield, a sword (for shield defense and parrying) | `true` or `false` |
| `parts_slots` | part slots | 0–4 |
| `abilities` | special abilities, **the whole list**: anything not in it is removed | see below |

Ability keys:

| Key | Ability | Key | Ability |
| --- | --- | --- | --- |
| `beam_coat` | Beam Coat | `mach_special` | Mach Special |
| `i_field` | I-Field | `true_mach_special` | Shin Mach Special |
| `planet_defensor` | Planet Defensor | `god_shadow` | God Shadow |
| `aura_barrier` | Aura Barrier | `getter_vision` | Getter Vision |
| `afterimage` | Afterimage | `shungensoku` | Phantom Step |
| `hyper_jammer` | Hyper Jammer | `hp_regen_10`, `hp_regen_20` | HP Regen 10%, 20% |

Transform, Combine and Separate cannot be added or removed by a mod: they need the forms behind them. A unit that has them keeps them.

### Pilots (pilots.json)

| Field | Meaning | Range |
| --- | --- | --- |
| `melee`, `ranged`, `evade`, `hit`, `reaction`, `skill` | the six stats at level 1 | 0–255 |
| `sp` | max SP at level 1 | 0–255 |
| `double_move` | the level from which the pilot acts twice a turn | 0–99 |
| `terrain` | terrain ranks, see below | |
| `skills` | skills, see below | |
| `spirits` | spirits, see below | |

The stats are the level-1 values; each level adds what the original does (melee, ranged, reaction, skill +1; evade, hit +2; SP +2).

**Skills** are written as a skill key and the levels at which it is learned: the Nth number is the level for L N, at most nine, ascending. `null` removes the skill:

```json
{ "133": { "skills": { "newtype": [1, 4, 8, 12, 18, 24, 30, 38, 45], "parry": null } } }
```

| Key | Skill | Key | Skill |
| --- | --- | --- | --- |
| `newtype` | Newtype | `esp` | ESP |
| `enhanced` | Enhanced | `potential` | Potential |
| `holy_warrior` | Holy Warrior | `parry` | Parry |
|  |  | `shield_defense` | Shield Defense |

Newtype, Enhanced, Holy Warrior, ESP and Potential share one row of levels in the original, so a pilot has one of them at most; writing one replaces the other. Skills you do not write stay as they are.

**Spirits** are the whole list: at most six entries, each `[spirit key, level learned]`, levels ascending:

```json
{ "133": { "spirits": [["focus", 1], ["guts", 5], ["valor", 12], ["soul", 40]] } }
```

Spirit keys: `self_destruct` Self-Destruct, `scout` Scan, `accel` Accel, `mercy` Mercy, `focus` Focus, `alert` Flash, `guts` Vigor, `strike` Bullseye, `trust` Trust, `wall` Wall, `cover` Cover, `valor` Valor, `spirit` Spirit, `great_guts` Guts, `luck` Fortune, `drain` Daunt, `effort` Gain, `soul` Soul, `stealth` Stealth, `awaken` Zeal, `refuel` Resupply, `provoke` Taunt, `encourage` Rouse, `disrupt` Disrupt, `friendship` Faith, `love` Love, `reactivate` Enable, `miracle` Miracle, `prayer` Prayer, `revive` Revive.

### Weapons (weapons.json)

| Field | Meaning | Range |
| --- | --- | --- |
| `power` | power before upgrades | 0–25500, a multiple of 100 |
| `range_min`, `range_max` | minimum and maximum range | min 0–30, max 1–30, min not above max |
| `hit` | hit modifier | -128–127 |
| `critical` | critical modifier | -128–127 |
| `ammo` | ammo, `-1` for none | -1–99 |
| `en` | EN cost | 0–255 |
| `morale` | morale required | 0–255 |
| `terrain` | the weapon's own terrain ranks | |

Upgrades add power as in the original. **A weapon carried by several units changes on all of them**; the ID table's "Carried by" column lists them.

### Terrain ranks (terrain)

Write only the terrains you change: `air`, `land`, `sea`, `space`, with `A`, `B`, `C`, `D` or `-`. The others stay as they are. Units, pilots and weapons all use this.

## How battles combine terrain

A unit's rank in battle is **the unit's rank plus the pilot's rank**, then banded:

| Rank | A | B | C | D | - |
| --- | --- | --- | --- | --- | --- |
| Points | 4 | 3 | 2 | 1 | 0 |

| Sum | 8 or more | 6–7 | 4–5 | 1–3 | 0 |
| --- | --- | --- | --- | --- | --- |
| Counts as | A (120%) | B (100%) | C (80%) | D (60%) | - (0%) |

The attacker's attack power and the defender's armor are each multiplied by their own rate. So **when the unit is already A, only the pilot can make it A in battle**. GoShogun (184) is A in space and Shingo (133) is B: 7 together, which the original counts as B. Raise Shingo's space to A and it is A.

A weapon's own terrain rank only depends on the weapon and multiplies once more.

## When changes take effect

No restart is needed after installing, enabling or disabling a package. The game keeps none of these values in memory and reads them again whenever it needs them, so:

- **When a save loads** (an intermission save or a suspend save in a stage alike), every unit, weapon and pilot is rebuilt from the packages enabled now. Load with the package on and you get the new values; disable it and load again and they are back to the original. The save itself is never changed.
- Unit values, and a weapon's power, max range and air rank, update on entering the map or the intermission.
- A weapon's other fields and everything about a pilot need a load (or a new game); a pilot also updates on leveling up. If you switch packages inside a stage, save and load.
- The library shows the changed values too.

## Shared records

Some pilots share one ability record (stats, terrain, skills), mostly other numbers of the same character (Domon's 4, 273 and 274, for example). Changing one changes all of them; the ID table's "Shares its record with" column lists them. Spirits have their own records, which can be shared as well.

Entries with the same name are different numbers: a transformed form, an enemy copy, a story-only one. Check the original values before you change one.

## Example

The example package "Main Cast: Space A" can be downloaded from the [MOD page](/en/mods/#srw64.space-a); its source is [`config/recomp/mods/space-a`](https://github.com/dyzz/srw64-recomp/tree/main/config/recomp/mods/space-a). It raises space to A for the 20 pilots and 21 units of the main cast below A there: the Mazinger team, Combattler V, Getter-3, GoShogun, Giant Robo, the Dunbine cast, Guncannon, Guntank and more.

Names and portraits: [Name and portrait mods](../mod-names/).

### Let an AI write it

The repository's [`docs/guide/data-mods/`](https://github.com/dyzz/srw64-recomp/tree/main/docs/guide/data-mods) folder has the format and the unit, pilot and weapon tables as CSV. Hand the whole folder to an AI assistant and ask it for the `units.json`, `pilots.json` and `weapons.json` you want.
