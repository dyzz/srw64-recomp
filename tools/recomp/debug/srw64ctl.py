#!/usr/bin/env python3
"""Command line for the native host's debug interface (docs/guide/debug-interface.md).

  srw64ctl.py launch [--language zh-Hans] [--images original] [--rules fixed] [--reuse-build]
  srw64ctl.py status [--history]
  srw64ctl.py keys e+return i i k e+z:600      # a key chord, or chord:hold_ms; "wait:500" pauses
  srw64ctl.py buttons r+start [--vis 30]
  srw64ctl.py shot [--window Options] [--no-overlays]
  srw64ctl.py tree [--window game]
  srw64ctl.py click --text 开始故事 | click X Y
  srw64ctl.py type ナナ ; uikey return [--mod cmd]
  srw64ctl.py menu 选项 游戏性调整
  srw64ctl.py settings --rules original --locale ja --images hd
  srw64ctl.py wait --dialogue --timeout 60 ; wait --vi 5000 ; wait --event dialogue:font
  srw64ctl.py events dialogue [--since N] [--kind font]
  srw64ctl.py quit

Commands other than launch use the session launch started last, or --run DIR.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))  # tools/, home of the recomp package
from recomp.debug.session import HostError, Session, run_keys  # noqa: E402


def key_steps(items: list[str]) -> list[dict]:
    steps = []
    for item in items:
        chord, _, hold = item.partition(":")
        if chord == "wait":
            steps.append({"wait_ms": int(hold or 500)})
        else:
            steps.append({"press": chord, **({"hold_ms": int(hold)} if hold else {})})
            steps.append({"wait_ms": 80})
    return steps


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--run", help="debug run directory (default: the last launched)")
    commands = parser.add_subparsers(dest="command", required=True)
    launch = commands.add_parser("launch")
    launch.add_argument("--language", choices=("ja", "zh-Hans", "en"))
    launch.add_argument("--images", choices=("original", "hd"))
    launch.add_argument("--rules", help="fixed (default), original, all, or comma-separated rule ids")
    launch.add_argument("--save", help="32 KiB SRAM to start from")
    launch.add_argument("--mini-stage")
    launch.add_argument("--reuse-build", action="store_true")
    status = commands.add_parser("status")
    status.add_argument("--history", action="store_true")
    keys = commands.add_parser("keys")
    keys.add_argument("chords", nargs="+")
    buttons = commands.add_parser("buttons")
    buttons.add_argument("buttons")
    buttons.add_argument("--vis", type=int, default=6)
    shot = commands.add_parser("shot")
    shot.add_argument("--window")
    shot.add_argument("--no-overlays", action="store_true")
    tree = commands.add_parser("tree")
    tree.add_argument("--window")
    click = commands.add_parser("click")
    click.add_argument("point", nargs="*", type=float)
    click.add_argument("--text")
    click.add_argument("--window")
    typing = commands.add_parser("type")
    typing.add_argument("text")
    typing.add_argument("--window")
    uikey = commands.add_parser("uikey")
    uikey.add_argument("key")
    uikey.add_argument("--mod", action="append", default=[])
    uikey.add_argument("--window")
    menu = commands.add_parser("menu")
    menu.add_argument("path", nargs="*")
    settings = commands.add_parser("settings")
    settings.add_argument("--rules")
    settings.add_argument("--locale", choices=("ja", "zh-Hans", "en"))
    settings.add_argument("--images", choices=("original", "hd"))
    wait = commands.add_parser("wait")
    wait.add_argument("--vi", type=int)
    wait.add_argument("--dialogue", action="store_true")
    wait.add_argument("--intro", action="store_true")
    wait.add_argument("--name-page", action="store_true")
    wait.add_argument("--title-menu", action="store_true")
    wait.add_argument("--text")
    wait.add_argument("--event", help="log:kind, e.g. dialogue:font")
    wait.add_argument("--timeout", type=float, default=60)
    events = commands.add_parser("events")
    events.add_argument("log")
    events.add_argument("--since", type=int, default=0)
    events.add_argument("--kind", action="append")
    commands.add_parser("quit")
    args = parser.parse_args()

    def optional(**values):
        return {k: v for k, v in values.items() if v not in (None, [], "")}

    try:
        if args.command == "launch":
            session = Session.launch(**optional(language=args.language, images=args.images, rules=args.rules,
                                                save=args.save, mini_stage=args.mini_stage), reuse_build=args.reuse_build)
            print(session.run)
            return 0
        session = Session.attach(args.run)
        client = session.client
        if args.command == "status":
            result = client.call("status", history=args.history)
        elif args.command == "keys":
            result = run_keys(client, key_steps(args.chords))[-2]
        elif args.command == "buttons":
            result = client.call("buttons", buttons=args.buttons, vis=args.vis)
        elif args.command == "shot":
            result = client.call("screenshot", **optional(window=args.window), overlays=not args.no_overlays)
        elif args.command == "tree":
            result = client.call("ui.tree", **optional(window=args.window))
        elif args.command == "click":
            point = {"x": args.point[0], "y": args.point[1]} if len(args.point) == 2 else {}
            result = client.call("ui.click", **point, **optional(text=args.text, window=args.window))
        elif args.command == "type":
            result = client.call("ui.type", text=args.text, **optional(window=args.window))
        elif args.command == "uikey":
            result = client.call("ui.key", key=args.key, modifiers=args.mod, **optional(window=args.window))
        elif args.command == "menu":
            result = client.call("menu", path=args.path)
        elif args.command == "settings":
            result = client.call("settings", **optional(rules=args.rules, locale=args.locale, images=args.images))
        elif args.command == "wait":
            until = optional(vi=args.vi, text=args.text)
            for flag, key in ((args.dialogue, "dialogue_active"), (args.intro, "intro_active"), (args.name_page, "name_page")):
                if flag:
                    until[key] = True
            if args.title_menu:
                until["title_major"] = 3
            if args.event:
                log, _, kind = args.event.partition(":")
                until["event"] = optional(log=log, kind=kind)
            result = session.wait(timeout=args.timeout, **until)
        elif args.command == "events":
            rows, cursor = session.events(args.log, args.since, args.kind)
            result = {"rows": rows, "next": cursor}
        else:
            result = session.quit()
    except HostError as error:
        print(f"srw64ctl: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, ensure_ascii=False, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
