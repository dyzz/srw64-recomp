#!/usr/bin/env python3
"""MCP server for debugging the native host (docs/guide/debug-interface.md).

Standard library only: MCP over stdio is newline-delimited JSON-RPC 2.0, and
the project environment has no MCP package. Registered for Claude Code in the
repository's .mcp.json.
"""
from __future__ import annotations

import base64
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))  # tools/, home of the recomp package
from recomp.debug.session import EVENT_LOGS, HostError, Session, run_keys  # noqa: E402

PROTOCOL_VERSIONS = ("2025-06-18", "2025-03-26", "2024-11-05")
INSTRUCTIONS = """Drive the SRW64 native host for debugging. Start with srw64_launch (or srw64_attach to a
session started with tools/recomp/debug/srw64ctl.py), then read srw64_status and take
srw64_screenshot. Two input layers:
- srw64_keys: the game keyboard (Z=A, X=B, Enter=START, arrows, E=R, Q=L, I/K=C-up/down,
  WASD stick, F6 images, F7 language, Esc quits). Goes through the same path as real keys,
  window focus not required. Reading controls: E+Z fast-forward, E+Enter skip, I/K text size.
- srw64_ui_tree / srw64_click / srw64_type / srw64_ui_key / srw64_menu: the shared SDL/RmlUi UI that
  replaces game screens (name page, settings window, menu bar), by generic keyboard and mouse.
Coordinates are points from the window's top-left; use srw64_ui_tree to find controls.
Every session runs in its own directory under build/recomp/debug/ and never touches play saves."""

