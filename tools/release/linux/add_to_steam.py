#!/usr/bin/env python3
"""Add SRW64 to the Steam library as a non-Steam game, with its artwork.

Run by add-to-steam.sh beside srw64.sh, once, in Desktop Mode with Steam open. It
writes a desktop entry (name in the game's language, icon), hands it to Steam the
way SteamOS's own "Add to Steam" does, reads the new shortcut's app id from
shortcuts.vdf and copies the artwork beside this script into Steam's grid folder.
When the game is already in the library it only refreshes the artwork.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
import urllib.parse

HERE = Path(__file__).resolve().parent          # <game>/steam
LAUNCHER = HERE.parent / 'srw64.sh'
NAMES = {'zh-Hans': '超级机器人大战64', 'en': 'Super Robot Wars 64', 'ja': 'スーパーロボット大戦64'}
FIRST_LOCALE = 'zh-Hans'                        # srw64.sh starts a first launch in Simplified Chinese
# Steam's grid names for a shortcut's app id, from the files steam_art.py writes.
ARTWORK = {'capsule.png': '{}p.png', 'wide.png': '{}.png', 'hero.png': '{}_hero.png',
           'logo.png': '{}_logo.png', 'icon.png': '{}_icon.png'}


def data_dir() -> Path:
    base = os.environ.get('XDG_DATA_HOME', '')
    return (Path(base) if base.startswith('/') else Path.home() / '.local/share') / 'srw64-recomp'


def game_locale() -> str:
    try:
        locale = json.loads((data_dir() / 'presentation.json').read_text(encoding='utf-8')).get('locale')
    except (OSError, ValueError):
        locale = None
    return locale if locale in NAMES else FIRST_LOCALE


def parse_vdf(data: bytes, at: int = 0) -> tuple[dict, int]:
    """Steam's binary KeyValues: 00 map, 01 string, 02 int32, 07 uint64, 08 end of map."""
    node = {}
    while True:
        kind = data[at]
        at += 1
        if kind == 0x08:
            return node, at
        end = data.index(b'\0', at)
        key, at = data[at:end].decode('utf-8', 'replace'), end + 1
        if kind == 0x00:
            node[key], at = parse_vdf(data, at)
        elif kind == 0x01:
            end = data.index(b'\0', at)
            node[key], at = data[at:end].decode('utf-8', 'replace'), end + 1
        elif kind == 0x02:
            node[key], at = struct.unpack_from('<I', data, at)[0], at + 4
        elif kind == 0x07:
            node[key], at = struct.unpack_from('<Q', data, at)[0], at + 8
        else:
            raise ValueError(f'Unknown KeyValues type {kind:#x} at {at - 1}')


def field(entry: dict, name: str):
    return next((value for key, value in entry.items() if key.lower() == name.lower()), None)


def shortcut_ids(vdf: Path, launcher: Path) -> list[int]:
    """App ids of the shortcuts in vdf that start launcher."""
    try:
        shortcuts = field(parse_vdf(vdf.read_bytes())[0], 'shortcuts') or {}
    except (OSError, ValueError, IndexError):
        return []
    return [field(entry, 'appid') for entry in shortcuts.values()
            if isinstance(entry, dict) and str(field(entry, 'exe') or '').strip('"') == str(launcher)
            and field(entry, 'appid') is not None]


def steam_users() -> list[Path]:
    roots = [Path.home() / '.steam/steam', Path.home() / '.local/share/Steam',
             Path.home() / '.var/app/com.valvesoftware.Steam/.local/share/Steam']
    found = {}
    for root in roots:
        if (root / 'userdata').is_dir():
            for user in (root / 'userdata').iterdir():
                if user.name.isdigit():
                    found.setdefault(user.resolve(), user)
    return list(found.values())


def find_shortcuts() -> dict[Path, list[int]]:
    found = {}
    for user in steam_users():
        ids = shortcut_ids(user / 'config/shortcuts.vdf', LAUNCHER)
        if ids:
            found[user] = ids
    return found


def steam_running() -> bool:
    try:
        return subprocess.run(['pgrep', '-x', 'steam'], stdout=subprocess.DEVNULL).returncode == 0
    except FileNotFoundError:
        return True  # cannot tell; let Steam's own handler decide


def write_desktop_entry(name: str) -> Path:
    entry = Path.home() / '.local/share/applications/srw64-recomp.desktop'
    entry.parent.mkdir(parents=True, exist_ok=True)
    quoted = f'"{LAUNCHER}"' if ' ' in str(LAUNCHER) else str(LAUNCHER)
    lines = ['[Desktop Entry]', 'Type=Application', f'Name={name}', 'Comment=SRW64 Recomp',
             f'Exec={quoted}', f'Path={LAUNCHER.parent}', 'Terminal=false', 'Categories=Game;']
    if (HERE / 'icon.png').is_file():
        lines.insert(5, f'Icon={HERE / "icon.png"}')
    entry.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    entry.chmod(0o755)
    return entry


def add(entry: Path) -> None:
    # What steamos-add-to-steam does: Steam imports the entry named by the URL.
    Path('/tmp/addnonsteamgamefile').touch()
    url = 'steam://addnonsteamgame/' + urllib.parse.quote(str(entry), safe='')
    try:
        subprocess.run(['steam', url], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
    except subprocess.TimeoutExpired:
        pass


def copy_artwork(found: dict[Path, list[int]]) -> int:
    copied = 0
    for user, ids in found.items():
        grid = user / 'config/grid'
        grid.mkdir(parents=True, exist_ok=True)
        for app_id in ids:
            for source, pattern in ARTWORK.items():
                if (HERE / source).is_file():
                    shutil.copyfile(HERE / source, grid / pattern.format(app_id))
                    copied += 1
    return copied


def main() -> int:
    if not LAUNCHER.is_file():
        print(f'找不到 {LAUNCHER}。\nMissing {LAUNCHER}.')
        return 1
    locale = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] in NAMES else game_locale()
    name = NAMES[locale]
    found = find_shortcuts()
    if found:
        copy_artwork(found)
        print(f'已在 Steam 库中，封面已更新。\nAlready in the Steam library; the artwork is refreshed.')
        return 0
    if not steam_running():
        print('请先打开 Steam，再运行一次。\nOpen Steam first, then run this again.')
        return 1
    add(write_desktop_entry(name))
    deadline = time.monotonic() + 30
    while not found and time.monotonic() < deadline:
        time.sleep(0.5)
        found = find_shortcuts()
    if not found:
        print('Steam 没有添加这个游戏。可以在 Steam 里选「添加非 Steam 游戏」手动添加 srw64.sh。\n'
              'Steam did not add the game. In Steam, choose Add a Non-Steam Game and pick srw64.sh.')
        return 1
    copy_artwork(found)
    print(f'已添加到 Steam：{name}。回到游戏模式即可在库里启动。\n'
          f'Added to Steam as "{name}". Start it from the library in Game Mode.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
