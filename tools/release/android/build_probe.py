#!/usr/bin/env python3
"""Build the Android A0 probe APK (docs/design/android-port.md) without Gradle.

Native code: the NDK's CMake toolchain builds SDL3, sdl2-compat and Plume from the
pinned sources and the probe as libmain.so. Java: SDL3's glue and ProbeActivity go
through javac and d8; aapt2 links the manifest; zipalign keeps the libraries
uncompressed on 16 KB pages; apksigner signs with a key kept under build/android.

  tools/release/android/build_probe.py [--install] [--run]
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent / 'probe'
MAIN_TREE = Path(os.environ.get('SRW64_MAIN_TREE', ROOT))
SDK = Path(os.environ.get('ANDROID_HOME', Path.home() / 'Library/Android/sdk'))
PACKAGE, ACTIVITY = 'org.srw64.probe', 'org.srw64.probe.ProbeActivity'
MIN_SDK, TARGET_SDK = 28, 35
NDK_VERSION = '28.2.13676358'   # sdkmanager "ndk;28.2.13676358"
CMAKE_VERSION = '3.31.6'        # sdkmanager "cmake;3.31.6"
sys.path.insert(0, str(ROOT / 'tools/release'))
from build_macos_dependencies import source  # noqa: E402  the pinned archives, checked


def latest(directory: Path) -> Path:
    versions = sorted((p for p in directory.iterdir() if p.is_dir()), key=lambda p: [int(x) for x in p.name.split('.') if x.isdigit()])
    if not versions:
        raise SystemExit(f'Nothing installed in {directory}')
    return versions[-1]


def ndk() -> Path:
    pinned = SDK / 'ndk' / NDK_VERSION
    if not pinned.is_dir():
        raise SystemExit(f'Install the pinned NDK: sdkmanager "ndk;{NDK_VERSION}"')
    return pinned


def cmake_bin() -> Path:
    pinned = SDK / 'cmake' / CMAKE_VERSION / 'bin'
    if not pinned.is_dir():
        raise SystemExit(f'Install the pinned CMake: sdkmanager "cmake;{CMAKE_VERSION}"')
    return pinned


def llvm(ndk_root: Path) -> Path:
    """The NDK's toolchain for this machine (darwin-x86_64 or linux-x86_64)."""
    hosts = [p for p in (ndk_root / 'toolchains/llvm/prebuilt').iterdir() if p.is_dir()]
    if len(hosts) != 1:
        raise SystemExit(f'Expected one host toolchain in {ndk_root}')
    return hosts[0]


def javac() -> Path:
    if os.environ.get('JAVA_HOME'):
        return Path(os.environ['JAVA_HOME']) / 'bin/javac'
    return Path(subprocess.run(['/usr/libexec/java_home', '-v', '17'], capture_output=True, text=True, check=True).stdout.strip()) / 'bin/javac'


def run(*command, **kwargs):
    print('+', ' '.join(str(c) for c in command), flush=True)
    return subprocess.run([str(c) for c in command], check=True, **kwargs)


ARCHIVES = MAIN_TREE / 'build/macos-deps/sources'   # the archive cache macOS and Linux share


def sources(names=('sdl3', 'sdl2-compat')) -> dict[str, Path]:
    """The pinned sources (config/recomp/macos-dependencies.json), fetched and checked once."""
    lock = json.loads((ROOT / 'config/recomp/macos-dependencies.json').read_text())['sources']
    ARCHIVES.mkdir(parents=True, exist_ok=True)
    found = {name: source(name, lock[name], ARCHIVES) for name in names}
    found['plume'] = MAIN_TREE / 'build/recomp/upstream/RT64/src/contrib/plume'
    return found


def native(build: Path, source: dict[str, Path]) -> list[Path]:
    ndk_root = ndk()
    cmake = cmake_bin()
    run(cmake / 'cmake', '-S', HERE, '-B', build, '-G', 'Ninja',
        f'-DCMAKE_MAKE_PROGRAM={cmake / "ninja"}',
        f'-DCMAKE_TOOLCHAIN_FILE={ndk_root / "build/cmake/android.toolchain.cmake"}',
        '-DANDROID_ABI=arm64-v8a', f'-DANDROID_PLATFORM=android-{MIN_SDK}', '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
        f'-DSRW64_SDL3_SOURCE={source["sdl3"]}', f'-DSRW64_SDL2_COMPAT_SOURCE={source["sdl2-compat"]}',
        f'-DSRW64_PLUME_SOURCE={source["plume"]}')
    run(cmake / 'cmake', '--build', build, '-j', str(os.cpu_count() or 8))
    libraries = [build / 'sdl3/libSDL3.so', build / 'sdl2-compat/libSDL2.so', build / 'libmain.so']
    strip = llvm(ndk_root) / 'bin/llvm-strip'
    stripped = build / 'stripped'
    stripped.mkdir(exist_ok=True)
    for library in libraries:
        run(strip, '--strip-unneeded', '-o', stripped / library.name, library)
    return [stripped / library.name for library in libraries]


