#!/usr/bin/env python3
"""Attach to the game on a Switch through its debug interface (docs/design/switch-port.md,
"The debug interface on the LAN").

The game opens the interface when /switch/Marchwind64/debug exists on the SD card;
package.py --upload IP --debug puts it there with a token, kept here in
build/recomp/switch-package/debug-token. On the Switch the game listens on every address
at port 46464 (the token keeps others out), so this reaches it however it was started, from
hbmenu too. Without the token file it reads the endpoint from the nxlink log instead (the
SRW64_DEBUG_ENDPOINT line). It writes build/recomp/debug/switch-<time>/debug.json in the
main checkout and makes it the current run, so srw64ctl.py, Session.attach() and the
srw64_* MCP tools reach the Switch; the run's console.log comes over the same connection.

  tools/release/switch/attach.py --host 192.168.1.236
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
from recomp.debug.session import HostError, Session  # noqa: E402


def main_checkout() -> Path:
    """Where the MCP tools look for the current run: the main checkout, also from a worktree."""
    common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"],
                            cwd=ROOT, capture_output=True, text=True).stdout.strip()
    return Path(common).parent if common else ROOT


def endpoint_from(log: Path) -> dict | None:
    if not log.exists():
        return None
    found = None
    for line in log.read_text(errors="replace").splitlines():
        if line.startswith("SRW64_DEBUG_ENDPOINT "):
            found = json.loads(line.split(" ", 1)[1].strip())
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", required=True, help="the Switch's address")
    parser.add_argument("--log", type=Path, default=ROOT / "build/recomp/switch-build/nxlink.log",
                        help="the nxlink log of the running game")
    parser.add_argument("--timeout", type=float, default=300, help="seconds to wait for the endpoint line")
    args = parser.parse_args()
    deadline = time.monotonic() + args.timeout
    token_file = ROOT / "build/recomp/switch-package/debug-token"
    if token_file.exists():
        remote = {"schema": "srw64.debug-endpoint.v2", "transport": "tcp", "port": 46464,
                  "token": token_file.read_text().strip(), "pid": -1}
    while not (token_file.exists() or (remote := endpoint_from(args.log))):
        if time.monotonic() > deadline:
            raise SystemExit(f"no SRW64_DEBUG_ENDPOINT line in {args.log}: is /switch/Marchwind64/debug on the SD card?")
        time.sleep(2)
    if remote.get("transport") != "tcp":
        raise SystemExit(f"the endpoint in {args.log} is not TCP")
    debug_dir = main_checkout() / "build/recomp/debug"
    run = debug_dir / ("switch-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ"))
    run.mkdir(parents=True)
    # remote.json marks the run as another machine's: files the game writes (screenshots,
    # recordings, event logs) come over the connection (Session.local_file).
    (run / "remote.json").write_text(json.dumps({"schema": "srw64.remote-run.v2", "host": args.host,
                                                 "pid": remote.get("pid"), "port": remote["port"]}, indent=2) + "\n")
    local = {**{k: remote[k] for k in ("schema", "transport", "token", "pid", "protocol") if k in remote},
             "host": args.host, "port": remote["port"]}
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
            if time.monotonic() > deadline:
                raise
            time.sleep(1)
    (debug_dir / "current").write_text(str(run) + "\n")
    print(f"SRW64_REMOTE_ATTACHED {run} host={args.host} port={remote['port']} vi={status.get('vi')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
