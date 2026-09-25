#!/usr/bin/env python3
"""Verify the Steam Deck entries and L2 / R2 enemy cycling (docs/native/enemy-cycle.md,
docs/design/steam-deck-controls.md) through the debug interface's virtual controller.

Title screen: View opens the settings window; afterwards the bottom-right settings entry
shows with the controller hint. Then the enemy-cycle mini stage (タケル at 8,8; foes at 13,10, 3,4 and far off at 20,26; a third-party unit at 22,5):
R2 walks every opponent in roster order and wraps, L2 steps back, a held R2 repeats, and A
on an enemy runs the original. Screenshots and cycle-checks.json go to the run directory."""
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session

ROOT = Path(__file__).resolve().parents[3]
s = Session.launch(language='zh-Hans', images='hd', diagnostics='light',
                   mini_stage=str(ROOT / 'config/recomp/mini-stages/enemy-cycle.json'))
print('RUN', s.run, flush=True)
checks = []


def status():
    return s.client.call('status')


def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (s.run / 'cycle-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2) + '\n')
    print(name, 'PASS' if passed else 'FAIL', json.dumps(state, ensure_ascii=False)[:300], flush=True)


def shot(name):
    s.client.call('screenshot', path=str(s.run / f'{name}.png'))


def pad(buttons, hold_ms=100, pause=.5):
    s.client.call('pad', press=buttons, hold_ms=hold_ms)
    time.sleep(pause)


def find(node, wanted):
    """The UI tree nodes whose id is `wanted`."""
    found = []
    if isinstance(node, dict):
        if node.get('id') == wanted:
            found.append(node)
        for value in node.values():
            found += find(value, wanted)
    elif isinstance(node, list):
        for value in node:
            found += find(value, wanted)
    return found


# --- Title screen ---------------------------------------------------------------------
s.wait(vi=600, timeout=90)
end = time.monotonic() + 60
while status()['intro'].get('title_major') != 3 and time.monotonic() < end:
    s.client.call('keys', press='return', hold_ms=100)
    time.sleep(.6)
time.sleep(1)
pad('view', pause=1)
st = status()
check('view-opens-settings', st['settings_window'], {'settings_window': st['settings_window']})
shot('title-settings')
pad('b', pause=1)
check('b-closes-settings', not status()['settings_window'], {})
entry = find(s.client.call('ui.tree'), 'settings-open')
check('title-entry', bool(entry), {'entry': entry[:1]})
shot('title-entry')

# --- The map ----------------------------------------------------------------------------
s.enter_mini_stage()
steps = lambda: status()['enemy_cycle']['steps']
end = time.monotonic() + 60
while steps() == 0 and time.monotonic() < end:
    pad('r2', pause=1)
st = status()['enemy_cycle']
check('first-step', st['steps'] >= 1, st)
shot('cycle-1')
count = st['last']['opponents'] if st['last'] else 0
seen = [(st['last']['side'], st['last']['slot'])] if st['last'] else []
for n in range(count):
    pad('r2', pause=.9)
    last = status()['enemy_cycle']['last']
    seen.append((last['side'], last['slot']))
    shot(f'cycle-{n + 2}')
check('all-opponents', count >= 4 and len(set(seen[:count])) == count and any(side == 2 for side, _ in seen), {'seen': seen, 'count': count})
check('wraps', len(seen) == count + 1 and seen[-1] == seen[0], {'seen': seen})
pad('l2', pause=.9)
last = status()['enemy_cycle']['last']
check('l2-back', last['direction'] == 'previous' and (last['side'], last['slot']) == seen[count - 1], {'last': last})
before = steps()
pad('r2', hold_ms=1500, pause=.8)
check('repeat', steps() - before >= 3, {'steps': steps() - before})
pad('a', pause=2)
shot('enemy-a')
pad('b', pause=1.5)
shot('after-b')
(s.run / 'enemy-cycle-status.json').write_text(json.dumps(status(), ensure_ascii=False, indent=2) + '\n')
s.quit()
failed = [c['check'] for c in checks if not c['passed']]
print('FAILED' if failed else 'ALL PASS', failed, flush=True)
sys.exit(1 if failed else 0)
