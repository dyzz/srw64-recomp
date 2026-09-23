#!/usr/bin/env python3
"""Verify the native データセーブ screens on a first-episode save.

Loads build/recomp/save-recovery-check/intermission-cold-1.source.sram (slot 1 used,
slot 2 empty) through the title ring, opens データセーブ and checks the medium choice,
the pause, the slot page, saving into the empty slot 2, the overwrite window on slot 1
(いいえ then はい), the Controller Pak message and the way back to the menu. The run's
SRAM copy is the only file written."""
import argparse
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, run_keys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reuse-build', action='store_true')
parser.add_argument('--keep-open', action='store_true')
args = parser.parse_args()
s = Session.launch(language='ja', images='original', save='build/recomp/save-recovery-check/intermission-cold-1.source.sram', reuse_build=args.reuse_build)
print('RUN', s.run, flush=True)
checks = []
def status():
    return s.client.call('status')
def page():
    return status()['save_page']
def wait_page(screen, timeout=30, **fields):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        p = page()
        if p.get('visible') and p.get('screen') == screen and all(p.get(k) == v for k, v in fields.items()):
            return p
        time.sleep(.1)
    raise AssertionError(f'save page {screen} {fields} did not open: {json.dumps(page(), ensure_ascii=False)[:400]}')
def keys(*names, pause=.4):
    for name in names:
        run_keys(s.client, [{'press': name}])
        time.sleep(pause)
def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (s.run/'save-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2)+'\n')
    assert passed, name
    print(name, 'PASS', flush=True)
def note(name, state):
    checks.append({'check': name, 'passed': None, 'state': state})
    print(name, 'NOTE', json.dumps(state, ensure_ascii=False)[:500], flush=True)
def shot(name):
    s.client.call('screenshot', path=str(s.run/name))

# Title ring: Load is the fourth item; the intermission follows the loading dialogue.
s.wait(vi=600)
for _ in range(8):
    keys('return', pause=.5)
    try:
        s.wait(title_major=3, timeout=4)
        break
    except Exception:
        pass
time.sleep(2.5)
keys('right', 'right', 'right', pause=1.5)
keys('return', pause=3)
for _ in range(8):
    st = status()
    if st['intermission_page'].get('visible') or (st.get('intro') or {}).get('title_major') != 7:
        break
    keys('z', pause=1.5)
s.wait(intermission_page=True, timeout=30)
check('intermission', status()['intermission_page'].get('visible'), status()['intermission_page'])

# データセーブ: the medium choice.
s.client.call('ui.click', text='intermission:0')
p = wait_page('choice')
check('choice', p['cursor'] == 0 and not p['waiting'] and p['labels']['rom'] and p['labels']['pak'], p)
note('labels', p['labels'])
time.sleep(1)
shot('save-choice.png')
keys('down')
check('choice-move', page()['cursor'] == 1, page())
keys('up')
keys('z', pause=.3)
p = wait_page('choice', waiting=True, timeout=5)
check('choice-wait', p['waiting'], p)
shot('save-checking.png')
p = wait_page('slots', timeout=10)
check('slots', p['medium'] == 0 and p['mode'] == 0 and p['cursor'] == 0 and len(p['slots']) == 2 and p['slots'][0]['used'] and not p['slots'][1]['used'], p)
first = p['slots'][0]
note('slot-1', {k: first.get(k) for k in ('name', 'level', 'episode', 'title', 'turns', 'funds', 'protagonist')})
check('slot-1-fields', first['name'] and first['level'] >= 1 and first['episode'] >= 1 and first['title'] and first.get('art'), first)
time.sleep(1)
shot('save-slots.png')
# Save into the empty slot 2: the page restarts with both slots equal.
keys('down')
check('slots-move', page()['cursor'] == 1, page())
serial = page()['serial']
keys('z', pause=2.5)
p = wait_page('slots', cursor=1, timeout=10)
check('saved-slot-2', p['serial'] != serial and p['slots'][1]['used'] and {k: p['slots'][1].get(k) for k in ('name', 'level', 'episode', 'title', 'turns', 'funds')} == {k: first.get(k) for k in ('name', 'level', 'episode', 'title', 'turns', 'funds')}, p)
shot('save-slot-2-saved.png')
# Overwrite window on slot 1: いいえ, then はい.
keys('up')
keys('z', pause=.6)
p = wait_page('slots', mode=1, timeout=5)
check('overwrite-window', p['window_cursor'] == 0 and p['cursor'] == 0, p)
shot('save-overwrite.png')
keys('down')
check('overwrite-move', page()['window_cursor'] == 1, page())
keys('z', pause=.6)
check('overwrite-no', page()['mode'] == 0, page())
keys('z', pause=.6)
p = wait_page('slots', mode=1, timeout=5)
serial = p['serial']
keys('z', pause=2.5)
p = wait_page('slots', mode=0, timeout=10)
check('overwrite-yes', p['serial'] != serial and p['slots'][0]['used'] and p['cursor'] == 0, p)
# X: back to the medium choice; the Controller Pak path shows the message.
keys('x', pause=2)
p = wait_page('choice', timeout=10)
check('back-to-choice', not p['waiting'], p)
keys('down')
keys('z', pause=.3)
p = wait_page('slots', mode=2, timeout=12)
check('pak-message', p['medium'] == 1 and p['status'] >= 2 and len(p.get('message', [])) >= 3, p)
note('pak-message-lines', [l['text'] for l in p['message']])
time.sleep(1)
shot('save-pak-message.png')
keys('x', pause=2)
p = wait_page('choice', timeout=10)
check('back-from-message', p['cursor'] == 1, p)
keys('x', pause=2)
s.wait(intermission_page=True, timeout=20)
check('back-to-menu', status()['intermission_page']['cursor'] == 0 and not page().get('visible'), status()['intermission_page'])
if args.keep_open:
    print('KEEP OPEN', flush=True)
    while s.alive():
        time.sleep(1)
print('EXIT', s.quit().get('exit_code'), flush=True)
