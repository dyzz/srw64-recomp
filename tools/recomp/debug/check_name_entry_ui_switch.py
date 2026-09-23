#!/usr/bin/env python3
"""Switch the protagonist selection / name entry pages to the original screens.

From the title menu: set name_entry_ui to original through the debug interface (the
choice persists in the presentation settings), start a new game, press through the
two prologues and check that no native name page opens while the selection overlay
is up; a screenshot shows the original page. Then switch back and check the
persisted value."""
import argparse
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, run_keys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reuse-build', action='store_true')
args = parser.parse_args()
s = Session.launch(language='ja', images='original', reuse_build=args.reuse_build)
print('RUN', s.run, flush=True)
settings_file = s.run / 'presentation-settings.json'
checks = []
def status():
    return s.client.call('status')
def keys(*names, pause=.4):
    for name in names:
        run_keys(s.client, [{'press': name}])
        time.sleep(pause)
def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (s.run/'name-entry-ui-switch-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2)+'\n')
    assert passed, name
    print(name, 'PASS', flush=True)
def shot(name):
    s.client.call('screenshot', path=str(s.run/name))
def saved_ui():
    return json.loads(settings_file.read_text()).get('name_entry_ui') if settings_file.exists() else None

s.wait(vi=600, timeout=300)   # a loaded machine runs the host well below speed
for _ in range(20):
    keys('return', pause=.5)
    try:
        s.wait(title_major=3, timeout=6)
        break
    except Exception:
        pass
time.sleep(2)
done = s.client.call('settings', name_entry_ui='original')
check('settings-original', done.get('name_entry_ui') == 'original' and saved_ui() == 'original', done)
# New game is the first title item; the prologues are intro screens and dialogue,
# both advanced with Z until nothing is active for three seconds.
keys('return', pause=3)
end = time.monotonic() + 600
quiet_since = None
advanced = 0
while time.monotonic() < end:
    st = status()
    if st['name_page'].get('visible'):
        raise AssertionError('the native name page opened although the setting is original')
    busy = (st.get('intro') or {}).get('loaded') or (st.get('dialogue') or {}).get('active')
    if busy:
        quiet_since = None
        advanced += 1
        keys('z', pause=.3)
        continue
    quiet_since = quiet_since or time.monotonic()
    if time.monotonic() - quiet_since >= 6:
        break
    time.sleep(.3)
st = status()
check('original-selection', quiet_since is not None and not st['name_page'].get('visible') and not (st.get('dialogue') or {}).get('active'),
      {'advanced': advanced, 'name_page': st['name_page'], 'intro': st.get('intro'), 'vi': st.get('vi')})
shot('name-entry-original.png')
# The original page answers to the game keys: right moves the route, A opens はい／いいえ.
keys('right', pause=1)
shot('name-entry-original-right.png')
done = s.client.call('settings', name_entry_ui='native')
check('settings-native', done.get('name_entry_ui') == 'native' and saved_ui() == 'native', done)
print('EXIT', s.quit().get('exit_code'), flush=True)
