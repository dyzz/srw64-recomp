---
title: Campaign mods (extra scenarios)
summary: Chain stages of your own into a campaign with the web level editor (or by hand), with maps you draw, new lines, story choices, reinforcements, win and loss conditions and how enemies act, and the new units and characters of packages too. Packed as a .srw64mod with saves of its own.
section: mods
order: 6
updated: 2026-11-26
---

An extra scenario is a small campaign of its own: stages played in the order you choose, with the usual intermission between them (upgrades, swaps, saving), and story choices that can lead to different endings. It ships in a mod package, starts from the title's MOD → Extra Scenarios, and **its saves are kept entirely apart from the main game**.

Each stage has four layers: **the map** (an original map, or a whole map you draw), **the units** (placed on squares in groups), **the story** (events and lines for the opening, during the battle and after the stage) and **the flow** (which stage comes next, an ending, or a choice for the player).

The new format on this page needs version 0.5.4 or later. Campaigns made in the old format for 0.5.1–0.5.3 still play.

## What works today

| Possible | Not yet |
| --- | --- |
| Several stages in a row, branching choices, more than one ending | Inserting new stages into the middle of the main story |
| Original maps, or a whole map you draw (you set each square's terrain) | Switching to a second map of your own within one stage; water on a drawn map does not move |
| New lines, choice texts and objective texts of your own, in each language; the original's “???” silhouette speakers | Using an original map with colony animation as the stand-in for a drawn map |
| Each stage's player, enemy and third-party units, reinforcements, how enemies act, the parts they drop and how worn they arrive | Starting a playtest from a stage in the middle |
| Win and loss conditions: everyone down, someone shot down, reaching an area, turns, units remaining, HP, Persuade… alone or combined | |
| New lines for characters in battle; choosing the sortie | |
| The new units and characters of packages ([New unit, character and weapon mods](../mod-units/)) | |
| Funds, story variables, and the team carried from stage to stage | |

## Using the level editor

The website's [level editor](/en/mods/editor/) writes a campaign stage by stage in the browser. **It needs no ROM and uploads nothing.** The original maps, units, characters and songs are read from the website, as in the library.

- **Overview**: the stage list and a flow chart (the lines show where each stage leads once cleared), the check results, the package details and the protagonist route.
- **Map**: use an original map as it is; or import a picture of your own (PNG, JPEG, WebP), cut it into squares and pick each square's terrain (Brush, Rectangle, Fill, Pick; “Guess from the colours” fills it in first for you to correct), choose an original map for it to stand in for, and see how it looks with the original images.
- **Units**: place units in groups, each group with a side and a time to arrive; change the selected unit's unit, pilot, level bonus and upgrades; for enemies and third parties, set how they act (attack freely, hunt one character, go to a square, hold position, guard one character and so on), how many turns they stay put, the part they drop when shot down, and their HP and EN on arrival.
- **Story**: before setting out, entering the battlefield, each event during the battle, after the battle; for each event, pick its timing and conditions and add actions step by step (lines, arrivals, departures, joining, music, camera, choices, variables…). Lines are written separately in Chinese, Japanese and English, with a preview beside them of how they look in the game. A separate column holds “Battle lines”.
- **Rules**: win and loss conditions, objective texts, when each group arrives, where the stage leads once cleared (an ending, the next stage, a choice for the player) and the battle music.

The project is saved in the browser by itself. “Save project” in the top bar saves it as a `.srw64edit` file, finished or not, and “Open project” picks it up again. Every step can be undone (Ctrl/⌘+Z). “Template” loads a ready two-stage campaign to start from. When you are done, press “Export .srw64mod”; before exporting, the page checks the campaign (units off the map, standing on squares they cannot enter, overlapping, more than 30 on a side, whether both sides are there and so on). The exported package carries its sources, so “Open project” can read it back too.

## Let an AI make it

An AI agent that can read and write files (Claude Code, Codex and the like) can follow this page and make the whole campaign. Open it in the [source repository](https://github.com/dyzz/srw64-recomp) folder, copy the request below, and replace the “Story” part with your outline:

```text
Please make an extra-scenario mod (a custom campaign) for Marchwind 64 (the PC recompilation of Super Robot Wars 64), packed as a .srw64mod file ready to install.

First read these and follow their format and rules exactly:
- https://srw64.dreamquest.club/en/docs/mod-campaign.md (the campaign and stage format, maps, packing)
- https://srw64.dreamquest.club/en/docs/mod-story.md (event timing, conditions and actions; stage events are written the same way)
- the sample campaign in the repository, config/recomp/campaigns/sample; follow its structure

Requirements:
- Write every new line in ja and en, with 「」 in Japanese and “ ” in English
- Look up character, unit and map numbers in the tables in docs/guide/data-mods/ and in docs/guide/story-mods/scenes.csv in the repository
- Use story variables 60–99 only

Story:
(how many stages; for each, which map, who joins, the enemies, when reinforcements come, how to win and lose; where the choices are and how many endings)

Steps:
1. Show me the stage list and each stage's design first; write files once I approve
2. Pack it with tools/mods/pack.py (it compiles and checks the whole campaign), and fix what fails
3. Tell me where the file is and how to install it
```

A few lessons:

- **The more concrete the outline, the better.** Go as far as “stage 2 uses the original stage 5 map, Amuro and Kamille sortie, six enemies, reinforcements on turn 3, win by destroying all enemies.” Left to pick numbers on its own, an AI easily gets them wrong.
- **Open it in the level editor first.** “Open project” in the editor reads a package the AI wrote, and one look shows whether the map and unit positions are right.
- **Play one stage at a time.** Make sure a stage loads, can be won and leads to the next once cleared, then play the whole chain.

The rest of this page is the full format.

## Files

```text
my-campaign/
  mod.json                 the package's details
  campaign.json            the campaign: name, first stage, protagonist route, stage order
  stages/<stage>.json      one stage each
  stages/<stage>.meta.json optional: the stage info page's contents
  status.json              optional: the stage info page's progress rules
  maps/<map>/              optional: maps you draw
```

How to write `mod.json` is in [Mod packages](../mods/#modjson); set `game` to `">=0.5.4"`. Packing compiles these sources into the package's `campaign/`; the sources stay out of the package (a package the editor exports carries a copy of them for the editor to read back). For the two optional stage info files, copy the sample campaign's.

## campaign.json

```json
{
  "schema": "srw64.campaign.v2",
  "id": "yourname.my-campaign",
  "name": {"zh-Hans": "我的战役", "en": "My Campaign", "ja": "マイキャンペーン"},
  "description": {"zh-Hans": "一句话简介。", "en": "One line about it.", "ja": "ひとことで紹介。"},
  "author": "yourname",
  "version": "1",
  "start": "valley",
  "route": 1,
  "stages": ["valley", "showdown"]
}
```

| Field | Meaning |
| --- | --- |
| `id` | Must be the same as `mod.json`'s `id`. It also names the campaign's save folder: **do not change it after release** |
| `name`, `description` | The name and description on the Extra Scenarios page: one string, or one per language (`zh-Hans`, `zh-Hant`, `en`, `ja`, `ko`, and [language packages' languages](../mod-language/)) |
| `start` | Which stage comes first |
| `route` | The protagonist route, required: 0 Brad, 1 Manami, 2 Ark, 3 Selain. Some original lines draw the protagonist group's portraits and crash without a route; a stage can set its own in `setup` |
| `stages` | An array of stage names, matching `stages/<stage>.json`. Stage names use lowercase letters, digits and `-` `_` |
| `status` | Optional: the stage info page's progress rules file |

The game records progress by scene number. The compiler gives each stage one of the original scene numbers in `stages` order (skipping the ones that cannot be borrowed), so you need not deal with it. But **saves record that number**, so to add stages after release, add them at the end of `stages`, never in the middle, or old saves will point at another stage. A campaign holds about 115 stages at most.

## Stage files

```json
{
  "schema": "srw64.stage.v1",
  "title": {"ja": "分かれ道", "zh-Hans": "岔路", "en": "Crossroads"},
  "map": 19,
  "units": {
    "place": [
      {"group": 0, "x": 14, "y": 14, "pilot": 36, "unit": 55, "faction": 0},
      {"group": 1, "x": 6, "y": 3, "pilot": 69, "unit": 317, "faction": 1, "level": 1,
       "ai": {"mode": "goto", "to": [14, 14]}, "drop": 3}
    ]
  },
  "objective": {"win": {"ja": "敵の全滅", "en": "Destroy all enemies"}, "lose": {"ja": "アムロの撃墜", "en": "Amuro shot down"}},
  "events": [
    {"when": "opening", "do": [
      {"bgm": 49}, {"fade": "in"},
      {"say": 36, "text": {"ja": "「行くぞ」", "en": "“Here we go.”"}},
      {"fade": "out"}, {"to_map": true},
      {"op": "3D65", "args": [127, 127]},
      {"fade": "in"}, {"deploy": 0}, {"deploy": 1}, {"bgm": 27}, {"op": "3D48"}
    ]},
    {"id": "all-down", "when": {"enemies_gone": true}, "do": [{"win": true}]},
    {"id": "amuro-down", "when": {"defeated": 36}, "do": [{"lose": true}]},
    {"when": "end", "do": [
      {"say": 36, "text": {"ja": "「次はどうする？」", "en": "“What now?”"}},
      {"choose": {"text": {"ja": ["進む", "戻る"], "en": ["Go on", "Go back"]},
                  "options": [[{"next": "onward"}], [{"next": "back"}]]}}
    ]}
  ]
}
```

| Field | Meaning |
| --- | --- |
| `title` | The title, shown on the title card, in the intermission and in the save list. Leave the stage number out: the intermission and the save list put “Stage N” in front by themselves, counting the stages cleared |
| `map` | An original map number, or the name of your own map under `maps/` (below) |
| `units` | `place` holds the stage's units, below; `copy_from` copies an original stage's whole set of deployment records (it reads your ROM; see Sharing) |
| `events` | The events, below |
| `objective` | Optional: the win and loss texts shown under Objectives in the map menu, up to 30 characters each. With it, the opening must have `{"op": "3D65", "args": [127, 127]}` (the editor writes it for you); without it the original's wording is used, and the opening has `3D65 win number, loss number` |
| `battle_lines` | Optional: new lines in this stage's battles, written as a story mod's [battle lines](../mod-story/#battle-lines), without `scene` |
| `setup` | Optional: `route` (this stage's protagonist route), `story_vars` (story variables on entering the stage), `last_choice`, `initial_resources` |
| `scene` | Optional: which scene number to borrow. Usually left out |
| `raw_events` | Optional: events in the original event format (for copying an original event whole); see the [stage script command reference](../script-commands/) |

### Units (units.place)

One record per unit:

| Field | Meaning |
| --- | --- |
| `group` | The group number. In the opening, `{"deploy": group}` brings a group in; reinforcements are a `deploy` when their time comes |
| `x`, `y` | The square, counted from 0 at the top left |
| `pilot`, `unit` | Character and unit numbers; a package's new character or unit is `"@name"` (this package's own) or `"other.package@name"` (listed in `requires`) |
| `faction` | The side: 0 player, 1 enemy, 2 or 4 third party |
| `level` | Added to the stage's base level |
| `upgrade` | Upgrade steps |
| `ai` | How an enemy or third-party unit acts: `{"mode": …, "wait": turns, "target": character, "to": [x, y]}`. `mode` is `attack` (attacks freely, going for low armor and HP first), `accurate` (attacks freely, picking by hit chance and skipping weapons under 30%), `hunt` (goes only after `target`), `goto` (heads for `to`, attacking no one on the way but answering back when hit), `flee` (as `goto`, without answering back) or `guard` (guards `target`, attacking what comes in range). `wait` is how many turns it stays put; 31 is for good |
| `drop` | The part (0–37) it drops when shot down, added to stock once the stage is cleared; without it, nothing drops |
| `entry` | How worn it arrives: 1 HP 10%, EN and ammo 0; 2 HP and EN 10%; 3/4/5 HP and EN 30/50/70%; 6 full HP, EN and ammo 0; 7 HP 10%, full EN. Without it, everything is full |

Without `copy_from`, each side has at most 30 units. Player units join for good once they arrive and carry over to the next stage.

### Events (events)

`when` is written one of two ways:

- The strings `"opening"`, `"setup"` and `"end"`: the stage's own opening, initial setup and ending events, at most one each. They always run and take no `id` or `if` (to test something, write an `if` in `do`). **Every stage needs an opening**; the ending runs after the victory settlement.
- An object: exactly as in a [story mod](../mod-story/)'s events (turns, shot down, HP, fights, every enemy gone, units remaining, reaching an area, Persuade…), with an `id` and without `scene`. `fired` and `not_fired` refer to events of the same stage.

`do` takes every story mod action (lines, choices, arrivals, departures, joining, changing units, variables, music, camera, waiting…), plus five that only campaigns have:

| Action | Meaning |
| --- | --- |
| `{"win": true}` | Clear the stage: the victory settlement, then the ending event |
| `{"lose": true}` | GAME OVER; the player can retry the stage. Do not run it in the middle of the opening |
| `{"next": "stage"}` | Where to go once the stage is cleared, written in the ending event; a branch writes a different `next` in each option of a choice or each branch of an `if` |
| `{"ending": true}` | Play the ending, then return to the title. Write it only in the last stage's ending event |
| `{"to_map": true}` | In the opening, switch from the world map to the battlefield |

The usual order of an opening: dialogue on the world map, `to_map` to the battlefield, the objectives, `deploy` for each group, and finally `{"op": "3D48"}` to close the dialogue window and hand over to the player (the sample campaign does exactly this). New lines can also go in `story/<language>/*.txt`, with `text` as `"@name"`, as in [story mods](../mod-story/#lines-in-text-files).

Original commands still work, such as `{"op": "3D5B", "args": [10]}` to add 10,000 funds, or `{"op": "3D3D", "args": [0, 0, 0, at most how many, group]}` to let the player pick who sorties from the team, placed where that group's first record is (the editor's “Choose the sortie” is this command). The commands are in the [stage script command reference](../script-commands/).

## Maps you draw

Put `maps/<map>/map.json` together with a base picture, and write `"map": "<map>"` in the stage:

```json
{"schema": "srw64.map.v1", "like": 20, "image": "base.png", "scale": 4,
 "terrain": [[99, 99, 99], [99, 5, 1]]}
```

- `image`: the whole map, sized squares × 16 × `scale` (`scale` is 4 by default, 1–8). 28×32 squares at `scale` 4 is 1792×2048. The HD images draw this picture; the original images draw the reduced version the tool makes.
- `terrain`: one terrain number per square (63 is a wall), with as many rows and columns as the picture has squares. The outer two rings are the map's border and count as border whatever they say. The terrain list in the editor gives each terrain's name, defense, hit, recovery and movement cost.
- `like`: the original map it stands in for. The stage's BGM, whether it is in space, the battle backgrounds and the water colour all follow that map, so pick one of the same kind of place. Only maps without colony animation can be picked; for space, only 66, 74, 112, 137 and 157.
- From 20×15 to 128×128 squares.

The editor's Map page does all this. By hand, `pack.py` calls the map tool by itself; to see the result alone, `python3 tools/mods/map_tool.py build maps/<map> -o <output folder>` also writes `preview.png`, the map as the original images show it.

## The start, and what to avoid

- **A campaign starts with nothing**: the roster is empty and the protagonist and partner have no names. Player units come in through the player-side records placed in a stage (arriving means joining for good), or through the story action `join`.
- **The team, funds, upgrades and variables carry over to the next stage**, and saves keep them.
- **Story variables** hold only the values 0–3. The original scripts keep reading 0–54, and 100–114 and 128–139 are reset on entering a stage. Keep the campaign's own variables in 60–99 (the editor's “+ New variable” picks from there).
- Better not to borrow original lines containing `{HeroName}` or `{PartnerName}`, or lines the original protagonists speak themselves: at the start the protagonist has no name.
- **Known crashes**: `3D49` when a character is not on the map; removing a unit still on the map with `3D5A …, 4000` (have it leave first).
- The Link menu is disabled inside a campaign.

## Compiling and packing

```bash
python3 tools/mods/pack.py my-campaign -o build/mods
```

The compiler is the same one the site's level editor runs (TypeScript): the command line needs [Node.js](https://nodejs.org/) 23 or later, which `python3` calls for you. `pack.py` compiles the whole campaign, writes `build/mods/<id>-<version>.srw64mod` and checks it by the game's rules; a mistake is reported with the stage, the event and the item it is in. To see only what each stage compiles to:

```bash
python3 tools/mods/stage.py compile my-campaign -o /tmp/out
```

## Installing and playing

1. Drag the `.srw64mod` into the game window to install it (or put it in the MOD folder; see [Mod packages](../mods/#installing)).
2. On the title screen, open “MOD”, go to the “Extra Scenarios” page and press “Play” on the campaign's row.
3. “Start Campaign” and the campaign's name appear in the middle of the title screen: start there. Loading from the title now loads this campaign's saves.
4. To return to the main game: “Back to Main Game” on the same page. Switching works on the title screen only.

The campaign's saves are in `campaigns/<id>/saves/` in the user directory, apart from the main game's.

## Sharing

A package made in the editor uses only numbers and what you wrote yourself, so it can be shared publicly. By hand, `copy_from`, `template` or `copy_from` on an original event copy original data from your ROM into the package; a package made that way is for your own play only. Text, map, character and unit numbers are only numbers and are fine. A map you draw must be a picture you have the right to pass on. Otherwise the [mod package copyright rules](../mods/#copyright) apply.
