#!/usr/bin/env python3
"""Spirits cast on the pre-battle page reach the settled exchange, attacking and defending.

The original settles an exchange, hit rolls included, before its confirmation screen
(801D3140 when the player picks a target, 801C90F4 when an enemy does). Every check
compares the participant table the battle plays from (8018B6E8, 0x5C per slot: +0x14
damage dealt, +0x26 the result of the attack on that slot, 0xFF hit / 0x16 miss) with
the page's own prediction, and the HP the battle actually took.
"""
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session

PARTICIPANTS = 0x8018B6E8
HIT, MISS = 0xFF, 0x16
# The defender of the enemy phase learns these for the test (鉄壁 気合 熱血 魂 集中 ひらめき).
DEFENDER_SPIRITS = [9, 12, 11, 17, 4, 5]


def main():
    s = Session.launch(language='zh-Hans', rules='fixed',
                       mini_stage='config/recomp/mini-stages/battle-spirit-settle.json')
    print('RUN', s.run, flush=True)
    checks = []

    def call(method, **params):
        return s.client.call(method, **params)

    def mem(address, size):
        return bytes.fromhex(call('memory.read', address=address, size=size)['hex'])

    def u16(address):
        return int.from_bytes(mem(address, 2), 'big')

    def u32(address):
        return int.from_bytes(mem(address, 4), 'big')

    def write(address, data):
        call('memory.write', address=address, hex=bytes(data).hex())

    def slot(n):
        base = PARTICIPANTS + n * 0x5C
        return {'unit': u32(base + 4), 'pilot': u32(base + 0xC), 'damage': u16(base + 0x14), 'result': mem(base + 0x26, 1)[0]}

    def page():
        return call('status')['battle_page']

    def wait_page(serial=0, timeout=90):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            p = page()
            if p.get('visible') and p['serial'] > serial and not p.get('spirit_running'):
                return p
            time.sleep(.1)
        call('screenshot', path=str(s.run / 'settle-timeout.png'))
        raise AssertionError('Battle page timeout')

    def click(action):
        call('ui.click', id='battle-' + action)
        time.sleep(.4)

    def buttons(*values):
        for value in values:
            call('buttons', buttons=value, vis=6)
            time.sleep(.7)

    def check(name, passed, **detail):
        checks.append({'check': name, 'passed': bool(passed), **detail})
        (s.run / 'settle-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2) + '\n')
        assert passed, (name, detail)
        print(name, 'PASS', json.dumps(detail, ensure_ascii=False), flush=True)

    def exchange(p):
        """Each side's settled attack against what the page predicts for it."""
        rows = []
        for n, key in ((0, 'attacker'), (1, 'defender')):
            c, dealt, result = p[key], slot(n)['damage'], slot(1 - n)['result']
            expected = (c['damage'], c['critical_damage']) if result == HIT else (0,)
            rows.append({'who': c['pilot_name'], 'hit': c['hit'], 'result': hex(result), 'settled': dealt,
                         'preview': c['damage'], 'critical': c['critical_damage'], 'ok': dealt in expected})
        return rows

    def top_up(n):
        write(slot(n)['pilot'] + 0x16, (500).to_bytes(2, 'big'))

    def cast(spirit):
        before = page()
        option = next(o for o in before['spirit_options'] if o['id'] == spirit)
        assert option['enabled'], option
        if not before.get('spirit_menu'):
            click('spirits')
        click(f"cast:{option['crew']}:{option['slot']}")
        return wait_page(before['serial'])

    def fight(p):
        """Start the exchange with the animation off; the HP each side lost."""
        if p.get('spirit_menu'):
            click('spirit-back')
        if page()['animation']:
            click('animation')
        units = [slot(0)['unit'], slot(1)['unit']]
        hp = [u16(u + 4) for u in units]
        settled = [slot(0)['damage'], slot(1)['damage']]
        results = [slot(1)['result'], slot(0)['result']]   # of slot 0's attack, of slot 1's
        vi = call('status')['vi']
        click('confirm')
        s.wait(vi=vi + 600, timeout=120)
        lost = [hp[n] - u16(units[n] + 4) for n in (0, 1)]
        return hp, lost, settled, results

    s.enter_mini_stage()

    # Attacking: 万丈 (raised to L50) on ロゼ.
    buttons('down', 'down', 'right', 'right', 'a', 'down', 'a', 'a', 'left', 'a')
    p = wait_page()
    check('attack-entry', p['mode'] == 1 and p['attacker']['pilot'] == 165)
    top_up(0)
    rows = exchange(p)
    check('attack-start', all(r['ok'] for r in rows), rows=rows)
    previous = p
    for spirit, name in [(12, 'spirit'), (13, 'guts'), (11, 'valor'), (17, 'soul'), (5, 'alert')]:
        p = cast(spirit)
        rows = exchange(p)
        check(f'attack-{name}-settled', all(r['ok'] for r in rows), rows=rows)
        a, b = p['attacker'], previous['attacker']
        if name == 'spirit':
            check('attack-spirit-morale', a['morale'] == b['morale'] + 10 and a['damage'] > b['damage'])
        if name == 'guts':
            check('attack-guts-hp', a['hp'] == a['max_hp'])
        if name in ('valor', 'soul'):
            # No critical under 熱血/魂: the settled damage is the prediction itself.
            check(f'attack-{name}-exact', a['critical'] == 0 and rows[0]['settled'] == a['damage'] > b['damage'])
        if name == 'alert':
            check('attack-alert-counter-misses', rows[1]['result'] == hex(MISS) and rows[1]['settled'] == 0)
        previous = p
    hp, lost, settled, results = fight(p)
    check('attack-battle-hp', lost[1] == min(settled[0], hp[1]) and lost[0] == 0,
          hp=hp, lost=lost, settled=settled)

    # Attacking: 甲児 on ガラリア. Re-choose the target until the pre-rolled attack misses,
    # then 必中 must turn it into a hit; 鉄壁 must reach the counter.
    buttons('up', 'up', 'right', 'a', 'down', 'a', 'a', 'right', 'a')
    p = wait_page(p['serial'])
    check('koji-entry', p['mode'] == 1 and p['attacker']['pilot'] == 196 and p['attacker']['hit'] < 100,
          hit=p['attacker']['hit'])
    top_up(0)
    for _ in range(20):
        if slot(1)['result'] == MISS:
            break
        # Back in target selection the cursor is on 甲児 again.
        click('back')
        buttons('right', 'a')
        p = wait_page(p['serial'], 15)
    check('koji-pre-rolled-miss', slot(1)['result'] == MISS)
    p = cast(7)
    rows = exchange(p)
    check('koji-sure-hit-turns-hit', p['attacker']['hit'] == 100 and rows[0]['result'] == hex(HIT) and rows[0]['ok'], rows=rows)
    before = p['defender']['damage']
    p = cast(9)
    rows = exchange(p)
    check('koji-wall-counter', rows[1]['ok'] and p['defender']['damage'] < before, rows=rows)
    hp, lost, settled, results = fight(p)
    check('koji-battle-hp', lost[1] == min(settled[0], hp[1]), hp=hp, lost=lost, settled=settled)

    # Defending: end the turn; the first enemy attack is answered with spirits.
    buttons('right', 'right', 'right', 'right', 'a', 'a', 'a')
    p = wait_page(p['serial'])
    check('defend-entry', p['mode'] == 2 and p['defender']['side'] == 0)
    pilot = slot(1)['pilot']
    write(pilot + 0xA, [len(DEFENDER_SPIRITS)] + DEFENDER_SPIRITS)
    top_up(1)
    # A language change rebuilds the page's spirit list from the edited record.
    call('settings', locale='en')
    time.sleep(.6)
    call('settings', locale='zh-Hans')
    time.sleep(.6)
    p = page()
    previous = p
    for spirit, name in [(9, 'wall'), (12, 'spirit'), (4, 'focus'), (17, 'soul'), (5, 'alert')]:
        p = cast(spirit)
        rows = exchange(p)
        check(f'defend-{name}-settled', all(r['ok'] for r in rows), rows=rows)
        a, b = p['attacker'], previous['attacker']
        if name == 'wall':
            check('defend-wall-less-damage', a['damage'] < b['damage'])
        if name == 'focus':
            # The shown rate is clamped to 0–100, so above 130 it stays at 100.
            check('defend-focus-enemy-hit', a['hit'] <= b['hit'] and (a['hit'] < b['hit'] or a['hit'] == 100),
                  before=b['hit'], after=a['hit'])
        if name == 'soul':
            check('defend-soul-exact', rows[1]['settled'] == p['defender']['damage'] or rows[1]['result'] == hex(MISS))
        if name == 'alert':
            check('defend-alert-enemy-misses', rows[0]['result'] == hex(MISS) and rows[0]['settled'] == 0)
        previous = p
    hp, lost, settled, results = fight(p)
    check('defend-battle-hp', lost[1] == 0 and lost[0] == min(settled[1], hp[0]), hp=hp, lost=lost, settled=settled)

    print('DONE', s.run, flush=True)
    exit_code = s.quit().get('exit_code')
    print('EXIT', exit_code, flush=True)
    assert exit_code == 0, f'Native host exit: {exit_code}'


if __name__ == '__main__':
    main()
