---
title: Mini stages
summary: Describe a map, the units on it and the event scripts in one JSON file, then jump straight into it in place of a scenario to reproduce a battle, a screen or a script command. Loaded through MCP only.
section: tools
order: 2
updated: 2026-10-06
---

A mini stage is a small scenario you write yourself: pick one of the original maps, place the allied and enemy units you need, and add a few event scripts (opening dialogue, turn start, reinforcements, victory conditions and so on). Once loaded, the game borrows a scenario’s slot and goes straight into your stage, skipping New Game, the prologue and hero selection, so you are on the map within seconds.

We use them during development to reproduce particular battles and screens: two specific units side by side to check the hit rates on the pre-battle screen, an enemy placed off screen to test L2/R2 cycling, or a single story command run on its own to see exactly what it does. The repository has more than sixty ready-made stages to learn from.

Mini stages are a debugging tool and **the game has no menu for them**: they can only be loaded through MCP, with the debug interface turned on. See [Controlling the game with an AI agent (MCP)](../mcp/) for how to connect.

## For AI agents: llms.txt

[`/docs/mini-stage/llms.txt`](/docs/mini-stage/llms.txt) puts everything needed to write a mini stage in one plain-text file for AI agents: the workflow, every limit the compiler checks, where to look up character and unit numbers, and the full reference of commands, conditions, markers, event types and deployment record fields (English names with the Chinese implementation notes and findings). Once the game is connected, hand the address to your agent:

> Read https://srw64.dreamquest.club/docs/mini-stage/llms.txt, then write a mini stage with one allied unit and two enemies next to it, compile it, load it with srw64_mini_stage_load and take a screenshot once inside.

## What you need

- The game connected as described in [the MCP guide](../mcp/) (`srw64_attach` works).
- The [source repository](https://github.com/dyzz/srw64-recomp) and Python 3, to compile a stage definition into the image the game reads.
- If the stage refers to original deployment records or events (`template`, `deployments_from` and `copy_from` below), also put your own ROM in the repository as `rom.z64` and run `make recomp-data` once to extract the original data from it. Stages that only use fields you write yourself do not need this.

## 1. Write the stage definition

A stage definition is a JSON file whose `schema` is `srw64.mini-stage.v1`. Below is a trimmed version of the repository’s `move-jump.json`: one allied and one enemy unit on map 20; the opening points the camera at the ally and hands over to the player phase.

```json
{
  "schema": "srw64.mini-stage.v1",
  "name": "my-test",
  "note": "Free text: what this stage tests",
  "map": 20,
  "deployments": [
    {"template": "base:stage_deployments:001f0f1c", "group": 0, "x": 8, "y": 8, "faction": 0},
    {"template": "base:stage_deployments:001f2720", "group": 1, "x": 12, "y": 8, "faction": 1}
  ],
  "events": [
    {"name": "opening", "type": 12, "header": [0, 0, 0, 0], "commands": [
      {"op": "3DD0"},
      {"op": "3D32", "args": [4]},
      {"op": "3D4D"},
      {"op": "3D65", "args": [17, 31]},
      {"op": "3D3B", "args": [1]},
      {"op": "3D45", "args": [0]},
      {"op": "3D45", "args": [1]},
      {"op": "3D35", "args": [16384, 0]},
      {"op": "3D48"}
    ]},
    {"name": "ending", "type": 14, "header": [0, 0, 0, 0], "commands": [
      {"op": "3DD0"},
      {"op": "3D4B", "args": [4]}
    ]}
  ]
}
```

The fields:

| Field | Meaning |
|---|---|
| `map` | Original map number (0–255). |
| `slot` | Optional. Which scenario’s slot (scene number) to borrow; without it, the scenario the game registers after loading. |
| `deployments_from` | Optional. Copies a scenario’s whole original deployment block, for example `base:stage_auxiliary:001e4268` for scenario 1. |
| `deployments` | Extra deployment records. `group` is the arrival group (what `3D45 <group>` brings in), `x`/`y` the square, `faction` 0 for allies and 1 for enemies; `template` starts from an original record, and fields such as `unit`, `actor`, `upgrade` and `behavior` change what you need. |
| `initial_resources` | Optional. Sets some units’ HP and EN to a percentage of their maximum on reaching the map: `{"side": 1, "slot": 0, "hp_percent": 30, "en_percent": 100}`. |
| `events` | Up to 63 events. Each has a `type` (0–14; for example 12 opening, 13 initial deployment, 0 turn start, 7 enemy count, 14 ending), four trigger parameters in `header`, and a list of `commands` written as `{"op": "3D45", "args": [1]}`. `copy_from` copies an original event as is. |

Every command, condition, event type and deployment record field is listed in the [stage script reference](../script-commands/); the research behind them is in the repository (in Chinese): [stage scripts and event types](https://github.com/dyzz/srw64-recomp/blob/main/docs/script/stage-script-exploration.md), [how commands are written](https://github.com/dyzz/srw64-recomp/blob/main/docs/script/script-debug-injection.md) and [the mini stage research notes](https://github.com/dyzz/srw64-recomp/blob/main/docs/script/mini-stage.md). The easiest start is to copy the closest stage in [`config/recomp/mini-stages/`](https://github.com/dyzz/srw64-recomp/tree/main/config/recomp/mini-stages); each file’s `note` says what it tests.

## 2. Compile it into an image

In the repository folder, run:

```bash
python3 tools/recomp/script_lab/mini_stage.py compile my-test.json --out my-test.image.json
```

The image (`srw64.mini-stage-image.v1`) is the format the game reads. More than 63 events, or events or deployments larger than the game’s buffers, stop the compiler with the reason.

The image contains original data copied from your ROM. Use it on your own computer only and don’t share it.

## 3. Load and enter it

Leave the game on the title’s main menu (the New Game / Continue level), then have the agent load the image:

> Wait for the title main menu, load /your/path/my-test.image.json with srw64_mini_stage_load, and take a screenshot once the stage is ready.

- The `path` given to `srw64_mini_stage_load` is a path **on the machine the game runs on**. With a Steam Deck, copy the image to the Deck first.
- The game enters the stage at once: the opening event plays, then it waits on the map for you. `mini_stage.ready` in `srw64_status` turns `true` when you can act.
- Outside the title main menu, or with an unreadable or invalid file, loading is refused with the reason.
- An installed game loads compiled images only. A development session started from source with `srw64_launch` can load a stage definition directly (the game calls the compiler itself); `srw64_launch`’s `mini_stage` argument brings a stage in at start, entered with F8 on the main menu.

From there, use the tools in [the tool list](../mcp/#tools): pick units, attack, look at the pre-battle screen, take screenshots or record.

## Things to keep in mind

- **It uses your own save folder.** Saving in the stage, or autosaves if they are on, write to your usual save slots. Back up `saves/` in your user folder first, or use a separate folder with `--user-dir` as described in the MCP guide.
- **No hero selection.** The protagonist’s and partner’s names and route are empty; set them in the stage if an event needs them, or start a normal new game first.
- **Random numbers differ from a normal game.** Entering directly comes a few hundred frames earlier than a normal new game, so enemy actions and hit rolls change. To reproduce a result, start again from the same image with the same inputs.
- **One stage at a time.** Loading another replaces the first. When you are done, quit with `srw64_quit`, or return to the title and load the next one.
