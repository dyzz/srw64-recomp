#!/usr/bin/env python3
"""Verify live funds editing on the native intermission menu and upgrade screens."""
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
def keys(*names, pause=.4):
    for name in names:
        run_keys(s.client, [{'press': name}])
        time.sleep(pause)
def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (s.run/'funds-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2)+'\n')
    assert passed, name
    print(name, 'PASS', flush=True)
def wait_page(key, timeout=30, **fields):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        p = status()[key]
        if p.get('visible') and all(p.get(k) == v for k, v in fields.items()):
            return p
        time.sleep(.1)
    raise AssertionError(f'{key} {fields} did not open')
def edit_funds(page_id, text):
    # The click opens the box with the old figure selected; typing replaces it.
    s.client.call('ui.click', text=page_id + '-funds')
    time.sleep(.5)
    s.client.call('ui.type', text=text)
    time.sleep(.2)
    s.client.call('ui.key', key='return')
    time.sleep(1)

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
p = wait_page('intermission_page')
check('menu-funds', p['funds'] == 14500, p)
edit_funds('intermission', '900000')
p = status()['intermission_page']
check('menu-funds-edited', p['funds'] == 900000 and p['visible'], p)
s.client.call('screenshot', path=str(s.run/'funds-menu.png'))
# Esc cancels an edit; the arrow keys are the menu's again afterwards.
s.client.call('ui.click', text='intermission-funds'); time.sleep(.3)
s.client.call('ui.type', text='1'); s.client.call('ui.key', key='escape'); time.sleep(.5)
keys('down')
p = status()['intermission_page']
check('menu-edit-cancel', p['funds'] == 900000 and p['cursor'] == 1, p)
# The stat screen: the new funds make HP affordable seven times over.
keys('z')   # the cursor is on ユニット改造
wait_page('upgrade_page', screen='list')
keys('z')
p = wait_page('upgrade_page', screen='stats')
check('stats-funds', p['funds'] == 900000, p)
edit_funds('upgrade', '1000')   # below HP's 2000
p = status()['upgrade_page']
check('stats-funds-edited', p['funds'] == 1000, p)
keys('z', pause=.8)
check('stats-poor-after-edit', status()['upgrade_page']['window'] == 'poor', status()['upgrade_page'])
keys('x')
edit_funds('upgrade', '50000')
keys('z', pause=.8)
check('stats-confirm-after-edit', status()['upgrade_page']['window'] == 'confirm', status()['upgrade_page'])
keys('z', pause=.8)
time.sleep(3)
p = wait_page('upgrade_page', screen='stats')
check('stats-upgrade-uses-edited-funds', p['funds'] == 48000 and p['rows'][0]['level'] == 1, p)
s.client.call('screenshot', path=str(s.run/'funds-stats.png'))
print('EXIT', s.quit().get('exit_code'), flush=True)
