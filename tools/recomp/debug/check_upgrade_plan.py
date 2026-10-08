#!/usr/bin/env python3
"""Verify the modern way to upgrade on the native ユニット改造 / 武器改造 screens.

Loads a stage-one-clear save, sets the funds to 900000 through the funds box, then on
ダイターン3: ←→ plan levels on the five-stat rows (green gauge cells, target values and
the whole price in 費用), B drops a plan, A then はい pays every planned level in one go
through the original step (levels, funds and the cursor row afterwards), a bare A still
plans one level and いいえ undoes it, the plan stops at what the funds cover; on the
weapon confirm screen ←→ plan levels and はい pays them all."""
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
s = Session.launch(language='ja', images='original', rules=list(rule_settings.CORRECTIONS), save=args.save, reuse_build=args.reuse_build)
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
    (s.run/'upgrade-plan-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2)+'\n')
    assert passed, name
    print(name, 'PASS', flush=True)
def shot(name):
    s.client.call('screenshot', path=str(s.run/name))
def edit_funds(page_id, text):
    s.client.call('ui.click', text=page_id + '-funds')
    time.sleep(.5)
    s.client.call('ui.type', text=text)
    time.sleep(.2)
    s.client.call('ui.key', key='return')
    time.sleep(1)
def cost(row, plan):
    return sum(step['price'] for step in row['steps'][:plan])

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
edit_funds('intermission', '900000')
funds = 900000

# ユニット改造 → ダイターン3 five stats.
s.client.call('ui.click', text='intermission:1')
wait_page('list')
keys('z')
p = wait_page('stats')
time.sleep(1.5)   # the page takes choices once its fade-in ends
hp0, en0 = p['rows'][0], p['rows'][1]
check('stats-open', p['cursor'] == 0 and p['plan_cost'] == 0 and all(r['plan'] == 0 for r in p['rows']) and len(hp0['steps']) == hp0['cap'] - hp0['level'], p)

# ←→ plans: three levels of HP, two of EN, then one back.
keys('right', 'right', 'right')
p = page()
hp = p['rows'][0]
check('hp-plan-3', hp['plan'] == 3 and hp['target'] == hp0['steps'][2]['value'] and hp['gauge'].startswith('+++') and p['plan_cost'] == cost(hp0, 3), p)
keys('down', 'right', 'right', 'right', 'left')
p = page()
en = p['rows'][1]
check('en-plan-2', p['cursor'] == 1 and en['plan'] == 2 and p['plan_cost'] == cost(hp0, 3) + cost(en0, 2), p)
shot('plan-stats.png')
# B drops the whole plan and stays.
keys('x')
p = page()
check('back-drops-plan', p['visible'] and p['screen'] == 'stats' and p['plan_cost'] == 0 and all(r['plan'] == 0 for r in p['rows']), p)
# A bare A plans the cursor row one level; いいえ undoes it.
keys('z')
p = page()
check('bare-a-plans-one', p['window'] == 'confirm' and p['rows'][1]['plan'] == 1, p)
keys('x')
p = page()
check('no-undoes-bare-a', p['window'] == '' and all(r['plan'] == 0 for r in p['rows']), p)

# The plan again, then はい: one fade, every level paid by the original step.
keys('up', 'right', 'right', 'right', 'down', 'right', 'right')
p = page()
total = p['plan_cost']
check('plan-again', total == cost(hp0, 3) + cost(en0, 2), p)
keys('z', pause=.6)
check('plan-window', page()['window'] == 'confirm', page())
keys('z', pause=.6)
time.sleep(3)
p = wait_page('stats')
funds -= total
hp, en = p['rows'][0], p['rows'][1]
check('plan-paid', hp['level'] == hp0['level'] + 3 and en['level'] == en0['level'] + 2 and p['funds'] == funds and
      hp['value'] == hp0['steps'][2]['value'] and en['value'] == en0['steps'][1]['value'] and p['cursor'] == 1 and p['plan_cost'] == 0,
      {'funds': p['funds'], 'expected_funds': funds, 'page': p})
shot('plan-stats-paid.png')

# The funds bound the plan: with just enough for one EN level, a second does not fit.
time.sleep(1.5)
edit_funds('upgrade', str(en['steps'][0]['price']))
keys('right', 'right')
p = page()
check('plan-bounded-by-funds', p['rows'][1]['plan'] == 1 and p['plan_cost'] == en['steps'][0]['price'], p)
keys('x')   # drop the plan
keys('x')
wait_page('list')
keys('x')
s.wait(intermission_page=True, timeout=20)

# 武器改造: ←→ on the confirm screen, はい pays the whole plan.
edit_funds('intermission', '900000')
funds = 900000
s.client.call('ui.click', text='intermission:2')
wait_page('list')
keys('z')
wait_page('weapons')
time.sleep(1.5)
keys('z')
p = wait_page('weapon')
time.sleep(1.5)
w0 = p['weapon']
check('weapon-plan-1', p['window'] == 'confirm' and w0['plan'] == 1 and p['plan_cost'] == w0['price'], p)
keys('right', 'right', 'right', 'left')
p = page()
w = p['weapon']
check('weapon-plan-3', w['plan'] == 3 and w['target'] == w0['steps'][2]['power'] and p['plan_cost'] == cost(w0, 3) and w['gauge'].startswith('>' * w0['level'] + '+++'), p)
shot('plan-weapon.png')
keys('z', pause=.6)
time.sleep(3)
p = wait_page('weapon')
w = p['weapon']
funds -= cost(w0, 3)
check('weapon-plan-paid', w['level'] == w0['level'] + 3 and w['power'] == w0['steps'][2]['power'] and p['funds'] == funds and w['plan'] == 1,
      {'funds': p['funds'], 'expected_funds': funds, 'weapon': w})
shot('plan-weapon-paid.png')
keys('down', 'z')
p = wait_page('weapons')
if p.get('window') == 'bonus':
    keys('z')
    wait_page('weapons')
keys('x')
wait_page('list')
keys('x')
s.wait(intermission_page=True, timeout=20)
if args.keep_open:
    print('KEEP OPEN', flush=True)
    while s.alive():
        time.sleep(1)
print('EXIT', s.quit().get('exit_code'), flush=True)
