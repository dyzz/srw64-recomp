#!/bin/sh
# Adds SRW64 to the Steam library with its artwork; run once in Desktop Mode with
# Steam open (see README.txt). Optional argument: zh-Hans, en or ja for the name;
# otherwise the game's language, or Simplified Chinese before the first launch.
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if message=$(python3 "$here/steam/add_to_steam.py" "$@" 2>&1); then kind=info; else kind=error; fi
echo "$message"
# Double-clicked in the file manager there is no terminal to show the result.
if command -v kdialog >/dev/null 2>&1; then
    if [ $kind = info ]; then kdialog --msgbox "$message"; else kdialog --error "$message"; fi
elif command -v zenity >/dev/null 2>&1; then
    zenity --$kind --text="$message"
fi
[ $kind = info ]
