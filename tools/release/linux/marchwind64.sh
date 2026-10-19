#!/bin/sh
# Directory, ROM and language policy is shared by the Windows/Linux native launcher.
# An existing user-data folder beside srw64 enables portable storage and exit sync.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# Keep the Steam Deck keyboard integration in both Desktop Mode and Game Mode.
if [ "${SteamDeck:-}" = 1 ]; then export SDL_ENABLE_STEAM_SCREEN_KEYBOARD="${SDL_ENABLE_STEAM_SCREEN_KEYBOARD:-1}"; fi
exec "$here/srw64" --play "$@"
