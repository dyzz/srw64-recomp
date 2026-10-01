#!/usr/bin/env python3
"""Play the sample custom campaign through (docs/design/custom-campaign.md, stage C1).

config/recomp/campaigns/sample: prologue (scene 1) -> crossroads (scene 2) -> a choice
-> ending-a (scene 3) or ending-b (scene 4), each stage winning on turn 1 by itself.
Checks that each registration takes the stage of its scene and map, that the
intermission and the save list show the campaign's titles and 第1話 after the first
clear, that リンク backs out, that the first option leads to ending-a and the ending
returns to the title, and that loading the save made after the prologue replays
crossroads, whose second option leads to ending-b. Evidence goes to the run directory."""
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, run_keys

ROOT = Path(__file__).resolve().parents[3]
s = Session.launch(language='zh-Hans', images='original', campaign=str(ROOT / 'config/recomp/campaigns/sample/campaign.json'))
print('RUN', s.run, flush=True)
checks = []


def status():
    return s.client.call('status')


def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (s.run / 'campaign-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2) + '\n')
    assert passed, f'{name}: {json.dumps(state, ensure_ascii=False)[:600]}'
    print(name, 'PASS', flush=True)


def shot(name):
    s.client.call('screenshot', path=str(s.run / name))


def keys(*names, pause=.4):
    for name in names:
        run_keys(s.client, [{'press': name}])
        time.sleep(pause)


def applied():
    rows = [json.loads(l) for l in (s.run / 'mini-stage-events.jsonl').read_text().splitlines()]
    return [(r['scene'], r['stage']) for r in rows if r['action'] == 'applied']


def maps():
    rows = [json.loads(l) for l in (s.run / 'mini-stage-events.jsonl').read_text().splitlines()]
    return [(r['scene'], r['map']) for r in rows if r['action'] == 'map']


def play_to_intermission(second=False, timeout=600):
    """A through dialogue and the clear screen; Down first picks a choice's second option."""
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        page = status()['intermission_page']
        if page.get('visible'):
            return page
        if second:
            keys('down', pause=.3)
        keys('z', pause=1.0)
    raise AssertionError('the intermission did not open')


def play_to_title(timeout=900):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if (status().get('intro') or {}).get('title_major') == 3:
            return
        keys('z', pause=2.0)
    raise AssertionError('the ending did not return to the title')


def next_stage():
    s.client.call('ui.click', text='intermission:8')
    time.sleep(2)


def ring_cursor():
    """The title ring's item (D_801CC3AC): 0 スタート, 1 オプション, 2 コンティニュー, 3 ロード."""
    return int(s.client.call('memory.read', address=0x801CC3AC, size=1)['hex'], 16)


def wait_for(predicate, timeout=30):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        st = status()
        if predicate(st):
            return st
        time.sleep(.2)
    raise AssertionError('timed out')


s.wait(vi=120)   # the debug socket opens before the host has read the campaign
campaign = status()['mini_stage'].get('campaign') or {}
check('campaign-loaded', campaign.get('id') == 'srw64.sample' and campaign.get('start') == 1 and len(campaign.get('stages', [])) == 4, campaign)
# Enter as Session.enter_mini_stage does, but only until the first stage registers: its
# opening waits for A on dialogue, so the map does not go idle by itself.
s.wait(vi=600)
for _ in range(20):
    if (status().get('intro') or {}).get('title_major') == 3:
        break
    keys('return', pause=.5)
s.client.call('ui.click', id='mini-enter')
wait_for(lambda st: st['mini_stage'].get('applied'), timeout=120)
check('prologue-on-scene-1', applied() == [(1, 'sample-prologue')], applied())

page = play_to_intermission()
shot('intermission-prologue.png')
check('prologue-cleared', (page['scene'], page['scene_title'], page['episode']) == (1, '序章　启程', 1), page)

# Save into cartridge slot 1: データセーブ, ROM cartridge, the first slot.
s.client.call('ui.click', text='intermission:0')
wait_for(lambda st: st['save_page'].get('visible') and st['save_page'].get('screen') == 'choice')
keys('z')       # ROM cartridge
wait_for(lambda st: st['save_page'].get('screen') == 'slots')
time.sleep(1)
keys('z')       # the empty slot 1 saves at once
slot = wait_for(lambda st: st['save_page']['slots'][0].get('used'))['save_page']['slots'][0]
check('save-list-title', slot.get('used') and slot.get('title') == '序章　启程' and slot.get('episode') == 1,
      {k: v for k, v in slot.items() if k != 'art'})
shot('save-list.png')
keys('x', pause=2)
keys('x', pause=3)

# リンク would move the next scene out of the campaign: it backs straight out.
s.client.call('ui.click', text='intermission:7')
time.sleep(4)
st = status()
link_rows = [json.loads(l) for l in (s.run / 'link-events.jsonl').read_text().splitlines()] if (s.run / 'link-events.jsonl').exists() else []
check('link-blocked', st['intermission_page'].get('visible') and not st['link_page'].get('visible') and
      any(r['kind'] == 'blocked' for r in link_rows), {'link': link_rows[-2:], 'notices': st.get('notices', [])[-2:]})

next_stage()
page = play_to_intermission()
check('crossroads-on-scene-2', applied()[-1] == (2, 'sample-crossroads') and maps()[-1] == (2, 19) and
      (page['scene'], page['scene_title'], page['episode']) == (2, '第2话　岔路', 2), {'applied': applied(), 'page': page})
next_stage()
check('first-option-ending-a', applied()[-1] == (3, 'sample-ending-a'), applied())
play_to_title()
check('ending-a-returns-to-title', maps()[-1] == (3, 0), maps())

# Load the save made after the prologue: crossroads again, then the second option.
time.sleep(3)   # the ring is still turning in when title_major first reads 3
for _ in range(8):
    if ring_cursor() == 3:
        break
    keys('right', pause=1.5)
shot('title-ring-load.png')
keys('return')
wait_for(lambda st: st['save_page'].get('visible') and st['save_page'].get('screen') == 'choice')
keys('z')       # ROM cartridge
wait_for(lambda st: st['save_page'].get('screen') == 'slots')
time.sleep(1)
keys('z', pause=1.5)   # slot 1, then はい
keys('z')
page = wait_for(lambda st: st['intermission_page'].get('visible'), timeout=60)['intermission_page']
check('save-loads-into-campaign', page.get('visible') and (page['scene'], page['scene_title']) == (1, '序章　启程'), page)
next_stage()
play_to_intermission(second=True)
check('crossroads-after-load', applied()[-1] == (2, 'sample-crossroads'), applied())
next_stage()
check('second-option-ending-b', applied()[-1] == (4, 'sample-ending-b'), applied())
play_to_title()
check('ending-b-returns-to-title', maps()[-1] == (4, 21), maps())
print('EXIT', s.quit().get('exit_code'), flush=True)
