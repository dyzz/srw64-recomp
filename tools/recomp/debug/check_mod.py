#!/usr/bin/env python3
"""The title screen's MOD manager (docs/design/custom-campaign.md §8).

Two runs with the campaigns under config/recomp/campaigns installed beside the run:
1. Main game: the title shows the MOD entry; the manager opens on its extra-scenario
   page (one row per campaign with its name, description and stage count, in Chinese
   and English); the art, dialogue and music pages show the HD switch, the dialogue
   folder and reload, and the music note; choosing the sample asks the launcher for it
   and closes the game.
2. In the sample campaign: the title offers 开始战役 for the campaign; the manager shows
   it as playing with the way back, which asks for the main game and closes the game.
A debug run has no launcher to restart it, so each run ends at the request. Evidence
goes to the run directories."""
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, run_keys

ROOT = Path(__file__).resolve().parents[3]
checks = []
evidence = []


def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    for run in evidence:
        (run / 'mod-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2) + '\n')
    assert passed, f'{name}: {json.dumps(state, ensure_ascii=False)[:800]}'
    print(name, 'PASS', flush=True)


def to_title(s):
    s.wait(vi=600)
    for _ in range(20):
        if (s.client.call('status').get('intro') or {}).get('title_major') == 3:
            return
        run_keys(s.client, [{'press': 'return'}])
        time.sleep(.5)
    raise AssertionError('title menu not reached')


def ids(s):
    """Every element id in the shared UI, with whether it is enabled."""
    found = {}
    def walk(node):
        if isinstance(node, dict):
            if node.get('id'):
                found[node['id']] = node.get('enabled', True)
            for value in node.values():
                walk(value)
        elif isinstance(node, list):
            for value in node:
                walk(value)
    walk(s.client.call('ui.tree'))
    return found


def text_of(s):
    return json.dumps(s.client.call('ui.tree'), ensure_ascii=False)


def page(s, name, shot):
    s.client.call('ui.click', id=f'mod-page:{name}')
    time.sleep(1.2)
    s.client.call('screenshot', path=str(s.run / shot))
    return text_of(s)


def ended(s, switch, timeout=60):
    """The game closes after a switch; the request is what the launcher would read."""
    end = time.monotonic() + timeout
    while time.monotonic() < end and s.alive():
        time.sleep(.5)
    return json.loads(switch.read_text()) if switch.exists() else None


# 1. The main game.
s = Session.launch(language='zh-Hans', images='original', campaigns=True)
evidence.append(s.run)
print('RUN', s.run, flush=True)
switch = s.run.parent / (s.run.name + '.campaign-switch.json')
to_title(s)
time.sleep(2)
s.client.call('screenshot', path=str(s.run / 'title-mod-entry.png'))
check('title-shows-mod-entry', 'mod-open' in ids(s), ids(s))
s.client.call('ui.click', id='mod-open')
time.sleep(1.5)
s.client.call('screenshot', path=str(s.run / 'mod-campaigns-zh.png'))
installed = sorted(p.name for p in (s.run.parent / (s.run.name + '.campaigns')).iterdir())
found = ids(s)
check('campaign-page', all(found.get(f'dlc-enter:{i}') for i in range(len(installed))) and
      all(f'mod-page:{p}' in found for p in ('campaigns', 'art', 'dialogue', 'audio')) and
      '示例战役' in text_of(s) and '共 4 关' in text_of(s) and '未开始' in text_of(s),
      {'installed': installed, 'ids': [k for k in found if k.startswith(('dlc', 'mod'))]})
art = page(s, 'art', 'mod-art-zh.png')
check('art-page', 'images:original' in ids(s) and 'images:hd' in ids(s) and ('HD 包已安装' in art or '没有找到 HD 包' in art), {'ids': list(ids(s))})
dialogue = page(s, 'dialogue', 'mod-dialogue-zh.png')
check('dialogue-page', 'mod-dialogue-reload' in ids(s) and '来自' in dialogue, {'ids': list(ids(s))})
check('audio-page', '还在开发中' in page(s, 'audio', 'mod-audio-zh.png'), {})
s.client.call('settings', locale='en')
time.sleep(2)
english = page(s, 'campaigns', 'mod-campaigns-en.png')
check('english', 'Sample Campaign' in english and 'Extra Scenarios' in english and 'Mods' in english, {})
s.client.call('settings', locale='zh-Hans')
time.sleep(1.5)
s.client.call('ui.click', id=f'dlc-enter:{installed.index("srw64.sample")}')
request = ended(s, switch)
check('enter-asks-for-the-campaign', request and request.get('schema') == 'srw64.campaign-switch.v1' and
      request.get('campaign', '').endswith('srw64.sample/campaign.json') and not s.alive(), request)

# 2. Inside the sample campaign: the way back.
s = Session.launch(language='zh-Hans', images='original', campaign=str(ROOT / 'config/recomp/campaigns/sample/campaign.json'),
                   campaigns=True)
evidence.append(s.run)
print('RUN', s.run, flush=True)
switch = s.run.parent / (s.run.name + '.campaign-switch.json')
to_title(s)
time.sleep(2)
s.client.call('screenshot', path=str(s.run / 'title-in-campaign.png'))
found = ids(s)
check('campaign-title', 'mod-open' in found and 'mini-enter' in found and '开始战役' in text_of(s) and '示例战役' in text_of(s), found)
s.client.call('ui.click', id='mod-open')   # beside the campaign's start button, which must not cover it
time.sleep(1.5)
s.client.call('screenshot', path=str(s.run / 'mod-in-campaign.png'))
found = ids(s)
sample = installed.index('srw64.sample')
check('playing-shown', found.get('dlc-leave') and found.get(f'dlc-enter:{sample}') is False and '游玩中' in text_of(s), found)
s.client.call('ui.click', id='dlc-leave')
request = ended(s, switch)
check('leave-asks-for-the-main-game', request and request.get('campaign') == '' and not s.alive(), request)
print('DONE', flush=True)
