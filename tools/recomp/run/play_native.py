#!/usr/bin/env python3
"""Open the native keyboard build with a separate, persistent playtest history."""
from __future__ import annotations

import fcntl
import argparse
import json
import shlex
import os
from datetime import datetime, timezone
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]


def main() -> int:
    from srw64_native import rule_settings
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--profile", type=Path, help="unified original-ROM presentation profile")
    parser.add_argument("--language", help="locale declared by the profile")
    parser.add_argument("--images", choices=("original", "hd"), help="initial image mode; F6 switches in game")
    mode.add_argument("--model-5600", action="store_true", help="isolated Japanese 96-face marker experiment")
    mode.add_argument("--native-waterdrop", action="store_true", help="original JP ROM with native GPU waterdrop")
    save_mode = parser.add_mutually_exclusive_group()
    save_mode.add_argument("--new-game", action="store_true", help="start with empty SRAM for the opening story")
    save_mode.add_argument("--restore-session", help="restore an intact history session by ID, or 'initial' for the frozen backup")
    save_mode.add_argument("--list-saves", action="store_true", help="inspect history integrity without starting the game")
    parser.add_argument("--mute", action="store_true", help="disable audio output for testing")
    parser.add_argument("--mini-stage", type=Path, help="compile this mini stage and substitute it for the first stage of a new game; F8 on the main menu starts it")
    parser.add_argument("--resolution-scale", type=int, choices=range(1, 9), help="internal resolution multiplier, 1..8; text size and layout stay the same")
    rules = parser.add_mutually_exclusive_group()
    rules.add_argument("--rules", choices=("original", "fixed", "all"), help="original rules, the bug-fix rules, or those plus the difficulty choices (docs/gameplay/rule-fixes.md); a first launch uses fixed, and the choice is remembered")
    rules.add_argument("--rule-fixes", metavar="IDS",
                       help=f"comma-separated subset of {', '.join(rule_settings.RULE_FIXES)} ('' for none); remembered for later launches")
    parser.add_argument("--upgrade-rules", type=Path, metavar="PATH",
                        help="upgrade increments, prices, caps and weapon types (docs/gameplay/upgrade-limits.md); "
                             "tools/recomp/gameplay/upgrade_rules.py export writes a template. Not remembered")
    args = parser.parse_args()
    if (args.language or args.images) and not args.profile:
        parser.error("--language and --images require --profile")
    config_name = "playtest-model5600.json" if args.model_5600 else "playtest-native-marker.json" if args.native_waterdrop else "playtest.json"
    config = json.loads((ROOT / "config/recomp/profiles" / config_name).read_text())
    expected_variant = "model5600" if args.model_5600 else "jp"
    if config.get("schema") != "srw64.native-playtest.v1" or config["variant"] != expected_variant:
        raise RuntimeError("unsupported playtest configuration")
    from srw64_native.save_history import SaveCandidate, SaveHistoryError, inspect_initial, inventory, select, stage_selection
    variants = json.loads((ROOT / "config/recomp/rom-variants.json").read_text())["variants"]
    identity = variants[config["variant"]]
    directory = ROOT / ("build/recomp/profile-play" if args.profile else "build/recomp/model-5600/play" if args.model_5600 else "build/recomp/native-marker/play" if args.native_waterdrop else "build/recomp/play")
    sessions = directory / "sessions"
    def available_saves() -> tuple[list[SaveCandidate], SaveCandidate]:
        candidates = inventory(sessions, game_id=identity["game_id"], variant=config["variant"], rom_sha256=identity["sha256"])
        initial = inspect_initial(ROOT / config["initial_save"], config["initial_save_sha256"])
        return candidates, initial
    if args.list_saves:
        candidates, initial = available_saves()
        print("仅检查文件完整性；游戏内槽位是否可用仍由原游戏读取校验。")
        for row in [*candidates, initial]:
            print(f"{row.session}  {'可核验' if row.accepted else '已跳过'}  {row.reason}")
        return 0
    directory.mkdir(parents=True, exist_ok=True)
    with (directory / "active.lock").open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print("已有试玩窗口正在运行，请先关闭那个窗口。", file=sys.stderr)
            return 1
        sessions.mkdir(exist_ok=True)
        try:
            rule_fixes = rule_settings.select(directory / "rules.json", args.rules, args.rule_fixes)
        except ValueError as error:
            print(f"规则设置无效：{error}", file=sys.stderr)
            return 1
        chosen = None
        skipped = []
        if not args.new_game and args.restore_session is None:
            # Nothing has been played in this tree yet (a fresh clone): start the
            # story instead of asking for --new-game. Damaged saves still stop below.
            candidates, initial = available_saves()
            if not candidates and not initial.path.exists():
                print("还没有存档，开始新游戏。/ No saves yet: starting a new game.", flush=True)
                args.new_game = True
        if args.new_game:
            source = None
        else:
            try:
                chosen, skipped = select(*available_saves(), requested_session=args.restore_session)
            except SaveHistoryError as error:
                print(f"无法选择存档：{error}", file=sys.stderr)
                return 1
            source = chosen.path
        output = sessions / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
        print("方向键：移动 / 菜单；Z：确认；X：取消；Enter：Start；Esc：关闭窗口。", flush=True)
        print("开场缩放文字：E+Enter（R+START）跳过整段，停在下一场景。", flush=True)
        if args.profile:
            from srw64_native.profile import load_profile
            profile = load_profile(args.profile, locale=args.language, images=args.images)
            from srw64_native.presentation_settings import selected_locale
            args.language = selected_locale(directory / "presentation.json", profile["locales"],
                                            profile["presentation"]["locale"], args.language)
            profile["presentation"]["locale"] = args.language
            print(f"语言：{profile['presentation']['locale']}；画面：{profile['presentation']['images']}；F7：循环切换语言（自动记住）；HD 资源齐全时可用 F6 切换图片与 5600 模型。", flush=True)
        if args.profile:
            print("剧情：↑/↓调自动速度；X恢复手动；E+Z按住快进；E+Enter跳过当前段；Q回看；I/K调字号。", flush=True)
        print(f"规则：{rule_settings.describe(rule_fixes)}。菜单栏「选项 → 游戏性调整」或「设置…」里可随时逐项开关（立即生效并记住）；--rules original 可回到原版规则。", flush=True)
        if args.new_game:
            print("选择 New Game → 女性超级系，使用默认姓名进入第一话开场。", flush=True)
        else:
            print("首次请从标题选择 Load → ROM卡带 → 存档1；以后按实际保存类型选择 Load 或 Continue。", flush=True)
        print(f"载入存档副本：{source}\n本次试玩目录：{output}", flush=True)
        for row in skipped:
            print(f"跳过会话 {row.session}：{row.reason}", flush=True)
        if chosen is not None:
            print(f"选择 {chosen.session}：{chosen.reason}。游戏内槽位仍由原游戏校验。", flush=True)
            # The frozen initial backup predates the setting and used the original rules.
            played = () if chosen.session == "initial" else rule_settings.recorded(sessions / chosen.session / "report.json")
            if played is None:
                print("注意：读不到该会话记录的规则设置。", flush=True)
            elif played != rule_fixes:
                print(f"注意：该存档上次按不同规则游玩（{rule_settings.describe(played)}）；本次按当前规则运行，存档格式不受影响。", flush=True)
            try:
                source = stage_selection(output, chosen, skipped)
            except (OSError, SaveHistoryError) as error:
                print(f"保存恢复来源副本失败：{error}", file=sys.stderr)
                return 1
        command = [
            sys.executable, str(ROOT / "tools/recomp/run/run_host_probe.py"),
            "--graphics", "--interactive", "--variant", config["variant"],
            "--output", str(output),
        ]
        environment = dict(os.environ)
        environment["SRW64_RULE_FIXES"] = ",".join(rule_fixes)
        environment.pop("SRW64_UPGRADE_RULES", None)
        if args.upgrade_rules:
            from srw64_native import upgrade_rules
            try:
                summary = upgrade_rules.report(args.upgrade_rules.resolve())
            except ValueError as error:
                print(f"升级规则文件无效：{error}", file=sys.stderr)
                return 1
            environment["SRW64_UPGRADE_RULES"] = summary["path"]
            print(f"升级规则文件：{summary['path']}（五项 {summary['stats'] or '原版'}，武器类型 {summary['weapon_types'] or '原版'}，"
                  f"上限覆盖 {summary['unit_caps']} 台，武器类型覆盖 {summary['weapon_type_overrides']} 件）", flush=True)
        environment["SRW64_MINI_STAGE_COMPILER"] = shlex.join([sys.executable, str(ROOT / "tools/recomp/script_lab/mini_stage.py")])
        if args.mini_stage:
            sys.path.insert(0, str(ROOT / "tools"))
            from recomp.script_lab.mini_stage import compile_stage
            image = output.parent / (output.name + ".mini-stage.json")
            image.parent.mkdir(parents=True, exist_ok=True)
            image.write_text(json.dumps(compile_stage(json.loads(args.mini_stage.read_text())), ensure_ascii=False, indent=2) + "\n")
            environment["SRW64_MINI_STAGE"] = str(image)
            print(f"迷你关卡镜像：{image}；在主菜单按 F8 进入。", flush=True)
        if not args.mute:
            command.append("--audio")
        if args.profile:
            command += ["--profile", str(args.profile.resolve()), "--presentation-settings", str(directory / "presentation.json"),
                        "--rule-settings", str(directory / "rules.json")]
            if args.language:
                command += ["--language", args.language]
            if args.images:
                command += ["--images", args.images]
        if source is not None:
            command += ["--save-from", str(source), "--save-sha256", chosen.sha256]
        if args.native_waterdrop:
            pack = ROOT / config["native_marker"]
            if not pack.exists():
                subprocess.run([sys.executable, str(ROOT / "tools/recomp/model5600/prepare_native_marker.py"), "--output", str(pack)], check=True)
            command += ["--native-marker", str(pack)]
        scale = args.resolution_scale if args.resolution_scale is not None else config.get("resolution_scale")
        if scale is not None:
            if type(scale) is not int or not 1 <= scale <= 8:
                raise RuntimeError("resolution_scale must be an integer in 1..8")
            command += ["--resolution-scale", str(scale)]
        return subprocess.run(command, cwd=ROOT, env=environment).returncode


if __name__ == "__main__":
    raise SystemExit(main())