WINDOW = {"description": "\"game\" (default), \"key\", a window number, or a title substring", "type": ["string", "integer"]}
TOOLS = [
    {"name": "srw64_launch", "description": "Build if needed and start a debug session of the game (interactive window, isolated run directory). Replaces the current session handle; an earlier game keeps running until quit.",
     "inputSchema": {"type": "object", "properties": {
         "language": {"type": "string", "enum": ["ja", "zh-Hans", "en"]},
         "images": {"type": "string", "enum": ["original", "hd"]},
         "rules": {"description": "\"fixed\" (default: corrections on), \"original\", \"all\", or a list of rule ids", "type": ["string", "array"]},
         "save": {"type": "string", "description": "path of a 32 KiB SRAM to start from"},
         "mini_stage": {"type": "string", "description": "mini stage definition to substitute for the first stage (F8 on the title menu)"},
         "reuse_build": {"type": "boolean", "description": "skip the rebuild when nothing changed since the last debug run"}}}},
    {"name": "srw64_attach", "description": "Use a running debug session (the latest one by default).",
     "inputSchema": {"type": "object", "properties": {"run": {"type": "string"}}}},
    {"name": "srw64_status", "description": "VI, window, locale, image mode, rules, title/intro state, the dialogue reader (page, text size, speed, history, skip) with its boxes, the name page, the Link Battler page, recent native notices (banners), native UI windows and focus, held keys.",
     "inputSchema": {"type": "object", "properties": {"history": {"type": "boolean", "description": "include the full dialogue history"}}}},
    {"name": "srw64_keys", "description": "Game keyboard. Either {press, hold_ms} or a list of steps: {press|down|up: \"e+z\", hold_ms}, {wait_ms}, {release_all: true}. Keys: z x space return up down left right q e i k j l w a s d escape f6 f7 f8.",
     "inputSchema": {"type": "object", "properties": {
         "press": {"type": "string"}, "hold_ms": {"type": "integer"},
         "steps": {"type": "array", "items": {"type": "object"}}}}},
    {"name": "srw64_buttons", "description": "N64 controller buttons directly, below the keyboard layer (a b z start up down left right l r c_up c_down c_left c_right), held for vis frames.",
     "inputSchema": {"type": "object", "required": ["buttons"], "properties": {
         "buttons": {"type": "string", "description": "e.g. \"r+start\""}, "vis": {"type": "integer", "minimum": 1, "maximum": 600}}}},
    {"name": "srw64_screenshot", "description": "Capture the next presented frame with the native overlays (name page) drawn on top, or render another window such as the settings panel.",
     "inputSchema": {"type": "object", "properties": {"window": WINDOW, "overlays": {"type": "boolean"}}}},
    {"name": "srw64_ui_tree", "description": "Visible native windows and their views: class, frame (points, top-left origin), text, enabled, focused.",
     "inputSchema": {"type": "object", "properties": {"window": WINDOW}}},
    {"name": "srw64_click", "description": "Click a native control by its text (button title, field text, label) or at x/y points.",
     "inputSchema": {"type": "object", "properties": {"text": {"type": "string"}, "x": {"type": "number"}, "y": {"type": "number"},
         "window": WINDOW, "button": {"type": "string", "enum": ["left", "right"]}, "count": {"type": "integer"}}}},
    {"name": "srw64_type", "description": "Type text into the focused native text field (click the field first). marked=true leaves it as an input-method composition; unmark=true commits the composition.",
     "inputSchema": {"type": "object", "properties": {"text": {"type": "string"}, "marked": {"type": "boolean"},
         "unmark": {"type": "boolean"}, "window": WINDOW}}},
    {"name": "srw64_ui_key", "description": "Send a key to the native UI (e.g. return, tab, escape, delete, arrows, a-z, f7) with optional modifiers.",
     "inputSchema": {"type": "object", "required": ["key"], "properties": {"key": {"type": "string"},
         "modifiers": {"type": "array", "items": {"type": "string", "enum": ["cmd", "shift", "option", "control"]}}, "window": WINDOW}}},
    {"name": "srw64_menu", "description": "Press a menu bar item by title path, or list a menu (empty path lists the bar).",
     "inputSchema": {"type": "object", "properties": {"path": {"type": "array", "items": {"type": "string"}}}}},
    {"name": "srw64_window", "description": "Resize the game window (640..2560 x 480..1600 points), bring it to the front (needed only to exercise physical-focus behaviour or real mouse events), or press its close button (the game then exits as a player closing the window would).",
     "inputSchema": {"type": "object", "properties": {"width": {"type": "integer"}, "height": {"type": "integer"}, "front": {"type": "boolean"},
         "close": {"type": "boolean"}}}},
    {"name": "srw64_settings", "description": "Change rules (preset or id list), language or image mode directly.",
     "inputSchema": {"type": "object", "properties": {"rules": {"type": ["string", "array"]},
         "locale": {"type": "string", "enum": ["ja", "zh-Hans", "en"]}, "images": {"type": "string", "enum": ["original", "hd"]},
         "battle_ui": {"type": "string", "enum": ["native", "original"]},
         "intermission_ui": {"type": "string", "enum": ["native", "original"]}}}},
    {"name": "srw64_mini_stage_load", "description": "Load a mini stage file (compiled image or source definition) at run time and enter it from the title menu, like dropping the file on the window.",
     "inputSchema": {"type": "object", "required": ["path"], "properties": {"path": {"type": "string"}}}},
    {"name": "srw64_wait", "description": "Wait until conditions hold: vi (at least), dialogue_active, intro_active, name_page, link_page, intermission_page, battle_page, title_major (3 = main menu), text (in the active dialogue), event {log, kind, count}.",
     "inputSchema": {"type": "object", "properties": {"until": {"type": "object"}, "timeout_s": {"type": "number"}}}},
    {"name": "srw64_events", "description": "Rows of an event log from a cursor. Logs: " + ", ".join(EVENT_LOGS) + ".",
     "inputSchema": {"type": "object", "required": ["log"], "properties": {"log": {"type": "string", "enum": list(EVENT_LOGS)},
         "since": {"type": "integer"}, "kinds": {"type": "array", "items": {"type": "string"}}}}},
    {"name": "srw64_quit", "description": "Quit the game normally and return the run report summary.",
     "inputSchema": {"type": "object", "properties": {}}},
]


