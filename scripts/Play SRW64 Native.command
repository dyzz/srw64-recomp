#!/bin/zsh
set -eu
cd -- "${0:A:h}/.."
if [[ ! -x .venv/bin/python ]]; then
  echo "Run make in $PWD first (see README)." >&2
  exit 1
fi
exec .venv/bin/python tools/recomp/run/play_native.py --profile config/recomp/profiles/play-profile.json "$@"
