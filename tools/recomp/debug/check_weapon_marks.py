#!/usr/bin/env python3
"""Screenshot the weapon markers on the native weapon lists in both image modes.

Loads the stage-one-clear save in Chinese, opens 武器改造's weapon list and 能力查看's
weapon list, first with HD images (the symbol font's markers) and then with original
images (the icons cut from the ROM font). Screenshots and weapon-marks.json go into
the run directory; the rows' markers come from the page state."""
import argparse
import json
import time
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, run_keys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reuse-build', action='store_true')
parser.add_argument('--save', default='build/recomp/save-recovery-check/intermission-cold-1.source.sram')
args = parser.parse_args()
results = []


def keys(*names, pause=.4):
    for name in names:
        run_keys(s.client, [{'press': name}])
        time.sleep(pause)


def status():
    return s.client.call('status')


def wait_for(fn, timeout=30, what='state'):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        st = status()
        if fn(st):
            return st
        time.sleep(.1)
    raise AssertionError(f'{what} did not appear')


def page(key, screen, timeout=30):
    return wait_for(lambda st: st[key].get('visible') and st[key].get('screen') == screen, timeout, f'{key} {screen}')[key]


def leave(key, presses):
    for _ in range(presses + 2):
        st = status()
        if st['intermission_page'].get('visible') and not st[key].get('visible'):
            return
        keys('x', pause=1.2)
    wait_for(lambda st: st['intermission_page'].get('visible') and not st[key].get('visible'), 30, 'intermission menu')


def record(name, state):
    rows = [{'name': r.get('display_name', r.get('name')), 'markers': r.get('markers', [])} for r in state.get('rows', [])]
    results.append({'shot': name, 'image_mode': status()['image_mode'], 'rows': rows})
    s.client.call('screenshot', path=str(s.run / f'{name}.png'))
    (s.run / 'weapon-marks.json').write_text(json.dumps(results, ensure_ascii=False, indent=2) + '\n')
    print(name, [r['markers'] for r in rows], flush=True)


def weapon_lists(mode):
    s.client.call('ui.click', text='intermission:2')
    page('upgrade_page', 'list')
    keys('z', pause=1)
    p = page('upgrade_page', 'weapons')
    time.sleep(1.5)
    record(f'upgrade-weapons-{mode}', p)
    leave('upgrade_page', 3)
    s.client.call('ui.click', text='intermission:3')
    page('ability_page', 'units')
    keys('z', pause=1)
    page('ability_page', 'unit')
    keys('z', pause=1)
    p = page('ability_page', 'weapons')
    time.sleep(1.5)
    record(f'ability-weapons-{mode}', p)
    leave('ability_page', 4)


s = Session.launch(language='zh-Hans', images='hd', save=args.save, reuse_build=args.reuse_build, timeout=1800)
print('RUN', s.run, flush=True)
try:
    s.wait(vi=600, timeout=300)
    for _ in range(8):
        keys('return', pause=.5)
        try:
            s.wait(title_major=3, timeout=4)
            break
        except Exception:
            pass
    time.sleep(2.5)
    keys('right', 'right', 'right', pause=1.5)
    keys('return', pause=3)
    for _ in range(8):
        st = status()
        if st['intermission_page'].get('visible') or (st.get('intro') or {}).get('title_major') != 7:
            break
        keys('z', pause=1.5)
    s.wait(intermission_page=True, timeout=60)
    print('image mode', status()['image_mode'], flush=True)
    for mode in ('hd', 'original'):
        s.client.call('settings', images=mode)
        wait_for(lambda st: st['image_mode']['current'] == (1 if mode == 'hd' else 0), 30, f'{mode} images')
        weapon_lists(mode)
finally:
    print('EXIT', s.quit().get('exit_code'), flush=True)
