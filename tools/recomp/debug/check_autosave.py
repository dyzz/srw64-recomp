#!/usr/bin/env python3
"""Verify autosaves and deleting in the running game (docs/design/save-slots-autosave.md S3/S4).

A fresh save library under build/recomp/save-slots-check/<time>/saves, its card the
first-episode save, in four runs:

1. The flow mini stage wins at the start of turn 1 and goes on to the intermission:
   an autosave when it is entered (801D8F74 with 0), another on 次のマップへ.
2. The enemy-cycle mini stage stands idle on turn 1: a turn autosave in the suspend
   format with its random numbers beside it.
3. The title's ロード lists the three autosaves newest first; the turn one goes through
   コンティニュー back to the map at the same turn, its random numbers put back.
4. The sortie autosave loads into the intermission with its funds; a save into slot 3
   can be deleted; R deletes the oldest autosave to trash/."""
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
SLOT_SIZE, SUSPEND_SIZE = 0x1F00, 0x3AE0

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reuse-build', action='store_true')
args = parser.parse_args()
work = ROOT / 'build/recomp/save-slots-check' / time.strftime('autosave-%Y%m%dT%H%M%S')
library = work / 'saves'
library.mkdir(parents=True)
shutil.copyfile(SOURCE, library / 'cartridge.sram')
checks = []


def intact(record: bytes) -> bool:
    body = record[2:2 + SLOT_SIZE]
    return bool(record[0] & 0x80) and record[1] == (sum(body + bytes(SLOT_SIZE - len(body))) & 0xFF)


