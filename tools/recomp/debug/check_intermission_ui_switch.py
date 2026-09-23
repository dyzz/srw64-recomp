#!/usr/bin/env python3
"""Switch the インターミッション screens between the native pages and the original
screens at run time; the choice persists in the presentation settings.

Loads a stage-one-clear save to the native main menu, switches to the original
screens (ユニット改造 then opens as the original list and the way back shows the
original menu), switches back (the next ユニット改造 is the native list and the way
back the native menu)."""
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
settings_file = s.run / 'presentation-settings.json'   # run_host_probe's default location
checks = []
def status():
    return s.client.call('status')
def keys(*names, pause=.4):
    for name in names:
        run_keys(s.client, [{'press': name}])
        time.sleep(pause)
def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (s.run/'intermission-ui-switch-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2)+'\n')
    assert passed, name
    print(name, 'PASS', flush=True)
def shot(name):
    s.client.call('screenshot', path=str(s.run/name))
def pages():
    st = status()
    return {'menu': bool(st['intermission_page'].get('visible')), 'upgrade': bool(st['upgrade_page'].get('visible')),
            'screen': st['upgrade_page'].get('screen'), 'vi': st.get('vi')}
def wait_for(predicate, timeout=30):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        p = pages()
        if predicate(p):
            return p
        time.sleep(.1)
    raise AssertionError(f'timed out: {pages()}')
def saved_ui():
    return json.loads(settings_file.read_text()).get('intermission_ui') if settings_file.exists() else None

# Title ring to the load screen, as check_intermission.py does.
s.wait(vi=600)
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
s.wait(intermission_page=True, timeout=30)
check('native-menu', pages()['menu'] and saved_ui() in (None, 'native'), pages())

# Original from the next screen on: the open native menu stays until it rebuilds.
done = s.client.call('settings', intermission_ui='original')
check('settings-original', done.get('intermission_ui') == 'original' and saved_ui() == 'original', done)
s.client.call('ui.click', text='intermission:1')
time.sleep(4)
p = pages()
check('original-upgrade-list', not p['upgrade'] and not p['menu'], p)
shot('ui-switch-original-list.png')
keys('x', pause=3)
p = pages()
check('original-menu', not p['menu'] and not p['upgrade'], p)
shot('ui-switch-original-menu.png')
# リンク (item 7) on the original menu opens the original link screen, not the native page.
keys('down', 'down', 'down', 'down', 'down', 'down', pause=.3)
keys('z', pause=4)
st = status()
check('original-link', not st['link_page'].get('visible') and not st['intermission_page'].get('visible'), {'link': st['link_page'], 'vi': st.get('vi')})
shot('ui-switch-original-link.png')
keys('x', pause=3)
keys('up', 'up', 'up', 'up', 'up', 'up', pause=.3)

# Back to the native pages: the original menu's cursor is still on ユニット改造.
done = s.client.call('settings', intermission_ui='native')
check('settings-native', done.get('intermission_ui') == 'native' and saved_ui() == 'native', done)
keys('z')
p = wait_for(lambda p: p['upgrade'] and p['screen'] == 'list')
check('native-upgrade-list', True, p)
shot('ui-switch-native-list.png')
keys('x')
p = wait_for(lambda p: p['menu'] and not p['upgrade'])
check('native-menu-again', status()['intermission_page']['cursor'] == 1, status()['intermission_page'])
shot('ui-switch-native-menu.png')
print('EXIT', s.quit().get('exit_code'), flush=True)
