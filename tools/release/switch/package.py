#!/usr/bin/env python3
"""The Switch's folder (docs/design/switch-port.md): build/recomp/switch-package/Marchwind64,
copied as it is to sdmc:/switch/Marchwind64 on the SD card. It holds the NRO and, beside it,
what bundled_resource() looks for there (src/host/host.cpp, the Switch entry): the fonts,
the dialogue text and the stages' metadata. The player adds rom.z64; the game keeps its
saves and settings in the same folder.

  tools/release/switch/package.py           after build.sh game
  tools/release/switch/package.py --upload IP
                                            also send what changed to the Switch's ftpd
                                            (port 5000), the ROM too when it is not there
  tools/release/switch/package.py --upload IP --debug
                                            and the empty debug file that opens the debug
                                            interface (attach.py)
"""
from __future__ import annotations

import argparse
import ftplib
import hashlib
import io
import json
import secrets
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[3]
BUILD = ROOT / 'build/recomp/switch-build'
PACKAGE = ROOT / 'build/recomp/switch-package/Marchwind64'
REMOTE = '/switch/Marchwind64'
TOKEN = PACKAGE.parent / 'debug-token'


def stage() -> None:
    nro = BUILD / 'Marchwind64.nro'
    if not nro.exists():
        raise SystemExit(f'{nro} is missing: run tools/release/switch/build.sh game')
    shutil.rmtree(PACKAGE, ignore_errors=True)
    PACKAGE.mkdir(parents=True)
    shutil.copyfile(nro, PACKAGE / nro.name)
    fonts = PACKAGE / 'fonts'
    fonts.mkdir()
    for path in sorted((ROOT / 'build/fonts').iterdir()):
        if path.suffix in ('.ttf', '.txt'):
            shutil.copyfile(path, fonts / path.name)
    if not (fonts / 'HarmonyOS_Sans_SC.ttf').exists():
        raise SystemExit('build/fonts has no HarmonyOS Sans: run tools/content/prepare_fonts.py')
    text = ROOT / 'content/dialogue'
    for path in sorted(text.rglob('*.txt')):
        target = PACKAGE / 'dialogue' / path.relative_to(text)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
    shutil.copytree(ROOT / 'content/stages', PACKAGE / 'stages')


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def upload(host: str, debug: bool) -> None:
    """Only the files whose digest differs from the last upload (switch-package/uploaded.json):
    ftpd has no checksum command, and the dialogue alone is 1,000 files."""
    record_path = PACKAGE.parent / 'uploaded.json'
    record = json.loads(record_path.read_text()) if record_path.exists() else {}
    if record.get('host') != host:
        record = {'host': host, 'files': {}}
    ftp = ftplib.FTP()
    ftp.connect(host, 5000, timeout=30)
    ftp.login()
    made: set[str] = set()

    def mkdirs(folder: str) -> None:
        parts = folder.strip('/').split('/')
        for depth in range(1, len(parts) + 1):
            path = '/' + '/'.join(parts[:depth])
            if path in made:
                continue
            try:
                ftp.mkd(path)
            except ftplib.error_perm:
                pass
            made.add(path)

    sent = 0
    for path in sorted(p for p in PACKAGE.rglob('*') if p.is_file()):
        relative = path.relative_to(PACKAGE).as_posix()
        value = digest(path)
        if record['files'].get(relative) == value:
            continue
        remote = f'{REMOTE}/{relative}'
        mkdirs(remote.rsplit('/', 1)[0])
        with path.open('rb') as data:
            ftp.storbinary(f'STOR {remote}', data)
        record['files'][relative] = value
        record_path.write_text(json.dumps(record, indent=1))
        sent += 1
    try:
        ftp.size(f'{REMOTE}/rom.z64')
    except ftplib.error_perm:
        with (ROOT / 'rom.z64').open('rb') as data:
            ftp.storbinary(f'STOR {REMOTE}/rom.z64', data)
        sent += 1
        print('rom.z64 sent')
    if debug:
        # The debug file holds the token the game then uses, so attach.py reaches it at its
        # fixed port without the game's debug.json (from hbmenu there is no nxlink log).
        token = secrets.token_hex(32)
        ftp.storbinary(f'STOR {REMOTE}/debug', io.BytesIO(token.encode() + b'\n'))
        TOKEN.write_text(token + '\n')
        TOKEN.chmod(0o600)
        print('debug interface on')
    ftp.quit()
    print(f'{sent} files sent to {host}:{REMOTE}')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--upload', metavar='IP', help="the Switch's address, ftpd running")
    parser.add_argument('--debug', action='store_true', help='with --upload: open the debug interface')
    args = parser.parse_args()
    stage()
    print(PACKAGE)
    if args.upload:
        upload(args.upload, args.debug)


if __name__ == '__main__':
    main()
