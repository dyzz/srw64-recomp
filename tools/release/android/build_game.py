#!/usr/bin/env python3
"""Build the game APK for arm64 Android (docs/design/android-port.md), for the builder's
own phone: it carries code generated from the ROM, like the Steam Deck package.

Inputs as for the Linux build (build/recomp/cpu-bound, build/recomp/audio-probe/audio.cpp,
the prepared build/recomp/upstream, build/fonts), plus the static text libraries from
tools/release/android/build_dependencies.py. The NDK's CMake toolchain builds
tools/release/android/game (SDL3, sdl2-compat, src/host as libmain.so); fonts, dialogue
text and licences go into the APK's assets, which SetupActivity unpacks on first start.
librashader (Vulkan, compiled with Cargo unless already built) goes beside libmain.so and
the built-in RetroArch filters into the assets (docs/native/bezels-and-filters.md).

  tools/release/android/build_game.py [--install] [--run]
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_probe import ROOT, SDK, MIN_SDK, ARCHIVES, latest, cmake_bin, ndk, llvm, run, sources, java, package  # noqa: E402

LIBRASHADER = ROOT / 'build/recomp/thirdparty/librashader/android/librashader.so'
# Built-in filters a phone GPU cannot run at full speed: crt-lottes samples so much per
# pixel that the Seeker (Mali-G615 MC2) shows 6-8 FPS at any line count
# (docs/native/bezels-and-filters.md). A player can still add it to their own folder.
PHONE_SKIPS = ('crt-lottes.slangp', 'crt-lottes.slang')
APP = Path(__file__).resolve().parent / 'app'
GAME = Path(__file__).resolve().parent / 'game'
PACKAGE, ACTIVITY = 'org.srw64.game', 'org.srw64.game.SetupActivity'


def native(build: Path, source: dict[str, Path], prefix: Path, file_to_c: Path, prepare: bool = True,
           quiet_game_code: bool = False) -> list[Path]:
    ndk_root = ndk()
    cmake = cmake_bin()
    if prepare:
        run(sys.executable, ROOT / 'tools/recomp/toolchain/prepare_rt64.py', stdout=subprocess.DEVNULL)
    run(cmake / 'cmake', '-S', GAME, '-B', build, '-G', 'Ninja',
        f'-DCMAKE_MAKE_PROGRAM={cmake / "ninja"}',
        f'-DCMAKE_TOOLCHAIN_FILE={ndk_root / "build/cmake/android.toolchain.cmake"}',
        '-DANDROID_ABI=arm64-v8a', f'-DANDROID_PLATFORM=android-{MIN_SDK}', '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
        f'-DSRW64_SDL3_SOURCE={source["sdl3"]}', f'-DSRW64_SDL2_COMPAT_SOURCE={source["sdl2-compat"]}',
        f'-DSRW64_ANDROID_PREFIX={prefix}', f'-DRT64_FILE_TO_C={file_to_c}', f'-DPython3_EXECUTABLE={sys.executable}')
    jobs = str(os.cpu_count() or 8)
    if quiet_game_code:
        # The code generated from the ROM stays out of the log (a public CI log): its
        # compiler output goes to a file, and only the error count is shown.
        log = build / 'game-code-build.log'
        with log.open('w') as output:
            failed = subprocess.run([str(cmake / 'cmake'), '--build', str(build), '--target', 'srw64_cpu', '-j', jobs],
                                    stdout=output, stderr=subprocess.STDOUT).returncode
        if failed:
            errors = sum(line.count('error:') for line in log.read_text(errors='replace').splitlines())
            raise SystemExit(f'srw64_cpu failed: {errors} errors (output kept out of the log)')
        log.unlink()
    run(cmake / 'cmake', '--build', build, '--target', 'srw64-gfx-host', 'SDL3-shared', '-j', jobs)
    libraries = [build / 'sdl3/libSDL3.so', build / 'sdl2-compat/libSDL2.so', build / 'host/libmain.so', LIBRASHADER]
    strip = llvm(ndk_root) / 'bin/llvm-strip'
    stripped = build / 'stripped'
    stripped.mkdir(exist_ok=True)
    for library in libraries:
        run(strip, '--strip-unneeded', '-o', stripped / library.name, library)
    return [stripped / library.name for library in libraries]


def host_file_to_c(build: Path) -> Path:
    """RT64's file_to_c, built for the build machine."""
    tool = build / 'host-tools/file_to_c'
    source = ROOT / 'build/recomp/upstream/RT64/src/tools/file_to_c/file_to_c.cpp'
    if not tool.exists() or tool.stat().st_mtime < source.stat().st_mtime:
        tool.parent.mkdir(parents=True, exist_ok=True)
        run('clang++', '-std=c++17', '-O2', '-o', tool, source)
    return tool


