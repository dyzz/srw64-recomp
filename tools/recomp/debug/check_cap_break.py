#!/usr/bin/env python3
"""Verify the native ユニット改造 / 武器改造 screens with 改造上限突破 (upgrade-cap-break) on.

Loads a stage-one-clear save with the rule enabled, sets the funds to 900000 through the
funds box, then on ダイターン3 (original cap 7): the five-stat page must show cap 15 with
the ▷ cells to 7 and ☆ beyond, an upgrade past the original cap must pay the table price
and fill a ● cell, level 15 must show これ以上, and the weapon list must show the same
15-cell gauge with the はい path actually paying and raising a weapon past 7."""
import argparse
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, run_keys
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'src'))
from srw64_native import rule_settings

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reuse-build', action='store_true')
parser.add_argument('--save', default='build/recomp/save-recovery-check/intermission-cold-1.source.sram')
parser.add_argument('--keep-open', action='store_true')
args = parser.parse_args()
s = Session.launch(language='ja', images='original', rules=list(rule_settings.CORRECTIONS)+['upgrade-cap-break'], save=args.save, reuse_build=args.reuse_build)
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
def keys(*names, pause=.4):
    for name in names:
        run_keys(s.client, [{'press': name}])
        time.sleep(pause)
def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (s.run/'cap-break-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2)+'\n')
    assert passed, name
    print(name, 'PASS', flush=True)
def note(name, state):
    checks.append({'check': name, 'passed': None, 'state': state})
    print(name, 'NOTE', json.dumps(state, ensure_ascii=False)[:300], flush=True)
def shot(name):
    s.client.call('screenshot', path=str(s.run/name))
def edit_funds(page_id, text):
    s.client.call('ui.click', text=page_id + '-funds')
    time.sleep(.5)
    s.client.call('ui.type', text=text)
    time.sleep(.2)
    s.client.call('ui.key', key='return')
    time.sleep(1)
def upgrade_stat(row):
    """One confirmed upgrade of the stat row under the cursor; returns the new page."""
    keys('z', 'z', pause=.6)
    time.sleep(3)
    return wait_page('stats')

# Title ring to the load screen, as check_upgrade.py does.
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
rules = status().get('rules')
note('rules', rules)
edit_funds('intermission', '900000')
check('menu-funds', status()['intermission_page']['funds'] == 900000, status()['intermission_page'])

# ユニット改造 → ダイターン3 five stats with the rule on.
s.client.call('ui.click', text='intermission:1')
p = wait_page('list')
check('list-open', [r['name'] for r in p['rows']][0] == 'ダイターン3' and p['cursor'] == 0, p)
keys('z')
p = wait_page('stats')
hp = p['rows'][0]
check('stats-cap-15', p['unit']['name'] == 'ダイターン3' and p['cap'] == 15 and hp['cap'] == 15 and hp['original_cap'] == 7, p)
check('stats-gauge-split', hp['gauge'] == '.'*7 + 'o'*8 and all(r['gauge'] == '.'*7 + 'o'*8 for r in p['rows']), p)
check('stats-first-price', hp['level'] == 0 and hp['price'] == 2000 and hp['preview'] == 8200, p)
shot('cap-break-stats-open.png')

# HP to level 7 (the original cap), then one more: the eighth pays the table price.
funds = 900000
prices = []
for level in range(7):
    p = page()
    prices.append(p['rows'][0]['price'])
    funds -= p['rows'][0]['price']
    p = upgrade_stat(0)
    check(f'hp-level-{level+1}', p['rows'][0]['level'] == level+1 and p['funds'] == funds, {'funds': p['funds'], 'row': p['rows'][0]})
hp = page()['rows'][0]
check('hp-at-original-cap-still-upgradable', hp['level'] == 7 and 'price' in hp and hp['gauge'] == '>'*7 + 'o'*8, hp)
shot('cap-break-stats-7.png')
price8 = hp['price']
preview8 = hp['preview']
funds -= price8
p = upgrade_stat(0)
hp = p['rows'][0]
check('hp-past-original-cap', hp['level'] == 8 and p['funds'] == funds and hp['value'] == preview8 and hp['gauge'] == '>'*7 + '*' + 'o'*7, {'funds': p['funds'], 'price8': price8, 'row': hp})
shot('cap-break-stats-8.png')
note('hp-prices-1-8', prices + [price8])

# On to 15: これ以上 and no price.
for level in range(8, 15):
    p = page()
    funds -= p['rows'][0]['price']
    p = upgrade_stat(0)
    assert p['rows'][0]['level'] == level+1, p['rows'][0]
hp = page()['rows'][0]
check('hp-level-15', hp['level'] == 15 and 'price' not in hp and hp['gauge'] == '>'*7 + '*'*8 and page()['funds'] == funds, {'funds': page()['funds'], 'row': hp})
time.sleep(1.5)   # the re-entered page finishes its fade before it takes a choice
keys('z', pause=.8)
check('hp-maxed-window', page()['window'] == 'maxed', page())
shot('cap-break-stats-maxed.png')
keys('x')
check('hp-maxed-closed', page()['window'] == '', page())
hp_value = hp['value']
# Back to the list: the shown HP is the propagated value.
keys('x')
p = wait_page('list')
check('list-hp-after', p['rows'][0]['hp'] == hp_value, p)
keys('x')
s.wait(intermission_page=True, timeout=20)

# 武器改造: the same gauge on the weapon rows, and the はい path actually paying.
edit_funds('intermission', '900000')
funds = 900000
s.client.call('ui.click', text='intermission:2')
p = wait_page('list')
keys('z')
p = wait_page('weapons')
w = p['rows'][0]
check('weapon-cap-15', w['cap'] == 15 and w['original_cap'] == 7 and w['gauge'] == '.'*7 + 'o'*8 and w['level'] == 0, w)
shot('cap-break-weapons-open.png')
power = w['power']
# はい: the original pays, writes the previewed power, raises the level and re-enters
# the confirm screen (next screen 12) for the same weapon, as the stat screen does.
keys('z')
for level in range(8):
    p = wait_page('weapon')
    w = p['weapon']
    check(f'weapon-confirm-level-{level}', p['window'] == 'confirm' and w['level'] == level and p['funds'] == funds, {'funds': p['funds'], 'weapon': w})
    price, preview = w['price'], w['preview']
    funds -= price
    keys('z', pause=1.5)   # the answer waits for the fade-in to end
    time.sleep(3)
    p = wait_page('weapon')
    w = p['weapon']
    check(f'weapon-level-{level+1}', w['level'] == level+1 and w['power'] == preview and p['funds'] == funds, {'funds': p['funds'], 'price': price, 'weapon': w})
w = page()['weapon']
check('weapon-past-original-cap', w['level'] == 8 and w['gauge'] == '>'*7 + '*' + 'o'*7 and w['power'] > power and 'price' in w, w)
shot('cap-break-weapon-confirm-8.png')
# いいえ back to the list: the full-upgrade bonus window if the unlock table names this weapon.
keys('down', 'z')
p = wait_page('weapons')
if p.get('window') == 'bonus':
    note('weapon-bonus', p.get('bonus'))
    shot('cap-break-weapon-bonus.png')
    keys('z')
    p = wait_page('weapons')
check('weapon-list-past-cap', p['rows'][0]['level'] == 8 and p['rows'][0]['gauge'] == '>'*7 + '*' + 'o'*7, p['rows'][0])
shot('cap-break-weapons-8.png')
keys('x')
wait_page('list')
keys('x')
s.wait(intermission_page=True, timeout=20)
if args.keep_open:
    print('KEEP OPEN', flush=True)
    while s.alive():
        time.sleep(1)
print('EXIT', s.quit().get('exit_code'), flush=True)
