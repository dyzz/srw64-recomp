#!/usr/bin/env python3
"""Verify the native title-menu pages (docs/native/native-title-menus.md) on a first-episode save.

Starts from build/recomp/save-recovery-check/intermission-cold-1.source.sram and walks
the ring: オプション (the サウンド toggle, which writes the SRAM header, and back),
サウンドセレクト (play, the previous song, stop, back), カラオケモード (start a song, leave
the カラオケ battle, the list again on that song), the original screens through the
title_ui switch, then ロード (medium choice, the pause, the slots, the load window and
the load into the インターミッション menu). Screenshots and title-checks.json go to the run
directory; the run's SRAM copy is the only save file written."""
import argparse
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, run_keys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--language', default='zh-Hans', choices=('ja', 'zh-Hans', 'en'))
parser.add_argument('--skip-karaoke', action='store_true')
args = parser.parse_args()
s = Session.launch(language=args.language, images='hd', diagnostics='light',
                   save='build/recomp/save-recovery-check/intermission-cold-1.source.sram')
print('RUN', s.run, flush=True)
checks = []


def status():
    return s.client.call('status')


def title():
    return status()['title_page']


def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (s.run / 'title-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2) + '\n')
    print(name, 'PASS' if passed else 'FAIL', flush=True)
    assert passed, (name, state)


def shot(name):
    s.client.call('screenshot', path=str(s.run / f'{name}.png'))


def wait(predicate, what, timeout=60):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        state = status()
        if predicate(state):
            return state
        time.sleep(.1)
    raise AssertionError(f'{what}: {json.dumps({k: status().get(k) for k in ("vi", "intro", "title_page", "save_page")}, ensure_ascii=False)[:600]}')


def ring_idle(state):
    step = (state.get('intro') or {}).get('step') or {}
    return step.get('major') == 3 and step.get('substate') == 2


def key(name, pause=.35):
    run_keys(s.client, [{'press': name, 'hold_ms': 90}])
    time.sleep(pause)


def ui(name, pause=.35):
    s.client.call('ui.key', key=name)
    time.sleep(pause)


def click(control, pause=.35):
    s.client.call('ui.click', id=control)
    time.sleep(pause)


def page(screen, timeout=60, **fields):
    return wait(lambda st: st['title_page'].get('visible') and st['title_page'].get('screen') == screen
                and all(st['title_page'].get(k) == v for k, v in fields.items()), f'title page {screen} {fields}', timeout)['title_page']


def rotate(steps):
    """Turn the ring: right moves the cursor +1 (スタート 0, オプション 1, コンティニュー 2, ロード 3)."""
    for _ in range(steps):
        wait(ring_idle, 'ring idle')
        time.sleep(.3)
        key('right', pause=.2)
    wait(ring_idle, 'ring idle')
    time.sleep(.3)


# Title ring.
s.wait(vi=600, timeout=240)
for _ in range(30):
    if ring_idle(status()):
        break
    key('return', pause=.6)
wait(ring_idle, 'ring idle', 90)
time.sleep(1.5)

# オプション
rotate(1)
key('return')
p = page('options')
shot('options.png'.removesuffix('.png'))
check('options', len(p['items']) == 3 and p['cursor'] == 0, p)
mono = p['mono']
click('tp-option:0')
p = page('options', mono=not mono)
check('sound-toggle', p['mono'] != mono and p['items'][0]['value'], p)
click('tp-option:0')
p = page('options', mono=mono)
check('sound-back', p['mono'] == mono, p)

# サウンドセレクト
click('tp-option:1')
click('tp-option:1')
p = page('sound')
shot('sound')
check('sound-list', len(p['songs']) >= 40 and p['current'] == 0 and not any(x['playing'] for x in p['songs']), {k: p[k] for k in ('current', 'top', 'title', 'exit')})
for _ in range(3):
    ui('down')
p = page('sound', current=3)
ui('return', pause=1.5)
p = page('sound', current=3)
check('play', p['songs'][3]['playing'], p['songs'][3])
ui('q', pause=1.5)
p = page('sound', current=2)
check('previous-song', p['songs'][2]['playing'], p['songs'][2])
shot('sound-playing')
for _ in range(12):
    ui('down', pause=.15)
p = page('sound')
check('scroll', p['top'] > 0 and p['current'] - p['top'] < p['rows'], {k: p[k] for k in ('current', 'top', 'rows')})
ui('escape', pause=.8)
p = page('sound')
check('stop', not any(x['playing'] for x in p['songs']), {k: p[k] for k in ('current', 'top')})
ui('escape')
p = page('options')
check('back-to-options', p['cursor'] == 1, p)

# カラオケモード
if not args.skip_karaoke:
    click('tp-option:2')
    click('tp-option:2')
    p = page('karaoke')
    shot('karaoke')
    check('karaoke-list', len(p['songs']) >= 15, {k: p[k] for k in ('current', 'title')})
    ui('down')
    page('karaoke', current=1)
    ui('return')
    wait(lambda st: not st['title_page'].get('visible'), 'karaoke battle', 30)
    time.sleep(12)
    shot('karaoke-battle')
    key('x', pause=1)
    p = page('karaoke', timeout=90, current=1)
    shot('karaoke-back')
    check('karaoke-return', p['current'] == 1, {k: p[k] for k in ('current', 'top')})
    ui('escape')
    page('options')

# The original screens through the switch.
ui('escape')
wait(ring_idle, 'back at the ring', 60)
s.client.call('settings', title_ui='original')
time.sleep(.5)
key('return')
wait(lambda st: (st['intro'].get('step') or {}).get('major') == 5, 'original options', 30)
time.sleep(2)
shot('options-original')
check('original-options', not title().get('visible'), title())
key('x')
wait(ring_idle, 'back at the ring', 60)
s.client.call('settings', title_ui='native')
time.sleep(1)

# ロード
rotate(2)
key('return')
sp = wait(lambda st: st['save_page'].get('visible') and st['save_page'].get('screen') == 'choice', 'load medium choice')['save_page']
shot('load-choice')
check('load-choice', sp.get('context') == 'title' and sp['cursor'] == 0 and sp['labels']['save_to'], {k: sp.get(k) for k in ('context', 'cursor', 'labels')})
ui('return')
sp = wait(lambda st: st['save_page'].get('visible') and st['save_page'].get('screen') == 'slots', 'load slots')['save_page']
shot('load-slots')
check('load-slots', sp['slots'][0]['used'] and not sp['slots'][1]['used'] and sp['mode'] == 0,
      {'slots': sp['slots'], 'mode': sp['mode']})
ui('down')
ui('return')
sp = status()['save_page']
check('empty-slot-refused', sp['mode'] == 0 and sp['cursor'] == 1, {k: sp[k] for k in ('mode', 'cursor')})
ui('up')
ui('return')
sp = wait(lambda st: st['save_page'].get('mode') == 1, 'load window')['save_page']
shot('load-window')
check('load-window', sp['labels']['overwrite'], sp['labels'])
ui('return')
im = wait(lambda st: st['intermission_page'].get('visible'), 'intermission after the load', 90)['intermission_page']
shot('loaded')
check('loaded', im.get('turns') == 7 and im.get('funds') == 14500, {k: im.get(k) for k in ('turns', 'funds', 'episode')})
check('pages-closed', not status()['save_page'].get('context') == 'title' or not status()['save_page'].get('visible'), status()['save_page'].get('screen'))
print(json.dumps({'passed': sum(c['passed'] for c in checks), 'checks': len(checks)}), flush=True)
s.quit()
