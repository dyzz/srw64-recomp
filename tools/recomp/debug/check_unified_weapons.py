#!/usr/bin/env python3
"""Verify unified weapon upgrades on the native 武器改造 screens
(docs/design/quick-load-and-unified-weapons.md §2).

Loads a stage-one-clear save and sets the funds to 900000. On ダイターン3's unified page:
the level is the level most shown weapons have, ←→ plan three levels and the price is
what every shown weapon of types 1–4 is missing at its own type's prices (read from the
weapon records and the overlay's price tables here, not from the page); はい pays it in
one go and every such weapon, those only the other forms use included, is at the new
level, the funds down by exactly that. A weapon set back to level 0 (as one that joined
later) comes back up free when the page opens again. With the setting per weapon the
original weapon list returns.

--bonus checks the full-upgrade bonus (D_801DC87C) on ダイターン3, which has none of
its own: two of its weapons are entered in the table in RAM as unlocking two others (as
マジンガーZ(JS) has two), and those are hidden (+0x22 bit 0x400) as bonus weapons are
until then. One payment reaching the cap unlocks both: two messages, one after the other. The original unlock
(801D0AE4), its message and the rule's timing then run as they would for ν's フィン
ファンネル. Levels to one below the original cap: no bonus; one more to the original cap:
the message names both weapons and the bonus weapon is there at that level; with
--cap-break, on to 15: no second unlock.

--cap-break turns on the rule upgrade-cap-break (every unit to 15 levels) and plans
straight to 15 with the funds at 9999999: every weapon reaches 15, the levels past the
unit's own cap show as ●／☆ cells, and the page then says it can go no further."""
import argparse
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, run_keys
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'src'))
from srw64_native import rule_settings

UNITS, UNIT_SIZE = 0x8016A210, 0x54
WEAPON_PRICES = {1: 0x801DC7BC, 2: 0x801DC77C, 3: 0x801DC73C, 4: 0x801DC6FC}

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reuse-build', action='store_true')
parser.add_argument('--language', default='ja')
parser.add_argument('--cap-break', action='store_true')
parser.add_argument('--bonus', action='store_true')
parser.add_argument('--direct', action='store_true', help='with --bonus: one payment from 0 straight to the cap (15 with --cap-break)')
parser.add_argument('--run', help='attach to a game already at the title (another machine, attach.py) instead of launching one; '
                    'its slot 1 must hold the save')
parser.add_argument('--save', default='build/recomp/save-recovery-check/intermission-cold-1.source.sram')
args = parser.parse_args()
rules = list(rule_settings.CORRECTIONS) + (['upgrade-cap-break'] if args.cap_break else [])
if args.run:
    s = Session.attach(args.run)
    s.client.call('settings', locale=args.language, rules=rules)
else:
    s = Session.launch(language=args.language, images='original', rules=rules, save=args.save, reuse_build=args.reuse_build)
print('RUN', s.run, flush=True)
checks = []
def call(method, **params):
    return s.client.call(method, **params)
def mem(address, size):
    return bytes.fromhex(call('memory.read', address=address, size=size)['hex'])
def u(address, size):
    return int.from_bytes(mem(address, size), 'big')
