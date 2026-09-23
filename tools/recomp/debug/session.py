#!/usr/bin/env python3
"""Debug sessions of the native host (docs/guide/debug-interface.md).

A session is an interactive graphics run of the host with SRW64_DEBUG=1 in its
own directory under build/recomp/debug/. It never touches the play history or
the remembered settings of build/recomp/profile-play. The host answers
JSON-RPC 2.0 requests, one per line, on <run>/debug.sock.
"""
from __future__ import annotations

from datetime import datetime, timezone
import json
import shlex
import os
from pathlib import Path
import socket
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
DEBUG_DIR = ROOT / "build/recomp/debug"
CURRENT = DEBUG_DIR / "current"
PROFILE = ROOT / "config/recomp/profiles/play-profile.json"
EVENT_LOGS = {"dialogue": "dialogue-events.jsonl", "intro": "intro-events.jsonl", "name": "name-entry-events.jsonl",
              "rules": "rule-fixes-events.jsonl", "images": "image-mode-events.jsonl", "control": "control-events.jsonl",
              "script": "script-inject-events.jsonl", "mini_stage": "mini-stage-events.jsonl",
              "settings": "settings-window-events.jsonl", "refunds": "upgrade-refund-events.jsonl",
              "link": "link-events.jsonl", "intermission": "intermission-events.jsonl", "parts": "parts-page-events.jsonl"}


class HostError(RuntimeError):
    """The host refused a request or could not be reached."""


class Client:
    """Line-delimited JSON-RPC over the session's Unix socket."""

    def __init__(self, path: Path, timeout: float = 30.0):
        self.path = Path(path)
        self.timeout = timeout
        self._socket: socket.socket | None = None
        self._buffer = b""
        self._next_id = 0

    def _connect(self) -> socket.socket:
        if self._socket is None:
            connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            connection.settimeout(self.timeout)
            try:
                connection.connect(str(self.path))
            except OSError as error:
                connection.close()
                raise HostError(f"cannot reach the host at {self.path}: {error}") from error
            self._socket = connection
        return self._socket

    def close(self) -> None:
        if self._socket is not None:
            self._socket.close()
            self._socket = None
            self._buffer = b""

    def call(self, method: str, **params) -> dict:
        self._next_id += 1
        request = {"jsonrpc": "2.0", "id": self._next_id, "method": method, "params": params}
        connection = self._connect()
        try:
            connection.sendall(json.dumps(request, ensure_ascii=False).encode() + b"\n")
            while b"\n" not in self._buffer:
                chunk = connection.recv(65536)
                if not chunk:
                    raise HostError("the host closed the connection (did the game exit?)")
                self._buffer += chunk
        except OSError as error:
            self.close()
            raise HostError(f"lost the host connection: {error}") from error
        line, self._buffer = self._buffer.split(b"\n", 1)
        response = json.loads(line)
        if "error" in response:
            raise HostError(response["error"].get("message", "host error"))
        return response["result"]


def rule_fixes(rules) -> str:
    """The SRW64_RULE_FIXES value for a preset name, an id list or None (the play default)."""
    sys.path.insert(0, str(ROOT / "src"))
    from srw64_native import rule_settings
    if rules is None:
        return ",".join(rule_settings.DEFAULT)
    if isinstance(rules, str):
        if rules in rule_settings.PRESETS:
            return ",".join(rule_settings.PRESETS[rules])
        return ",".join(rule_settings.parse(rules))
    return ",".join(rule_settings.parse(",".join(rules)))


