#!/usr/bin/env python3
"""Verify holding R while choosing a move destination (docs/native/move-jump.md).

Launches the move-jump mini stage (タケル at 8,8 in open ground, a friend at 10,8, one foe
at 15,12), opens 移動 for タケル and, through the debug keyboard (E is R):
holds R (the farthest squares light up, the cursor stays on the unit), jumps right,
walks the rim down, holds a direction to repeat, releases R, steps one square in and
holds R again (the cursor jumps out along that ray), then confirms with A while R is
still held and sees the unit walk. Screenshots, the farthest-square map and
move-checks.json go to the run directory."""
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session

ROOT = Path(__file__).resolve().parents[3]
s = Session.launch(language='zh-Hans', images='hd', diagnostics='light',
                   mini_stage=str(ROOT / 'config/recomp/mini-stages/move-jump.json'))
print('RUN', s.run, flush=True)
checks = []


def status():
    return s.client.call('status')


def mj():
    return status()['move_jump']


def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (s.run / 'move-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2) + '\n')
    print(name, 'PASS' if passed else 'FAIL', flush=True)
    assert passed, (name, state)


def shot(name):
    s.client.call('screenshot', path=str(s.run / f'{name}.png'))


def press(key, hold_ms=90, pause=.35):
    s.client.call('keys', press=key, hold_ms=hold_ms)
    time.sleep(pause)


def choosing(state):
    st = status()
    return st['move_jump'].get('select_vi') is not None and st['vi'] - st['move_jump']['select_vi'] < 8


def cell(c):
    return (c % 31 - 15, c // 31 - 15) if c is not None and c >= 0 else None


s.enter_mini_stage()
time.sleep(5)
press('z', pause=1.2)
press('z', pause=1.2)
end = time.monotonic() + 15
while not choosing(None) and time.monotonic() < end:
    time.sleep(.2)
state = mj()
check('choosing', choosing(None), state)
shot('range')

s.client.call('keys', down='e')
time.sleep(.6)
state = mj()
far = state['farthest']
check('r-lights', state['active'] and len(far) >= 8 and state['cursor'] == 15 * 31 + 15, {k: state[k] for k in ('active', 'cursor')} | {'farthest': len(far)})
rows = []
for y in range(31):
    row = ''
    for x in range(31):
        c = y * 31 + x
        row += '@' if c == 15 * 31 + 15 else '#' if c in far else '.'
    rows.append(row)
(s.run / 'farthest.txt').write_text('\n'.join(r for r in rows if r.strip('.')) + '\n')
check('highlight-drawn', state.get('highlight_vi') and status()['vi'] - state['highlight_vi'] < 8, {'highlight_vi': state.get('highlight_vi')})
shot('held')

press('right')
state = mj()
rightmost = max(cell(c)[0] for c in far)
check('jump-right', state['cursor'] in far and cell(state['cursor'])[0] == rightmost and state['jumps'] == 1, {'cursor': cell(state['cursor']), 'rightmost': rightmost})
shot('right')
before = state['jumps']
for _ in range(2):
    press('down')
state = mj()
check('rim-down', state['cursor'] in far and state['jumps'] == before + 2 and cell(state['cursor'])[1] > 0, {'cursor': cell(state['cursor'])})
shot('rim-down')
before = state['jumps']
press('left', hold_ms=1200)
state = mj()
check('repeat', state['cursor'] in far and state['jumps'] >= before + 4, {'cursor': cell(state['cursor']), 'jumps': state['jumps'] - before})
shot('repeat')

s.client.call('keys', up='e')
time.sleep(.5)
state = mj()
check('released', not state['active'], {'active': state['active']})
rim = cell(state['cursor'])
step = 'right' if rim[0] < 0 else 'left'
press(step, pause=.8)
state = mj()
check('normal-step', state['cursor'] not in far, {'cursor': cell(state['cursor'])})
s.client.call('keys', down='e')
time.sleep(.6)
state = mj()
check('ray-jump', state['active'] and state['cursor'] in far, {'from': cell(state['cursor'])})
shot('ray')

press('z', pause=.5)
s.client.call('keys', up='e')
time.sleep(3)
st = status()
check('moved', st['vi'] - (st['move_jump'].get('select_vi') or 0) > 60, {'select_vi': st['move_jump'].get('select_vi'), 'vi': st['vi']})
shot('moved')
print('DONE', flush=True)
s.quit()