class Server:
    def __init__(self):
        self.session: Session | None = None

    def need(self) -> Session:
        if self.session is None:
            try:
                self.session = Session.attach()
            except HostError as error:
                raise HostError(f"{error}. Call srw64_launch first.") from error
        return self.session

    def call_tool(self, name: str, args: dict) -> list[dict]:
        if name == "srw64_launch":
            self.session = Session.launch(**{k: args[k] for k in ("language", "images", "rules", "save", "mini_stage", "reuse_build") if k in args})
            return text({"run": str(self.session.run), "status": self.session.client.call("status")})
        if name == "srw64_attach":
            self.session = Session.attach(args.get("run"))
            return text({"run": str(self.session.run), "status": self.session.client.call("status")})
        session = self.need()
        client = session.client
        if name == "srw64_status":
            return text(client.call("status", history=bool(args.get("history"))))
        if name == "srw64_keys":
            steps = args.get("steps") or ([{"press": args["press"], **({"hold_ms": args["hold_ms"]} if "hold_ms" in args else {})}]
                                          if "press" in args else None)
            if not steps:
                raise HostError("srw64_keys needs press or steps")
            return text(run_keys(client, steps))
        if name == "srw64_buttons":
            return text(client.call("buttons", **args))
        if name == "srw64_screenshot":
            result = client.call("screenshot", **args)
            data = base64.b64encode(Path(result["path"]).read_bytes()).decode()
            return [{"type": "image", "data": data, "mimeType": "image/png"}] + text(result)
        if name == "srw64_ui_tree":
            return text(client.call("ui.tree", **args))
        if name == "srw64_click":
            return text(client.call("ui.click", **args))
        if name == "srw64_type":
            return text(client.call("ui.type", **args))
        if name == "srw64_ui_key":
            return text(client.call("ui.key", **args))
        if name == "srw64_menu":
            return text(client.call("menu", **args))
        if name == "srw64_window":
            return text(client.call("window", **args))
        if name == "srw64_settings":
            return text(client.call("settings", **args))
        if name == "srw64_mini_stage_load":
            return text(client.call("mini_stage.load", path=args["path"]))
        if name == "srw64_wait":
            return text(session.wait(timeout=float(args.get("timeout_s", 30)), **args.get("until", {})))
        if name == "srw64_events":
            rows, cursor = session.events(args["log"], int(args.get("since", 0)), args.get("kinds"))
            return text({"rows": rows, "next": cursor})
        if name == "srw64_quit":
            report = session.quit()
            self.session = None
            keys = ("status", "exit_code", "interactive", "rule_fixes", "native_log_path")
            return text({k: report[k] for k in keys if k in report} or report)
        raise HostError(f"unknown tool {name!r}")

    def handle(self, message: dict) -> dict | None:
        """One JSON-RPC message in; the response, or None for notifications."""
        method, ident = message.get("method"), message.get("id")
        if ident is None:
            return None  # notifications such as notifications/initialized
        try:
            if method == "initialize":
                wanted = (message.get("params") or {}).get("protocolVersion")
                result = {"protocolVersion": wanted if wanted in PROTOCOL_VERSIONS else PROTOCOL_VERSIONS[0],
                          "capabilities": {"tools": {}},
                          "serverInfo": {"name": "srw64", "version": "0.1.0"},
                          "instructions": INSTRUCTIONS}
            elif method == "ping":
                result = {}
            elif method == "tools/list":
                result = {"tools": TOOLS}
            elif method == "tools/call":
                params = message.get("params") or {}
                try:
                    result = {"content": self.call_tool(params.get("name", ""), params.get("arguments") or {}), "isError": False}
                except (HostError, ValueError, KeyError, OSError) as error:
                    result = {"content": text(f"{type(error).__name__}: {error}"), "isError": True}
            else:
                return {"jsonrpc": "2.0", "id": ident, "error": {"code": -32601, "message": f"unknown method {method}"}}
        except Exception as error:  # a protocol bug must not kill the server
            return {"jsonrpc": "2.0", "id": ident, "error": {"code": -32603, "message": str(error)}}
        return {"jsonrpc": "2.0", "id": ident, "result": result}


def text(value) -> list[dict]:
    return [{"type": "text", "text": value if isinstance(value, str) else json.dumps(value, ensure_ascii=False, indent=1)}]


def main() -> int:
    server = Server()
    for line in sys.stdin:
        if not line.strip():
            continue
        try:
            message = json.loads(line)
        except json.JSONDecodeError:
            response = {"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": "parse error"}}
        else:
            response = server.handle(message)
        if response is not None:
            sys.stdout.write(json.dumps(response, ensure_ascii=False) + "\n")
            sys.stdout.flush()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
