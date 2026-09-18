#!/bin/zsh
set -eu
cd -- "${0:A:h}/.."
exec .venv/bin/python tools/recomp/run/play_native.py --profile config/recomp/profiles/play-profile.json "$@"
