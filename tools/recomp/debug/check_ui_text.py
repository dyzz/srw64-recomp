#!/usr/bin/env python3
"""Check the original UI drawn natively (docs/native/native-ui-text.md) on the battle-ui mini stage.

Walks the unit command menu, the phase menu, the unit list (部隊表), the end-turn confirmation, the
objectives window (作戦目的), the enemy phase's original weapon list, the battle HUD and a map battle's damage figure, and checks
the ui_text status (labels in the reading language, the number moved into the confirmation
sentence, the objective bodies, no pass left original for a mismatch) and the HUD badge drawn as
text. Screenshots and ui-text-checks.json go to the run directory."""
import argparse
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session

EXPECT = {
    'zh-Hans': {'command': '移动', 'phase': '回合结束', 'list': '我方部队表', 'confirm': '还有4台', 'objectives': '作战目标'},
    'en': {'command': 'Move', 'phase': 'End Turn', 'list': 'Ally Unit List', 'confirm': '4 unit', 'objectives': 'Objectives'},
    'ja': {'command': '移動', 'phase': 'フェイズ終了', 'list': '部隊表', 'confirm': 'ユニットが', 'objectives': '作戦目的'},
}
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--language', default='zh-Hans', choices=sorted(EXPECT))
args = parser.parse_args()
expect = EXPECT[args.language]
s = Session.launch(language=args.language, images='hd', diagnostics='light', mini_stage='config/recomp/mini-stages/battle-ui.json')
print('RUN', s.run, flush=True)
checks = []


def status():
    return s.client.call('status')


def texts(st):
    ui = st.get('ui_text') or {}
    return [x.get('text') or x.get('number') or '' for x in ui.get('back', []) + ui.get('front', [])]


def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (s.run / 'ui-text-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2) + '\n')
    print(name, 'PASS' if passed else 'FAIL', flush=True)
    assert passed, (name, state)


def shot(name):
    s.client.call('screenshot', path=str(s.run / f'{name}.png'))


def key(name, pause=.6):
    s.client.call('keys', press=name, hold_ms=100)
    time.sleep(pause)


def shown(name, word, timeout=10):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        st = status()
        if any(word in t for t in texts(st)):
            shot(name)
            check(name, True, texts(st)[:40])
            return st
        time.sleep(.2)
    shot(name)
    check(name, False, texts(status())[:40])


s.enter_mini_stage()
time.sleep(3)
key('z', 1)
shown('command-menu', expect['command'])
key('x', 1)
key('up', 1)
key('z', 1)
shown('phase-menu', expect['phase'])
key('down', .6)
key('z', 1.2)
shown('unit-list', expect['list'])
key('x', 1.2)
key('down', .5)
key('down', .5)
key('z', 2)
st = shown('objectives', expect['objectives'])
check('objective-bodies', sum('body' in x for x in (st['ui_text'].get('back') or [])) == 2, st['ui_text'].get('back'))
key('x', 1.5)
end = time.monotonic() + 10
while time.monotonic() < end and any('body' in x for x in status()['ui_text'].get('back', [])):
    time.sleep(.2)
key('up', .5)
key('up', .5)
key('up', .5)
key('z', 1.2)
st = shown('end-turn', expect['confirm'])
# Translated sentences take the number into their own word order; Japanese keeps its cells.
if args.language != 'ja':
    check('number-in-sentence', any(x.get('consumed') for x in st['ui_text'].get('front', [])), st['ui_text'].get('front'))
key('z', 1)
end = time.monotonic() + 120
while time.monotonic() < end and not status()['battle_page'].get('visible'):
    time.sleep(.2)
s.client.call('ui.click', id='battle-weapon')
time.sleep(1.5)
st = status()
shot('weapon-list')
weapons = [x for x in st['ui_text'].get('back', []) if '' in (x.get('text') or '') or '' in (x.get('text') or '')]
check('weapon-markers', len(weapons) >= 3, [x.get('text') for x in weapons])
key('z', 1)
end = time.monotonic() + 20
while time.monotonic() < end and not status()['battle_page'].get('visible'):
    time.sleep(.2)
s.client.call('ui.click', id='battle-confirm')
time.sleep(5)
st = status()
shot('battle-hud')
numbers = [x for x in st['ui_text'].get('front', []) if 'number' in x]
check('hud-numbers', len(numbers) >= 4, numbers)
grid = [json.loads(line) for line in (s.run / 'scene-sprites.jsonl').read_text().splitlines() if '"grid text"' in line]
check('hud-badge', any(row['scene'] in (1154, 1155, 1156) for row in grid), [row['scene'] for row in grid])
check('no-mismatch', st['ui_text']['mismatched'] == 0, {k: st['ui_text'][k] for k in ('passes', 'drawn', 'mismatched')})
# The next enemy attack without animation: the damage figure pops up on the map (80209900).
end = time.monotonic() + 90
while time.monotonic() < end and not status()['battle_page'].get('visible'):
    time.sleep(.2)
if status()['battle_page'].get('animation'):
    s.client.call('ui.click', id='battle-animation')
    time.sleep(.5)
s.client.call('ui.click', id='battle-confirm')
figures = []
end = time.monotonic() + 40
while time.monotonic() < end and not figures:
    figures = [json.loads(line) for line in (s.run / 'ui-text.jsonl').read_text().splitlines() if '"damage"' in line]
    time.sleep(.2)
time.sleep(.6)
shot('map-damage')
check('map-damage-figure', any(any(c.isdigit() for c in row['figure']) for row in figures), [row['figure'] for row in figures])
print(json.dumps({'passed': sum(c['passed'] for c in checks), 'checks': len(checks)}), flush=True)
s.quit()
