#!/bin/sh
# The game for the PS Vita (docs/design/psv-port.md, V2): srw64-gl-host (src/host with
# SRW64_GL_ONLY) cross-compiled with VitaSDK in Docker, into build/recomp/vita-build.
# Run `make` first (generated CPU code, pinned upstream checkouts, the RSP audio code).
#   tools/release/vita/build_game.sh            configure if needed and build
#   tools/release/vita/build_game.sh -k         keep going past errors (to see them all)
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
python=${PYTHON:-$root/.venv/bin/python}
[ -x "$python" ] || python=python3
# The runtime patches are applied here, where git owns the checkouts (as build.sh does).
for script in prepare_runtime_lifecycle prepare_vita_runtime; do
    "$python" "$root/tools/recomp/toolchain/$script.py" > /dev/null
done
docker build -q --platform linux/arm64 -t srw64-vita-build:vitasdk "$root/tools/release/vita" > /dev/null
mounts="-v $root:$root"
common=$(cd "$root" && git rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)
if [ -n "$common" ]; then
    main=$(dirname -- "$common")
    case "$root" in "$main"|"$main"/*) [ "$main" = "$root" ] || mounts="-v $main:$main $mounts";; esac
fi
keep=""
[ "${1:-}" = "-k" ] && keep="-- -k 0"
build=build/recomp/vita-build
# ICU with break iteration (build_icu.sh): VitaSDK's has none.
icu=$root/build/recomp/vita-deps/prefix
[ -f "$icu/lib/libicuuc.a" ] || "$root/tools/release/vita/build_icu.sh"
# shellcheck disable=SC2086
docker run --rm --platform linux/arm64 $mounts -w "$root" -e HOME=/tmp -e PYTHONUTF8=1 \
    -e GIT_CONFIG_COUNT=1 -e GIT_CONFIG_KEY_0=safe.directory -e GIT_CONFIG_VALUE_0='*' \
    srw64-vita-build:vitasdk sh -c "
        set -e
        [ -f $build/build.ninja ] || cmake -S src/host -B $build -G Ninja \
            -DCMAKE_TOOLCHAIN_FILE=\$VITASDK/share/vita.toolchain.cmake -DCMAKE_BUILD_TYPE=Release \
            -DSRW64_GL_ONLY=ON -DSRW64_ASSERTS=OFF \
            -DICU_INCLUDE_DIR=$icu/include -DICU_UC_LIBRARY_RELEASE=$icu/lib/libicuuc.a \
            -DICU_I18N_LIBRARY_RELEASE=$icu/lib/libicui18n.a -DICU_DATA_LIBRARY_RELEASE=$icu/lib/libicudata.a
        cmake --build $build --target eboot.bin srw64-vita-sfo -j 10 $keep"
"$python" "$root/tools/release/vita/package_vpk.py" --build "$root/$build"
