#!/bin/sh
# ICU for the PS Vita (docs/design/psv-port.md, V2): VitaSDK's own ICU is built without
# break iteration (no ubrk_*), which the text layout needs, so the pinned ICU
# (config/recomp/macos-dependencies.json) is cross-compiled here, static, with its data
# inside and trimmed by icu-data-filter.json (line breaking, properties, normalization,
# English, Japanese and Chinese). In Docker; the result goes to build/recomp/vita-deps/prefix.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
main=$(dirname -- "$(cd "$root" && git rev-parse --path-format=absolute --git-common-dir)")
source=$main/build/macos-deps/sources/icu/icu/source
[ -x "$source/configure" ] || { echo "ICU sources missing: $source (the macOS dependency build fetches them)"; exit 1; }
mounts="-v $root:$root"
[ "$main" = "$root" ] || mounts="-v $main:$main $mounts"
deps=$root/build/recomp/vita-deps
mkdir -p "$deps"
# shellcheck disable=SC2086
exec docker run --rm --platform linux/arm64 $mounts -w "$deps" -e HOME=/tmp srw64-vita-build:vitasdk sh -c "
    set -e
    [ -d icu-src ] || cp -R '$source/..' icu-src
    # configure knows no arm-vita-eabi; ICU's readme says to start from mh-linux.
    cp icu-src/source/config/mh-linux icu-src/source/config/mh-unknown
    python3 $root/tools/release/vita/icu_patch.py icu-src
    if [ ! -x host-icu/bin/icupkg ]; then
        mkdir -p host-icu && cd host-icu
        ../icu-src/source/configure --disable-tests --disable-samples --disable-extras --disable-icuio > configure.log
        make -j10 > make.log
        cd ..
    fi
    # The sources carry only the prebuilt data archive: trimmed with icupkg (icu_trim.py
    # names what goes), then packaged into libicudata.a as the full one would be.
    in=icu-src/source/data/in
    mkdir -p \$in/full
    [ -f \$in/full/icudt78l.dat ] || mv \$in/icudt78l.dat \$in/full/icudt78l.dat
    export LD_LIBRARY_PATH=$deps/host-icu/lib
    host-icu/bin/icupkg -l \$in/full/icudt78l.dat | python3 $root/tools/release/vita/icu_trim.py > remove.lst
    rm -f \$in/icudt78l.dat
    host-icu/bin/icupkg -r remove.lst \$in/full/icudt78l.dat \$in/icudt78l.dat
    ls -la \$in/full/icudt78l.dat \$in/icudt78l.dat
    mkdir -p icu && cd icu
    if [ ! -f Makefile ]; then
        CC=arm-vita-eabi-gcc CXX=arm-vita-eabi-g++ AR=arm-vita-eabi-ar RANLIB=arm-vita-eabi-ranlib \
        CFLAGS='-O2' CXXFLAGS='-O2' \
        CPPFLAGS='-DU_HAVE_TZSET=0 -DU_HAVE_TZNAME=0 -DU_HAVE_TIMEZONE=0 -DU_ENABLE_DYLOAD=0 -DU_HAVE_MMAP=0 -DU_HAVE_POPEN=0' \
        ../icu-src/source/configure --host=arm-vita-eabi --with-cross-build=$deps/host-icu --prefix=$deps/prefix \
            --enable-static --disable-shared --with-data-packaging=static \
            --disable-tests --disable-samples --disable-extras --disable-icuio --disable-tools > configure.log
    fi
    make -j10 > make.log 2>&1 || { tail -40 make.log; exit 1; }
    make install > install.log
    ls -la $deps/prefix/lib
"
