#!/bin/sh
# Marchwind64, Steam Deck edition (runs on other x86-64 Linux too); see README.txt.
# Finds your ROM (Super Robot Taisen 64, Japan, Rev 0) and starts the game.
# Other options go straight to the program: ./srw64 --play --help lists them.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
data=${XDG_DATA_HOME:-}
case $data in /*) ;; *) data=$HOME/.local/share ;; esac
data=$data/srw64-recomp

fail() {
    echo "marchwind64: $1" >&2
    # Steam Deck Game Mode and desktop launchers have no terminal to show this.
    if command -v kdialog >/dev/null 2>&1; then kdialog --error "$1" || true
    elif command -v zenity >/dev/null 2>&1; then zenity --error --text="$1" || true
    fi
    exit 1
}

given_rom=no given_language=no
for argument in "$@"; do
    case $argument in
        --rom) given_rom=yes ;;
        --language) given_language=yes ;;
    esac
done
if [ $given_rom = no ]; then
    rom=
    # Any byte order: the game turns a .v64 or .n64 dump into .z64 itself.
    for candidate in "${SRW64_ROM:-}" "$data/rom.z64" "$here/rom.z64" "$data/rom.n64" "$here/rom.n64" "$data/rom.v64" "$here/rom.v64"; do
        if [ -n "$candidate" ] && [ -f "$candidate" ]; then rom=$candidate; break; fi
    done
    [ -n "$rom" ] || fail "找不到 ROM：请把超级机器人大战 64（日版 Rev 0）复制到 $data/rom.z64，或放在本脚本旁边并命名为 rom.z64。
No ROM found: copy your Super Robot Taisen 64 ROM (Japan, Rev 0) to $data/rom.z64, or next to this script as rom.z64."
    set -- --rom "$rom" "$@"
fi
# On the Deck, text entry (names) opens the Steam on-screen keyboard; Game Mode sets
# this already, Desktop Mode does not.
if [ "${SteamDeck:-}" = 1 ]; then export SDL_ENABLE_STEAM_SCREEN_KEYBOARD="${SDL_ENABLE_STEAM_SCREEN_KEYBOARD:-1}"; fi
# First launch starts in Simplified Chinese; the settings window (View button) changes it.
if [ $given_language = no ] && [ ! -f "$data/presentation.json" ]; then
    set -- --language zh-Hans "$@"
fi
exec "$here/srw64" --play "$@"
