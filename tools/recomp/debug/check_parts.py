#!/usr/bin/env python3
"""Verify the native 強化パーツ screens in the current native build.

Loads a stage-one-clear save, opens 強化パーツ from the native intermission menu and
checks the machine list (sorted by pilot level), the slots screen with its stat
columns, the inventory (はずす first), the removal on an empty slot re-entering the
screen, the way back, and — when the save owns parts — equipping one and its holders
screen."""
import argparse
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, run_keys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reuse-build', action='store_true')
parser.add_argument('--save', default='build/recomp/save-recovery-check/intermission-cold-1.source.sram')
parser.add_argument('--keep-open', action='store_true')
args = parser.parse_args()
s = Session.launch(language='ja', images='original', save=args.save, reuse_build=args.reuse_build)
print('RUN', s.run, flush=True)
checks = []
def status():
    return s.client.call('status')
def page():
    return status()['parts_page']
def wait_page(screen, timeout=30, **fields):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        p = page()
        if p.get('visible') and p.get('screen') == screen and all(p.get(k) == v for k, v in fields.items()):
            return p
        time.sleep(.1)
    raise AssertionError(f'parts page {screen} {fields} did not open: {json.dumps(page(), ensure_ascii=False)[:300]}')
def wait_hidden(timeout=30):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if not page().get('visible'):
            return
        time.sleep(.1)
    raise AssertionError('parts page still visible')
def keys(*names, pause=.4):
    for name in names:
        run_keys(s.client, [{'press': name}])
        time.sleep(pause)
def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (s.run/'parts-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2)+'\n')
    assert passed, name
    print(name, 'PASS', flush=True)
def note(name, state):
    checks.append({'check': name, 'passed': None, 'state': state})
    print(name, 'NOTE', json.dumps(state, ensure_ascii=False)[:300], flush=True)
def shot(name):
    s.client.call('screenshot', path=str(s.run/name))

# Title ring to the load screen, as check_intermission.py does.
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

# 強化パーツ: the machine list sorted by pilot level, ダイターン3 (万丈 Lv5) first.
s.client.call('ui.click', text='intermission:6')
p = wait_page('list')
names = [r['name'] for r in p['rows']]
levels = [r.get('level') for r in p['rows']]
check('list-rows', names == ['ダイターン3', 'ドール', 'スイームルグ'] and levels == sorted(levels, reverse=True) and p['pages'] == 1 and p['cursor'] == 0, p)
check('list-row-fields', p['rows'][0]['pilot'] == '万丈' and len(p['rows'][0]['parts']) >= 1 and all(x['part'] == -1 for x in p['rows'][0]['parts']), p['rows'][0])
check('list-labels', p['labels']['title'] == '強化パーツ' and p['labels']['equipped'] and p['labels']['level'], p['labels'])
shot('parts-list.png')
keys('down')
check('list-move', page()['cursor'] == 1, page())
keys('up', 'up')
check('list-wrap', page()['cursor'] == 2, page())
keys('down')   # back to the top
keys('z')
p = wait_page('slots')
unit = p['unit']
check('slots-open', unit['name'] == 'ダイターン3' and p['mode'] == 0 and p['cursor'] == 0 and len(unit['parts']) >= 1, p)
stat = {x['key']: x for x in p['stats']}
check('slots-stats', stat['hp']['current'] == 8000 and stat['hp']['preview'] == 8000 and stat['en']['current'] == 200 and stat['move']['current'] > 0 and stat['limit']['current'] > 0, p['stats'])
inv = p['inventory']
check('inventory-remove-first', inv['rows'][0]['remove'] and inv['rows'][0]['name'] == p['labels']['remove'] and inv['cursor'] == 0 and inv['page'] == 0, inv)
owned = [r for r in inv['rows'] if not r['remove']]
note('inventory-owned', owned)
shot('parts-slots.png')
if len(unit['parts']) > 1:
    keys('down')
    check('slot-move', page()['cursor'] == 1, page())
    keys('up')
# Open the inventory: mode 1, the cursor on はずす, no description.
keys('z')
p = wait_page('slots', mode=1)
check('inventory-open', p['inventory']['cursor'] == 0 and p['selected'] == 0x12 and p['description'] == [] and p['cursor'] == 0, p)
shot('parts-inventory.png')
if owned:
    keys('down')
    p = wait_page('slots', mode=1)
    check('inventory-move', p['inventory']['cursor'] == 1 and p['selected'] == owned[0]['part'] and p['description'], p)
    shot('parts-inventory-part.png')
    keys('up')
    wait_page('slots', mode=1)
# Esc closes the inventory without leaving the screen.
keys('x')
p = wait_page('slots', mode=0)
check('inventory-close', p['cursor'] == 0, p)
# はずす on an empty slot: the original just re-enters the screen (a new serial).
serial = p['serial']
keys('z')
wait_page('slots', mode=1)
keys('z', pause=1.5)
time.sleep(3)
p = wait_page('slots')
check('remove-empty-reenters', p['serial'] > serial and p['mode'] == 0 and all(x['part'] == -1 for x in p['unit']['parts']), p)
if owned:
    # Equip the first owned part: the holders screen lists every copy, A on a free
    # one equips it into slot 0 and the slots screen returns with it.
    keys('z')
    wait_page('slots', mode=1)
    keys('down')
    wait_page('slots', mode=1)
    keys('z', pause=1.5)
    p = wait_page('holders')
    check('holders-open', p['part']['part'] == owned[0]['part'] and len(p['rows']) == owned[0]['owned'] and p['target_slot'] == 0, p)
    shot('parts-holders.png')
    free = [n for n, r in enumerate(p['rows']) if r['free']]
    if free:
        for _ in range(free[0]):
            keys('down')
        keys('z', pause=1.5)
        time.sleep(3)
        p = wait_page('slots')
        check('equipped', p['unit']['parts'][0]['part'] == owned[0]['part'], p)
        shot('parts-equipped.png')
        # Remove it again through はずす.
        keys('z')
        wait_page('slots', mode=1)
        keys('z', pause=1.5)
        time.sleep(3)
        p = wait_page('slots')
        check('removed', p['unit']['parts'][0]['part'] == -1, p)
# Back: the list with the cursor still on ダイターン3, then the menu on 強化パーツ.
keys('x')
p = wait_page('list')
check('back-to-list', p['cursor'] == 0 and p['rows'][0]['name'] == 'ダイターン3', p)
keys('x')
wait_hidden()
s.wait(intermission_page=True, timeout=20)
check('back-to-menu', status()['intermission_page']['cursor'] == 6, status()['intermission_page'])
if args.keep_open:
    print('KEEP OPEN', flush=True)
    while s.alive():
        time.sleep(1)
print('EXIT', s.quit().get('exit_code'), flush=True)
