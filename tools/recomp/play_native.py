#!/usr/bin/env python3
"""Open the native keyboard build with a separate, persistent playtest history."""
from __future__ import annotations

import fcntl
import argparse
import json
from datetime import datetime, timezone
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
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
    parser.add_argument("--resolution-scale", type=int, choices=range(1, 9), help="internal resolution multiplier, 1..8; text size and layout stay the same")
    args = parser.parse_args()
    if (args.language or args.images) and not args.profile:
        parser.error("--language and --images require --profile")
    config_name = "playtest-model5600.json" if args.model_5600 else "playtest-native-marker.json" if args.native_waterdrop else "playtest.json"
    config = json.loads((ROOT / "config/recomp" / config_name).read_text())
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
        chosen = None
        skipped = []
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
            print(f"语言：{profile['presentation']['locale']}；画面：{profile['presentation']['images']}；F7：中文 / 日文热切换（自动记住）；HD 资源齐全时可用 F6 切换图片与 5600 模型。", flush=True)
        if args.profile:
            print("剧情：↑/↓调自动速度；X恢复手动；E+Z按住快进；E+Enter跳过当前段；Q回看；I/K调字号。", flush=True)
        if args.new_game:
            print("选择 New Game → 女性超级系，使用默认姓名进入第一话开场。", flush=True)
        else:
            print("首次请从标题选择 Load → ROM卡带 → 存档1；以后按实际保存类型选择 Load 或 Continue。", flush=True)
        print(f"载入存档副本：{source}\n本次试玩目录：{output}", flush=True)
        for row in skipped:
            print(f"跳过会话 {row.session}：{row.reason}", flush=True)
        if chosen is not None:
            print(f"选择 {chosen.session}：{chosen.reason}。游戏内槽位仍由原游戏校验。", flush=True)
            try:
                source = stage_selection(output, chosen, skipped)
            except (OSError, SaveHistoryError) as error:
                print(f"保存恢复来源副本失败：{error}", file=sys.stderr)
                return 1
        command = [
            sys.executable, str(ROOT / "tools/recomp/run_host_probe.py"),
            "--graphics", "--interactive", "--variant", config["variant"],
            "--output", str(output),
        ]
        if not args.mute:
            command.append("--audio")
        if args.profile:
            command += ["--profile", str(args.profile.resolve()), "--presentation-settings", str(directory / "presentation.json")]
            if args.language:
                command += ["--language", args.language]
            if args.images:
                command += ["--images", args.images]
        if source is not None:
            command += ["--save-from", str(source), "--save-sha256", chosen.sha256]
        if args.native_waterdrop:
            pack = ROOT / config["native_marker"]
            if not pack.exists():
                subprocess.run([sys.executable, str(ROOT / "tools/recomp/prepare_native_marker.py"), "--output", str(pack)], check=True)
            command += ["--native-marker", str(pack)]
        scale = args.resolution_scale if args.resolution_scale is not None else config.get("resolution_scale")
        if scale is not None:
            if type(scale) is not int or not 1 <= scale <= 8:
                raise RuntimeError("resolution_scale must be an integer in 1..8")
            command += ["--resolution-scale", str(scale)]
        return subprocess.run(command, cwd=ROOT).returncode


if __name__ == "__main__":
    raise SystemExit(main())