class Session:
    def __init__(self, run: Path, process: subprocess.Popen | None = None):
        self.run = Path(run)
        self.process = process
        self.client = Client(self.run / "debug.sock")

    # -- lifecycle ---------------------------------------------------------------

    @classmethod
    def launch(cls, language: str | None = None, images: str | None = None, rules=None, save: str | None = None,
               mini_stage: str | None = None, reuse_build: bool = False, audio: bool = False,
               timeout: float = 900.0, env: dict | None = None) -> "Session":
        """Build if needed and start a session; returns once the host listens."""
        DEBUG_DIR.mkdir(parents=True, exist_ok=True)
        run = DEBUG_DIR / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
        command = [sys.executable, str(ROOT / "tools/recomp/run/run_host_probe.py"), "--graphics", "--interactive",
                   "--diagnostics", "full", "--profile", str(PROFILE), "--output", str(run)]
        if language:
            command += ["--language", language]
        if images:
            command += ["--images", images]
        if save:
            command += ["--save-from", str(Path(save).resolve())]
        if audio:
            command.append("--audio")
        if reuse_build:
            previous = cls.previous_run()
            if previous is not None:
                command += ["--reuse-build-from", str(previous)]
        environment = dict(os.environ, SRW64_DEBUG="1", SRW64_RULE_FIXES=rule_fixes(rules))
        environment.pop("SRW64_MINI_STAGE", None)
        environment["SRW64_MINI_STAGE_COMPILER"] = shlex.join([sys.executable, str(ROOT / "tools/recomp/script_lab/mini_stage.py")])
        environment.pop("SRW64_NATIVE_NAME_ENTRY", None)
        if mini_stage:
            sys.path.insert(0, str(ROOT / "tools"))
            from recomp.script_lab.mini_stage import compile_stage
            image = run.parent / (run.name + ".mini-stage.json")
            image.write_text(json.dumps(compile_stage(json.loads(Path(mini_stage).read_text())), ensure_ascii=False) + "\n")
            environment["SRW64_MINI_STAGE"] = str(image)
        log = open(run.parent / (run.name + ".launch.log"), "w")
        process = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        session = cls(run, process)
        deadline = time.monotonic() + timeout
        while not (run / "debug.sock").exists():
            if process.poll() is not None:
                raise HostError(f"the host exited before listening; see {log.name}")
            if time.monotonic() > deadline:
                raise HostError(f"no debug socket after {timeout:.0f} s; see {log.name}")
            time.sleep(0.2)
        CURRENT.write_text(str(run) + "\n")
        return session

    def enter_mini_stage(self, timeout: float = 90.0) -> dict:
        """Enter via the current native title control, using observed states.

        No original-name-entry override, fixed-VI input recording or save needed.
        """
        self.wait(vi=600, timeout=timeout)
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            state = self.client.call("status")
            if not state.get("mini_stage", {}).get("available"):
                raise HostError("Launch with mini_stage before entering it")
            if state["intro"].get("title_major") == 3:
                break
            self.client.call("keys", press="return", hold_ms=100)
            time.sleep(0.5)
        else:
            raise HostError("Title menu was not reached")
        self.client.call("screenshot", path=str(self.run / "mini-menu.png"))
        self.client.call("ui.click", id="mini-enter")
        while time.monotonic() < end:
            state = self.client.call("status")
            if state.get("name_page", {}).get("visible"):
                raise HostError("Direct mini-stage entry exposed a name page")
            if state.get("mini_stage", {}).get("ready"):
                self.client.call("screenshot", path=str(self.run / "mini-ready.png"))
                (self.run / "direct-entry.json").write_text(json.dumps(state, ensure_ascii=False, indent=2) + "\n")
                return state
            time.sleep(0.1)
        raise HostError("Mini stage did not become ready")

    @classmethod
    def previous_run(cls) -> Path | None:
        """The newest finished debug run with a report, for build reuse."""
        runs = sorted((p for p in DEBUG_DIR.glob("*Z") if (p / "report.json").exists()), reverse=True) if DEBUG_DIR.exists() else []
        return runs[0] if runs else None

    @classmethod
    def attach(cls, run: str | Path | None = None) -> "Session":
        """The given run, or the one `launch` last started."""
        if run is None:
            if not CURRENT.exists():
                raise HostError("no debug session has been launched; run srw64ctl launch first")
            run = CURRENT.read_text().strip()
        session = cls(Path(run))
        if not (session.run / "debug.sock").exists():
            raise HostError(f"{session.run} has no debug socket (not a debug run, or it has ended)")
        return session

    def alive(self) -> bool:
        if self.process is not None:
            return self.process.poll() is None
        try:
            self.client.call("methods")
            return True
        except HostError:
            return False

    def quit(self, timeout: float = 30.0) -> dict:
        try:
            self.client.call("quit")
        except HostError:
            pass
        self.client.close()
        deadline = time.monotonic() + timeout
        report = self.run / "report.json"
        while time.monotonic() < deadline:
            if self.process is not None and self.process.poll() is not None and report.exists():
                break
            if self.process is None and report.exists():
                break
            time.sleep(0.2)
        return json.loads(report.read_text()) if report.exists() else {"status": "report not written yet"}

    # -- reading -----------------------------------------------------------------

    def events(self, log: str, since: int = 0, kinds: list[str] | None = None) -> tuple[list[dict], int]:
        """Rows of one event log from line `since`, and the cursor for the next call."""
        if log not in EVENT_LOGS:
            raise HostError(f"unknown log {log!r}; known: {', '.join(EVENT_LOGS)}")
        path = self.run / EVENT_LOGS[log]
        if not path.exists():
            return [], since
        lines = path.read_text().splitlines()
        rows = []
        for line in lines[since:]:
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                break  # a line being written; the next call picks it up
            if not kinds or row.get("kind") in kinds or row.get("action") in kinds:
                rows.append(row)
        return rows, len(lines)

    def wait(self, timeout: float = 30.0, poll: float = 0.1, **until) -> dict:
        """Poll status until every condition holds; returns the matching status."""
        deadline = time.monotonic() + timeout
        while True:
            status = self.client.call("status")
            if satisfied(status, until, self):
                return status
            if time.monotonic() > deadline:
                raise HostError(f"timed out waiting for {until}; last VI {status.get('vi')}")
            time.sleep(poll)


