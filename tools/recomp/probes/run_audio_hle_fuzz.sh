#!/bin/sh
# Builds and runs audio_hle_fuzz.cpp: the native audio microcode against the recompiled one.
#   tools/recomp/probes/run_audio_hle_fuzz.sh [cases per command] [seed]
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
RUNTIME=$ROOT/build/recomp/upstream/N64ModernRuntime
HLE=$ROOT/src/thirdparty/mupen64plus-rsp-hle
OUT=$ROOT/build/recomp/audio-hle-fuzz
mkdir -p "$OUT"
for file in alist alist_audio audio memory; do
    clang -O2 -c "$HLE/$file.c" -o "$OUT/$file.o"
done
clang++ -std=c++20 -O2 -w -I"$RUNTIME/librecomp/include" -I"$RUNTIME/librecomp/include/librecomp" -I"$RUNTIME/ultramodern/include" \
    -I"$RUNTIME/thirdparty/sse2neon" -I"$ROOT/build/recomp/upstream/N64Recomp/include" \
    "$ROOT/build/recomp/audio-probe/audio.cpp" "$RUNTIME/librecomp/src/rsp.cpp" "$ROOT/tools/recomp/probes/audio_hle_fuzz.cpp" \
    "$OUT"/alist.o "$OUT"/alist_audio.o "$OUT"/audio.o "$OUT"/memory.o -o "$OUT/audio-hle-fuzz"
exec "$OUT/audio-hle-fuzz" "$ROOT/build/recomp/audio-probe/rdram-after-native.bin" "$@"
