#!/usr/bin/env python3
"""The title screen's MOD manager and a campaign's own saves (docs/design/custom-campaign.md §8).

One run with the campaigns under config/recomp/campaigns installed beside it and a save
library for the main game:
1. The title shows MOD; the manager's four pages show the campaigns (name, description,
   stage count), the Original/HD switch, the dialogue folder and reload, and the music
   note, in Chinese and English.
2. Entering the sample swaps the game's saves for the sample's without leaving the game:
   the title offers 开始战役; the prologue is cleared and saved to slot 1, then ending-a
   is played back to the title.
3. Back in the main game the load list does not show that save, and the main library's
   card has no record from it; entering the sample again, slot 1 holds it and loads into
   the campaign's intermission.
Evidence goes to the run directory."""
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, run_keys

s = Session.launch(language='zh-Hans', images='original', campaigns=True)
print('RUN', s.run, flush=True)
checks = []
main_library = s.run.parent / (s.run.name + '.saves')
sample_library = s.run.parent / (s.run.name + '.campaign-saves') / 'srw64.sample' / 'saves'


def status():
    return s.client.call('status')


def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (s.run / 'mod-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2) + '\n')
    assert passed, f'{name}: {json.dumps(state, ensure_ascii=False)[:800]}'
    print(name, 'PASS', flush=True)


def keys(*names, pause=.4):
    for name in names:
        run_keys(s.client, [{'press': name}])
        time.sleep(pause)


def wait_for(predicate, timeout=30):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        st = status()
        if predicate(st):
            return st
        time.sleep(.2)
    raise AssertionError('timed out')


def ids():
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


def text():
    return json.dumps(s.client.call('ui.tree'), ensure_ascii=False)


def page(name, shot):
    s.client.call('ui.click', id=f'mod-page:{name}')
    time.sleep(1.2)
    s.client.call('screenshot', path=str(s.run / shot))
    return text()


def ring_cursor():
    """The title ring's item (D_801CC3AC): 0 スタート, 1 オプション, 2 コンティニュー, 3 ロード."""
    return int(s.client.call('memory.read', address=0x801CC3AC, size=1)['hex'], 16)


def to_ring():
    for _ in range(30):
        if (status().get('intro') or {}).get('title_major') == 3:
            time.sleep(3)   # the ring is still turning in when title_major first reads 3
            return
        keys('return', pause=.5)
    raise AssertionError('title ring not reached')


def load_list():
    """The title's ロード, ROM cartridge: the slot page's rows."""
    for _ in range(8):
        if ring_cursor() == 3:
            break
        keys('right', pause=1.5)
    keys('return')
    wait_for(lambda st: st['save_page'].get('visible') and st['save_page'].get('screen') == 'choice')
    keys('z')
    page_state = wait_for(lambda st: st['save_page'].get('screen') == 'slots')['save_page']
    time.sleep(1)
    return [{k: v for k, v in slot.items() if k != 'art'} for slot in page_state['slots']]


def mini_events(action):
    return [json.loads(l) for l in (s.run / 'mini-stage-events.jsonl').read_text().splitlines() if json.loads(l)['action'] == action]


def play_to(predicate, second=False, timeout=900, pause=1.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        st = status()
        if predicate(st):
            return st
        if second:
            keys('down', pause=.3)
        keys('z', pause=pause)
    raise AssertionError('timed out playing')


# 1. The manager's pages.
s.wait(vi=600)
to_ring()
s.client.call('screenshot', path=str(s.run / 'title.png'))
check('title-shows-mod', 'mod-open' in ids(), list(ids()))
s.client.call('ui.click', id='mod-open')
time.sleep(1.5)
s.client.call('screenshot', path=str(s.run / 'mod-campaigns-zh.png'))
installed = sorted(p.name for p in (s.run.parent / (s.run.name + '.campaigns')).iterdir())
sample = installed.index('srw64.sample')
found = ids()
check('campaign-page', all(found.get(f'dlc-enter:{i}') for i in range(len(installed))) and
      all(f'mod-page:{p}' in found for p in ('campaigns', 'art', 'dialogue', 'audio')) and '示例战役' in text() and '共 4 关' in text(),
      {'ids': [k for k in found if k.startswith(('dlc', 'mod'))]})
art = page('art', 'mod-art-zh.png')
check('art-page', 'images:hd' in ids() and ('HD 包已安装' in art or '没有找到 HD 包' in art), {})
dialogue = page('dialogue', 'mod-dialogue-zh.png')
check('dialogue-page', 'mod-dialogue-reload' in ids() and '来自' in dialogue, {})
check('audio-page', '还在开发中' in page('audio', 'mod-audio-zh.png'), {})
s.client.call('settings', locale='en')
time.sleep(2)
check('english', 'Sample Campaign' in page('campaigns', 'mod-campaigns-en.png') and 'Extra Scenarios' in text(), {})
s.client.call('settings', locale='zh-Hans')
time.sleep(1.5)

# 2. Into the sample, without leaving the game.
s.client.call('ui.click', id=f'dlc-enter:{sample}')
time.sleep(2)
st = status()
s.client.call('screenshot', path=str(s.run / 'title-in-sample.png'))
check('entered-sample', s.alive() and (st['mini_stage'].get('campaign') or {}).get('id') == 'srw64.sample' and
      not st['ui']['input_owners'].get('settings') and 'mini-enter' in ids() and '开始战役' in text(),
      {'campaign': st['mini_stage'].get('campaign'), 'notices': st.get('notices', [])[-2:]})
s.client.call('ui.click', id='mini-enter')
wait_for(lambda st: st['mini_stage'].get('applied'), timeout=120)
play_to(lambda st: st['intermission_page'].get('visible'))
s.client.call('ui.click', id='intermission:0')
wait_for(lambda st: st['save_page'].get('visible') and st['save_page'].get('screen') == 'choice')
keys('z')
wait_for(lambda st: st['save_page'].get('screen') == 'slots')
time.sleep(1)
keys('z')   # the empty slot 1 saves at once
slot = wait_for(lambda st: st['save_page']['slots'][0].get('used'))['save_page']['slots'][0]
check('saved-in-sample', slot.get('title') == '序章　启程' and (sample_library / 'cartridge.sram').is_file(),
      {k: v for k, v in slot.items() if k != 'art'})
keys('x', pause=2)
keys('x', pause=3)
s.client.call('ui.click', id='intermission:8')
time.sleep(2)
# Crossroads (A takes the choice's first option) back to the intermission, then ending-a.
play_to(lambda st: st['intermission_page'].get('visible') and st['intermission_page'].get('scene') == 2)
s.client.call('ui.click', id='intermission:8')
wait_for(lambda st: len(mini_events('applied')) >= 3, timeout=120)
play_to(lambda st: (st.get('intro') or {}).get('title_major') == 3, pause=2.0)

# 3. Back to the main game: its saves do not hold the sample's.
to_ring()
s.client.call('ui.click', id='mod-open')
time.sleep(1.5)
s.client.call('screenshot', path=str(s.run / 'mod-in-sample.png'))
check('playing-shown', ids().get('dlc-leave') and ids().get(f'dlc-enter:{sample}') is False and '游玩中' in text(), list(ids()))
s.client.call('ui.click', id='dlc-leave')
time.sleep(2)
check('left-sample', s.alive() and not status()['mini_stage'].get('campaign') and 'mini-enter' not in ids(), {})
main_slots = load_list()
s.client.call('screenshot', path=str(s.run / 'main-load-list.png'))
check('main-saves-apart', not any(sl.get('title') == '序章　启程' for sl in main_slots), main_slots)
keys('x', pause=2)
keys('x', pause=3)

# The sample again: its save is there and loads.
to_ring()
s.client.call('ui.click', id='mod-open')
time.sleep(1.5)
s.client.call('ui.click', id=f'dlc-enter:{sample}')
time.sleep(2)
to_ring()
sample_slots = load_list()
s.client.call('screenshot', path=str(s.run / 'sample-load-list.png'))
check('sample-save-kept', sample_slots[0].get('used') and sample_slots[0].get('title') == '序章　启程', sample_slots)
keys('z', pause=1.5)
keys('z')
page_state = wait_for(lambda st: st['intermission_page'].get('visible'), timeout=60)['intermission_page']
check('sample-save-loads', (page_state['scene'], page_state['scene_title']) == (1, '序章　启程'), page_state)
print('EXIT', s.quit().get('exit_code'), flush=True)
