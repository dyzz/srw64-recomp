#!/usr/bin/env python3
"""Verify extended save slots in the running game (docs/design/save-slots-autosave.md S2).

Starts from build/recomp/save-recovery-check/intermission-cold-1.source.sram (slot 1:
episode 1, 7 turns, 14,500 funds) with a fresh save library in a new directory under
build/recomp/save-slots-check/, the way the standalone app passes SRW64_SAVE_LIBRARY.

Run 1 (データセーブ): the list shows slots 1-2 and a free slot 3 on page 2; saving into
slot 3 writes saves/slots/003.rec and leaves the cartridge alone; the overwrite window
works on it; saving into slot 2 publishes the cartridge at once.
Run 2 (title ロード): slot 3, its funds changed on disk, loads into the intermission
with those funds; the cartridge's slot 1 is unchanged."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, run_keys

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / 'build/recomp/save-recovery-check/intermission-cold-1.source.sram'
SLOT_SIZE, SLOT_OFFSETS = 0x1F00, (0x10, 0x1F10)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reuse-build', action='store_true')
args = parser.parse_args()

work = ROOT / 'build/recomp/save-slots-check' / time.strftime('%Y%m%dT%H%M%S')
library = work / 'saves'
library.mkdir(parents=True)
shutil.copyfile(SOURCE, library / 'cartridge.sram')
checks = []


def checksum(record: bytes) -> int:
    body = record[2:2 + SLOT_SIZE]
    return sum(body + bytes(SLOT_SIZE - len(body))) & 0xFF


def intact(record: bytes) -> bool:
    return bool(record[0] & 0x80) and record[1] == checksum(record)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (work / 'save-slots-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2) + '\n')
    assert passed, f'{name}: {json.dumps(state, ensure_ascii=False)[:600]}'
    print(name, 'PASS', flush=True)


class Run:
    def __init__(self, save: Path):
        self.s = Session.launch(language='ja', images='original', save=str(save), reuse_build=args.reuse_build,
                                env={'SRW64_SAVE_LIBRARY': str(library)})
        print('RUN', self.s.run, flush=True)

    def status(self):
        return self.s.client.call('status')

    def page(self):
        return self.status()['save_page']

    def wait_page(self, screen, timeout=30, **fields):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            p = self.page()
            if p.get('visible') and p.get('screen') == screen and all(p.get(k) == v for k, v in fields.items()):
                return p
            time.sleep(.1)
        raise AssertionError(f'save page {screen} {fields}: {json.dumps(self.page(), ensure_ascii=False)[:500]}')

    def keys(self, *names, pause=.4):
        for name in names:
            run_keys(self.s.client, [{'press': name}])
            time.sleep(pause)

    def move_to(self, index, key='down'):
        # A key pressed while the page fades in is not taken: press until the cursor is there.
        for _ in range(12):
            if self.page()['cursor'] == index:
                return self.page()
            self.keys(key)
        raise AssertionError(f'cursor did not reach {index}: {self.page()}')

    def shot(self, name):
        self.s.client.call('screenshot', path=str(work / name))

    def title_load(self):
        # The title ring's fourth item is ロード.
        self.s.wait(vi=600)
        for _ in range(8):
            self.keys('return', pause=.5)
            try:
                self.s.wait(title_major=3, timeout=4)
                break
            except Exception:
                pass
        time.sleep(2.5)
        self.keys('right', 'right', 'right', pause=1.5)
        self.keys('return', pause=2)
        self.keys('z', pause=.3)          # ROMカートリッジ
        return self.wait_page('slots', timeout=15)


def header(slot):
    return {k: slot.get(k) for k in ('name', 'level', 'episode', 'title', 'turns', 'funds')}


# --- Run 1: saving ----------------------------------------------------------------------
before = digest(library / 'cartridge.sram')
r = Run(library / 'cartridge.sram')
p = r.title_load()
r.keys('z', pause=.6)
r.wait_page('slots', mode=1, timeout=5)
r.keys('z', pause=3)
for _ in range(8):
    st = r.status()
    if st['intermission_page'].get('visible'):
        break
    r.keys('z', pause=1.5)
r.s.wait(intermission_page=True, timeout=30)
r.s.client.call('ui.click', text='intermission:0')
r.wait_page('choice')
r.keys('z', pause=.3)
p = r.wait_page('slots', timeout=10)
first = p['slots'][0]
check('list', p['count'] == 3 and p['pages'] == 2 and p['page'] == 0 and [s['number'] for s in p['slots']] == [1, 2]
      and first['used'] and not p['slots'][1]['used'], p)
r.shot('slots-page-1.png')
p = r.move_to(2)
check('page-2', p['cursor'] == 2 and p['page'] == 1 and [s['number'] for s in p['slots']] == [3] and not p['slots'][0]['used'], p)
r.shot('slots-page-2.png')
serial = p['serial']
r.keys('z', pause=2.5)
p = r.wait_page('slots', cursor=2, timeout=10)
record = (library / 'slots/003.rec').read_bytes() if (library / 'slots/003.rec').exists() else b''
check('saved-slot-3', p['serial'] != serial and p['count'] == 4 and p['slots'][0]['used']
      and header(p['slots'][0]) == header(first) and len(record) == SLOT_SIZE and intact(record), p)
check('cartridge-untouched', digest(library / 'cartridge.sram') == before, {'before': before})
r.shot('slot-3-saved.png')
# Left turns back to page 1; the overwrite window on slot 3 saves again.
r.keys('left')
check('page-left', r.page()['cursor'] == 0 and r.page()['page'] == 0, r.page())
r.move_to(2)
r.keys('z', pause=.6)
p = r.wait_page('slots', mode=1, timeout=5)
check('overwrite-window', p['cursor'] == 2, p)
stamp = (library / 'slots/003.rec').stat().st_mtime_ns
r.keys('z', pause=2.5)
p = r.wait_page('slots', mode=0, timeout=10)
check('overwrote-slot-3', (library / 'slots/003.rec').stat().st_mtime_ns != stamp and p['cursor'] == 2, p)
# Slot 2 is the cartridge's: the card is published while the game runs.
r.keys('up')
serial = r.page()['serial']
r.keys('z', pause=2.5)
p = r.wait_page('slots', cursor=1, timeout=10)
card = (library / 'cartridge.sram').read_bytes()
check('cartridge-published', p['serial'] != serial and p['slots'][1]['used'] and intact(card[SLOT_OFFSETS[1]:SLOT_OFFSETS[1] + SLOT_SIZE])
      and (library / 'cartridge.sram.prev').exists(), p)
check('store-log', any(json.loads(line)['kind'] == 'slot-write' for line in (r.s.run / 'save-store-events.jsonl').read_text().splitlines()),
      {'log': str(r.s.run / 'save-store-events.jsonl')})
print('EXIT', r.s.quit().get('exit_code'), flush=True)

# --- Run 2: loading slot 3 from the title ----------------------------------------------
# Slot 3 gets other funds (+0x54, u32) and a fixed checksum, so the intermission shows
# where the load came from.
record = bytearray((library / 'slots/003.rec').read_bytes())
record[0x54:0x58] = (123456).to_bytes(4, 'big')
record[1] = checksum(bytes(record))
(library / 'slots/003.rec').write_bytes(bytes(record))
card_before = digest(library / 'cartridge.sram')
r = Run(library / 'cartridge.sram')
p = r.title_load()
check('load-list', p['count'] == 3 and p['pages'] == 2, p)
p = r.move_to(2)
check('load-slot-3', p['page'] == 1 and p['slots'][0]['number'] == 3 and p['slots'][0]['funds'] == 123456, p)
r.shot('load-slot-3.png')
r.keys('z', pause=.6)
r.wait_page('slots', mode=1, timeout=5)
r.keys('z', pause=3)
for _ in range(8):
    if r.status()['intermission_page'].get('visible'):
        break
    r.keys('z', pause=1.5)
r.s.wait(intermission_page=True, timeout=30)
im = r.status()['intermission_page']
check('loaded-funds', im.get('funds') == 123456, im)
check('load-log', any(json.loads(line)['kind'] == 'slot-read' and json.loads(line)['slot'] == 3
                      for line in (r.s.run / 'save-store-events.jsonl').read_text().splitlines()), {})
r.shot('loaded-slot-3.png')
print('EXIT', r.s.quit().get('exit_code'), flush=True)
check('cartridge-after-load', digest(library / 'cartridge.sram') == card_before, {})
print('ALL', len(checks), 'PASS', flush=True)
