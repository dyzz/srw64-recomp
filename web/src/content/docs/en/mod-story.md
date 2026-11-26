---
title: Story mods
summary: Add events to the original stages, saying when they fire, what must hold, what they do and the new lines they speak. Characters can join or stay and story branches can change, without taking any of the original event slots or touching an original event.
section: mods
order: 5
updated: 2026-11-26
---

A story mod adds events to the original stages. On a given turn, when someone is shot down, when two characters fight, when Persuade is chosen and so on, it plays the lines you wrote, has characters join or leave, or sets story variables so that the original story afterwards takes another branch. The game decides when an event fires. You write only the conditions and what happens, and you need not know the original event tables. No original event is changed or pushed out.

Installing and managing packages is in [Mod packages: installing and making them](../mods/). Version 0.5.3 or later is needed; the conditions and actions marked “0.5.4” below need 0.5.4 or later.

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
| `{"defeated": {"who": 18, "by": 4}}` | 18 is shot down by 4 (4 is the other side of the fight). 0.5.4 |
| `{"downed": {"side": "player", "group": 2, "who": 18, "at_least": 2}}` | Enough units have been shot down in this stage so far (write at least one of `side`, `group` and `who`; `at_least` defaults to 1). Only units shot down in battle count, not ones that left. 0.5.4 |
| `{"group_gone": 3}` | The stage's original group 3 has been shot down (at least one of it was shot down and none is left on the map). 0.5.4 |
| `{"reach": {"who": 18, "area": [10, 4, 12, 6]}}` | 18 enters this area (the squares of two corners, edges included); with `"side": "player"` in place of `who`, any unit of that side. A unit already standing inside counts. 0.5.4 |
| `{"remaining": {"side": "player", "at_most": 2}}` | `side` can be `player` too: two or fewer player units remain. 0.5.4 |
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
| `"turn": {"from": 3, "to": 5}` | The current turn is in this range (either end can be left out). 0.5.4 |
| `"on_map": [18]`, `"defeated": [18]`, `"absent": [18]` | These characters are on the map / have been shot down in this stage / are not on the map. 0.5.4 |
| `"hp": {"who": 18, "below": 50}` | On the map, with HP below 50%. 0.5.4 |
| `"count": {"side": "enemy", "at_most": 3, "at_least": 1}` | How many units of this side are on the map. 0.5.4 |
| `"in_area": {"who": 18, "area": [x0, y0, x1, y1]}` | In this area right now (`side` works too, as for `reach`). 0.5.4 |
| `"downed": {…}` | As `downed` under when. 0.5.4 |
| `"route": [0, 1]` | The protagonist route (0 Brad, 1 Manami, 2 Ark, 3 Selain). 0.5.4 |
| `"choice": 2` | The last choice was the second option. 0.5.4 |
| `"funds": {"at_least": 50000}` | Funds (`below` works too). 0.5.4 |
| `"any": [{…}, {…}]`, `"all": [...]`, `"not": {…}` | One of them holds / all hold / it does not hold. Inside go the same conditions, nested as deep as you like. 0.5.4 |

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
| `{"say": 18, "text": {...}, "mode": 0}` | One line (below). The speaker can also be a package's new character, `"@name"`, with its own name plate and portrait (0.5.4) |
| `{"say": 36, "silhouette": true, "text": {...}}` | Speak as the original's silhouette (named “???”, with a black portrait). Only the 33 characters the original has a silhouette for can (Banjo, Daisaku, Amuro, Chibodee and others); anyone else is a compile error. 0.5.4 |
| `{"join": {"pilot": 18, "unit": 27}}` | The character and unit join the player |
| `{"remove": {"pilot": 18, "unit": 27}}` | Take them off the roster (the unit must have left the map first) |
| `{"remove_unit": {"pilot": 166, "unit": 228}}` | Take only the unit off; the character stays on the roster. The character must be in that unit. 0.5.4 |
| `{"replace": {"pilot": 171, "unit": 224, "with": "@final"}}` | Change units: the old unit on the roster becomes the new one, the character gets in, and upgrade steps move over by the original's carry-over table. The new unit is written `@name` (see [New unit, character and weapon mods](../mod-units/)); for upgrades to carry over, give the new unit `inherits_from`. 0.5.4 |
| `{"unlock": 881}` | Unlock a weapon the story unlocks (`@new weapon` works too). Changing units does not carry the old weapon's unlock over; unlock the new weapon again. 0.5.4 |
| `{"build": "@final"}` | When the event starts, build a combined unit (a new unit with `combine`) for the player: the crew is each part's main pilot, the upgrade steps follow `inherits_from`, and it does not deploy. A later `unlock` in the same event can unlock its weapons; when there is one already, none is built. 0.5.4 |
| `{"crew": {"unit": "@final", "pilots": [171, 168, 170, 169, 166]}}` | When the event has run, make these characters the unit's crew (up to 5; the first is the main pilot and must already be in it). For a package's new unit, `crew` in `new_units.json` suits better: it is put on when the units change. 0.5.4 |
| `{"leave": 18}` | The character's unit leaves the map (not shot down) |
| `{"leave_group": 11}`, `{"deploy": 11}` | One of the stage's original deployment groups leaves or arrives |
| `{"deploy": "rival"}`, `{"leave_group": "rival"}` | The package's own reinforcement group (`deployments`, below) arrives or leaves |
| `{"set": {"131": 3}}` | Set story variables |
| `{"if": {"var": 128, "is": 2}, "then": [...], "else": [...]}` | Branch on a story variable |
| `{"if": {…conditions…}, "then": [...], "else": [...]}` | Branch on any of the conditions under “if”, written as in an event's `if`. They are judged before the run reaches this step, so the step just before it (a `join`, say) may not count yet. 0.5.4 |
| `{"choose": {"text": {"ja": ["はい", "いいえ"], "en": ["Yes", "No"]}, "options": [[...], [...]]}}` | Let the player choose (2–3 options); `options` lists, in order, what each option does. `text` can also be the text number of an original choice. Options you write take up to 24 characters each and need `ja`. Ask the question with a `say` before it. 0.5.4 |
| `{"focus": 18}` | Move the camera to the character |
| `{"bgm": 11}`, `{"sound": 124}` | Change the music, play a sound |
| `{"fade": "out"}`, `{"fade": "in"}`, `{"wait": 30}` | Fade out, fade in, wait (frames) |
| `{"close": "window"}` | Close the dialogue window |
| `{"op": "3D35", "args": [2335, 1]}` | An original script command as is; see the [Stage script reference](../script-commands/). Original lines can be used directly, as in `{"op": "3D40", "args": [43215]}` |

