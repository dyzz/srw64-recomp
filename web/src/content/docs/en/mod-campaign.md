---
title: Campaign mods (extra scenarios)
summary: Chain stages you arrange yourself into a campaign, with recruits, the intermission between stages, story choices and endings, packed as a .srw64mod with saves of its own. For now it uses the original maps, characters, units and lines only.
section: mods
order: 5
updated: 2026-10-31
---

An extra scenario is a small campaign of its own: stages played in the order you choose, with the usual intermission between them (upgrades, swaps, saving), and story choices that can lead to different endings. It ships in a mod package, starts from the title's MOD → Extra Scenarios, and **its saves are kept entirely apart from the main game**.

Each stage is a [mini stage](../mini-stage/): an original map, the units on it, and event scripts for the opening, the win and the ending. A campaign adds a stage list on top that chains them together.

It needs version 0.5.1 or later.

## What works today

| Possible | Not yet |
| --- | --- |
| Several stages in a row, branching choices, more than one ending | New lines (dialogue can only use lines already in the game) |
| Each stage's map, who joins, enemies, reinforcements, win and loss conditions | New maps (original maps only) |
| Campaign name, description and stage titles in each language | New characters, portraits or battle lines |
| Funds, story variables, and the team carried from stage to stage | |

Dialogue and choices are **references to the original lines by number**: the game shows the line's own speaker and portrait, in whichever language the player plays. Writing the story means picking existing lines that say what you need.

## What you need

