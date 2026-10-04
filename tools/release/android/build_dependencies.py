#!/usr/bin/env python3
"""Build the host's text libraries for arm64 Android (docs/design/android-port.md).

FreeType, HarfBuzz and ICU from the same pinned sources as the macOS build
(config/recomp/macos-dependencies.json), as static libraries in
build/android/deps/prefix, so libmain.so carries them (an APK cannot ship ICU's
versioned shared libraries). ICU cross-compiles against an ICU built for the build
machine, whose tools generate the data: the macOS tree that
tools/release/build_macos_dependencies.py leaves, or else one built here
(build/android/deps/host-icu, the Linux CI job). SDL comes from the game's own CMake build.

  tools/release/android/build_dependencies.py [--jobs 8]
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_probe import ROOT, MAIN_TREE, cmake_bin, ndk, llvm, sources  # noqa: E402
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import icu_data  # noqa: E402

API = 28
TRIPLE = 'aarch64-linux-android'


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--jobs', type=int, default=os.cpu_count() or 8)
    args = parser.parse_args()
    lock = json.loads((ROOT / 'config/recomp/macos-dependencies.json').read_text())
    src = sources(('freetype', 'harfbuzz', 'icu'))
    # Only the break rules the game reads (tools/release/icu_data.py): 31.6 MB of data -> 0.5 MB.
    icu_data.prepare_source(src['icu'])
    ndk_root = ndk()
    toolchain = llvm(ndk_root)
    tools = cmake_bin()
    work = ROOT / 'build/android/deps'
    prefix = work / 'prefix'
    prefix.mkdir(parents=True, exist_ok=True)

    def run(argv, cwd=None, env=None):
        print('+', ' '.join(str(a) for a in argv), flush=True)
        subprocess.run([str(a) for a in argv], cwd=cwd, env=env, check=True)

    host_icu = MAIN_TREE / 'build/macos-deps' / f'{lock["minimum_macos"]}-{lock["architecture"]}' / 'icu'
    if sys.platform != 'darwin' or not (host_icu / 'bin').exists():
        # ICU for this machine, only for its data tools (the cross build's --with-cross-build).
        host_icu = work / 'host-icu'
        if not (host_icu / 'bin/icupkg').exists():
            host_icu.mkdir(parents=True, exist_ok=True)
            host_env = {k: v for k, v in os.environ.items() if k not in ('CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS')}
            run([src['icu'] / 'source/configure', '--disable-tests', '--disable-samples', '--disable-extras',
                 '--disable-icuio'], host_icu, host_env)
            run(['make', f'-j{args.jobs}'], host_icu, host_env)

    def cmake(name: str, options: list[str]) -> None:
        build = work / name
        run([tools / 'cmake', '-S', src[name], '-B', build, '-G', 'Ninja',
             f'-DCMAKE_MAKE_PROGRAM={tools / "ninja"}',
             f'-DCMAKE_TOOLCHAIN_FILE={ndk_root / "build/cmake/android.toolchain.cmake"}',
             '-DANDROID_ABI=arm64-v8a', f'-DANDROID_PLATFORM=android-{API}', '-DCMAKE_BUILD_TYPE=Release',
             '-DCMAKE_POSITION_INDEPENDENT_CODE=ON', f'-DCMAKE_INSTALL_PREFIX={prefix}',
             '-DCMAKE_INSTALL_INCLUDEDIR=include', '-DCMAKE_INSTALL_LIBDIR=lib',
             f'-DCMAKE_FIND_ROOT_PATH={prefix}', f'-DCMAKE_PREFIX_PATH={prefix}', '-DBUILD_SHARED_LIBS=OFF', *options])
        run([tools / 'cmake', '--build', build, '--parallel', args.jobs])
        run([tools / 'cmake', '--install', build])

    cmake('freetype', ['-DFT_DISABLE_HARFBUZZ=ON', '-DFT_DISABLE_PNG=ON', '-DFT_DISABLE_BROTLI=ON',
                       '-DFT_DISABLE_BZIP2=ON', '-DFT_DISABLE_ZLIB=ON'])
    cmake('harfbuzz', ['-DHB_HAVE_FREETYPE=ON', '-DHB_HAVE_GLIB=OFF', '-DHB_HAVE_GRAPHITE2=OFF',
                       '-DHB_HAVE_ICU=OFF', '-DHB_BUILD_UTILS=OFF', '-DHB_BUILD_SUBSET=OFF',
                       '-DHB_BUILD_RASTER=OFF', '-DHB_BUILD_VECTOR=OFF', '-DHB_BUILD_GPU=OFF',
                       f'-DFREETYPE_INCLUDE_DIRS={prefix}/include/freetype2',
                       f'-DFREETYPE_LIBRARY={prefix}/lib/libfreetype.a'])
    icu = work / 'icu'
    stamp = icu / 'srw64-icu-data.txt'
    if icu.exists() and (not stamp.is_file() or stamp.read_text().strip() != icu_data.digest()):
        shutil.rmtree(icu)   # built with other data
    icu.mkdir(exist_ok=True)
    stamp.write_text(icu_data.digest() + '\n')
    env = {k: v for k, v in os.environ.items() if k not in ('CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS')}
    env.update(CC=str(toolchain / f'bin/{TRIPLE}{API}-clang'), CXX=str(toolchain / f'bin/{TRIPLE}{API}-clang++'),
               AR=str(toolchain / 'bin/llvm-ar'), RANLIB=str(toolchain / 'bin/llvm-ranlib'),
               CFLAGS='-O2 -fPIC', CXXFLAGS='-O2 -fPIC')
    if not (icu / 'Makefile').exists():
        run([src['icu'] / 'source/configure', f'--host={TRIPLE}', f'--with-cross-build={host_icu}',
             f'--prefix={prefix}', '--enable-static', '--disable-shared', '--with-data-packaging=static',
             '--disable-tests', '--disable-samples', '--disable-extras', '--disable-icuio', '--disable-tools'], icu, env)
    run(['make', f'-j{args.jobs}'], icu, env)
    run(['make', 'install'], icu, env)
    print(f'SRW64_ANDROID_DEPENDENCIES {prefix}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