**Lines.** `text` must have `ja`. Other languages left out show the Japanese (Traditional Chinese falls back to Simplified). A line holds up to 200 characters and the reader pages it itself, so do not write `<STOP>`. Write the quotation marks yourself, as the original lines have them: 「」 in Japanese and Traditional Chinese, “ ” in Simplified Chinese and English. `mode` is how the original dialogue shows the line: on the map usually 0 for the player and 2 for the enemy, and on the world map 0 and 1 alternate top and bottom.

**The level on joining** follows the original rule, the stage's base level (from the average of the player's top 15). `"level": {"offset": 3}` adds to it or takes from it, and `"level": {"like": 4}` gives character 4's level at the moment of joining. SP, abilities, skills and spirits come from the original by level. To change the base values use `data/pilots.json` (enemy characters often have no SP or spirits; the example adds SP).

## The package's own reinforcements

A story file's top level can have `deployments`: deployment groups of the package's own (enemies can use the package's new units and characters too). Events bring one in with `{"deploy": "<id>"}` and take it off with `{"leave_group": "<id>"}`:

```json
"deployments": [
  { "id": "rival", "scene": 78, "units": [
    { "x": 10, "y": 9, "pilot": "@rival", "unit": "@rz", "level": 1 },
    { "x": 12, "y": 10, "pilot": 289, "unit": 21, "side": "enemy" } ] }
]
```

```json
{ "id": "rival-comes", "scene": 78, "when": { "turn": { "turn": 2, "side": "player" } },
  "do": [ { "deploy": "rival" } ] }
```

- `id` uses lowercase letters, digits and `- _ .`; the events using it must be in the same stage (`scene`). 1 to 30 units a group.
- Each unit has `x`, `y`, `pilot` (a number or `@new character`) and `unit` (a number or `@new unit`); optionally `side` (`enemy`, the default; `third`, a third party; `third_hostile`, a third party that also attacks you; `player`), `level` (0 to 20 over the stage's base level) and `upgrade` (upgrade step 0 to 5).
- Characters numbered 287 and up (and a package's new characters) are made as generic pilots; a character below 287 who is already on the player's roster is that same character from the roster.
- The game picks the group number from those the stage leaves unused; when there is no room the group is left out and the events naming it do not fire.

## Battle lines

Beside `events`, `battle_lines` adds new lines to battle animations (0.5.4):

```json
"battle_lines": [
  { "id": "see", "scene": 129, "who": 36, "against": 287, "when": ["attack", "evade"],
    "text": { "ja": "「見える！」", "en": "“I can see it!”" }, "once": true }
]
```

- `when` is attacking `attack`, shot down `defeated`, heavy damage `heavy`, medium damage `damaged`, light damage `light`, evading `evade`, no effect `no_effect`, out of ammo or EN `no_ammo` and out of range `out_of_range`: one, or a list.
- `against` is the opponent; without it the line is for anyone. `if` (the same conditions as an event's) works too, and `once` has it said only once in the stage. A line holds up to 60 characters.
- At those moments this line takes the place of the one the original would say. Only characters the original gives battle lines say them; where the original picks a special line, such as a combination attack's talk, it may still be the original's. A package's new characters have their own battle lines, written another way; see [New unit, character and weapon mods](../mod-units/#battle-lines-for-new-characters).

## Hidden elements on the stage information page

A story file's top level can have `stage_info`: hidden elements added to a stage's information page, beside the original's:

```json
"stage_info": [
  { "id": "final", "scene": 78,
    "title": { "ja": "ファイナルダンクーガ", "zh-Hans": "终极断空我", "en": "Final Dancouga" },
    "text":  { "ja": "クリア時にアランとブラックウイングがいれば入手", "zh-Hans": "通关时阿兰和黑翼在队即可获得", "en": "Clear with Alan and Black Wing on your side" },
    "done":  { "units": ["@final"] },
    "inherit": { "after": [78], "do": [ { "build": "@final" } ] } }
]
```

- `scene` is the stage; `title` and `text` go by language, `ja` required, up to 200 characters each. A stage with several route cards gets it on every card.
- `done` is optional; with it the roster decides: when the `units` (numbers or `@name`) and `pilots` (numbers) are all on your side it shows as "Done", otherwise "Not started".
- `id` (lowercase letters, digits and `- _ .`, unique in the package) is optional; with it the entry joins the cross-playthrough record like the original's hidden elements: once `done` held in any playthrough, the page marks it "Finished before".
- `inherit` carries it into a later playthrough: with the difficulty rule "New game plus" on, the entry in the record and its `done` not yet true, the intermission after a stage of `after` (or before one of `before`) runs `do`. `do` takes only `join`, `replace`, `unlock`, `crew` and `build`, and changes no story variable. `inherit` needs `id` and `done`. With its package removed or turned off the entry is neither granted nor shown, but the record keeps it for when the package comes back.

## Lines in text files

When there is a lot of dialogue, or someone else translates it, `text` (and a stage information entry's `title` and `text`) can be `"@name"`, with each language's words in the package's `story/<locale>/*.txt`:

```json
{ "say": "@kai", "text": "@kai-joins" }
```

```text
# story/ja/kai.txt
@kai-joins カイ
カイ、出撃する！

# story/zh-Hans/kai.txt
@kai-joins 凯
凯，出击！
```

- The format is the shipped dialogue files': `@name note` starts an entry and the next line is the line itself; `#` is a comment and blank lines are free. A line starting with `@`, `#` or `\` takes a `\` in front. Names use lowercase letters, digits and `- _ .` only.
- One line per entry: the reader pages and wraps it. Up to 200 characters.
- `ja` must have the entry; other languages fall back to `ja` (Traditional Chinese to Simplified). A name appears once per language, over as many files as you like.
- Packing builds the words into the story file and leaves the `.txt` files out. Writing `{"ja": …}` in the JSON still works, and the two can be mixed.

## Things to know

- **Original events read only the original variables.** To stop an original event or make it play another part, change the variable it checks. In the example, persuasion sets `131 = 3`, and the original ending plays Master Asia's farewell only when 131 is 1 or 2.
- **A unit leaving (`leave`) does not fire the original shot-down event.** What the original does after the defeat (in the example, Wong arriving and the victory condition opening up) your events must do themselves.
- Before moving a storyline to another stage, read that stage's original events: which variable the conversation sets, what follows a defeat, and which variables the ending event plays which parts by. The example's OZ-route events were adapted from scene 137's.
- **Flags are kept beside the saves** (`story-state/` in the `saves/` folder), not in the save files themselves. Take a save to another emulator or delete that folder, and the flags are gone, so events across stages will not fire. Events within one stage are not affected.
- Story mods do not run in custom campaign stages (`campaign/`).

## Checking and packing

The compiler is the same one the site's level editor runs (TypeScript): the command line needs [Node.js](https://nodejs.org/) 23 or later, which `python3` calls for you.

```bash
python3 tools/mods/story.py compile story/master.json -o out.json
```

```bash
python3 tools/mods/pack.py yourname.story -o build/mods
```

The first compiles one story script so you can see the commands each event becomes. The second packs the package and checks it by the game's rules. A package the game cannot read is reported in the log as `SRW64_STORY_MOD_PROBLEM`.

The repository's [`docs/guide/story-mods/`](https://github.com/dyzz/srw64-recomp/tree/main/docs/guide/story-mods) folder has the full format and the scene table, which you can hand to an AI assistant to write story scripts from your ideas.
