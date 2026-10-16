#!/bin/sh
# The PS Vita V0 probe (docs/design/psv-port.md) from macOS or any Docker host: the
# recompiled game, the RSP audio microcode and the runtime as ARMv7 static libraries
# with VitaSDK's arm-vita-eabi-gcc, and (--qemu) the same sources as a Linux ARMv7
# headless host run under qemu-arm. Run `make` first (generated CPU code, pinned
# upstream checkouts). Arguments go to build_probe.py; results in build/recomp/vita.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
python=${PYTHON:-$root/.venv/bin/python}
[ -x "$python" ] || python=python3
# The runtime patches are applied here, where git owns the checkouts (as the Linux build does).
"$python" "$root/tools/release/vita/build_probe.py" --prepare-only
docker build --platform linux/arm64 -t srw64-vita-build:vitasdk "$root/tools/release/vita"
# Same absolute path inside the container, so generated files name the same sources. A git
# worktree links its build/ into the main checkout, so that is mounted too.
mounts="-v $root:$root"
common=$(cd "$root" && git rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)
if [ -n "$common" ]; then
    main=$(dirname -- "$common")
    case "$root" in "$main"|"$main"/*) [ "$main" = "$root" ] || mounts="-v $main:$main $mounts";; esac
fi
# shellcheck disable=SC2086
exec docker run --rm --platform linux/arm64 $mounts -w "$root" -e HOME=/tmp -e PYTHONUTF8=1 \
    srw64-vita-build:vitasdk python3 tools/release/vita/build_probe.py "$@"
