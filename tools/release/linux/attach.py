#!/usr/bin/env python3
"""Attach to the game on a Steam Deck (or any Linux machine reached by ssh) through its
debug interface (docs/guide/debug-interface.md, "On a Steam Deck").

The game opens the interface when its Options → About → AI debug interface switch is on,
or when started with --debug (in Steam, the shortcut's launch options `%command% --debug`);
then play as usual. This finds the running game's
debug.json under the player's data directory, forwards a local port over ssh to the
loopback port it names, writes build/recomp/debug/deck-<time>/debug.json (that port and
the game's token) and makes it the current run, so srw64ctl.py, Session.attach() and the
srw64_* MCP tools reach the Deck. Screenshots,
recordings and event logs come back over the same connection (Session.local_file).

  tools/release/linux/attach.py [--host Deck]           attach to the running game
  tools/release/linux/attach.py --start [-- ARGS]       start it with --debug first
  tools/release/linux/attach.py --start --data-dir '~/srw64-debug'  ... with its own data (a copy
                       of the player's saves, ROM linked; quote ~ so it means the Deck's home)
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shlex
import socket
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import CURRENT, DEBUG_DIR, HostError, Session  # noqa: E402

DATA = "${XDG_DATA_HOME:-$HOME/.local/share}/srw64-recomp"


def remote_path(path: str) -> str:
    """A path for the remote shell, quoted, with ~/ still meaning that machine's home."""
    return '"$HOME"/' + shlex.quote(path[2:]) if path.startswith("~/") else shlex.quote(path)


def ssh(host: str, script: str, check: bool = True) -> str:
    return subprocess.run(["ssh", host, script], check=check, capture_output=True, text=True).stdout


def find_endpoint(host: str, data: str) -> dict | None:
    """The debug.json of the newest run whose game is still running, with its run directory."""
    script = f"""for f in $(ls -t {data}/sessions/*/run/debug.json 2>/dev/null); do
  p=$(sed -n 's/.*"pid": *\\([0-9]*\\).*/\\1/p' "$f")
  if [ -n "$p" ] && kill -0 "$p" 2>/dev/null; then echo "$f"; cat "$f"; exit 0; fi
done"""
    found = ssh(host, script, check=False)
    if not found.strip():
        return None
    path, _, body = found.partition("\n")
    endpoint = json.loads(body)
    if endpoint.get("transport") != "tcp":
        raise SystemExit(f"{path} on {host} is not a loopback endpoint; is the game older than this script?")
    endpoint["run"] = str(Path(path).parent)
    return endpoint


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def start(host: str, args: argparse.Namespace) -> None:
    data_home = ""
    if args.data_dir:
        # A separate data directory: the ROM and the HD pack linked, the saves and the
        # settings copied, so a debugging run never writes the player's own.
        d = remote_path(args.data_dir)
        ssh(host, f"""set -e; S={DATA}; D={d}/srw64-recomp; mkdir -p "$D"
[ -e "$D/rom.z64" ] || ln -s "$S/rom.z64" "$D/rom.z64"
[ -e "$D/hd" ] || {{ [ -d "$S/hd" ] && ln -s "$S/hd" "$D/hd"; }} || true
[ -e "$D/saves" ] || cp -r "$S/saves" "$D/saves"
[ -e "$D/presentation.json" ] || cp "$S/presentation.json" "$D/" 2>/dev/null || true""")
        data_home = d
    # A transient unit of the user's systemd, beside the games Steam starts: SteamOS has
    # KillUserProcesses=True, so anything left in the ssh login's scope ends with it.
    # Its output goes to `journalctl --user -u srw64-debug`.
    setenv = " ".join(f"--setenv={k}={v}" for k, v in (("DISPLAY", shlex.quote(args.display)), ("SteamDeck", "1"))) + \
        (f' --setenv=XDG_DATA_HOME={data_home}' if data_home else "")
    extra = " ".join(shlex.quote(a) for a in args.extra)
    game = remote_path(args.game_dir)
    ssh(host, f"systemctl --user reset-failed srw64-debug 2>/dev/null; "
              f"systemd-run --user --unit=srw64-debug --collect --working-directory={game} {setenv} "
              f"{game}/marchwind64.sh --debug {extra}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="Deck", help="ssh destination (default: Deck)")
    parser.add_argument("--start", action="store_true", help="start the game with --debug instead of finding one running")
    parser.add_argument("--data-dir", help="a separate XDG_DATA_HOME on the remote machine (--start prepares it)")
    parser.add_argument("--game-dir", default="~/Games/SRW64", help="with --start: where marchwind64.sh is (default ~/Games/SRW64)")
    parser.add_argument("--display", default=":1", help="with --start: the X display (Game Mode :1, Desktop Mode :0)")
    parser.add_argument("--timeout", type=float, default=120, help="seconds to wait for the debug interface")
    parser.add_argument("extra", nargs="*", help="game arguments after --")
    args = parser.parse_args()
    data = remote_path(args.data_dir) + "/srw64-recomp" if args.data_dir else DATA
    if args.start:
        start(args.host, args)
    deadline = time.monotonic() + args.timeout
    while not (remote := find_endpoint(args.host, data)):
        if not args.start or time.monotonic() > deadline:
            raise SystemExit(f"no game with --debug is running on {args.host} (data: {data}); "
                             "turn on Options → About → AI debug interface there, start it from Steam with the "
                             "launch options `%command% --debug`, or pass --start")
        time.sleep(2)
    run = DEBUG_DIR / ("deck-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ"))
    run.mkdir(parents=True)
    port = free_port()
    forward = subprocess.Popen(["ssh", "-N", "-o", "ExitOnForwardFailure=yes",
                                "-L", f"127.0.0.1:{port}:127.0.0.1:{remote['port']}", args.host],
                               stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               start_new_session=True)
    (run / "remote.json").write_text(json.dumps({"schema": "srw64.remote-run.v2", "host": args.host,
                                                 "run": remote["run"], "pid": remote["pid"],
                                                 "port": remote["port"], "forward_pid": forward.pid}, indent=2) + "\n")
    # The local side of the forward, with the game's token: owner-only, like the original.
    local = {**{k: remote[k] for k in ("schema", "transport", "token", "pid", "protocol") if k in remote},
             "host": "127.0.0.1", "port": port, "forwarded_from": f"{args.host}:{remote['port']}"}
    descriptor = os.open(run / "debug.json", os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(descriptor, "w") as out:
        out.write(json.dumps(local, indent=2) + "\n")
    session = Session(run)
    while True:
        try:
            status = session.client.call("status")
            break
        except HostError:
            session.client.close()
            if forward.poll() is not None:
                raise SystemExit(f"the ssh forward to {args.host} ended (exit {forward.returncode})")
            if time.monotonic() > deadline:
                raise
            time.sleep(1)
    CURRENT.write_text(str(run) + "\n")
    print(f"SRW64_REMOTE_ATTACHED {run} host={args.host} remote={remote['run']} vi={status.get('vi')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