def page():
    return call('status')['upgrade_page']
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
    (s.run/'unified-weapons-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2)+'\n')
    assert passed, (name, state)
    print(name, 'PASS', flush=True)
def shot(name):
    if args.run:   # the host writes on its own machine; fetch it
        import shutil
        shutil.copy(s.local_file(call('screenshot')['path']), s.run/name)
    else:
        call('screenshot', path=str(s.run/name))
def edit_funds(page_id, text):
    call('ui.click', text=page_id + '-funds')
    time.sleep(.5)
    call('ui.type', text=text)
    time.sleep(.2)
    call('ui.key', key='return')
    time.sleep(1)
def weapons(slot):
    """Every weapon of the unit: index, number, forms, type, level, power, hidden."""
    unit = UNITS + slot * UNIT_SIZE
    base, count = u(unit + 0x30, 4), u(unit + 0x2C, 1)
    rows = []
    for i in range(count):
        w = mem(base + i * 0x24, 0x24)
        rows.append({'index': i, 'number': int.from_bytes(w[2:4], 'big'), 'power': int.from_bytes(w[6:8], 'big'),
                     'forms': [int.from_bytes(w[0x18 + 2 * f:0x1A + 2 * f], 'big', signed=True) for f in range(5)],
                     'type': w[0x15], 'level': w[0x16], 'hidden': bool(int.from_bytes(w[0x22:0x24], 'big') & 0x600),
                     'address': base + i * 0x24})
    return rows
def counted(rows):
    return [w for w in rows if 1 <= w['type'] <= 4 and not w['hidden']]
def expected_cost(rows, target, cap):
    total = 0
    for w in counted(rows):
        for level in range(w['level'], min(target, cap)):
            total += u(WEAPON_PRICES[w['type']] + level * 4, 4)
    return total

# Title ring to the load screen, as check_upgrade_plan.py does.
s.wait(vi=600)
if args.run:
    s.wait(timeout=240, title_major=2)   # past the opening; START earlier starts the prologue
    time.sleep(2)
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
    st = call('status')
    if st['intermission_page'].get('visible') or (st.get('intro') or {}).get('title_major') != 7:
        break
    keys('z', pause=1.5)
s.wait(intermission_page=True, timeout=30)
call('settings', weapon_upgrade='unified')
funds = 9999999 if args.cap_break or args.bonus else 900000
edit_funds('intermission', str(funds))

# 武器改造 → the first unit (ダイターン3): the unified page.
call('ui.click', text='intermission:2')
wait_page('list')
keys('z')
p = wait_page('unified')
time.sleep(1.5)
slot, number = p['unit']['slot'], p['unit']['number']
before = weapons(slot)
levels = [w['level'] for w in counted(before)]
mode = max(set(levels), key=lambda l: (levels.count(l), l))
others = [w for w in counted(before) if number not in w['forms']]
check('unified-open', p['level'] == mode and p['plan'] == 0 and p['weapons'] == len(counted(before)) and p['plan_cost'] == 0
      and p.get('next_price') == expected_cost(before, mode + 1, p['cap']),
      {'page': p, 'levels': levels, 'other_forms_only': [w['number'] for w in others]})
shot('unified-open.png')

UNLOCKS = 0x801DC87C
def pay(levels):
    """Plan `levels` more and pay them; the rebuilt page."""
    keys(*['right'] * levels, pause=.25)
    keys('z', pause=.6)
    keys('z', pause=.6)
    time.sleep(3)
    return wait_page('unified')
if args.bonus:
    own = [w for w in counted(before) if number in w['forms']]   # the table names this form, so its own weapons
    pairs = [(own[0], own[-1]), (own[1], own[-2])]
    source, bonus = pairs[0]
    for n, (a, b) in enumerate(pairs):
        call('memory.write', address=UNLOCKS + 6 * n, hex=b''.join(v.to_bytes(2, 'big') for v in (number, a['number'], b['number'])).hex())
        call('memory.write', address=b['address'] + 0x22, hex=(u(b['address'] + 0x22, 2) | 0x400).to_bytes(2, 'big').hex())
    keys('x')
    wait_page('list')
    keys('z')
    p = wait_page('unified')
    time.sleep(1.5)
    original = p['original_cap']
    if args.direct:
        # One plan from 0 to the cap, past the original cap with the rule on: both bonuses come
        # from that one payment; the bonus weapons, hidden while it was paid, cost nothing and
        # are at the new level when the page opens again.
        check('bonus-hidden', p['weapons'] == len(counted(before)) - 2 and p['level'] == 0, {'weapons': p['weapons']})
        top = p['cap']
        hidden_now = weapons(slot)
        cost = expected_cost(hidden_now, top, top)
        p = pay(top)
        after = weapons(slot)
        firsts = [next(w for w in after if w['index'] == b['index']) for _, b in pairs]
        check('direct-bonus-1', p['window'] == 'bonus' and p['bonus']['numbers'] == [pairs[0][0]['number'], pairs[0][1]['number']]
              and p['funds'] == funds - cost == u(0x8010F5F4, 4),
              {'window': p['window'], 'bonus': p.get('bonus'), 'funds': p['funds'], 'expected': funds - cost})
        shot('direct-bonus-1.png')
        keys('z', pause=.8)
        p = page()
        check('direct-bonus-2', p['window'] == 'bonus' and p['bonus']['numbers'] == [pairs[1][0]['number'], pairs[1][1]['number']], {'window': p['window'], 'bonus': p.get('bonus')})
        keys('z', pause=.8)
        p = page()
        after = weapons(slot)
        check('direct-levels', p['window'] == '' and p['level'] == top and p['weapons'] == len(counted(before))
              and all(w['level'] == top for w in counted(after)) and all(not w['hidden'] and w['level'] == top for w in
              (next(a for a in after if a['index'] == b['index']) for _, b in pairs)),
              {'level': p['level'], 'levels': [(w['number'], w['level'], w['hidden']) for w in after]})
        shot('direct-after.png')
        print('ALL', len(checks), 'PASS', flush=True)
        if not args.run:
            print('exit', s.quit().get('exit_code'))
        sys.exit(0)
    check('bonus-hidden', p['weapons'] == len(counted(before)) - 2 and p['level'] == 0, {'weapons': p['weapons'], 'source': source['number'], 'bonus': bonus['number']})
    p = pay(original - 1)
    hidden = next(w for w in weapons(slot) if w['index'] == bonus['index'])
    check('bonus-not-yet', p['window'] == '' and p['level'] == original - 1 and hidden['hidden'], {'window': p['window'], 'level': p['level'], 'bonus': hidden})
    p = pay(1)
    shown = next(w for w in weapons(slot) if w['index'] == bonus['index'])
    check('bonus-at-original-cap', p['window'] == 'bonus' and p['bonus']['numbers'] == [source['number'], bonus['number']] and not shown['hidden']
          and shown['level'] == original and p['level'] == original,
          {'window': p['window'], 'bonus': p.get('bonus'), 'weapon': shown, 'level': p['level']})
    shot('bonus-message.png')
    keys('z', pause=.8)
    p = page()
    second = next(w for w in weapons(slot) if w['index'] == pairs[1][1]['index'])
    check('bonus-second', p['window'] == 'bonus' and p['bonus']['numbers'] == [pairs[1][0]['number'], pairs[1][1]['number']]
          and not second['hidden'] and second['level'] == original, {'window': p['window'], 'bonus': p.get('bonus'), 'weapon': second})
    shot('bonus-message-2.png')
    keys('z', pause=.8)
    p = page()
    check('bonus-listed', p['window'] == '' and p['weapons'] == len(counted(before)) and p['level'] == original,
          {'weapons': p['weapons'], 'level': p['level'], 'rows': [(r['number'], r['level']) for r in p['rows']]})
    shot('bonus-listed.png')
    if args.cap_break:
        call('ui.click', text='upgrade-funds')   # keep the funds ample for the rest
        time.sleep(.5); call('ui.type', text='9999999'); time.sleep(.2); call('ui.key', key='return'); time.sleep(1)
        p = pay(15)
        check('bonus-once', p['window'] == '' and p['level'] == 15 and all(w['level'] == 15 for w in counted(weapons(slot))),
              {'window': p['window'], 'level': p['level'], 'levels': [(w['number'], w['level']) for w in weapons(slot)]})
        shot('bonus-15.png')
    print('ALL', len(checks), 'PASS', flush=True)
    if not args.run:
        print('exit', s.quit().get('exit_code'))
    sys.exit(0)

# ←→: three levels, then four and back to three; with the cap broken, every level to 15.
if args.cap_break:
    keys(*['right'] * 20, pause=.25)
    p = page()
    plan = p['cap'] - mode
    check('cap-break-page', p['cap'] == 15 and p['original_cap'] < 15 and p['plan'] == plan and '#' in p['gauge'],
          {'cap': p['cap'], 'original_cap': p['original_cap'], 'plan': p['plan'], 'gauge': p['gauge']})
else:
    keys('right', 'right', 'right', 'right', 'left')
    p = page()
    plan = 3
target = mode + plan
cost = expected_cost(before, target, p['cap'])
check('unified-plan', p['plan'] == plan and p['target'] == target and p['plan_cost'] == cost and p['gauge'].count('+') + p['gauge'].count('#') == plan
      and all('target' in r for r in p['rows'] if r['level'] < target), {k: p[k] for k in ('plan', 'target', 'plan_cost', 'gauge')} | {'expected_cost': cost})
shot('unified-plan.png')

# A: the window; はい pays every missing level.
keys('z', pause=.6)
p = page()
check('unified-window', p['window'] == 'confirm' and p['plan_cost'] == cost, p)
shot('unified-window.png')
keys('z', pause=.6)
time.sleep(3)
p = wait_page('unified')
after = weapons(slot)
funds -= cost
raised = [w for w in counted(after)]
check('unified-paid', p['level'] == target and p['funds'] == funds == u(0x8010F5F4, 4)
      and all(w['level'] >= min(target, p['cap']) for w in raised)
      and all(a['level'] == b['level'] for a, b in zip(after, before) if not (1 <= b['type'] <= 4) or b['hidden'])
      and all(a['power'] > b['power'] for a, b in zip(after, before) if 1 <= b['type'] <= 4 and not b['hidden'] and b['level'] < target),
      {'funds': p['funds'], 'expected': funds, 'levels': [(w['number'], w['level'], w['power']) for w in after]})
if others:
    check('unified-other-forms', all(next(a for a in after if a['index'] == w['index'])['level'] >= target for w in others),
          [(w['number'], w['forms']) for w in others])
shot('unified-paid.png')

# A weapon at 0 again (as one that joined later): free on the next opening.
victim = counted(after)[0]
call('memory.write', address=victim['address'] + 0x16, hex='00')
keys('x')
wait_page('list')
keys('z')
p = wait_page('unified')
time.sleep(1.5)
synced = next(w for w in weapons(slot) if w['index'] == victim['index'])
check('unified-free-sync', synced['level'] == victim['level'] and synced['power'] == victim['power'] and p['funds'] == funds and p['level'] == target,
      {'victim': victim, 'synced': synced, 'funds': p['funds']})

if args.cap_break:
    keys('z', pause=.6)
    p = page()
    check('cap-break-maxed', p['window'] == 'maxed' and p['level'] == 15 and 'next_price' not in p, {'window': p['window'], 'level': p['level']})
    keys('z', pause=.6)

# Per weapon: the original weapon list again.
call('settings', weapon_upgrade='single')
keys('x')
wait_page('list')
keys('z')
p = wait_page('weapons')
check('single-list', p['screen'] == 'weapons' and len(p['rows']) > 0, {'screen': p['screen'], 'rows': len(p['rows'])})
call('settings', weapon_upgrade='unified')
print('ALL', len(checks), 'PASS', flush=True)
if not args.run:
    report = s.quit()
    print('exit', report.get('exit_code'))
