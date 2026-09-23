#!/usr/bin/env python3
"""Verify the native ユニット能力／パイロット能力 screens in the current native build.

Loads a stage-one-clear save and, from the native intermission menu, checks the unit
list (cursor, sub-pilot line), the unit page (size, repair cost, HP/EN, stats,
terrain, prev/next through the list), the weapon list (rows, cursor, back), then the
pilot list and the pilot page (stats, SP, spirits, skills, terrain, prev/next)."""
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
    return status()['ability_page']
def wait_page(screen, timeout=30, **fields):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        p = page()
        if p.get('visible') and p.get('screen') == screen and all(p.get(k) == v for k, v in fields.items()):
            return p
        time.sleep(.1)
    raise AssertionError(f'ability page {screen} {fields} did not open: {json.dumps(page(), ensure_ascii=False)[:300]}')
def wait_hidden(timeout=30):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if not page().get('visible'):
            return
        time.sleep(.1)
    raise AssertionError('ability page still visible')
def keys(*names, pause=.4):
    for name in names:
        run_keys(s.client, [{'press': name}])
        time.sleep(pause)
def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (s.run/'ability-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2)+'\n')
    assert passed, name
    print(name, 'PASS', flush=True)
def note(name, state):
    checks.append({'check': name, 'passed': None, 'state': state})
    print(name, 'NOTE', json.dumps(state, ensure_ascii=False)[:400], flush=True)
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

# ユニット能力: the unit list.
s.client.call('ui.click', text='intermission:3')
p = wait_page('units')
names = [r['name'] for r in p['rows']]
# 801C4FB0 lists every form: ダイターン3's three and ドール's two.
check('unit-list', names == ['ダイターン3', 'ダイファイター', 'ダイタンク', 'スイームルグ', 'ドール', 'ドール(飛行)'] and p['pages'] == 1 and p['cursor'] == 0 and p['rows'][0]['hp'] == 8000 and p['rows'][0]['pilot'] == '万丈', p)
note('labels', p['labels'])
shot('ability-units.png')
keys('down')
check('unit-list-move', page()['cursor'] == 1, page())
keys('up')
keys('z')
p = wait_page('unit')
check('unit-page', p['unit']['name'] == 'ダイターン3' and p['hp'] == 8000 and p['en'] == 200 and p['move'] == 5 and p['mobility'] == 70 and p['armor'] == 1800 and p['limit'] == 270 and p['terrain'] == 'AABA' and p['size'] == 'LL' and p['repair'] == 14000, p)
check('unit-page-abilities', p['shield'] and '変形' in p['abilities'] and len(p['types']) == 2 and p['index'] == 0 and p['count'] == 6, {'shield': p['shield'], 'abilities': p['abilities'], 'types': p['types']})
shot('ability-unit.png')
# E steps to the next unit through the original's R edge (the screen re-enters).
serial = p['serial']
keys('e', pause=1.5)
time.sleep(2.5)
p = wait_page('unit')
check('unit-page-next', p['serial'] > serial and p['unit']['name'] == names[1] and p['index'] == 1, p['unit'])
keys('q', pause=1.5)
time.sleep(2.5)
p = wait_page('unit')
check('unit-page-prev', p['unit']['name'] == 'ダイターン3' and p['index'] == 0, p['unit'])
# Z: the weapon list.
keys('z', pause=1.5)
p = wait_page('weapons')
check('weapons', p['unit']['name'] == 'ダイターン3' and len(p['rows']) == 6 and p['rows'][0]['name'].endswith('ダイターンミサイル') and p['rows'][0]['power'] == 900 and p['cursor'] == 0 and p['pages'] == 2, p)
keys('right')
check('weapons-page', page()['page'] == 1 and len(page()['rows']) >= 1, page())
keys('left')
shot('ability-weapons.png')
keys('down')
check('weapons-move', page()['cursor'] == 1, page())
keys('x', pause=1.5)
p = wait_page('unit')
check('weapons-back', p['unit']['name'] == 'ダイターン3', p['unit'])
keys('x', pause=1.5)
p = wait_page('units')
check('unit-page-back', p['cursor'] == 0, p)
keys('x')
wait_hidden()
s.wait(intermission_page=True, timeout=20)
check('back-to-menu', status()['intermission_page']['cursor'] == 3, status()['intermission_page'])

# パイロット能力: the pilot list and page.
s.client.call('ui.click', text='intermission:4')
p = wait_page('pilots')
names = [r['name'] for r in p['rows']]
check('pilot-list', '万丈' in names and all(r['unit'] for r in p['rows'][:3]) and p['cursor'] == 0, p)
shot('ability-pilots.png')
keys('z', pause=1.5)
p = wait_page('pilot')
first = names[0]
check('pilot-page', p['pilot']['name'] == first and p['pilot']['level'] >= 1 and p['sp_max'] > 0 and p['morale'] == 100 and p['stats']['melee'] > 0 and all(k in p['stats'] for k in ('melee', 'ranged', 'hit', 'evade', 'skill', 'reaction')) and len(p['terrain']) == 4 and p['spirits'], p)
note('pilot-page-detail', {k: p[k] for k in ('morale', 'sp', 'sp_max', 'spirits', 'skills', 'terrain', 'over') if k in p} | {'next': p.get('next')})
shot('ability-pilot.png')
keys('e', pause=1.5)
time.sleep(2.5)
p = wait_page('pilot')
check('pilot-page-next', p['pilot']['name'] == names[1] and p['index'] == 1, p['pilot'])
keys('x', pause=1.5)
p = wait_page('pilots')
check('pilot-page-back', p['cursor'] == 1, p)
keys('x')
wait_hidden()
s.wait(intermission_page=True, timeout=20)
check('back-to-menu-2', status()['intermission_page']['cursor'] == 4, status()['intermission_page'])
if args.keep_open:
    print('KEEP OPEN', flush=True)
    while s.alive():
        time.sleep(1)
print('EXIT', s.quit().get('exit_code'), flush=True)
