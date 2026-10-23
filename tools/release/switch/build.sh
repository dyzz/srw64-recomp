#!/bin/sh
# Nintendo Switch builds (docs/design/switch-port.md) from macOS or any Docker host, in
# the devkitA64 image. Push an NRO to the console with nxlink, run natively on the Mac
# (see README.txt), not in the container.
#   build.sh smoke       the toolchain check in smoke/: build/recomp/switch-smoke/srw64-smoke.nro
#   build.sh game [-k]   srw64-gl-host (src/host with SRW64_GL_ONLY) for the Switch:
#                        build/recomp/switch-build/Marchwind64.nro; -k keeps going past errors
# Run `make` first (generated CPU code, pinned upstream checkouts, the RSP audio code).
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
python=${PYTHON:-$root/.venv/bin/python}
[ -x "$python" ] || python=python3
target=${1:-smoke}
docker build --platform linux/arm64 -q -t srw64-switch-build:devkita64 "$root/tools/release/switch" >/dev/null
# Same absolute path inside the container, so generated files name the same sources. A git
# worktree links its build/ into the main checkout, so that is mounted too.
mounts="-v $root:$root"
common=$(cd "$root" && git rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)
if [ -n "$common" ]; then
    main=$(dirname -- "$common")
    case "$root" in "$main"|"$main"/*) [ "$main" = "$root" ] || mounts="-v $main:$main $mounts";; esac
fi
run() {
    # shellcheck disable=SC2086
    docker run --rm --platform linux/arm64 $mounts -w "$root" -e HOME=/tmp -e PYTHONUTF8=1 \
        -e GIT_CONFIG_COUNT=1 -e GIT_CONFIG_KEY_0=safe.directory -e GIT_CONFIG_VALUE_0='*' \
        srw64-switch-build:devkita64 "$@"
}
case "$target" in
smoke)
    out=build/recomp/switch-smoke
    run sh -c "/opt/devkitpro/portlibs/switch/bin/aarch64-none-elf-cmake -S tools/release/switch/smoke \
        -B $out -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build $out"
    echo "$root/$out/srw64-smoke.nro"
    ;;
game)
    # The runtime patches are applied here, where git owns the checkouts.
    for script in prepare_runtime_lifecycle prepare_switch_runtime; do
        "$python" "$root/tools/recomp/toolchain/$script.py" > /dev/null
    done
    # ICU with its data subset (build_icu.sh): devkitPro has none.
    icu=$root/build/recomp/switch-deps/prefix
    [ -f "$icu/lib/libicuuc.a" ] || "$root/tools/release/switch/build_icu.sh"
    keep=""
    [ "${2:-}" = "-k" ] && keep="-- -k 0"
    out=build/recomp/switch-build
    run sh -c "set -e
        [ -f $out/build.ninja ] || /opt/devkitpro/portlibs/switch/bin/aarch64-none-elf-cmake -S src/host -B $out -G Ninja \
            -DCMAKE_BUILD_TYPE=Release -DSRW64_GL_ONLY=ON -DSRW64_ASSERTS=OFF \
            -DICU_INCLUDE_DIR=$icu/include -DICU_UC_LIBRARY_RELEASE=$icu/lib/libicuuc.a \
            -DICU_I18N_LIBRARY_RELEASE=$icu/lib/libicui18n.a \
            -DSRW64_ICU_DATA_SOURCE=$root/build/recomp/switch-deps/icudt78_dat.c
        cmake --build $out --target srw64-gl-host_nro -j 10 $keep"
    echo "$root/$out/Marchwind64.nro"
    ;;
*)
    echo "usage: $0 smoke | game [-k]" >&2
    exit 2
    ;;
esac