def satisfied(status: dict, until: dict, session: Session | None = None) -> bool:
    """Conditions: vi (at least), dialogue_active, intro_active, name_page (visible),
    link_page (visible), intermission_page (visible), battle_page (visible), parts_page (visible), title_major, text (substring of the
    active dialogue), event ({log, kind})."""
    dialogue = status.get("dialogue") or {}
    intro = (status.get("intro") or {}).get("step") or {}
    checks = {
        "vi": lambda value: status.get("vi", 0) >= value,
        "dialogue_active": lambda value: bool(dialogue.get("active")) == value,
        "intro_active": lambda value: bool(intro.get("active")) == value,
        "name_page": lambda value: bool((status.get("name_page") or {}).get("visible")) == value,
        "link_page": lambda value: bool((status.get("link_page") or {}).get("visible")) == value,
        "intermission_page": lambda value: bool((status.get("intermission_page") or {}).get("visible")) == value,
        "title_major": lambda value: (status.get("intro") or {}).get("title_major") == value,
        "text": lambda value: any(value in box.get("text", "") for box in dialogue.get("boxes", []) if box.get("active")),
        "battle_page": lambda value: bool((status.get("battle_page") or {}).get("visible")) == value,
        "parts_page": lambda value: bool((status.get("parts_page") or {}).get("visible")) == value,
    }
    for key, value in until.items():
        if key == "event":
            if session is None:
                return False
            rows, _ = session.events(value["log"], 0, [value["kind"]] if value.get("kind") else None)
            if len(rows) < value.get("count", 1):
                return False
            continue
        if key not in checks:
            raise HostError(f"unknown wait condition {key!r}")
        if not checks[key](value):
            return False
    return True


def run_keys(client: Client, steps: list[dict]) -> list[dict]:
    """Game keyboard steps: {"press": "e+return", "hold_ms": 150}, {"down": "e+z"},
    {"up": "e+z"}, {"release_all": true} or {"wait_ms": 500}. Returns each step's result."""
    results = []
    for step in steps:
        if not isinstance(step, dict) or not step:
            raise HostError(f"a key step must be an object, got {step!r}")
        if "wait_ms" in step:
            wait = step["wait_ms"]
            if not isinstance(wait, (int, float)) or not 0 <= wait <= 60000:
                raise HostError("wait_ms must be 0..60000")
            time.sleep(wait / 1000)
            results.append({"waited_ms": wait})
            continue
        unknown = set(step) - {"press", "down", "up", "release_all", "hold_ms"}
        if unknown:
            raise HostError(f"unknown key step fields: {', '.join(sorted(unknown))}")
        results.append(client.call("keys", **step))
    return results