- The [source repository](https://github.com/dyzz/srw64-recomp) and Python 3, to compile the stages and pack the package.
- Your own ROM, placed in the repository as `rom.z64`, with `make recomp-data` run once. It is needed to look up character, unit and map numbers, and to start from the original deployment records or events.
- To have an AI try each stage in the game as it writes: connect the game as described in [Controlling the game with an AI agent (MCP)](../mcp/).

## Let an AI make it

An AI agent that can browse the web and write files (Claude Code, Codex and the like) can follow these guides and make the whole campaign. Open it in the repository folder, copy the request below, and replace the “Story” part with your outline:

```text
Please make an extra-scenario mod (a custom campaign) for Marchwind 64 (the PC recompilation of Super Robot Wars 64), packed as a .srw64mod file ready to install.

First read these and follow their format, rules and numbers exactly:
- https://srw64.dreamquest.club/en/docs/mod-campaign.md (the campaign format, the rules, finding lines, packing)
- https://srw64.dreamquest.club/docs/mini-stage/llms.txt (each stage's format and every script command)
- the sample campaign in the repository, config/recomp/campaigns/sample; follow its structure

Limits:
- Original maps, characters and units only; dialogue and choices only use text numbers already in the game, no new lines
- First find fitting lines in content/dialogue/en/story/ and list their text numbers, speakers and text for me to approve before putting them in a stage
- Start every event with 3DD0 and use no protagonist route markers; avoid lines containing {HeroName} or {PartnerName}
- Keep the campaign's own flags in variables 55–99 or 140–199

Story:
(how many stages; for each, which original stage's map, who joins, the enemies, how to win and lose; where the choices are and how many endings)

Steps:
1. Show me the stage list and each stage's design first; write files once I approve
2. Compile the whole campaign to check it, and fix what fails
3. If you can reach my game through MCP, load each stage on its own, play it to the win and take a screenshot
4. Pack it with tools/mods/pack.py and tell me where the file is and how to install it
```

A few lessons:

- **The more concrete the outline, the better.** Go as far as “stage 2 uses the original stage 5 map, Amuro and Kamille sortie, six enemies, reinforcements on turn 3, win by destroying all enemies.” Left to choose maps and units on its own, an AI easily gets the numbers wrong.
- **Read the lines it picks.** Stitching lines together is where things go wrong most: borrowed lines that do not follow each other, or that mention people and events your story does not have.
- **Check one stage at a time.** Make sure a stage loads, can be won and its ending leads to the next, then play the whole chain.

The rest of this page is the full guide, for people and for the AI alike.

## Files

```text
my-campaign/
  mod.json                 the package's details
  campaign.json            stage list, start, titles
  stages/<stage>.json      one stage each, in the mini stage format
  stages/<stage>.meta.json optional: the stage info page's contents
  status.json              optional: the stage info page's progress rules
```

How to write `mod.json` is in [Mod packages](../mods/#modjson); set `game` to `">=0.5.1"`. Packing compiles `campaign.json` and `stages/` into the package's `campaign/campaign.json`; the sources stay out of the package. For the two optional stage info files, copy the sample campaign's.

## campaign.json

```json
{
  "schema": "srw64.campaign.v1",
  "id": "yourname.my-campaign",
  "name": {"zh-Hans": "我的战役", "en": "My Campaign", "ja": "マイキャンペーン"},
  "description": {"zh-Hans": "一句话简介。", "en": "One line about it.", "ja": "ひとことで紹介。"},
  "start": "first",
  "stages": {
    "first":  {"scene": 1, "file": "stages/first.json",
               "title": {"zh-Hans": "启程", "en": "Setting Out", "ja": "旅立ち"}},
    "second": {"scene": 2, "file": "stages/second.json",
               "title": {"zh-Hans": "岔路", "en": "Crossroads", "ja": "分かれ道"}}
  }
}
```

| Field | Meaning |
| --- | --- |
| `id` | Must be the same as `mod.json`'s `id`. It also names the campaign's save folder: **do not change it after release** |
| `name`, `description` | The name and description on the Extra Scenarios page: one string, or one per language (`zh-Hans`, `en`, `ja`) |
| `start` | Which entry of `stages` comes first |
| `stages` | Each stage: `scene`, the scene number it borrows (below); `file`, the stage file; `title`, shown on the title card, in the intermission and in the save list. Leave the stage number out of the title: the intermission and the save list put “Stage N” in front by themselves, counting the stages cleared |

## Borrowing scene numbers

The game records progress by scene number. Every stage of a campaign borrows one of the original scene numbers, and when the game loads that scene it loads your stage instead. Saves record that number, so:

- each stage borrows a **different** number, from 0 to 142;
- 109–122 (the Link stages) and 38, 48, 68, 73, 79, 81, 83, 86, 88, 93, 95, 96, 104 and 132 (their intermission menu has only two items) cannot be borrowed;
- a `slot` written in the stage file has no effect; `scene` decides.

The number you borrow has nothing to do with the map: the stage file's `map` picks that.

## Connecting stages

As in the original game, in each stage's **ending event** (type 14): `3D4B <scene borrowed by the next stage>`. A branch puts a different `3D4B` in each condition block:

```json
{"name": "ending", "type": 14, "header": [0, 0, 0, 0], "commands": [
  {"op": "3DD0"},
  {"op": "3D3E", "args": [18401]},
  {"op": "3D44", "args": [0, 2, 18402]},
  {"op": "3E10"}, {"op": "3D47"}, {"op": "3D4B", "args": [3]}, {"op": "3E1D"},
  {"op": "3E11"}, {"op": "3D47"}, {"op": "3D4B", "args": [4]}, {"op": "3E1D"}
]}
```

`3D44 0, option count, text number` shows the choice; `3E10`, `3E11` and `3E12` are the first, second and third option; `3E1D` ends the block. You can also note the choice with `3E13 variable, value` and test it with `3E03` in a later stage.

The **last stage**'s ending event runs `3D71`, which plays the ending and returns to the title. Put `3D71` in the ending event only, never on the battlefield.

The compiler checks that every `3D4B` leads to a scene borrowed by a stage of this campaign (500 aside), that `start` exists, and that no scene number is used twice or is one that cannot be borrowed.

## What each stage needs

- **Opening event** (type 12): dialogue on the world map if you like, then `3D4D` to switch to the battlefield, `3D65` for the win and loss conditions shown, `3D45 group` to bring units in, and finally `3D48` to close the dialogue window and hand over to the player.
- **Winning**: for example an event of type 7 (a side has at most so many units left) that runs `3D4A`.
- **Losing**: `3D4C` is GAME OVER, and the player can retry the stage. Do not run it in the middle of the opening event, before the retry snapshot exists.
- **Ending event** (type 14): runs after the `3D4A` victory; the after-stage dialogue and `3D4B` go here.

Every command, event type and deployment record field is in the [stage script command reference](../script-commands/); [`config/recomp/mini-stages/`](https://github.com/dyzz/srw64-recomp/tree/main/config/recomp/mini-stages) in the repository has more than sixty stages to learn from.

## The start, and what to avoid

- **A campaign starts with nothing**: the roster is empty, the protagonist and partner have no names and there is no route. Register allies with `3D5A character, 0, unit, 500` (unit 999 registers the pilot alone), or put player-side deployment records in a stage and bring them in with `3D45`; player units that appear this way join for good. Add funds with `3D5B n` (n × 1000).
- **The team, funds, upgrades and variables carry over to the next stage**, and saves keep them.
- **No protagonist routes**: start every event with `3DD0` and do not use 3DD1–3DD8; do not pick lines containing `{HeroName}` or `{PartnerName}`, or lines spoken by the original protagonists.
- **Story variables** hold only the values 0–3. Each stage start sets 100–114 and 128–139 to 3 and 54 to 1, and the original scripts keep reading 0–54: keep the campaign's own flags in 55–99 and 140–199.
- **Known crashes**: `3D49` when a character is not on the map; `3D5A …, 4000` on a unit still on the map (withdraw it with `3D46` first).
- The Link menu is disabled inside a campaign.

## Finding lines, maps and units

**Lines** are in the repository's `content/dialogue/<language>/story/scene-NNNN.txt` (language `en`, `zh-Hans` or `zh-Hant`; NNNN is the scene number):

```text
@17464 Banjo · 万丈
> 「世のため人のため、ムゲゾルバドス
>  帝国の野望を打ち砕く
“For the world and for the people, Daitarn 3 crushes the ambitions of the Muge Zorbados Empire.
---
…
```

The number after `@` is the text number to put in the dialogue command `3D3E` (`3D3F`–`3D43` show it in other ways). One entry can have several pages (split by `---`) and is shown whole. Lines starting with `>` are the Japanese original, the translation follows, and lines starting with `#` are comments. A choice is an entry too, with one `*` line per option; the option count in `3D44` must match. Full-text search is the quickest way to find a fitting line, for example searching `en/story` for “retreat”.

**Maps and units** are in `assets/original-data/records/`, which `make recomp-data` writes, one JSON object per line:

| File | Use |
| --- | --- |
| `scenarios.jsonl` | Each scene's (each stage's) title |
| `stage_maps.jsonl` | Scene number → map number |
| `stage_auxiliary.jsonl` | A stage's whole set of deployment records, for `deployments_from` |
| `stage_deployments.jsonl` | Single deployment records, as a `template` |
| `actors.jsonl`, `units.jsonl` | Character and unit numbers |

To use “the map and enemies of original stage N”, find its scene number in `scenarios`, then take the map number and deployment records from `stage_maps` and `stage_auxiliary`.

## Compile, try, pack

Check the whole campaign (it lists each stage's borrowed scene and where its `3D4B` lead):

```bash
python3 tools/recomp/script_lab/mini_stage.py campaign my-campaign/campaign.json --out /tmp/check.json
```

To try one stage: add `"slot": <its borrowed scene>` to the stage file for the moment, compile it as described for [mini stages](../mini-stage/), and load it with `srw64_mini_stage_load`. A stage tried alone runs inside the main game, so the `3D4B` in its ending leads into the original story: stop once the stage is cleared. The stage may save, so use a separate user directory with `--user-dir`.

Pack it:

```bash
python3 tools/mods/pack.py my-campaign -o build/mods
```

This writes `build/mods/<id>-<version>.srw64mod` and checks it with `tools/mods/validate.py`.

## Installing and playing

1. Drag the `.srw64mod` into the game window to install it (or put it in the MOD folder; see [Mod packages](../mods/#installing)).
2. On the title screen, open “MOD”, go to the “Extra Scenarios” page and press “Play” on the campaign's row.
3. “Start Campaign” and the campaign's name appear in the middle of the title screen: start there. Loading from the title now loads this campaign's saves.
4. To return to the main game: “Back to Main Game” on the same page. Switching works on the title screen only.

The campaign's saves are in `campaigns/<id>/saves/` in the user directory, apart from the main game's.

## Sharing

`template`, `deployments_from` and `copy_from` copy original data from your ROM into the package; a package made that way is for your own play only. To share one publicly, write every deployment record field by field yourself (`group`, `x`, `y`, `actor`, `unit`, `faction`, `level_offset`, `upgrade` and so on) and write every event yourself; text, map, character and unit numbers are only numbers and are fine. Otherwise the [mod package copyright rules](../mods/#copyright) apply.
