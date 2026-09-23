#!/usr/bin/env python3
"""Verify the native インターミッション menu in the current native build.

Loads a stage-one-clear save through the title ring, then checks the page's data,
cursor wrap, the のりかえ window and its refusal, a sub-screen and back, the リンク
hand-off, three languages and 次のマップへ. Evidence goes to the run directory."""
import argparse
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, run_keys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reuse-build', action='store_true')
parser.add_argument('--save', default='build/recomp/save-recovery-check/intermission-cold-1.source.sram')
args = parser.parse_args()
s = Session.launch(language='ja', images='original', save=args.save, reuse_build=args.reuse_build)
print('RUN', s.run, flush=True)
checks = []
def page():
    return s.client.call('status')['intermission_page']
def wait_page(visible=True, timeout=30):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        p = page()
        if bool(p.get('visible')) == visible:
            return p
        time.sleep(.1)
    raise AssertionError(f'intermission page visible != {visible}')
def keys(*names, pause=.4):
    for name in names:
        run_keys(s.client, [{'press': name}])
        time.sleep(pause)
def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (s.run/'intermission-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2)+'\n')
    assert passed, name
    print(name, 'PASS', flush=True)
def shot(name):
    s.client.call('screenshot', path=str(s.run/name))

# Title ring: ロード is three to the right of スタート; ROM cartridge, slot 1, yes.
s.wait(vi=600)
for _ in range(8):
    keys('return', pause=.5)
    try:
        s.wait(title_major=3, timeout=4)
        break
    except Exception:
        pass
time.sleep(2.5)   # the ring is still turning in when title_major first reads 3
keys('right', 'right', 'right', pause=1.5)
keys('return', pause=3)
# ROM cartridge, slot 1, yes: a press during a fade is dropped, so press while the
# load screen (title_major 7) is still up, never once the menu has opened.
for _ in range(8):
    status = s.client.call('status')
    if status['intermission_page'].get('visible') or (status.get('intro') or {}).get('title_major') != 7:
        break
    keys('z', pause=1.5)
p = wait_page()
check('data', (p['turns'], p['funds'], p['episode'], p['scene'], len(p['items']), p['restricted']) == (7, 14500, 1, 1, 9, False), p)
shot('intermission-ja.png')
keys('up')
check('wraps-to-last', page()['cursor'] == 8, page())
keys('down')
check('wraps-to-first', page()['cursor'] == 0, page())
keys(*['down']*5)
keys('z')
check('swap-window', page()['submenu'], page())
keys('down', 'z', pause=.8)
p = page()
check('fairy-refused', p['visible'] and p['submenu'] and p['swap_refused'], p)
shot('intermission-swap-refused.png')
keys('x')
check('swap-closed', not page()['submenu'], page())
first = page()['serial']
s.client.call('ui.click', text='intermission:1')
wait_page(False)
time.sleep(3)
keys('x')
p = wait_page()
check('sub-screen-and-back', p['serial'] > first and p['cursor'] == 1, p)
s.client.call('ui.click', text='intermission:7')
s.wait(link_page=True, timeout=20)
check('link-hand-off', not page()['visible'], page())
s.client.call('ui.key', key='escape')
p = wait_page()
check('link-back', p['cursor'] == 7, p)
for locale, first_item in (('zh-Hans', '保存数据'), ('en', 'Save Data')):
    s.client.call('settings', locale=locale)
    time.sleep(2.5)
    p = page()
    check(f'language-{locale}', p['items'][0] == first_item, p)
    shot(f'intermission-{locale}.png')
s.client.call('ui.click', text='intermission:8')
wait_page(False)
s.wait(dialogue_active=True, timeout=60)
rows, _ = s.events('intermission', 0, ['choose'])
check('next-stage', rows[-1]['exit'] == 2 and not s.client.call('status')['ui']['input_owners']['intermission'], rows[-1])
print('EXIT', s.quit().get('exit_code'), flush=True)
