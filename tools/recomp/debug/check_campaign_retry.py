#!/usr/bin/env python3
"""A game over inside a custom campaign retries the campaign's stage, not the original.

config/recomp/campaigns/retry has one stage borrowing scene 5. Its turn-1 event asks a
choice: the first option is a game over (3D4C), the second a win. The check takes the
first option once, presses through GAME OVER, and expects the retry to register the
same campaign stage on scene 5 again (with its map); then it takes the second option
and plays the ending back to the title. Evidence goes to the run directory."""
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, run_keys

ROOT = Path(__file__).resolve().parents[3]
s = Session.launch(language='zh-Hans', images='original', campaign=str(ROOT / 'config/recomp/campaigns/retry/campaign.json'))
print('RUN', s.run, flush=True)
checks = []


def status():
    return s.client.call('status')


def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (s.run / 'campaign-retry-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2) + '\n')
    assert passed, f'{name}: {json.dumps(state, ensure_ascii=False)[:600]}'
    print(name, 'PASS', flush=True)


def keys(*names, pause=.4):
    for name in names:
        run_keys(s.client, [{'press': name}])
        time.sleep(pause)


def rows(action):
    events = [json.loads(l) for l in (s.run / 'mini-stage-events.jsonl').read_text().splitlines()]
    return [r for r in events if r['action'] == action]


def wait_for(predicate, timeout=30):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        st = status()
        if predicate(st):
            return st
        time.sleep(.2)
    raise AssertionError('timed out')


s.wait(vi=600)
for _ in range(20):
    if (status().get('intro') or {}).get('title_major') == 3:
        break
    keys('return', pause=.5)
s.client.call('ui.click', id='mini-enter')
wait_for(lambda st: st['mini_stage'].get('applied'), timeout=120)
check('stage-on-scene-5', [(r['scene'], r['stage']) for r in rows('applied')] == [(5, 'retry-stand')], rows('applied'))

# First option: game over. A through the dialogue, the choice and GAME OVER until the
# stage registers a second time.
end = time.monotonic() + 600
while time.monotonic() < end and len(rows('applied')) < 2:
    keys('z', pause=1.0)
s.client.call('screenshot', path=str(s.run / 'after-retry.png'))
applied = [(r['scene'], r['stage']) for r in rows('applied')]
check('retry-registers-the-campaign-stage', applied == [(5, 'retry-stand')] * 2 and not rows('unmapped'),
      {'applied': applied, 'unmapped': rows('unmapped')})
check('retry-keeps-the-stage-map', [(r['scene'], r['map']) for r in rows('map')][-1] == (5, 20), rows('map'))

# Second option: win, then the ending back to the title.
end = time.monotonic() + 900
while time.monotonic() < end:
    if (status().get('intro') or {}).get('title_major') == 3:
        break
    keys('down', pause=.3)
    keys('z', pause=1.5)
check('second-option-wins-to-the-ending', (status().get('intro') or {}).get('title_major') == 3 and len(rows('applied')) == 2,
      {'applied': len(rows('applied'))})
print('EXIT', s.quit().get('exit_code'), flush=True)
