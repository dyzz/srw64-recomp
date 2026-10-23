#!/bin/sh
# ICU for the Switch (docs/design/switch-port.md, S0): devkitPro has no ICU package, so the
# pinned ICU (config/recomp/macos-dependencies.json) is cross-compiled here, static. Its
# data library is not linked: the subset the game uses goes in as C source instead
# (tools/release/icu_data.py c-source, 1.1 MB, as on Android; ICU's own build rejects a cut
# .dat). In Docker; the result goes to build/recomp/switch-deps/prefix and icudt78_dat.c.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
python=${PYTHON:-$root/.venv/bin/python}
[ -x "$python" ] || python=python3
main=$(dirname -- "$(cd "$root" && git rev-parse --path-format=absolute --git-common-dir)")
source=$main/build/macos-deps/sources/icu/icu/source
[ -x "$source/configure" ] || { echo "ICU sources missing: $source (the macOS dependency build fetches them)"; exit 1; }
deps=$root/build/recomp/switch-deps
mkdir -p "$deps"
"$python" "$root/tools/release/icu_data.py" c-source --out "$deps/icudt78_dat.c" --archives "$main/build/macos-deps/sources"
mounts="-v $root:$root"
[ "$main" = "$root" ] || mounts="-v $main:$main $mounts"
docker build --platform linux/arm64 -q -t srw64-switch-build:devkita64 "$root/tools/release/switch" >/dev/null
# shellcheck disable=SC2086
exec docker run --rm --platform linux/arm64 $mounts -w "$deps" -e HOME=/tmp srw64-switch-build:devkita64 sh -c "
    set -e
    [ -d icu-src ] || cp -R '$source/..' icu-src
    # configure knows no aarch64-none-elf; ICU's readme says to start from mh-linux.
    cp icu-src/source/config/mh-linux icu-src/source/config/mh-unknown
    if [ ! -x host-icu/bin/icupkg ]; then
        mkdir -p host-icu && cd host-icu
        ../icu-src/source/configure --disable-tests --disable-samples --disable-extras --disable-icuio > configure.log
        make -j10 > make.log
        cd ..
    fi
    export PATH=/opt/devkitpro/devkitA64/bin:\$PATH
    arch='-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -ffunction-sections -fdata-sections -D__SWITCH__'
    mkdir -p icu && cd icu
    if [ ! -f Makefile ]; then
        CC=aarch64-none-elf-gcc CXX=aarch64-none-elf-g++ AR=aarch64-none-elf-ar RANLIB=aarch64-none-elf-ranlib \
        CFLAGS=\"-O2 \$arch\" CXXFLAGS=\"-O2 \$arch\" \
        CPPFLAGS='-I/opt/devkitpro/libnx/include -DU_HAVE_TZSET=0 -DU_HAVE_TZNAME=0 -DU_HAVE_TIMEZONE=0 -DU_ENABLE_DYLOAD=0 -DU_HAVE_MMAP=0 -DU_HAVE_POPEN=0' \
        LDFLAGS=\"-specs=/opt/devkitpro/libnx/switch.specs \$arch -L/opt/devkitpro/libnx/lib\" LIBS='-lnx' \
        ../icu-src/source/configure --host=aarch64-none-elf --with-cross-build=$deps/host-icu --prefix=$deps/prefix \
            --enable-static --disable-shared --with-data-packaging=static \
            --disable-tests --disable-samples --disable-extras --disable-icuio --disable-tools > configure.log
    fi
    make -j10 > make.log 2>&1 || { tail -40 make.log; exit 1; }
    make install > install.log
    ls -la $deps/prefix/lib
"
