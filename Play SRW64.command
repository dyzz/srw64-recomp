#!/bin/zsh
set -eu
cd -- "${0:A:h}"
exec .venv/bin/python tools/recomp/play_native.py "$@"
