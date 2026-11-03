---
title: Story mods
summary: Add events to the original stages, saying when they fire, what must hold, what they do and the new lines they speak. Characters can join or stay and story branches can change, without taking any of the original event slots or touching an original event.
section: mods
order: 5
updated: 2026-11-03
---

A story mod adds events to the original stages. On a given turn, when someone is shot down, when two characters fight, when Persuade is chosen and so on, it plays the lines you wrote, has characters join or leave, or sets story variables so that the original story afterwards takes another branch. The game decides when an event fires. You write only the conditions and what happens, and you need not know the original event tables. No original event is changed or pushed out.

Installing and managing packages is in [Mod packages: installing and making them](../mods/). Version 0.5.3 or later is needed.

The complete example is "Master Asia Lives", with its source in [`config/recomp/mods/master-asia`](https://github.com/dyzz/srw64-recomp/tree/main/config/recomp/mods/master-asia). On the Independence Army route's "Death Match! Showdown on Lantau Island (Part 2)" and the OZ route's "Fist of Despair and Sorrow (Part 2)", once Domon has talked with Master Asia in battle he can persuade him. With 50 kills or more Master Asia withdraws and joins after the stage.

## What a package holds

```text
yourname.story/
  mod.json
  story/
    <any name>.json   story scripts, as many as you like
  data/
    pilots.json       numbers to change as well, if needed (see Number mods)
```

When the package is packed (`tools/mods/pack.py`), the story scripts in `story/` are compiled into the form the game reads. A mistake is reported with the event and the item it is in.

## Writing events

```json
{
  "schema": "srw64.story.v1",
  "events": [
    {
      "id": "persuade",
      "scene": 129,
      "when": { "persuade": { "by": 4, "target": 18 } },
      "if": { "vars": { "131": 0 }, "kills": { "4": 50 } },
      "do": [
        { "say": 4, "text": { "ja": "「師匠！」", "zh-Hans": "“师父！”", "en": "“Master!”" } },
        { "say": 18, "mode": 2, "text": { "ja": "「……」", "zh-Hans": "“……”", "en": "“...”" } },
        { "leave": 18 },
        { "set": { "131": 3 } }
      ]
    },
    {
      "id": "joins",
      "scene": 129,
      "when": { "after": "end" },
      "if": { "fired": ["persuade"] },
      "do": [ { "join": { "pilot": 18, "unit": 27 } } ]
    }
  ]
}
```

Each event has:

| Field | Meaning |
| --- | --- |
| `id` | The event's name, unique in the package; other events refer to it by this |
| `scene` | The stage, by scene number (0–255). Every stage's scene number is in the [scene table](https://github.com/dyzz/srw64-recomp/blob/main/docs/guide/story-mods/scenes.csv) |
| `when` | When it fires (below) |
| `if` | Optional: what must also hold |
| `do` | What it does, in order |
| `repeat` | Optional: `true` lets it fire again |
| `set_flags` | Optional: flags set when the event has run, for later stages |
| `note` | Optional: any notes; the game does not read them |

**By default each event fires once per stage.** The game keeps track, in the suspend save too, so you need not set variables to stop a replay as the original events do.

Pilot and unit numbers are in the [pilot and unit tables of Number mods](../mod-data/).

## when

| Written as | Meaning |
| --- | --- |
| `{"turn": {"turn": 3, "side": "player"}}` | From turn 3, at the start of that side's phase (`player`, `enemy`, `third`) |
| `{"defeated": 18}` | The character's unit is shot down (leaving does not count) |
| `{"hp_below": {"who": 18, "percent": 30}}` | The character's HP falls below 30% |
| `{"battle": {"between": [4, 18], "moment": "after"}}` | After the two fight (`before` is before the battle; 0 means anyone) |
| `{"enemies_gone": true}` | Every enemy is gone; `{"by_turn": 5}` limits it to turn 5 or earlier |
| `{"remaining": {"side": "enemy", "at_most": 3}}` | Three or fewer enemy (or `third`-party) units remain |
| `{"persuade": {"by": 4, "target": 18}}` | With 4 next to 18, the command menu offers Persuade; choosing it fires the event |
| `{"after": "opening"}`, `"setup"`, `"end"` | Right after the stage's original opening, setup or ending event |

When an original event is due at the same moment it runs first, and yours is looked at on the next check. Events after the opening, setup or ending cannot `repeat`.

## if

| Written as | Meaning |
| --- | --- |
| `"vars": {"131": 0}` | A story variable (0–199) has this value (0–3). The original story keeps its branches in these. In scene 129, for instance, 131 is "Domon has talked with Master Asia in battle" (0 when he has) |
| `"kills": {"4": 50}` | This player character has at least this many kills |
| `"kills_below": {"4": 50}` | Fewer kills than this |
| `"fired": ["persuade"]` | These events of the package have fired in this stage |
| `"not_fired": [...]` | They have not fired yet in this stage |
| `"roster": {"pilots": [18], "units": [27]}` | These characters and units are on the player's roster (deployed or not) |
| `"not_roster": {...}` | None of them is on the roster |
| `"flags": ["persuaded"]` | These flags of the package are set |
| `"not_flags": [...]` | None of them is set |

When the character is not on the player's roster, neither `kills` nor `kills_below` holds.

**Success and failure lines.** Write two events for the same moment with conditions that exclude each other, and the game picks the one that holds. In the example, enough kills means success and too few plays the refusal:

```json
{ "id": "persuade", "when": { "persuade": { "by": 4, "target": 18 } },
  "if": { "vars": { "131": 0 }, "kills": { "4": 50 }, "not_fired": ["refused"] }, "do": [] },
{ "id": "refused", "when": { "persuade": { "by": 4, "target": 18 } },
  "if": { "vars": { "131": 0 }, "kills_below": { "4": 50 } }, "do": [] }
```

The refusal fires only once and the success needs `not_fired: refused`, so after a refusal neither holds in that stage. Persuade leaves the menu and the story goes as it was. To let the player try again, give the refusal `"repeat": true` and drop the success's `not_fired`.

## Across stages: flags

`fired` covers one stage and is cleared for the next. To let a later stage know what happened, write `"set_flags": ["name"]` on the event (lower-case letters, digits, `_` and `-`, up to 32 characters). The flag is set when the event has run. Flags belong to your package alone and have nothing to do with the original story variables. They follow the suspend and intermission saves and are cleared only by a new game.

```json
{ "id": "persuade", "scene": 129, "set_flags": ["persuaded"], "when": { "persuade": { "by": 4, "target": 18 } }, "do": [] },
{ "id": "comes-along", "scene": 81, "when": { "after": "opening" },
  "if": { "roster": { "pilots": [18] }, "flags": ["persuaded"] },
  "do": [ { "say": 18, "text": { "ja": "「ドモン、遅れを取るでないぞ！」", "en": "“Domon, don't fall behind!”" } } ] }
```

The roster is checked as well because the original story or another event may take him off the roster later. With the flag alone he would speak even when he is gone.

## do

| Action | Meaning |
| --- | --- |
| `{"say": 18, "text": {...}, "mode": 0}` | One line (below) |
| `{"join": {"pilot": 18, "unit": 27}}` | The character and unit join the player |
| `{"remove": {"pilot": 18, "unit": 27}}` | Take them off the roster (the unit must have left the map first) |
| `{"leave": 18}` | The character's unit leaves the map (not shot down) |
| `{"leave_group": 11}`, `{"deploy": 11}` | One of the stage's original deployment groups leaves or arrives |
| `{"set": {"131": 3}}` | Set story variables |
| `{"if": {"var": 128, "is": 2}, "then": [...], "else": [...]}` | Branch on a story variable |
| `{"focus": 18}` | Move the camera to the character |
| `{"bgm": 11}`, `{"sound": 124}` | Change the music, play a sound |
| `{"fade": "out"}`, `{"fade": "in"}`, `{"wait": 30}` | Fade out, fade in, wait (frames) |
| `{"close": "window"}` | Close the dialogue window |
| `{"op": "3D35", "args": [2335, 1]}` | An original script command as is; see the [Stage script reference](../script-commands/). Original lines can be used directly, as in `{"op": "3D40", "args": [43215]}` |

**Lines.** `text` must have `ja`. Other languages left out show the Japanese (Traditional Chinese falls back to Simplified). A line holds up to 200 characters and the reader pages it itself, so do not write `<STOP>`. Write the quotation marks yourself, as the original lines have them: 「」 in Japanese and Traditional Chinese, “ ” in Simplified Chinese and English. `mode` is how the original dialogue shows the line: on the map usually 0 for the player and 2 for the enemy, and on the world map 0 and 1 alternate top and bottom.

**The level on joining** follows the original rule, the stage's base level (from the average of the player's top 15). `"level": {"offset": 3}` adds to it or takes from it, and `"level": {"like": 4}` gives character 4's level at the moment of joining. SP, abilities, skills and spirits come from the original by level. To change the base values use `data/pilots.json` (enemy characters often have no SP or spirits; the example adds SP).

