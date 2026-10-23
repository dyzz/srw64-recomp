#!/bin/sh
# Nintendo Switch builds (docs/design/switch-port.md) from macOS or any Docker host, in
# the devkitA64 image. `build.sh smoke` builds the toolchain check in smoke/ into
# build/recomp/switch-smoke/srw64-smoke.nro. Push an NRO to the console with nxlink, run
# natively on the Mac (see README.txt), not in the container.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
target=${1:-smoke}
docker build --platform linux/arm64 -q -t srw64-switch-build:devkita64 "$root/tools/release/switch" >/dev/null
# Same absolute path inside the container, so generated files name the same sources.
run() {
    docker run --rm --platform linux/arm64 -v "$root:$root" -w "$root" -e HOME=/tmp \
        srw64-switch-build:devkita64 "$@"
}
case "$target" in
smoke)
    out=build/recomp/switch-smoke
    run sh -c "/opt/devkitpro/portlibs/switch/bin/aarch64-none-elf-cmake -S tools/release/switch/smoke \
        -B $out -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build $out"
    echo "$root/$out/srw64-smoke.nro"
    ;;
*)
    echo "usage: $0 [smoke]" >&2
    exit 2
    ;;
esac
