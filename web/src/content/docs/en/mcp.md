---
title: Driving the game with an AI agent (MCP)
summary: Connect Claude Code or any other MCP client to the running game to read its state, take screenshots, press keys, work the menus and read or write memory.
section: tools
order: 1
updated: 2026-10-06
---

Marchwind 64 has a built-in debug interface. Turn it on in the options and the game opens a port on your computer. The MCP server in the source repository connects to that port and offers the game as a set of tools to any client that speaks the [Model Context Protocol](https://modelcontextprotocol.io/), such as Claude Code, Codex or Cursor.

Once connected, an agent can:

- see what the game is doing: the title, dialogue, intermission screens, the pre-battle screen, the settings window;
- take screenshots and record MP4 clips;
- press keys like a player, or use the buttons and text fields of the native screens directly;
- change the language, image mode, rules and interface size;
- read and write game memory, for debugging.

This is how we test the game ourselves: agents play through scenes, reproduce bugs and check translated layouts. You can use it the same way, to script runs, record footage or have an agent look into a problem you hit.

## What you need

- **The game** for Windows, macOS, Linux or Steam Deck.
- **Python 3.** The one that comes with your system is fine. No extra packages are needed. On Windows, install it from [python.org](https://www.python.org/downloads/); the command is `python`.
- **The MCP server script.** It lives in the [source repository](https://github.com/dyzz/srw64-recomp). Cloning is enough; nothing needs to be built:

  ```bash
  git clone https://github.com/dyzz/srw64-recomp.git
  ```

  The files used are `tools/recomp/debug/mcp_server.py` and `session.py` next to it.
- **An AI client with MCP support.**

## 1. Turn on the debug interface in the game

Open **Options → About** and set **AI Debug Interface (MCP)** to **On**. It takes effect at once, no restart needed. The setting is saved, and every time the game starts a notice at the top of the screen reminds you it's still on. Turn it off in the same place when you're done; any open connection ends immediately.

While it's on, the row shows the address the game listens on and the folder of this run, with a **Copy Run Folder** button that copies the full path.

The game opens a port on this computer only (`127.0.0.1`) and writes `debug.json` to the run folder, with the port number and a random token. A client has to present the token before anything else, and only your user account can read the file. The file is deleted when the game exits or the switch is turned off.

<details>
<summary>You can also turn it on with <code>--debug</code> at launch (for that run only)</summary>

- **macOS:** `/Applications/Marchwind64.app/Contents/MacOS/srw64-gfx-host --play --rom ~/Games/srw64.z64 --debug` (`.z64`, `.v64` and `.n64` all work)
- **Windows:** run `Marchwind64.cmd --debug` in the extracted folder
- **Linux:** `./marchwind64.sh --debug`
- **Steam Deck:** in Steam, set the game's launch options to `%command% --debug`

Turned on this way, the switch in the options shows **On** and can't turn it off for that run.

</details>

The run folder on each system:

| System | Run folder |
|---|---|
| Windows | `%LOCALAPPDATA%\SRW64Recomp\sessions\<session>\run\` |
| macOS | `~/Library/Application Support/SRW64Recomp/sessions/<session>/run/` |
| Linux / Steam Deck | `~/.local/share/srw64-recomp/sessions/<session>/run/` |

## 2. Add the MCP server to your client

The MCP server is a Python script that talks over standard input and output.

**Claude Code:**

```bash
claude mcp add srw64 -- python3 /path/to/srw64-recomp/tools/recomp/debug/mcp_server.py
```

**Other clients** mostly take JSON like this; check your client's docs for where the file lives and the exact top-level key:

```json
{
  "mcpServers": {
    "srw64": {
      "command": "python3",
      "args": ["/path/to/srw64-recomp/tools/recomp/debug/mcp_server.py"]
    }
  }
}
```

On Windows, use `python` instead of `python3`, and a path such as `C:\\path\\to\\srw64-recomp\\tools\\recomp\\debug\\mcp_server.py` (backslashes are doubled in JSON).

Your client should now list a set of tools whose names start with `srw64_`.

## 3. Connect and start working

Just ask the agent to connect. It doesn't need a path:

> Use srw64_attach to connect to the game, then take a screenshot and tell me which screen it's on.

Without arguments, `srw64_attach` looks in your user folder for games with the debug interface on and connects to the newest one that answers. If several games are running, or you want a particular one, pass the path from **Copy Run Folder** as its `run` argument.

After that, give it tasks in plain language:

- "Wait for the main menu, start a new game and fast-forward to the first choice."
- "Switch the language to English and compare a screenshot with the Chinese version of the same screen. Is any text overflowing?"
- "Record 10 seconds. I want to see where this battle animation stutters."
- "The dialogue is stuck. Read the status and the recent event log and tell me which script command it stopped on."

Agents usually read `srw64_status` and a screenshot to work out where they are, then use `srw64_wait` to wait for a condition instead of guessing at delays.

## Tools

| Tool | What it does |
|---|---|
| `srw64_attach` | Connect to a running game. Without arguments it finds the newest one; pass a run folder as `run` to pick one. |
| `srw64_launch` | Build from source and start an isolated development session (needs a full build setup; see below). |
| `srw64_status` | Current state: frame count, window, language, image mode, rules, title and opening, dialogue (page, text size, speed, fast-forward), the native screens, notices, held keys. |
| `srw64_screenshot` | Capture the next frame with native overlays and return it as an image; can also capture other windows such as the settings window. |
| `srw64_record` | Record the next few seconds to MP4 and return the file path. |
| `srw64_record_start` / `srw64_record_stop` | Start and stop an open-ended recording. |
| `srw64_wait` | Wait until conditions hold: a frame count, dialogue showing, a screen opening, the main menu, given text in the dialogue, or a kind of event in an event log. |
| `srw64_events` | Read an event log (dialogue, opening, rules, settings, intermission screens, script and more). |
| `srw64_keys` | The game keyboard. Always the classic layout, whatever the player has bound: Z = A, X = B, Enter = START, arrow keys, Q/E = L/R, I/K = C-up/down, WASD = stick. Takes key combinations and hold times, such as `e+z` to fast-forward. |
| `srw64_pad` | A virtual controller with Steam Deck button names, acting as the default bindings do (View opens the settings, and so on). |
| `srw64_buttons` | Press N64 controller buttons directly, below the keyboard layer. |
| `srw64_ui_tree` | List the controls of the native screens (name page, settings window, menu bar) with their positions and text. |
| `srw64_click` / `srw64_type` / `srw64_ui_key` | Click a control by its text or position, type into a text field (input method composition included), send keys to the interface. |
| `srw64_menu` | Press a menu bar item by its path, or list a menu. |
| `srw64_window` | Resize the window, bring it to the front or close it. |
| `srw64_settings` | Set the rules, language, image mode, interface size, aspect ratio and the original/modern mode of each screen directly. |
| `srw64_mini_stage_load` | Load a compiled mini stage image and enter it; see [Mini stages](../mini-stage/). |
| `srw64_viewer_start` | Play a chosen battle animation in the Battle Viewer. |
| `srw64_memory` / `srw64_memory_write` | Read or write game memory as hex. Writes are limited to 4096 bytes at a time. |
| `srw64_quit` | Quit the game normally and return the run report. |

When a tool fails it returns an error result; the MCP server keeps running.

## Things to keep in mind

- **This is your real game and your real saves.** The agent connects to the game you play, with your usual saves and settings, so an agent that saves or changes settings really changes them. To experiment safely, back up `saves/` in your user folder first, or point `--user-dir` at a separate folder and copy in the saves you need.
- **`srw64_memory_write` can break the game.** It changes memory behind the game's back. Use it only when you know what you're doing.
- **The interface is local only.** The game listens on `127.0.0.1`, so other devices on your network can't reach it, and other programs on your computer also need the token in `debug.json`. Don't share that file. Remote use goes through ssh forwarding (see below).
- **`srw64_quit` closes the game.** To disconnect and keep playing, turn the switch off in **Options → About** instead.
- **Turn the switch off when you're done.** It stays saved, which is what the notice at startup is for.

## Connecting to a Steam Deck

You need ssh access from your computer to the Deck (for example a host named `Deck` in `~/.ssh/config`), and the debug interface on in the Deck game's **Options → About** (or the game started with `--debug` as above). In the repository on your computer, run:

```bash
python3 tools/release/linux/attach.py --host Deck
```

It finds the running game on the Deck, forwards its port to your computer with `ssh -L` and makes it the current session. The agent can then call `srw64_attach` with no arguments. Screenshots, recordings and event logs are files the game writes on the Deck; they come back over the same connection automatically.

`attach.py --start` starts the game over ssh with `--debug`. Add `--data-dir '~/srw64-debug'` to give it a separate data folder: the ROM and HD pack are linked, the saves and settings are copied, so your own saves stay untouched.

## Android

On Android, turn it on in **Options → About** too. The phone reaches your computer through adb: enable USB debugging (or wireless debugging) in the phone's developer options, connect it with adb, then run this in the repository:

```bash
python3 tools/release/android/attach.py --no-start
```

It forwards the game's interface to your computer with `adb forward` and makes it the current session; then call `srw64_attach` as usual. Screenshots, recordings and event logs come back over the same connection. Android uses no token; your computer reaches the interface through the adb forward.

## For developers: starting from source

With a full local build setup (see the build docs in the repository), `srw64_launch` builds as needed and starts a separate debug session. Its run folder sits under `build/recomp/debug/` in the repository and never touches your play saves. It can also set the language, image mode, rules, a save or a mini stage on launch. The session ends when the MCP server exits.

`docs/guide/debug-interface.md` in the repository covers the rest: the `srw64ctl.py` command line, the Python scripting interface and what the interface can reach.
