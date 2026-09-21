#!/usr/bin/env python3
"""Verify spirits, facing and player cancel/reselection in the current native build."""
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session

s = Session.launch(language='zh-Hans', mini_stage='config/recomp/mini-stages/battle-ui-spirits.json')
print('RUN', s.run, flush=True)
s.enter_mini_stage()
checks = []
def state():
    return s.client.call('status')
def wait_page():
    end = time.monotonic() + 15
    while time.monotonic() < end:
        st = state()
        if st['battle_page'].get('visible'):
            return st
        time.sleep(.1)
    raise AssertionError('Battle page did not open')
def keys(*names):
    # Controller-layer presses held for a fixed VI count; keyboard taps timed in
    # wall-clock were occasionally missed while a map menu was still opening.
    for name in names:
        s.client.call('buttons', buttons={'z': 'a', 'x': 'b'}.get(name, name), vis=6)
        time.sleep(.7)
def check(name, passed, st):
    checks.append({'check': name, 'passed': bool(passed), 'state': st})
    (s.run/'spirit-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2)+'\n')
    assert passed, name
    print(name, 'PASS', flush=True)

# The fixture casts real 3D55 spirits and raises Sho's morale to 130.
keys('right', 'right', 'z', 'down', 'z', 'down', 'down', 'z')
time.sleep(.7)
keys('left', 'z')
st = wait_page()
p = st['battle_page']
check('player-confirm', p['mode'] == 1 and p['can_cancel'], st)
check('spirits-both-sides', {4,7,11,14} <= {x['id'] for x in p['attacker']['active_spirits']} and 9 in {x['id'] for x in p['defender']['active_spirits']}, st)
check('sure-hit-heat-clone', p['attacker']['hit'] == 100 and p['attacker']['critical'] == 0 and p['attacker']['morale'] >= 130 and p['attacker']['defense']['clone'] == 50, st)
s.client.call('screenshot', path=str(s.run/'spirits-player.png'))
s.client.call('ui.key', key='escape')
time.sleep(.3)
st = state()
check('escape-to-target', not st['battle_page']['visible'], st)
s.client.call('screenshot', path=str(s.run/'player-back-to-target.png'))
keys('right', 'z')
st = wait_page()
check('target-reselection', st['battle_page']['serial'] > p['serial'], st)
s.client.call('ui.key', key='return')
time.sleep(.3)
st = state()
check('keyboard-confirm', not st['battle_page']['visible'], st)
print('EXIT', s.quit().get('exit_code'), flush=True)
