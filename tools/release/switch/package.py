#!/usr/bin/env python3
"""The Switch's folder (docs/design/switch-port.md): build/recomp/switch-package/Marchwind64,
copied as it is to sdmc:/switch/Marchwind64 on the SD card. It holds the NRO and, beside it,
what bundled_resource() looks for there (src/host/host.cpp, the Switch entry): the fonts,
the dialogue text and the stages' metadata; and game/, the data prepared from the player's
ROM on this computer (prepare_data.py: the ROM, the art pack, the dialogue data), which the
game reads as it is. The game keeps its saves, settings and run in the same folder.

  tools/release/switch/package.py           after build.sh game
  tools/release/switch/package.py --upload IP
                                            also send what changed to the Switch's ftpd
                                            (port 5000), the ROM too when it is not there
  tools/release/switch/package.py --upload IP --clean
                                            and remove what launches before prepared data left
                                            there (sessions/, content-cache/, hash-memo.txt,
                                            rom.z64); saves/ stays
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
    data = ROOT / 'build/recomp/switch-data/game'
    if not (data / 'prepared.json').exists():
        raise SystemExit(f'{data} is missing: run tools/release/switch/prepare_data.py ROM')
    shutil.copytree(data, PACKAGE / 'game')


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def remove_tree(ftp: ftplib.FTP, path: str) -> None:
    try:
        names = ftp.nlst(path)
    except ftplib.error_perm:
        return
    for name in names:
        child = name if name.startswith('/') else f'{path}/{name}'
        if child.rstrip('/') == path.rstrip('/'):
            continue
        try:
            ftp.delete(child)
        except ftplib.error_perm:
            remove_tree(ftp, child)
    try:
        ftp.rmd(path)
    except ftplib.error_perm:
        pass


HD = ROOT / 'build/recomp/switch-romfs/hd'


def upload(host: str, debug: bool, clean: bool = False, hd: bool = False) -> None:
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

    if clean:
        for old in ('sessions', 'content-cache'):
            remove_tree(ftp, f'{REMOTE}/{old}')
        for old in ('hash-memo.txt', 'rom.z64'):
            try:
                ftp.delete(f'{REMOTE}/{old}')
            except ftplib.error_perm:
                pass
        print('old launch data removed')
    # The folders run_prepared writes into (prepare_data.cpp makes them in its output).
    for folder in ('run/runtime-data/saves', 'saves/slots'):
        mkdirs(f'{REMOTE}/{folder}')
    sent = 0
    # With hd: the prepared HD pack beside the NRO too (a development NRO built without it,
    # SRW64_SWITCH_EMBED_HD=OFF build.sh game), recorded like the rest: sent once.
    files = [(p, p.relative_to(PACKAGE).as_posix()) for p in sorted(PACKAGE.rglob('*')) if p.is_file()]
    if hd:
        files += [(p, 'hd/' + p.relative_to(HD).as_posix()) for p in sorted(HD.rglob('*')) if p.is_file()]
    for path, relative in files:
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
    parser.add_argument('--clean', action='store_true', help='with --upload: remove the old launch data')
    parser.add_argument('--hd', action='store_true', help='with --upload: send the prepared HD pack beside the NRO')
    args = parser.parse_args()
    stage()
    print(PACKAGE)
    if args.upload:
        upload(args.upload, args.debug, args.clean, args.hd)


if __name__ == '__main__':
    main()
