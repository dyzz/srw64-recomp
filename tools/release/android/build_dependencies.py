#!/usr/bin/env python3
"""Build the host's text libraries for arm64 Android (docs/design/android-port.md).

FreeType, HarfBuzz and ICU from the same pinned sources as the macOS build
(config/recomp/macos-dependencies.json), as static libraries in
build/android/deps/prefix, so libmain.so carries them (an APK cannot ship ICU's
versioned shared libraries). ICU cross-compiles against the macOS ICU build tree that
tools/release/build_macos_dependencies.py leaves (its tools generate the data).
SDL comes from the game's own CMake build.

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

ROOT = Path(__file__).resolve().parents[3]
MAIN_TREE = Path(os.environ.get('SRW64_MAIN_TREE', ROOT))
SDK = Path(os.environ.get('ANDROID_HOME', Path.home() / 'Library/Android/sdk'))
API = 28
TRIPLE = 'aarch64-linux-android'


def latest(directory: Path) -> Path:
    return sorted((p for p in directory.iterdir() if p.is_dir()),
                  key=lambda p: [int(x) for x in p.name.split('.') if x.isdigit()])[-1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--jobs', type=int, default=os.cpu_count() or 8)
    args = parser.parse_args()
    lock = json.loads((ROOT / 'config/recomp/macos-dependencies.json').read_text())
    cache = MAIN_TREE / 'build/macos-deps'
    sources = {}
    for name in ('freetype', 'harfbuzz', 'icu'):
        stamp = cache / 'sources' / f'{name}.sha256'
        if not stamp.exists() or stamp.read_text().strip() != lock['sources'][name]['sha256']:
            raise SystemExit(f'{name}: unpack the pinned sources with tools/release/build_macos_dependencies.py first')
        sources[name] = next((cache / 'sources' / name).iterdir())
    host_icu = cache / f'{lock["minimum_macos"]}-{lock["architecture"]}' / 'icu'
    if not (host_icu / 'bin').exists():
        raise SystemExit(f'No macOS ICU build tree at {host_icu} (tools/release/build_macos_dependencies.py)')
    ndk = latest(SDK / 'ndk')
    toolchain = ndk / 'toolchains/llvm/prebuilt/darwin-x86_64'
    cmake_bin = latest(SDK / 'cmake') / 'bin'
    work = ROOT / 'build/android/deps'
    prefix = work / 'prefix'
    prefix.mkdir(parents=True, exist_ok=True)

    def run(argv, cwd=None, env=None):
        print('+', ' '.join(str(a) for a in argv), flush=True)
        subprocess.run([str(a) for a in argv], cwd=cwd, env=env, check=True)

    def cmake(name: str, options: list[str]) -> None:
        build = work / name
        run([cmake_bin / 'cmake', '-S', sources[name], '-B', build, '-G', 'Ninja',
             f'-DCMAKE_MAKE_PROGRAM={cmake_bin / "ninja"}',
             f'-DCMAKE_TOOLCHAIN_FILE={ndk / "build/cmake/android.toolchain.cmake"}',
             '-DANDROID_ABI=arm64-v8a', f'-DANDROID_PLATFORM=android-{API}', '-DCMAKE_BUILD_TYPE=Release',
             '-DCMAKE_POSITION_INDEPENDENT_CODE=ON', f'-DCMAKE_INSTALL_PREFIX={prefix}',
             '-DCMAKE_INSTALL_INCLUDEDIR=include', '-DCMAKE_INSTALL_LIBDIR=lib',
             f'-DCMAKE_FIND_ROOT_PATH={prefix}', f'-DCMAKE_PREFIX_PATH={prefix}', '-DBUILD_SHARED_LIBS=OFF', *options])
        run([cmake_bin / 'cmake', '--build', build, '--parallel', args.jobs])
        run([cmake_bin / 'cmake', '--install', build])

    cmake('freetype', ['-DFT_DISABLE_HARFBUZZ=ON', '-DFT_DISABLE_PNG=ON', '-DFT_DISABLE_BROTLI=ON',
                       '-DFT_DISABLE_BZIP2=ON', '-DFT_DISABLE_ZLIB=ON'])
    cmake('harfbuzz', ['-DHB_HAVE_FREETYPE=ON', '-DHB_HAVE_GLIB=OFF', '-DHB_HAVE_GRAPHITE2=OFF',
                       '-DHB_HAVE_ICU=OFF', '-DHB_BUILD_UTILS=OFF', '-DHB_BUILD_SUBSET=OFF',
                       '-DHB_BUILD_RASTER=OFF', '-DHB_BUILD_VECTOR=OFF', '-DHB_BUILD_GPU=OFF',
                       f'-DFREETYPE_INCLUDE_DIRS={prefix}/include/freetype2',
                       f'-DFREETYPE_LIBRARY={prefix}/lib/libfreetype.a'])
    icu = work / 'icu'
    icu.mkdir(exist_ok=True)
    env = {k: v for k, v in os.environ.items() if k not in ('CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS')}
    env.update(CC=str(toolchain / f'bin/{TRIPLE}{API}-clang'), CXX=str(toolchain / f'bin/{TRIPLE}{API}-clang++'),
               AR=str(toolchain / 'bin/llvm-ar'), RANLIB=str(toolchain / 'bin/llvm-ranlib'),
               CFLAGS='-O2 -fPIC', CXXFLAGS='-O2 -fPIC')
    if not (icu / 'Makefile').exists():
        run([sources['icu'] / 'source/configure', f'--host={TRIPLE}', f'--with-cross-build={host_icu}',
             f'--prefix={prefix}', '--enable-static', '--disable-shared', '--with-data-packaging=static',
             '--disable-tests', '--disable-samples', '--disable-extras', '--disable-icuio', '--disable-tools'], icu, env)
    run(['make', f'-j{args.jobs}'], icu, env)
    run(['make', 'install'], icu, env)
    print(f'SRW64_ANDROID_DEPENDENCIES {prefix}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
