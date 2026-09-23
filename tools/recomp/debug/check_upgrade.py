#!/usr/bin/env python3
"""Verify the native ユニット改造 screens in the current native build.

Loads a stage-one-clear save, opens ユニット改造 from the native intermission menu and
checks the machine list, the five-stat screen with a real upgrade, the 資金が足りません
message, the way back, and the 武器改造 list handing over to the original weapon list."""
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
args = parser.parse_args()
s = Session.launch(language='ja', images='original', save=args.save, reuse_build=args.reuse_build)
print('RUN', s.run, flush=True)
checks = []
def status():
    return s.client.call('status')
def page():
    return status()['upgrade_page']
def wait_page(screen, timeout=30):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        p = page()
        if p.get('visible') and p.get('screen') == screen:
            return p
        time.sleep(.1)
    raise AssertionError(f'upgrade page {screen} did not open')
def wait_hidden(timeout=30):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if not page().get('visible'):
            return
        time.sleep(.1)
    raise AssertionError('upgrade page still visible')
def keys(*names, pause=.4):
    for name in names:
        run_keys(s.client, [{'press': name}])
        time.sleep(pause)
def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (s.run/'upgrade-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2)+'\n')
    assert passed, name
    print(name, 'PASS', flush=True)
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

# ユニット改造: the list of the three stage-one machines.
s.client.call('ui.click', text='intermission:1')
p = wait_page('list')
names = [r['name'] for r in p['rows']]
check('list-rows', names == ['ダイターン3', 'スイームルグ', 'ドール'] and p['pages'] == 1 and p['cursor'] == 0, p)
check('list-row-fields', p['rows'][0]['hp'] == 8000 and p['rows'][0]['en'] == 200 and p['rows'][0]['pilot'] == '万丈' and p['funds'] == 14500, p)
shot('upgrade-list.png')
keys('down')
check('list-move', page()['cursor'] == 1, page())
keys('up', 'up')
check('list-wrap', page()['cursor'] == 2, page())
keys('down')
keys('z')
p = wait_page('stats')
check('stats-open', p['unit']['name'] == 'ダイターン3' and p['cap'] == 7 and len(p['rows']) == 5 and p['rows'][0]['level'] == 0 and p['rows'][0]['price'] == 2000 and p['rows'][0]['preview'] == 8200, p)
check('stats-gauge', p['rows'][0]['gauge'] == '.......', p)
shot('upgrade-stats.png')
# Upgrade HP once: 2000 out of 14500, level 1, the gauge's first cell filled.
keys('z')
check('stats-confirm-window', page()['window'] == 'confirm', page())
shot('upgrade-confirm.png')
keys('x')
check('stats-confirm-cancel', page()['window'] == '', page())
keys('z', 'z', pause=.6)
time.sleep(3)
p = wait_page('stats')
check('stats-upgraded', p['funds'] == 12500 and p['rows'][0]['level'] == 1 and p['rows'][0]['value'] == 8200 and p['rows'][0]['price'] == 4000 and p['rows'][0]['gauge'] == '>......', p)
shot('upgrade-after.png')
# Two more HP levels leave 2500; the fourth costs 8000: the message, not the window.
for _ in range(2):
    keys('z', 'z', pause=.6)
    time.sleep(3)
    p = wait_page('stats')
check('stats-three-levels', p['funds'] == 2500 and p['rows'][0]['level'] == 3, p)
keys('z')
check('stats-poor-message', page()['window'] == 'poor', page())
shot('upgrade-poor.png')
keys('x')
check('stats-message-closed', page()['window'] == '', page())
# Back: the list (cursor still on ダイターン3), then the menu.
keys('x')
p = wait_page('list')
check('back-to-list', p['cursor'] == 0 and p['rows'][0]['hp'] == 8600 and p['funds'] == 2500, p)
keys('x')
wait_hidden()
s.wait(intermission_page=True, timeout=20)
check('back-to-menu', status()['intermission_page']['cursor'] == 1, status()['intermission_page'])
# 武器改造: the same native list, then the original weapon list.
s.client.call('ui.click', text='intermission:2')
p = wait_page('list')
# The weapon list (801C6B80) lists every form: ダイターン3's three and ドール's two.
names = [r['name'] for r in p['rows']]
check('weapon-list', p['kind'] == 'weapons' and p['title'] == '武器改造' and names == ['ダイターン3', 'ダイファイター', 'ダイタンク', 'スイームルグ', 'ドール', 'ドール(飛行)'] and all(r['hp'] == 8600 for r in p['rows'][:3]), p)
shot('upgrade-weapon-list.png')
keys('z')
p = wait_page('weapons')
# ダイターン3's weapons; the first row is a type-2 weapon at level 0.
first = p['rows'][0]
check('weapon-rows', p['unit']['name'] == 'ダイターン3' and len(p['rows']) >= 1 and first['level'] == 0 and first['type'] in (1, 2, 3, 4) and first['price'] == {1: 5000, 2: 4000, 3: 3000, 4: 2000}[first['type']] and first['preview'] == first['power'] + 100, p)
shot('upgrade-weapons.png')
keys('down')
check('weapon-move', page()['cursor'] == 1, page())
keys('up', 'z')
p = wait_page('weapon')
check('weapon-confirm-open', p['window'] == 'confirm' and p['weapon']['name'] == first['name'] and p['weapon']['price'] == first['price'], p)
shot('upgrade-weapon-confirm.png')
# 2500 left: いいえ first, then はい refused for lack of funds.
keys('down', 'z')
p = wait_page('weapons')
check('weapon-cancel', p['cursor'] == 0, p)
keys('z')
wait_page('weapon')
keys('z', pause=1.5)   # the answer waits for the fade-in to end
check('weapon-poor', page()['window'] == 'poor', page())
shot('upgrade-weapon-poor.png')
keys('x')
wait_page('weapons')
keys('x')
p = wait_page('list')
check('weapon-list-back', p['kind'] == 'weapons', p)
keys('x')
s.wait(intermission_page=True, timeout=20)
print('EXIT', s.quit().get('exit_code'), flush=True)