def stage_assets(build: Path, deps: Path) -> Path:
    assets = build / 'assets'
    shutil.rmtree(assets, ignore_errors=True)
    resources = assets / 'resources'
    fonts = resources / 'fonts'
    fonts.mkdir(parents=True)
    for path in sorted((ROOT / 'build/fonts').iterdir()):
        if path.suffix in ('.ttf', '.txt'):
            shutil.copyfile(path, fonts / path.name)
    if not (fonts / 'HarmonyOS_Sans_SC.ttf').exists():
        raise SystemExit('build/fonts has no HarmonyOS Sans: run tools/content/prepare_fonts.py')
    text = ROOT / 'content/dialogue'
    for path in sorted(text.rglob('*.txt')):
        target = resources / 'dialogue' / path.relative_to(text)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
    filters = ROOT / 'build/filters'
    shutil.copytree(filters, resources / 'filters', ignore=shutil.ignore_patterns('.commit', *PHONE_SKIPS))
    notice = resources / 'filters/NOTICE.txt'
    notice.write_text(''.join(line for line in notice.read_text().splitlines(keepends=True)
                              if not line.strip().endswith(PHONE_SKIPS)))
    licenses = resources / 'licenses'
    licenses.mkdir()
    shutil.copyfile(LIBRASHADER.parent / 'LICENSE.md', licenses / 'librashader-LICENSE.md')
    archives = ARCHIVES
    for name, pattern in (('sdl3', 'LICENSE.txt'), ('sdl2-compat', 'LICENSE.txt'), ('freetype', 'LICENSE.TXT'),
                          ('harfbuzz', 'COPYING'), ('icu', 'LICENSE')):
        found = sorted((archives / name).glob(f'*/{pattern}'))
        if found:
            shutil.copyfile(found[0], licenses / f'{name}-{pattern}')
    return assets


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build', type=Path, default=ROOT / 'build/android/game')
    parser.add_argument('--install', action='store_true', help='adb install the APK')
    parser.add_argument('--run', action='store_true', help='start it and follow its log')
    parser.add_argument('--no-prepare', action='store_true', help='keep local RT64 experiments (skip prepare_rt64.py)')
    parser.add_argument('--quiet-game-code', action='store_true',
                        help='keep the generated game code\'s compiler output out of the log (CI)')
    parser.add_argument('--validation', type=Path, metavar='SO',
                        help="Khronos libVkLayer_khronos_validation.so (arm64) to ship; Plume enables it (the host builds without NDEBUG)")
    args = parser.parse_args()
    prefix = ROOT / 'build/android/deps/prefix'
    if not (prefix / 'lib/libicuuc.a').exists():
        raise SystemExit('Build the text libraries first: tools/release/android/build_dependencies.py')
    source = sources()
    run(sys.executable, ROOT / 'tools/recomp/toolchain/fetch_librashader.py', '--android')
    run(sys.executable, ROOT / 'tools/content/fetch_filters.py')
    tools = latest(SDK / 'build-tools')
    android_jar = latest(SDK / 'platforms') / 'android.jar'
    libraries = native(args.build, source, prefix, host_file_to_c(args.build.parent), not args.no_prepare,
                       args.quiet_game_code)
    if args.validation:
        libraries.append(args.validation.resolve(strict=True))
    dex = java(args.build, source['sdl3'], android_jar, tools, APP / 'java')
    assets = stage_assets(args.build, prefix)
    apk = package(args.build, libraries, dex, android_jar, tools, APP, assets, 'srw64-android')
    print(f'SRW64_ANDROID_GAME {apk} {apk.stat().st_size} bytes')
    if args.install or args.run:
        run('adb', 'install', '-r', apk)
    if args.run:
        run('adb', 'logcat', '-c')
        run('adb', 'shell', 'am', 'start', '-n', f'{PACKAGE}/{ACTIVITY}')
        subprocess.run(['adb', 'logcat', '-s', 'SRW64:*', 'SDL:*', 'SDL/APP:*', 'AndroidRuntime:E', 'DEBUG:*'])
    return 0


if __name__ == '__main__':
    sys.exit(main())
