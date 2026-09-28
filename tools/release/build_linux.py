#!/usr/bin/env python3
"""Build the Linux x64 game host, its runtime libraries and a portable tarball.

Runs on Linux: the Ubuntu 22.04 container in tools/release/linux (glibc 2.35
baseline), started from macOS by tools/release/linux/build.sh. The platform-
neutral inputs come from a normal `make` on any machine: the generated CPU code
(build/recomp/cpu-bound), the RSPRecomp audio source, the pinned and patched
upstream checkouts under build/recomp/upstream, and the prepared fonts
(build/fonts). Everything else is written under build/linux-x64.

The tarball holds no ROM and no imported content, but the program is compiled
from recompiled game code: it is for the builder's own use, like the macOS app.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tarfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_macos_dependencies import source  # the same pinned archives and checks

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / 'build/linux-x64'
# macOS and Linux ship the same pinned SDL3/sdl2-compat, FreeType, HarfBuzz and ICU.
LOCK = ROOT / 'config/recomp/macos-dependencies.json'
ARCHIVES = ROOT / 'build/macos-deps/sources'  # archive cache shared with the macOS recipe
BASELINE_GLIBC = (2, 35)
# Libraries every desktop Linux (and SteamOS) provides; everything else ships in lib/.
SYSTEM = {'linux-vdso.so.1', 'ld-linux-x86-64.so.2', 'libc.so.6', 'libm.so.6', 'libdl.so.2',
          'libpthread.so.0', 'librt.so.1', 'libstdc++.so.6', 'libgcc_s.so.1', 'libz.so.1',
          'libdbus-1.so.3'}


def run(argv: list, cwd: Path | None = None, env: dict | None = None) -> None:
    print('+ ' + ' '.join(str(a) for a in argv), flush=True)
    subprocess.run([str(a) for a in argv], cwd=cwd, env=env, check=True)


def output(argv: list) -> str:
    return subprocess.run([str(a) for a in argv], check=True, capture_output=True, text=True).stdout


def build_dependencies(jobs: int) -> Path:
    lock = json.loads(LOCK.read_text(encoding='utf-8'))
    work = WORK / 'deps'
    prefix = work / 'prefix'
    report = work / 'dependencies.json'
    if report.is_file() and json.loads(report.read_text(encoding='utf-8')).get('sources') == lock['sources']:
        return prefix
    if prefix.exists():
        shutil.rmtree(prefix)
    ARCHIVES.mkdir(parents=True, exist_ok=True)
    sources = {name: source(name, entry, ARCHIVES) for name, entry in lock['sources'].items()}
    env = {k: v for k, v in os.environ.items() if k not in (
        'CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS', 'CMAKE_PREFIX_PATH', 'PKG_CONFIG_PATH', 'LD_LIBRARY_PATH')}
    env.update(PKG_CONFIG_PATH=str(prefix / 'lib/pkgconfig'))

    def cmake(name: str, options: list[str]) -> None:
        build = work / name
        run(['cmake', '-S', sources[name], '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
             '-DCMAKE_C_COMPILER=clang', '-DCMAKE_CXX_COMPILER=clang++',
             f'-DCMAKE_INSTALL_PREFIX={prefix}', '-DCMAKE_INSTALL_INCLUDEDIR=include',
             '-DCMAKE_INSTALL_LIBDIR=lib', f'-DCMAKE_PREFIX_PATH={prefix}',
             '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF', '-DBUILD_SHARED_LIBS=ON',
             f'-DPython3_EXECUTABLE={sys.executable}', *options], env=env)
        run(['cmake', '--build', build, '--parallel', str(jobs)], env=env)
        run(['cmake', '--install', build], env=env)

    # SDL3 loads X11, Wayland, PipeWire/PulseAudio/ALSA and D-Bus at run time.
    cmake('sdl3', ['-DSDL_SHARED=ON', '-DSDL_STATIC=OFF', '-DSDL_TEST_LIBRARY=OFF',
                   '-DSDL_TESTS=OFF', '-DSDL_EXAMPLES=OFF'])
    cmake('sdl2-compat', ['-DSDL2COMPAT_TESTS=OFF', '-DSDL2COMPAT_STATIC=OFF',
                         f'-DSDL3_DIR={prefix}/lib/cmake/SDL3'])
    cmake('freetype', ['-DFT_DISABLE_HARFBUZZ=ON', '-DFT_DISABLE_PNG=ON',
                       '-DFT_DISABLE_BROTLI=ON', '-DFT_DISABLE_BZIP2=ON'])
    cmake('harfbuzz', ['-DHB_HAVE_FREETYPE=ON', '-DHB_HAVE_GLIB=OFF', '-DHB_HAVE_GRAPHITE2=OFF',
                       '-DHB_HAVE_ICU=OFF', '-DHB_BUILD_UTILS=OFF', '-DHB_BUILD_SUBSET=OFF',
                       '-DHB_BUILD_RASTER=OFF', '-DHB_BUILD_VECTOR=OFF', '-DHB_BUILD_GPU=OFF',
                       f'-DFREETYPE_INCLUDE_DIRS={prefix}/include/freetype2',
                       f'-DFREETYPE_LIBRARY={prefix}/lib/libfreetype.so'])
    icu_build = work / 'icu'
    if icu_build.exists():
        shutil.rmtree(icu_build)
    icu_build.mkdir(parents=True)
    icu_env = dict(env, CC='clang', CXX='clang++', CFLAGS='-O2', CXXFLAGS='-O2')
    run([sources['icu'] / 'source/configure', f'--prefix={prefix}', '--enable-shared', '--disable-static',
         '--disable-tests', '--disable-samples', '--disable-extras', '--disable-icuio'], icu_build, icu_env)
    run(['make', f'-j{jobs}'], icu_build, icu_env)
    run(['make', 'install'], icu_build, icu_env)
    report.write_text(json.dumps({'schema': 'srw64.linux-runtime-dependencies.v1', 'sources': lock['sources'],
                                  'prefix': str(prefix)}, indent=2) + '\n', encoding='utf-8')
    return prefix


def check_inputs() -> None:
    generated = json.loads((ROOT / 'build/recomp/cpu-bound/report.json').read_text(encoding='utf-8'))
    if generated.get('status') != 'generated':
        raise SystemExit('CPU generation is incomplete: run make first')
    for item in generated['generated_files']:
        path = ROOT / 'build/recomp/cpu-bound' / item['path']
        if hashlib.sha256(path.read_bytes()).hexdigest() != item['sha256']:
            raise SystemExit(f'Generated source differs from its report: {path}')
    patches = json.loads((ROOT / 'build/recomp/graphics-source-patches.json').read_text(encoding='utf-8'))
    hooks = ROOT / 'build/recomp/upstream/RT64/src/rhi/rt64_render_hooks.h'
    if 'RenderHookPresented' not in hooks.read_text(encoding='utf-8'):
        raise SystemExit('RT64 lacks the presented hook: run tools/recomp/toolchain/prepare_rt64.py')
    for required in (ROOT / 'build/recomp/audio-probe/audio.cpp', ROOT / 'build/fonts/HarmonyOS_Sans_SC.ttf'):
        if not required.is_file():
            raise SystemExit(f'Missing build input {required}: run make and tools/content/prepare_fonts.py')
    print(f"Inputs: RT64 {patches['rt64_commit'][:9]}, {len(generated['generated_files'])} generated files", flush=True)


def build_host(prefix: Path, jobs: int) -> Path:
    venv = WORK / 'venv'
    python = venv / 'bin/python'
    if not python.exists():
        run(['python3.11', '-m', 'venv', venv])
        run([python, '-m', 'pip', 'install', '--quiet', '-r', ROOT / 'requirements.lock'])
    build = WORK / 'gfx-build'
    env = dict(os.environ, PYTHONUTF8='1')
    run(['cmake', '-S', ROOT / 'src/host', '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
         '-DCMAKE_C_COMPILER=clang', '-DCMAKE_CXX_COMPILER=clang++', '-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld',
         '-DSRW64_ENABLE_RT64=ON', f'-DCMAKE_PREFIX_PATH={prefix}', f'-DICU_ROOT={prefix}',
         f'-DPython3_EXECUTABLE={python}',
         # xdg-desktop-portal through D-Bus instead of linking GTK for RT64's file dialog.
         '-DNFD_PORTAL=ON'], env=env)
    run(['cmake', '--build', build, '--target', 'srw64-gfx-host', '--parallel', str(jobs)], env=env)
    return build / 'srw64-gfx-host'


def needed(path: Path) -> list[str]:
    return re.findall(r'\(NEEDED\)\s+Shared library: \[([^\]]+)\]', output(['readelf', '-d', path]))


def glibc_versions(path: Path) -> set[tuple[int, ...]]:
    return {tuple(map(int, v.split('.'))) for v in re.findall(r'GLIBC_(\d+(?:\.\d+)+)', output(['objdump', '-T', path]))}


def package(binary: Path, prefix: Path, hd: Path | None = None) -> Path:
    git = ['git', '-c', 'safe.directory=*', '-C', ROOT]
    revision = output([*git, 'rev-parse', '--short', 'HEAD']).strip()
    # Uncommitted program sources make the commit name misleading.
    if output([*git, 'status', '--porcelain', '--', 'src', 'cmake', 'config', 'content', 'tools/release']).strip():
        revision += '-dirty'
    # The Steam Deck edition; the same program runs on other x86-64 Linux desktops.
    name = f'SRW64-SteamDeck-{revision}'
    stage = WORK / 'package' / name
    if stage.exists():
        shutil.rmtree(stage)
    (stage / 'lib').mkdir(parents=True)
    program = stage / 'srw64'
    shutil.copy2(binary, program)
    run(['strip', '--strip-unneeded', program])
    # Bundle the non-system closure, plus SDL3, which sdl2-compat opens at run time.
    pending, bundled = [*needed(program), 'libSDL3.so.0'], {}
    while pending:
        soname = pending.pop()
        if soname in SYSTEM or soname in bundled:
            continue
        found = prefix / 'lib' / soname
        if not found.exists():
            raise SystemExit(f'{soname} is neither a baseline system library nor built in {prefix}')
        target = stage / 'lib' / soname
        shutil.copyfile(found.resolve(strict=True), target)
        bundled[soname] = target
        pending += needed(target)
    # Strip before patchelf: stripping a patched file misaligns its segments.
    for library in bundled.values():
        run(['strip', '--strip-unneeded', library])
        run(['patchelf', '--set-rpath', '$ORIGIN', library])
    run(['patchelf', '--set-rpath', '$ORIGIN/lib', program])
    # The dynamic loader maps every bundled library before --help prints.
    clean = {k: v for k, v in os.environ.items() if k != 'LD_LIBRARY_PATH'}
    if 'Usage:' not in subprocess.run([program, '--play', '--help'], env=clean, capture_output=True, text=True).stdout:
        raise SystemExit('The packaged program does not load its bundled libraries')
    newest = max(v for path in [program, *bundled.values()] for v in glibc_versions(path))
    if newest > BASELINE_GLIBC:
        raise SystemExit(f'Needs glibc {".".join(map(str, newest))}, above the {BASELINE_GLIBC} baseline')
    for path in [program, *bundled.values()]:
        unknown = set(needed(path)) - SYSTEM - set(bundled)
        if unknown:
            raise SystemExit(f'{path.name} needs unbundled libraries: {sorted(unknown)}')
    fonts = stage / 'fonts'
    fonts.mkdir()
    for path in sorted((ROOT / 'build/fonts').iterdir()):
        if path.suffix in ('.ttf', '.txt'):
            shutil.copyfile(path, fonts / path.name)
    ui = stage / 'ui'
    ui.mkdir()
    for path in sorted((ROOT / 'content/ui').glob('*.png')):
        shutil.copyfile(path, ui / path.name)
    source_text = ROOT / 'content/dialogue'
    for path in sorted(source_text.rglob('*.txt')):
        target = stage / 'dialogue' / path.relative_to(source_text)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
    if hd is not None:
        # A prepared HD folder (tools/release/prepare_hd_bundle.py): the launcher starts in
        # HD when it finds hd/ beside the program. For the builder's own use.
        if not (hd / 'hd.json').is_file() or not (hd / 'art/rt64.json').is_file():
            raise SystemExit(f'Not a prepared HD folder: {hd}')
        shutil.copytree(hd, stage / 'hd', symlinks=False)
        name += '-hd'
    licenses = stage / 'licenses'
    licenses.mkdir()
    for name_, pattern in (('sdl3', 'LICENSE.txt'), ('sdl2-compat', 'LICENSE.txt'), ('freetype', 'LICENSE.TXT'),
                           ('harfbuzz', 'COPYING'), ('icu', 'LICENSE')):
        found = sorted((ARCHIVES / name_).glob(f'*/{pattern}'))
        if found:
            shutil.copyfile(found[0], licenses / f'{name_}-{pattern}')
    recipe = Path(__file__).resolve().parent / 'linux'
    for script in ('srw64.sh', 'add-to-steam.sh'):
        shutil.copyfile(recipe / script, stage / script)
        (stage / script).chmod(0o755)
    shutil.copyfile(recipe / 'README.txt', stage / 'README.txt')
    # add-to-steam.sh: the Steam library entry, named in the game's language, with artwork
    # made from the HD title images. Without those sources the game is added without art.
    (stage / 'steam').mkdir()
    shutil.copyfile(recipe / 'add_to_steam.py', stage / 'steam/add_to_steam.py')
    scenes = json.loads((ROOT / 'content/art/stage1-hd.json').read_text(encoding='utf-8'))['scene_images']['path']
    if (ROOT / scenes / 'title-logo.png').is_file():
        run([WORK / 'venv/bin/python', recipe / 'steam_art.py', '--output', stage / 'steam'])
    else:
        print(f'No Steam artwork: {ROOT / scenes} is not here', flush=True)
    archive = WORK / f'{name}.tar.gz'
    with tarfile.open(archive, 'w:gz') as tar:
        tar.add(stage, arcname=name)
    report = {'schema': 'srw64.linux-package.v1', 'archive': archive.name, 'revision': revision,
              'glibc_required': '.'.join(map(str, newest)), 'bundled': sorted(bundled),
              'program_sha256': hashlib.sha256(program.read_bytes()).hexdigest(),
              'verification': 'ELF linkage and glibc symbol versions; not a run on another distribution'}
    (WORK / 'package.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
    return archive


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    parser.add_argument('--no-package', action='store_true', help='stop after building the program')
    parser.add_argument('--hd', type=Path, help='bundle a prepared HD folder as hd/ (personal builds)')
    args = parser.parse_args()
    if sys.platform != 'linux' or platform.machine() != 'x86_64':
        parser.error('This recipe builds on x86-64 Linux (see tools/release/linux/build.sh)')
    check_inputs()
    prefix = build_dependencies(args.jobs)
    binary = build_host(prefix, args.jobs)
    if not args.no_package:
        print(f'Package: {package(binary, prefix, args.hd.resolve() if args.hd else None)}')


if __name__ == '__main__':
    main()