def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (work / 'autosave-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2) + '\n')
    assert passed, f'{name}: {json.dumps(state, ensure_ascii=False)[:600]}'
    print(name, 'PASS', flush=True)


def autos(kind):
    return sorted((library / 'auto').glob(f'{kind}-*.{"sus" if kind == "turn" else "rec"}'))


class Run:
    def __init__(self, mini_stage=None):
        self.s = Session.launch(language='ja', images='original', save=str(library / 'cartridge.sram'),
                                reuse_build=args.reuse_build, mini_stage=mini_stage,
                                env={'SRW64_SAVE_LIBRARY': str(library)})
        print('RUN', self.s.run, flush=True)

    def status(self):
        return self.s.client.call('status')

    def page(self):
        return self.status()['save_page']

    def events(self, name):
        path = self.s.run / name
        return [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []

    def until(self, test, timeout, press=None, every=1.5):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            value = test()
            if value:
                return value
            if press:
                run_keys(self.s.client, [{'press': press}])
            time.sleep(every)
        raise AssertionError(f'timed out after {timeout}s')

    def keys(self, *names, pause=.4):
        for name in names:
            run_keys(self.s.client, [{'press': name}])
            time.sleep(pause)

    def wait_page(self, screen, timeout=30, **fields):
        return self.until(lambda: (lambda p: p if p.get('visible') and p.get('screen') == screen and all(p.get(k) == v for k, v in fields.items()) else None)(self.page()),
                          timeout, every=.1)

    def move_to(self, index):
        for _ in range(16):
            if self.page()['cursor'] == index:
                return self.page()
            self.keys('down')
        raise AssertionError(f'cursor did not reach {index}: {self.page()}')

    def memory(self, address, size):
        return bytes.fromhex(self.s.client.call('memory.read', address=address, size=size)['hex'])

    def shot(self, name):
        self.s.client.call('screenshot', path=str(work / name))

    def title_load(self):
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
        self.keys('z', pause=.3)
        return self.wait_page('slots', timeout=15)

    def quit(self):
        print('EXIT', self.s.quit().get('exit_code'), flush=True)


# --- 1. A map cleared, then 次のマップへ ------------------------------------------------
r = Run(str(ROOT / 'config/recomp/mini-stages/flow.json'))
# This stage wins as soon as turn 1 begins, so it never stands "ready": enter it from the
# title directly and press on through the story.
r.s.wait(vi=600)
r.until(lambda: r.status()['intro'].get('title_major') == 3, 60, press='return', every=.5)
r.s.client.call('ui.click', id='mini-enter')
r.until(lambda: [e for e in r.events('mini-stage-events.jsonl') if e.get('action') == 'entered'], 30, every=.5)
r.until(lambda: r.status()['intermission_page'].get('visible'), 600, press='z', every=2.0)
time.sleep(1.5)
if r.page().get('visible'):   # a last A may have opened データセーブ
    r.keys('x', pause=2)
    r.keys('x', pause=2)
entered = [e for e in r.events('autosave-events.jsonl') if e['kind'] == 'intermission' and e['node'] == 'entered']
check('entered-autosave', entered and entered[0]['saved'] and len(autos('inter')) == 1 and intact(autos('inter')[0].read_bytes()), entered)
about = json.loads(autos('inter')[0].with_suffix('.json').read_text())
check('entered-about', about['node'] == 'entered' and about['funds'] == r.status()['intermission_page']['funds'], about)
r.shot('entered.png')
r.s.client.call('ui.click', text='intermission:8')
r.until(lambda: [e for e in r.events('autosave-events.jsonl') if e.get('node') == 'sortie'], 30, every=.5)
sortie = [e for e in r.events('autosave-events.jsonl') if e.get('node') == 'sortie']
check('sortie-autosave', sortie[0]['saved'] and len(autos('inter')) == 2, sortie)
sortie_about = json.loads(autos('inter')[-1].with_suffix('.json').read_text())
r.quit()

# --- 2. A turn autosave -------------------------------------------------------------------
turns_before = len(autos('turn'))
r = Run(str(ROOT / 'config/recomp/mini-stages/enemy-cycle.json'))
r.s.enter_mini_stage()
r.until(lambda: [e for e in r.events('autosave-events.jsonl') if e['kind'] == 'turn' and e['saved']], 120, every=1)
turn = autos('turn')[-1:]
check('turn-autosave', len(autos('turn')) == turns_before + 1 and len(turn[0].read_bytes()) == SUSPEND_SIZE and intact(turn[0].read_bytes()), [str(p) for p in turn])
turn_about = json.loads(turn[0].with_suffix('.json').read_text())
check('turn-about', len(turn_about['rng']) == 0x834 * 2 and turn_about['record_sha256'] == hashlib.sha256(turn[0].read_bytes()).hexdigest(), {k: v for k, v in turn_about.items() if k != 'rng'})
saved_turn = int.from_bytes(r.memory(0x8010F5EA, 2), 'big')
time.sleep(3)
check('once-a-turn', len(autos('turn')) == turns_before + 1, {})
r.shot('turn-saved.png')
r.quit()

# --- 3. Loading the turn autosave ---------------------------------------------------------
r = Run()
p = r.title_load()
listed = 2 + len(autos('inter')) + len(autos('turn'))
check('load-list', p['count'] == listed, p)
p = r.move_to(2)
first = p['slots'][0]
check('newest-first', first['kind'] == 'turn' and first['map_turn'] == saved_turn + 1, p)
r.shot('load-turn.png')
r.keys('z', pause=.6)
r.wait_page('slots', mode=1, timeout=5)
r.keys('z', pause=1)
r.until(lambda: [e for e in r.events('autosave-events.jsonl') if e['kind'] == 'rng-restored'], 90, every=1)
restored = [e for e in r.events('autosave-events.jsonl') if e['kind'] == 'restored']
check('turn-restored', restored and restored[0]['rng'], restored)
reads = [e for e in r.events('save-store-events.jsonl') if e['kind'] == 'record-read' and e['record'] == turn[0].name]
check('suspend-read', any(e['length'] == SUSPEND_SIZE for e in reads)
      and any(e['kind'] == 'disarm-suspend' for e in r.events('save-store-events.jsonl')), reads)
time.sleep(4)
check('same-turn', int.from_bytes(r.memory(0x8010F5EA, 2), 'big') == saved_turn and len(autos('turn')) == turns_before + 1, {'saved_turn': saved_turn})
r.shot('turn-loaded.png')
r.quit()

# --- 4. Loading the sortie autosave; slot 3; deleting ------------------------------------
r = Run()
p = r.title_load()
p = r.move_to(3)
check('sortie-is-next', p['slots'][1].get('time') == sortie_about['time'], p)
check('sortie-entry', p['slots'][1]['kind'] == 'intermission' and p['tools'] == {'delete': True}, p)
r.keys('z', pause=.6)
r.wait_page('slots', mode=1, timeout=5)
r.keys('z', pause=3)
r.until(lambda: r.status()['intermission_page'].get('visible'), 60, press='z', every=1.5)
im = r.status()['intermission_page']
check('sortie-loaded', im['funds'] == sortie_about['funds'], {'intermission': im.get('funds'), 'about': sortie_about['funds']})
reads = [e for e in r.events('save-store-events.jsonl') if e['kind'] == 'record-read' and e['record'] == autos('inter')[-1].name]
check('sortie-read', bool(reads), reads)
# Save into slot 3: R would delete it.
r.s.client.call('ui.click', text='intermission:0')
r.wait_page('choice')
r.keys('z', pause=.3)
p = r.wait_page('slots', timeout=10)
p = r.move_to(2)
r.keys('z', pause=2.5)
p = r.wait_page('slots', cursor=2, timeout=10)
check('slot-3', p['slots'][0]['used'] and p['tools'] == {'delete': True}, p)
r.keys('q', pause=.8)   # L does nothing here: no notes
check('no-notes', r.page()['mode'] == 0 and not (library / 'slots/003.json').exists()
      and 'save-note-input' not in json.dumps(r.s.client.call('ui.tree')), r.page())
r.shot('slot-3.png')
r.quit()
# R on the oldest autosave, はい: it goes to trash/.
r = Run()
p = r.title_load()
oldest = min(autos('inter') + autos('turn'), key=lambda path: path.stem.split('-')[1])
p = r.move_to(p['count'] - 1)
r.keys('e', pause=.6)
p = r.wait_page('slots', mode=3, timeout=5)
check('delete-window', p['window_cursor'] == 1, p)
r.shot('delete-window.png')
r.keys('up', pause=.4)
r.keys('z', pause=1)
p = r.wait_page('slots', mode=0, timeout=5)
check('deleted', not oldest.exists() and any(t.name.endswith(oldest.name) for t in (library / 'trash').iterdir()) and p['count'] == listed, p)
r.quit()
print('ALL', len(checks), 'PASS', flush=True)
