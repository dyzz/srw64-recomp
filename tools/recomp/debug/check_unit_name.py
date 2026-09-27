#!/usr/bin/env python3
"""The 部隊名 stays マーチウィンド (docs/native/fixed-unit-name.md), in the unit-name mini stage.

Enters config/recomp/mini-stages/unit-name.json from the title menu and presses A
through its dialogue until the stage reaches the battlefield. Checks: the naming
choice 「それでかまわない／気に入らない」 (text 24045) is answered with its first
option without a window, so アーク's agreement (24046) shows and the other branch
(24050, 24054, 3D5E, 24055) does not; the lone 3D5E after it does nothing and no
name page opens; 忍's line (24060) prints the 部隊名. The name should read in the
page language (三月风 / March Wind) once the dialogue shows default names by
language; until then it reads マーチウィンド, which the check reports."""
import argparse
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, run_keys

ROOT = Path(__file__).resolve().parents[3]
STAGE = ROOT / 'config/recomp/mini-stages/unit-name.json'

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reuse-build', action='store_true')
parser.add_argument('--language', default='zh-Hans', choices=('ja', 'zh-Hans', 'en'))
args = parser.parse_args()
default = json.loads((ROOT / f'content/locales/{args.language}.json').read_text())['ui']['unit_default_name']
s = Session.launch(language=args.language, images='original', mini_stage=str(STAGE), reuse_build=args.reuse_build)
print('RUN', s.run, flush=True)
checks = []


def status():
    return s.client.call('status')


def keys(*names, pause=.4):
    for name in names:
        run_keys(s.client, [{'press': name}])
        time.sleep(pause)


def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (s.run / 'unit-name-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2) + '\n')
    assert passed, name
    print(name, 'PASS', flush=True)


# Title menu, then the mini stage.
s.wait(vi=600, timeout=300)
for _ in range(40):
    if status()['intro'].get('title_major') == 3:
        break
    keys('return', pause=.5)
time.sleep(2)
s.client.call('ui.click', id='mini-enter')

# A through every line until the stage is on the battlefield (ready); note each line
# shown, and that no name page ever opens.
seen, texts, page_opened, shot_taken = [], {}, False, False
end = time.monotonic() + 900
while time.monotonic() < end:
    st = status()
    page_opened |= bool(st['name_page'].get('visible'))
    if (st.get('mini_stage') or {}).get('ready'):
        break
    boxes = [box for box in (st.get('dialogue') or {}).get('boxes', []) if box.get('active')]
    for box in boxes:
        if box.get('text_id') not in seen:
            seen.append(box.get('text_id'))
        texts[box.get('text_id')] = box.get('text', '')
    if any(box.get('text_id') == 24060 for box in boxes) and not shot_taken:
        time.sleep(1.5)
        s.client.call('screenshot', path=str(s.run / 'unit-name-dialogue.png'))
        shot_taken = True
    if boxes:
        keys('z', pause=.6)
    else:
        time.sleep(.3)

rows, _ = s.events('unit_name')
check('no-name-page', not page_opened, {'seen': seen})
check('default-registered', any(r.get('kind') == 'default' and r.get('names', {}).get('ja') == 'マーチウィンド' for r in rows), rows)
check('choice-answered', [r.get('text') for r in rows if r.get('kind') == 'choice-answered'] == [24045], rows)
check('3d5e-skipped', sum(r.get('kind') == 'page-skipped' for r in rows) == 1, rows)
check('first-branch', all(t in seen for t in (24044, 24046, 24060, 24062)) and not any(t in seen for t in (24050, 24054, 24055)), seen)
line = texts.get(24060, '')
localized = default in line
check('name-in-dialogue', localized or 'マーチウィンド' in line, {'line': line, 'localized': localized})
print('LOCALIZED', 'yes' if localized else 'no (the dialogue still shows the stored マーチウィンド)', flush=True)
print('EXIT', s.quit().get('exit_code'), flush=True)
