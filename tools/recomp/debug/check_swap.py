#!/usr/bin/env python3
"""Verify the native のりかえ screens with the swap-test mini stage.

The stage (config/recomp/mini-stages/swap-test.json) registers ヒイロ／ウイングガンダム and
五飛／ガンダムシュピーゲル (both class 3) and ends without battles; after its ending the
intermission opens with swap candidates.
Checks the pilot list, the machine list for the first pilot, the confirm page, いいえ
back, then はい: the pilot ends up in the chosen machine and the list reflects it; a
second swap puts things back."""
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
s = Session.launch(language='ja', images='original', mini_stage='config/recomp/mini-stages/swap-test.json', reuse_build=args.reuse_build)
print('RUN', s.run, flush=True)
checks = []
def status():
    return s.client.call('status')
def page():
    return status()['swap_page']
def wait_page(screen, timeout=30, **fields):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        p = page()
        if p.get('visible') and p.get('screen') == screen and all(p.get(k) == v for k, v in fields.items()):
            return p
        time.sleep(.1)
    raise AssertionError(f'swap page {screen} {fields} did not open: {json.dumps(page(), ensure_ascii=False)[:300]}')
def keys(*names, pause=.4):
    for name in names:
        run_keys(s.client, [{'press': name}])
        time.sleep(pause)
def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (s.run/'swap-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2)+'\n')
    assert passed, name
    print(name, 'PASS', flush=True)
def note(name, state):
    checks.append({'check': name, 'passed': None, 'state': state})
    print(name, 'NOTE', json.dumps(state, ensure_ascii=False)[:400], flush=True)
def shot(name):
    s.client.call('screenshot', path=str(s.run/name))

# Title menu → F8 enters the mini stage; it plays itself, the dialogue needs Z.
s.wait(vi=600)
for _ in range(8):
    keys('return', pause=.5)
    try:
        s.wait(title_major=3, timeout=4)
        break
    except Exception:
        pass
time.sleep(1)
keys('f8', pause=2)
end = time.monotonic() + 600
while time.monotonic() < end:
    st = status()
    if st['intermission_page'].get('visible'):
        break
    if (st.get('dialogue') or {}).get('active'):
        keys('z', pause=.25)
    else:
        time.sleep(.5)
else:
    raise AssertionError('the stage did not reach the intermission')
check('intermission', status()['intermission_page'].get('visible'), status()['intermission_page'])

# のりかえ → パイロット: the candidate list.
s.client.call('ui.click', text='intermission:5')
time.sleep(.8)
s.client.call('ui.click', text='intermission-swap:0')
p = wait_page('pilots')
names = [r['name'] for r in p['rows']]
note('candidates', names)
check('pilots', len(p['rows']) >= 1 and p['cursor'] == 0 and all(r['unit'] for r in p['rows']), p)
shot('swap-pilots.png')
if len(p['rows']) > 1:
    keys('down')
    check('pilots-move', page()['cursor'] == 1, page())
    keys('up')
first = p['rows'][0]
keys('z')
p = wait_page('targets')
check('targets', p['pilot']['name'] == first['name'] and len(p['rows']) >= 1 and p['cursor'] == 0, p)
targets = [r['name'] for r in p['rows']]
note('targets', targets)
shot('swap-targets.png')
if len(p['rows']) > 1:
    keys('down')
    check('targets-move', page()['cursor'] == 1, page())
    keys('up')
target = p['rows'][0]
keys('z', pause=1.5)
p = wait_page('confirm')
check('confirm', p['pilot']['name'] == first['name'] and p['to']['name'] == target['name'] and p['cursor'] == 0 and len(p['terrain']) == 4 and 'after' in p['evade'], p)
note('confirm-detail', {'from': p['from'].get('name'), 'to': p['to']['name'], 'limit': p['to'].get('limit'), 'evade': p['evade'], 'hit': p['hit'], 'terrain': p['terrain']})
shot('swap-confirm.png')
# いいえ: back to the machine list.
keys('down')
check('confirm-move', page()['cursor'] == 1, page())
keys('z', pause=1.5)
p = wait_page('targets')
check('confirm-no', p['pilot']['name'] == first['name'], p)
# はい: the swap, back to the pilot list with the pilot in the chosen machine.
keys('z', pause=1.5)
wait_page('confirm')
keys('z', pause=1.5)
time.sleep(1)
p = wait_page('pilots')
row = next((r for r in p['rows'] if r['name'] == first['name']), None)
check('swapped', row is not None and row['unit'] == target['name'], p)
shot('swap-after.png')
# Swap back through the same pilot and the machine it came from, when listed.
keys('z')
p = wait_page('targets')
back = next((n for n, r in enumerate(p['rows']) if r['name'] == first['unit']), None)
if back is not None:
    for _ in range(back):
        keys('down')
    keys('z', pause=1.5)
    wait_page('confirm')
    keys('z', pause=1.5)
    time.sleep(1)
    p = wait_page('pilots')
    row = next((r for r in p['rows'] if r['name'] == first['name']), None)
    check('swapped-back', row is not None and row['unit'] == first['unit'], p)
else:
    note('swap-back-skipped', targets)
    keys('x', pause=1.5)
    wait_page('pilots')
keys('x', pause=1.5)
s.wait(intermission_page=True, timeout=20)
check('back-to-menu', status()['intermission_page']['cursor'] == 5, status()['intermission_page'])
if args.keep_open:
    print('KEEP OPEN', flush=True)
    while s.alive():
        time.sleep(1)
print('EXIT', s.quit().get('exit_code'), flush=True)