## Things to know

- **Original events read only the original variables.** To stop an original event or make it play another part, change the variable it checks. In the example, persuasion sets `131 = 3`, and the original ending plays Master Asia's farewell only when 131 is 1 or 2.
- **A unit leaving (`leave`) does not fire the original shot-down event.** What the original does after the defeat (in the example, Wong arriving and the victory condition opening up) your events must do themselves.
- Before moving a storyline to another stage, read that stage's original events: which variable the conversation sets, what follows a defeat, and which variables the ending event plays which parts by. The example's OZ-route events were adapted from scene 137's.
- **Flags are kept beside the saves** (`story-state/` in the `saves/` folder), not in the save files themselves. Take a save to another emulator or delete that folder, and the flags are gone, so events across stages will not fire. Events within one stage are not affected.
- Story mods do not run in custom campaign stages (`campaign/`).

## Checking and packing

```bash
python3 tools/mods/story.py compile story/master.json -o out.json
```

```bash
python3 tools/mods/pack.py yourname.story -o build/mods
```

The first compiles one story script so you can see the commands each event becomes. The second packs the package and checks it by the game's rules. A package the game cannot read is reported in the log as `SRW64_STORY_MOD_PROBLEM`.

The repository's [`docs/guide/story-mods/`](https://github.com/dyzz/srw64-recomp/tree/main/docs/guide/story-mods) folder has the full format and the scene table, which you can hand to an AI assistant to write story scripts from your ideas.
