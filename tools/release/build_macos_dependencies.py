#!/usr/bin/env python3
"""Build pinned macOS runtime libraries locally, without Homebrew runtime linkage.

Build tools may come from Homebrew. Source archives, build trees and installed
libraries stay under build/macos-deps; no system installation is modified.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile

from package_macos import check_deployment, macho_files, run

ROOT = Path(__file__).resolve().parents[2]
LOCK = ROOT / 'config/recomp/macos-dependencies.json'


def source(name: str, entry: dict, directory: Path) -> Path:
    archive = directory / entry['url'].rsplit('/', 1)[1]
    if not archive.exists():
        partial = archive.with_suffix(archive.suffix + '.part')
        subprocess.run(['curl', '--fail', '--location', '--retry', '2',
                        '--output', str(partial), entry['url']], check=True)
        partial.rename(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != entry['sha256']:
        raise ValueError(f'Source checksum mismatch: {archive}')
    destination = directory / name
    stamp = directory / (name + '.sha256')
    if destination.exists() and (not stamp.exists() or stamp.read_text().strip() != entry['sha256']):
        raise ValueError(f'Source cache differs from lock: {destination}; use a fresh source/build directory')
    if not destination.exists():
        with tempfile.TemporaryDirectory(dir=directory, prefix=f'.{name}-') as temp:
            with tarfile.open(archive) as payload:
                payload.extractall(temp, filter='data')
            Path(temp).rename(destination)
        stamp.write_text(entry['sha256'] + '\n')
    roots = list(destination.iterdir())
    if len(roots) != 1 or not roots[0].is_dir():
        raise ValueError(f'Expected one source directory in {archive}')
    return roots[0]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, default=6)
    args = parser.parse_args()
    if sys.platform != 'darwin' or platform.machine() != 'arm64':
        parser.error('This release recipe currently targets Apple Silicon on macOS')
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    lock = json.loads(LOCK.read_text())
    minimum, arch = lock['minimum_macos'], lock['architecture']
    work = ROOT / 'build/macos-deps' / f'{minimum}-{arch}'
    prefix = work / 'prefix'
    downloads = work.parent / 'sources'
    downloads.mkdir(parents=True, exist_ok=True)
    prefix.mkdir(parents=True, exist_ok=True)
    sources = {name: source(name, entry, downloads) for name, entry in lock['sources'].items()}
    sdk = run(['xcrun', '--sdk', 'macosx', '--show-sdk-path']).strip()
    env = {k: v for k, v in os.environ.items() if k not in (
        'CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS', 'CPATH', 'LIBRARY_PATH',
        'CMAKE_PREFIX_PATH', 'PKG_CONFIG_PATH', 'DYLD_LIBRARY_PATH')}
    env.update(MACOSX_DEPLOYMENT_TARGET=minimum, SDKROOT=sdk,
               PKG_CONFIG_LIBDIR=str(prefix / 'lib/pkgconfig'))

    def command(argv: list[str], cwd: Path | None = None) -> None:
        print('+ ' + ' '.join(argv), flush=True)
        subprocess.run(argv, cwd=cwd, env=env, check=True)

    def cmake(name: str, options: list[str]) -> None:
        build = work / name
        command(['cmake', '-S', str(sources[name]), '-B', str(build), '-G', 'Ninja',
                 f'-DCMAKE_MAKE_PROGRAM={shutil.which("ninja")}',
                 '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_COMPILER=/usr/bin/clang',
                 '-DCMAKE_CXX_COMPILER=/usr/bin/clang++',
                 f'-DCMAKE_OSX_SYSROOT={sdk}', f'-DCMAKE_OSX_DEPLOYMENT_TARGET={minimum}',
                 f'-DCMAKE_OSX_ARCHITECTURES={arch}', f'-DCMAKE_INSTALL_PREFIX={prefix}',
                 '-DCMAKE_INSTALL_INCLUDEDIR=include', '-DCMAKE_INSTALL_LIBDIR=lib',
                 f'-DCMAKE_PREFIX_PATH={prefix}', '-DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew;/usr/local',
                 '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF', '-DBUILD_SHARED_LIBS=ON',
                 f'-DPython3_EXECUTABLE={sys.executable}', *options])
        command(['cmake', '--build', str(build), '--parallel', str(args.jobs)])
        command(['cmake', '--install', str(build)])

    cmake('sdl3', ['-DSDL_SHARED=ON', '-DSDL_STATIC=OFF', '-DSDL_TEST_LIBRARY=OFF',
                   '-DSDL_TESTS=OFF', '-DSDL_EXAMPLES=OFF'])
    cmake('sdl2-compat', ['-DSDL2COMPAT_TESTS=OFF', '-DSDL2COMPAT_STATIC=OFF',
                         f'-DSDL3_DIR={prefix}/lib/cmake/SDL3'])
    cmake('freetype', ['-DFT_DISABLE_HARFBUZZ=ON', '-DFT_DISABLE_PNG=ON',
                       '-DFT_DISABLE_BROTLI=ON', '-DFT_DISABLE_BZIP2=ON',
                       f'-DZLIB_LIBRARY={sdk}/usr/lib/libz.tbd',
                       f'-DZLIB_INCLUDE_DIR={sdk}/usr/include'])
    cmake('harfbuzz', ['-DHB_HAVE_FREETYPE=ON', '-DHB_HAVE_CORETEXT=OFF',
                       '-DHB_HAVE_GLIB=OFF', '-DHB_HAVE_GRAPHITE2=OFF', '-DHB_HAVE_ICU=OFF',
                       '-DHB_BUILD_UTILS=OFF', '-DHB_BUILD_SUBSET=OFF', '-DHB_BUILD_RASTER=OFF',
                       '-DHB_BUILD_VECTOR=OFF', '-DHB_BUILD_GPU=OFF',
                       f'-DFREETYPE_INCLUDE_DIRS={prefix}/include/freetype2',
                       f'-DFREETYPE_LIBRARY={prefix}/lib/libfreetype.dylib'])
    icu_build = work / 'icu'
    icu_build.mkdir(exist_ok=True)
    env.update(CC='/usr/bin/clang', CXX='/usr/bin/clang++',
               CFLAGS=f'-O2 -arch {arch} -mmacosx-version-min={minimum}',
               CXXFLAGS=f'-O2 -arch {arch} -mmacosx-version-min={minimum}',
               LDFLAGS=f'-arch {arch} -mmacosx-version-min={minimum}')
    command([str(sources['icu'] / 'source/configure'), f'--prefix={prefix}',
             '--enable-shared', '--enable-rpath', '--disable-static', '--disable-tests', '--disable-samples',
             '--disable-extras', '--disable-icuio'], icu_build)
    command(['make', f'-j{args.jobs}'], icu_build)
    command(['make', 'install'], icu_build)
    libraries = macho_files(prefix / 'lib')
    check_deployment(libraries, minimum)
    for library in libraries:
        linked = run(['otool', '-L', str(library)])
        if '/opt/homebrew/' in linked or '/usr/local/' in linked:
            raise ValueError(f'Unexpected external dependency in {library}:\n{linked}')
    report = {'schema': 'srw64.macos-runtime-dependencies.v1', 'minimum_macos': minimum,
              'architecture': arch, 'prefix': str(prefix), 'sources': lock['sources'],
              'libraries': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in libraries},
              'verification': 'deployment load commands and linkage; not an old-OS runtime test'}
    (work / 'dependencies.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Runtime dependencies ready: {prefix}')


if __name__ == '__main__':
    main()