def java(build: Path, sdl3: Path, android_jar: Path, tools: Path, own: Path = HERE / 'java') -> Path:
    classes, dex = build / 'classes', build / 'dex'
    for directory in (classes, dex):
        shutil.rmtree(directory, ignore_errors=True)
        directory.mkdir(parents=True)
    files = sorted((sdl3 / 'android-project/app/src/main/java').rglob('*.java')) + sorted(own.rglob('*.java'))
    run(javac(), '-nowarn', '-Xlint:none', '-source', '11', '-target', '11', '-encoding', 'UTF-8',
        '-classpath', android_jar, '-d', classes, *files)
    run(tools / 'd8', '--release', '--min-api', MIN_SDK, '--lib', android_jar, '--output', dex,
        *sorted(classes.rglob('*.class')))
    return dex / 'classes.dex'


def package(build: Path, libraries: list[Path], dex: Path, android_jar: Path, tools: Path, app: Path = HERE,
            assets: Path | None = None, name: str = 'srw64-android-probe') -> Path:
    compiled = build / 'res.zip'
    run(tools / 'aapt2', 'compile', '--dir', app / 'res', '-o', compiled)
    linked = build / 'linked.apk'
    run(tools / 'aapt2', 'link', '-o', linked, '-I', android_jar, '--manifest', app / 'AndroidManifest.xml',
        '--min-sdk-version', MIN_SDK, '--target-sdk-version', TARGET_SDK, *(['-A', assets] if assets else []), compiled)
    unaligned = build / 'unaligned.apk'
    with zipfile.ZipFile(linked) as source, zipfile.ZipFile(unaligned, 'w') as target:
        for item in source.infolist():
            target.writestr(item, source.read(item.filename))
        target.write(dex, 'classes.dex', compress_type=zipfile.ZIP_DEFLATED)
        # extractNativeLibs=false: the loader maps them straight from the APK.
        for library in libraries:
            target.write(library, f'lib/arm64-v8a/{library.name}', compress_type=zipfile.ZIP_STORED)
    aligned = build / 'aligned.apk'
    run(tools / 'zipalign', '-f', '-P', '16', '4', unaligned, aligned)
    # The release key lets a build install over any other (and keep the player's files):
    # SRW64_ANDROID_KEYSTORE with its password in SRW64_ANDROID_KEYSTORE_PASSWORD (CI), or
    # ~/.config/srw64/android-release.keystore with the password in the .password file
    # beside it. Without one, a key kept under build/android.
    local = Path.home() / '.config/srw64/android-release.keystore'
    if os.environ.get('SRW64_ANDROID_KEYSTORE'):
        keystore, password = Path(os.environ['SRW64_ANDROID_KEYSTORE']), 'env:SRW64_ANDROID_KEYSTORE_PASSWORD'
    elif local.is_file():
        keystore, password = local, f'file:{local.with_suffix(".password")}'
    else:
        keystore, password = None, 'pass:android'
    if keystore is None:
        keystore = build.parent / 'probe-debug.keystore'
        if not keystore.exists():
            run('keytool', '-genkeypair', '-keystore', keystore, '-storepass', 'android', '-keypass', 'android',
                '-alias', 'probe', '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000',
                '-dname', 'CN=SRW64 probe')
    apk = build / f'{name}.apk'
    run(tools / 'apksigner', 'sign', '--ks', keystore, '--ks-pass', password, '--out', apk, aligned)
    return apk


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build', type=Path, default=ROOT / 'build/android/probe')
    parser.add_argument('--install', action='store_true', help='adb install the APK')
    parser.add_argument('--run', action='store_true', help='start it and follow its log')
    args = parser.parse_args()
    source = sources()
    tools = latest(SDK / 'build-tools')
    android_jar = latest(SDK / 'platforms') / 'android.jar'
    libraries = native(args.build, source)
    dex = java(args.build, source['sdl3'], android_jar, tools)
    apk = package(args.build, libraries, dex, android_jar, tools)
    print(f'SRW64_ANDROID_PROBE {apk} {apk.stat().st_size} bytes')
    if args.install or args.run:
        run('adb', 'install', '-r', apk)
    if args.run:
        run('adb', 'logcat', '-c')
        run('adb', 'shell', 'am', 'start', '-n', f'{PACKAGE}/{ACTIVITY}')
        subprocess.run(['adb', 'logcat', '-s', 'SRW64:*', 'SDL:*', 'SDL2COMPAT:*', 'sdl2-compat:*', 'AndroidRuntime:E', 'DEBUG:*'])
    return 0


if __name__ == '__main__':
    sys.exit(main())
