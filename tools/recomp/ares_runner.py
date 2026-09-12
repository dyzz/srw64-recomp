#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


ARES_BINARY = Path("/Applications/ares.app/Contents/MacOS/ares")
# Quartz keyboard: path=0, vendor=0, product=0, so HID::Device::id() is 0.
# Device id 1 is the generic mouse (product=1), not the keyboard.
KEYBOARD_DEVICE = "0x0/0"
KEY_IDS = {
    "F10": 10,
    "F11": 11,
    "F12": 12,
    "J": 49,
    "K": 50,
    "Q": 56,
    "E": 44,
    "Up": 92,
    "Down": 93,
    "Left": 94,
    "Right": 95,
    "Return": 97,
}


class RunnerError(RuntimeError):
    pass


def _binding(key: str) -> str:
    return f"{KEYBOARD_DEVICE}/{KEY_IDS[key]};;"


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _pid_alive(pid: int) -> bool:
    if pid <= 0:
        return False
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


def _port_open(host: str, port: int, pid: int | None = None) -> bool:
    """Check the listening socket without connecting to the RSP server.

    ares requires '+' to be the first byte on every accepted GDB connection.
    A generic TCP health probe connects and closes without that byte, which can
    leave the single-client server wedged and the emulation thread halted.
    This runner targets macOS, so lsof gives us a non-invasive readiness check.
    """
    command = ["lsof", "-nP"]
    if pid is not None:
        command.extend(("-a", "-p", str(pid)))
    command.extend((f"-iTCP:{port}", "-sTCP:LISTEN"))
    result = subprocess.run(
        command,
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return result.returncode == 0


def _read_session(path: Path) -> dict:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise RunnerError(f"no runner session at {path}") from exc
    except json.JSONDecodeError as exc:
        raise RunnerError(f"invalid runner session at {path}: {exc}") from exc
    if not isinstance(document, dict) or document.get("schema") != "srw64.ares-session.v1":
        raise RunnerError("unsupported ares session file")
    return document


def _write_session(path: Path, document: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _command(
    rom: Path, runtime_dir: Path, port: int, await_gdb_client: bool
) -> list[str]:
    settings_path = runtime_dir / "ares-settings.bml"
    screenshots = runtime_dir / "screenshots"
    screenshots.mkdir(parents=True, exist_ok=True)
    setting_values = {
        "Audio/Mute": "true",
        # Keep scripted CPU/debugger work running without capturing background keys.
        "Input/Defocus": "Block",
        "Boot/AwaitGDBClient": "true" if await_gdb_client else "false",
        "DebugServer/Enabled": "true",
        "DebugServer/Port": str(port),
        "DebugServer/UseIPv4": "false",
        "General/AutoSaveMemory": "false",
        "General/NoFilePrompt": "true",
        "Paths/Screenshots": str(screenshots.resolve()),
        "Nintendo64/Input/Controller.Port.1/Gamepad/Up": _binding("Up"),
        "Nintendo64/Input/Controller.Port.1/Gamepad/Down": _binding("Down"),
        "Nintendo64/Input/Controller.Port.1/Gamepad/Left": _binding("Left"),
        "Nintendo64/Input/Controller.Port.1/Gamepad/Right": _binding("Right"),
        "Nintendo64/Input/Controller.Port.1/Gamepad/A": _binding("J"),
        "Nintendo64/Input/Controller.Port.1/Gamepad/B": _binding("K"),
        "Nintendo64/Input/Controller.Port.1/Gamepad/L": _binding("Q"),
        "Nintendo64/Input/Controller.Port.1/Gamepad/R": _binding("E"),
        "Nintendo64/Input/Controller.Port.1/Gamepad/Start": _binding("Return"),
        "Hotkey/LoadState": _binding("F10"),
        "Hotkey/SaveState": _binding("F11"),
        "Hotkey/CaptureScreenshot": _binding("F12"),
    }
    command = [
        str(ARES_BINARY),
        "--system",
        "Nintendo 64",
        "--no-file-prompt",
        "--settings-file",
        str(settings_path.resolve()),
    ]
    for name, value in setting_values.items():
        command.extend(("--setting", f"{name}={value}"))
    command.append(str(rom.resolve()))
    return command


def start(
    rom: Path,
    runtime_dir: Path,
    port: int,
    timeout: float,
    await_gdb_client: bool,
) -> dict:
    if not ARES_BINARY.is_file():
        raise RunnerError(f"ares binary not found at {ARES_BINARY}")
    if not rom.is_file():
        raise RunnerError(f"ROM not found at {rom}")
    session_path = runtime_dir / "session.json"
    if session_path.exists():
        previous = _read_session(session_path)
        if _pid_alive(int(previous["pid"])):
            raise RunnerError(f"tracked ares process {previous['pid']} is already running")
    if _port_open("::1", port):
        raise RunnerError(f"RSP port {port} is already in use")

    runtime_dir.mkdir(parents=True, exist_ok=True)
    command = _command(rom, runtime_dir, port, await_gdb_client)
    log_path = runtime_dir / "ares.log"
    with log_path.open("ab") as log:
        process = subprocess.Popen(
            command,
            cwd=runtime_dir,
            stdin=subprocess.DEVNULL,
            stdout=log,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RunnerError(f"ares exited early with status {process.returncode}; see {log_path}")
        if _port_open("::1", port, process.pid):
            break
        time.sleep(0.1)
    else:
        process.terminate()
        raise RunnerError(f"ares did not open RSP port {port} within {timeout:.1f}s")

    session = {
        "schema": "srw64.ares-session.v1",
        "pid": process.pid,
        "port": port,
        "host": "::1",
        "rom_name": rom.name,
        "rom_path": str(rom.resolve()),
        "rom_sha256": _sha256(rom),
        "await_gdb_client": await_gdb_client,
        "settings_path": str((runtime_dir / "ares-settings.bml").resolve()),
        "log_path": str(log_path.resolve()),
        "keymap": {
            "dpad": "arrow keys",
            "A": "J",
            "B": "K",
            "L": "Q",
            "R": "E",
            "Start": "Return",
            "load_state": "F10",
            "save_state": "F11",
            "screenshot": "F12",
        },
    }
    _write_session(session_path, session)
    return {"status": "running", **session}


def status(runtime_dir: Path) -> dict:
    session = _read_session(runtime_dir / "session.json")
    pid = int(session["pid"])
    alive = _pid_alive(pid)
    return {
        **session,
        "status": "running" if alive else "stopped",
        "rsp_open": alive
        and _port_open(str(session["host"]), int(session["port"]), pid),
    }


def stop(runtime_dir: Path, timeout: float) -> dict:
    session = _read_session(runtime_dir / "session.json")
    pid = int(session["pid"])
    if not _pid_alive(pid):
        return {"status": "already-stopped", "pid": pid}
    command = subprocess.run(
        ["ps", "-p", str(pid), "-o", "command="],
        check=False,
        capture_output=True,
        text=True,
    ).stdout.strip()
    if str(ARES_BINARY) not in command or str(session["rom_path"]) not in command:
        raise RunnerError(f"refusing to stop pid {pid}; command does not match tracked ares session")
    os.kill(pid, signal.SIGTERM)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline and _pid_alive(pid):
        time.sleep(0.1)
    if _pid_alive(pid):
        raise RunnerError(f"ares pid {pid} did not stop within {timeout:.1f}s")
    return {"status": "stopped", "pid": pid}


def main() -> int:
    parser = argparse.ArgumentParser(prog="ares_runner.py")
    parser.add_argument("command", choices=("start", "status", "stop"))
    parser.add_argument("--rom", type=Path, default=Path("build/recomp/runtime/srw64-jp.z64"))
    parser.add_argument("--runtime-dir", type=Path, default=Path("build/recomp/runtime"))
    parser.add_argument("--port", type=int, default=9124)
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument(
        "--await-gdb-client",
        action="store_true",
        help="halt at the N64 boot vector until the RSP probe resumes execution",
    )
    args = parser.parse_args()
    try:
        if args.command == "start":
            result = start(
                args.rom,
                args.runtime_dir,
                args.port,
                args.timeout,
                args.await_gdb_client,
            )
        elif args.command == "status":
            result = status(args.runtime_dir)
        else:
            result = stop(args.runtime_dir, args.timeout)
    except (RunnerError, OSError) as exc:
        print(json.dumps({"status": "error", "error": str(exc)}, indent=2), file=sys.stderr)
        return 2
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
