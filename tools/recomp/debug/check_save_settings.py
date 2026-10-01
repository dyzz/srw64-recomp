#!/usr/bin/env python3
"""The settings window's 存档 page with a save library, live (docs/design/save-slots-autosave.md §8).

A fresh library under build/recomp/save-slots-check/<time>/saves: its card the
first-episode save, and in import/ a Project64 file of the same card whose slot 2 is a
copy of slot 1 with one byte changed (a wrong checksum). On the title: autosave off and
back on, four kept, the card exported for every emulator, slot 1 imported, slot 2 refused
once and imported repaired on the second press."""
import argparse
import json
from pathlib import Path
import shutil
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, run_keys

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / 'build/recomp/save-recovery-check/intermission-cold-1.source.sram'
SLOT = 0x1F00
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reuse-build', action='store_true')
args = parser.parse_args()
work = ROOT / 'build/recomp/save-slots-check' / time.strftime('settings-%Y%m%dT%H%M%S')
library = work / 'saves'
(library / 'import').mkdir(parents=True)
card = SOURCE.read_bytes()
shutil.copyfile(SOURCE, library / 'cartridge.sram')
damaged = bytearray(card)
damaged[0x1F10:0x1F10 + SLOT] = card[0x10:0x10 + SLOT]
damaged[0x1F10 + 0x300] ^= 1
swapped = b''.join(bytes(reversed(damaged[i:i + 4])) for i in range(0, len(damaged), 4))
(library / 'import' / 'SRW64.sra').write_bytes(swapped)
checks = []


def intact(record):
    body = record[2:2 + SLOT]
    return bool(record[0] & 0x80) and record[1] == (sum(body + bytes(SLOT - len(body))) & 0xFF)


def check(name, passed, state=None, timeout=6):
    end = time.monotonic() + timeout
    while callable(passed) and not passed() and time.monotonic() < end:
        time.sleep(.2)
    result = passed() if callable(passed) else passed
    state = state() if callable(state) else state
    checks.append({'check': name, 'passed': bool(result), 'state': state})
    (work / 'save-settings-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2) + '\n')
    assert result, (name, state)
    print(name, 'PASS', flush=True)


s = Session.launch(language='ja', images='original', save=str(library / 'cartridge.sram'), reuse_build=args.reuse_build,
                   env={'SRW64_SAVE_LIBRARY': str(library)})
print('RUN', s.run, flush=True)


def nodes(row=None):
    if row is None:
        row = s.client.call('ui.tree')['windows'][0]['views']
    yield row
    for child in row.get('children', []):
        yield from nodes(child)


def text_of(id):
    row = next((r for r in nodes() if r.get('id') == id), None)
    return row and row.get('text', '')


def settings():
    path = library / 'settings.json'
    return json.loads(path.read_text()) if path.exists() else {}


s.wait(vi=600, timeout=300)
for _ in range(20):
    run_keys(s.client, [{'press': 'return'}])
    try:
        s.wait(title_major=3, timeout=6)
        break
    except Exception:
        time.sleep(.5)
time.sleep(2)
s.client.call('ui.key', key=',', modifiers=['cmd' if sys.platform == 'darwin' else 'control'])
time.sleep(1)
s.client.call('ui.click', id='settings-page:saves')
time.sleep(.8)
check('page', lambda: any(r.get('id') == 'autosave:on' for r in nodes()) and any('SRW64.sra' in r.get('text', '') for r in nodes()))
s.client.call('screenshot', path=str(work / 'settings-saves.png'))
s.client.call('ui.click', id='autosave:off')
check('autosave-off', lambda: settings().get('autosave') is False, settings)
s.client.call('ui.click', id='autosave:on')
s.client.call('ui.click', id='autosave-turn:10')
check('autosave-choices', lambda: settings().get('autosave') is True and settings().get('autosave_turn') == 10, settings)
s.client.call('ui.click', id='save-export')
exported = {p.name: p.read_bytes() for p in (library / 'export').glob('*')} if (library / 'export').exists() else {}
check('export', lambda: len(list((library / 'export').glob('srw64-*'))) == 4, lambda: sorted(p.name for p in (library / 'export').glob('*')))
files = {p.name: p.read_bytes() for p in (library / 'export').glob('srw64-*')}
word = lambda b: b''.join(bytes(reversed(b[i:i + 4])) for i in range(0, len(b), 4))
check('export-formats', files['srw64-ares.ram'] == card and word(files['srw64-project64.sra']) == card
      and files['srw64-mupen64plus.sra'] == files['srw64-project64.sra'] and len(files['srw64-retroarch.srm']) == 0x48800
      and word(files['srw64-retroarch.srm'][0x20800:0x28800]) == card, {})
s.client.call('ui.click', id='save-import:0:SRW64.sra')
check('import-slot-1', lambda: (library / 'slots/003.rec').exists() and (library / 'slots/003.rec').read_bytes() == card[0x10:0x10 + SLOT])
s.client.call('ui.click', id='save-import:1:SRW64.sra')
time.sleep(1)
check('damaged-refused-once', not (library / 'slots/004.rec').exists() and any('もう一度' in r.get('text', '') for r in nodes()), {})
s.client.call('screenshot', path=str(work / 'settings-saves-damaged.png'))
s.client.call('ui.click', id='save-import:1:SRW64.sra')
check('damaged-repaired', lambda: (library / 'slots/004.rec').exists() and intact((library / 'slots/004.rec').read_bytes()))
s.client.call('screenshot', path=str(work / 'settings-saves-imported.png'))
print('EXIT', s.quit().get('exit_code'), flush=True)
print('ALL', len(checks), 'PASS', flush=True)
