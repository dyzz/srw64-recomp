#!/usr/bin/env python3
"""Start the game on the connected phone with its debug interface and attach to it
(docs/design/android-port.md).

The app listens on the abstract socket @srw64-debug when its Options → About → AI debug
interface switch is on, or when started with --ez debug true; adb forwards a local port to
it. With the switch on, --no-start only forwards and attaches to the game as it is.
This writes build/recomp/debug/android-<time>/debug.tcp
and makes it the current run, so srw64ctl.py and Session.attach() reach the phone.
Screenshots, recordings and event logs come back over the same connection (the host's
file.read, Session.local_file).

  tools/release/android/attach.py [--save SRAM] [--env SRW64_KEY=VALUE] [--no-start] [-- extra host arguments]
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
from pathlib import Path
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import CURRENT, DEBUG_DIR, HostError, Session  # noqa: E402

PACKAGE = "org.srw64.game"


def adb(*args: str, **kwargs) -> str:
    return subprocess.run(["adb", *args], check=True, capture_output=True, text=True, **kwargs).stdout


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--save", type=Path, help="32 KiB card to place as the player's cartridge.sram first")
    parser.add_argument("--no-start", action="store_true", help="attach to the running game (its debug switch on, or started with --ez debug true)")
    parser.add_argument("--env", action="append", default=[], metavar="SRW64_KEY=VALUE",
                        help="development variable for the host (repeatable)")
    parser.add_argument("extra", nargs="*", help="host arguments after --")
    args = parser.parse_args()
    if not args.no_start:
        adb("shell", "am", "force-stop", PACKAGE)
        if args.save:
            data = args.save.read_bytes()
            if len(data) != 32768:
                raise SystemExit(f"{args.save} is not a 32 KiB card")
            subprocess.run(["adb", "shell", "run-as", PACKAGE, "sh", "-c",
                            "'mkdir -p files/user/saves && cat > files/user/saves/cartridge.sram'"], input=data, check=True)
        command = ["shell", "am", "start", "-n", f"{PACKAGE}/.SetupActivity", "--ez", "debug", "true"]
        if args.extra:
            command += ["--esa", "args", ",".join(args.extra)]
        if args.env:
            command += ["--esa", "env", ",".join(args.env)]
        adb(*command)
    port = adb("forward", "tcp:0", "localabstract:srw64-debug").strip()
    run = DEBUG_DIR / ("android-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ"))
    run.mkdir(parents=True)
    (run / "debug.tcp").write_text(port + "\n")
    CURRENT.write_text(str(run) + "\n")
    session = Session(run)
    deadline = time.monotonic() + 90
    while True:
        try:
            status = session.client.call("status")
            break
        except HostError:
            session.client.close()
            if time.monotonic() > deadline:
                raise
            time.sleep(1)
    print(f"SRW64_ANDROID_ATTACHED {run} port={port} vi={status.get('vi')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
