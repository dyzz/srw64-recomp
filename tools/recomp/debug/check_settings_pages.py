#!/usr/bin/env python3
"""The settings window's pages, live (docs/native/settings-window.md §7).

On the title menu: open the window with Ctrl/Cmd+comma, screenshot every page in
Chinese, English and Japanese at the smallest window (960 x 720) and a large one,
turn pages with Q/E and the controller's L1/R1, move and press with the keys,
click a rule on another page by id, close and reopen on the saved page, and open
and close with the controller's View button."""
import argparse
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, run_keys

PAGES = ("general", "interface", "rules", "controls", "about")
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reuse-build', action='store_true')
args = parser.parse_args()
s = Session.launch(language='zh-Hans', images='original', reuse_build=args.reuse_build)
print('RUN', s.run, flush=True)
settings_file = s.run / 'presentation-settings.json'
checks = []
command = 'cmd' if sys.platform == 'darwin' else 'control'


def status():
    return s.client.call('status')


def check(name, passed, state=None, timeout=6):
    """passed is a value or a function polled until it holds (the window rebuilds on
    its next frame, and a loaded machine runs the host well below speed); state is
    what to record, or a function giving it."""
    end = time.monotonic() + timeout
    while callable(passed) and not passed() and time.monotonic() < end:
        time.sleep(.2)
    result = passed() if callable(passed) else passed
    state = state() if callable(state) else state
    checks.append({'check': name, 'passed': bool(result), 'state': state})
    (s.run/'settings-pages-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2)+'\n')
    if not result:
        (s.run/'settings-pages-failure.json').write_text(json.dumps({'status': status(), 'tree': s.client.call('ui.tree')}, ensure_ascii=False, indent=1)+'\n')
    assert result, (name, state)
    print(name, 'PASS', flush=True)


def shot(name):
    s.client.call('screenshot', path=str(s.run/name))


def nodes(row=None):
    """Every element of the shared UI, depth first."""
    if row is None:
        row = s.client.call('ui.tree')['windows'][0]['views']
    yield row
    for child in row.get('children', []):
        yield from nodes(child)


def element(id):
    return next((row for row in nodes() if row.get('id') == id), None)


def shown_page():
    return next((id[14:] for id in (f'settings-page:{p}' for p in PAGES) if (element(id) or {}).get('state')), None)


def focused():
    return next((row.get('id') for row in nodes() if row.get('focused')), None)


def saved(field):
    return json.loads(settings_file.read_text()).get(field) if settings_file.exists() else None


def key(name, *modifiers, pause=.4):
    s.client.call('ui.key', key=name, modifiers=list(modifiers))
    time.sleep(pause)


def pad(name, pause=.5):
    s.client.call('pad', press=name, hold_ms=80)
    time.sleep(pause)


def settings_open():
    return status()['ui']['input_owners']['settings']


def wait_for(predicate, what, timeout=20):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if predicate():
            return
        time.sleep(.2)
    raise AssertionError(f'timed out: {what}')


s.wait(vi=600, timeout=300)   # a loaded machine runs the host well below speed
for _ in range(20):
    run_keys(s.client, [{'press': 'return'}])
    try:
        s.wait(title_major=3, timeout=6)
        break
    except Exception:
        time.sleep(.5)
time.sleep(2)
s.client.call('window', width=960, height=720)
key(',', command)
wait_for(settings_open, 'the window opens')
check('opens-on-general', lambda: shown_page() == 'general' and focused() == 'locale:zh-Hans', lambda: {'page': shown_page(), 'focus': focused()})

# Every page in every language at the smallest window.
for locale in ('zh-Hans', 'en', 'ja'):
    s.client.call('settings', locale=locale)
    wait_for(lambda: status().get('locale') == locale and not status()['ui']['input_owners']['locale'], f'locale {locale}', 60)
    time.sleep(1)
    for page in PAGES:
        s.client.call('ui.click', id=f'settings-page:{page}')
        time.sleep(.6)
        # The page is saved when it changes; the window opened on general with nothing saved.
        check(f'{locale}-{page}-shown', lambda: shown_page() == page and saved('settings_page') in (page, '' if page == 'general' else page),
              lambda: {'page': shown_page(), 'saved': saved('settings_page')})
        shot(f'settings-{locale}-{page}-960.png')
        (s.run/f'settings-{locale}-{page}-tree.json').write_text(json.dumps(s.client.call('ui.tree'), ensure_ascii=False)+'\n')

# Keys: Q/E turn the page and focus its first setting; arrows move, Enter presses.
s.client.call('settings', locale='zh-Hans')
wait_for(lambda: status().get('locale') == 'zh-Hans' and not status()['ui']['input_owners']['locale'], 'locale zh-Hans', 60)
s.client.call('ui.click', id='settings-page:general')
time.sleep(.5)
key('down')
check('down-leaves-the-tabs', lambda: focused() == 'locale:zh-Hans', lambda: {'focus': focused()})
key('e')
check('e-turns-to-interface', lambda: shown_page() == 'interface' and focused() == 'battle-ui:native', lambda: {'page': shown_page(), 'focus': focused()})
key('right')
check('right-moves-within-row', lambda: focused() == 'battle-ui:original', lambda: {'focus': focused()})
key('return', pause=.8)
check('enter-presses', lambda: saved('battle_ui') == 'original' and focused() == 'battle-ui:original', lambda: {'battle_ui': saved('battle_ui'), 'focus': focused()})
shot('settings-keys-interface-960.png')
key('left')
key('return', pause=.8)
check('restored', lambda: saved('battle_ui') == 'native', lambda: {'battle_ui': saved('battle_ui')})
key('down')
check('down-moves-rows', lambda: focused() == 'intermission-ui:native', lambda: {'focus': focused()})
key('q')
check('q-turns-back', lambda: shown_page() == 'general', lambda: {'page': shown_page(), 'focus': focused()})

# The rules page scrolls to keep the focused row in view.
s.client.call('ui.click', id='settings-page:rules')
time.sleep(.5)
key('down')   # the presets row, then the rules
for _ in range(14):
    key('down', pause=.2)
last = focused()
check('down-reaches-last-rule', lambda: last == 'rule:parts-carry-over', lambda: {'focus': last})
frame = element(last)['frame']
body = element('set-body')['frame']
check('last-rule-in-view', lambda: body[1] <= frame[1] and frame[1] + frame[3] <= body[1] + body[3] + 1, lambda: {'rule': frame, 'body': body})
shot('settings-rules-scrolled-960.png')

# A debug click by id on another page turns to it first.
s.client.call('ui.click', id='settings-page:about')
time.sleep(.5)
s.client.call('ui.click', id='rule:boss-dummy-half')
time.sleep(.6)
check('click-turns-to-rules', lambda: shown_page() == 'rules' and 'boss-dummy-half' in status()['rules'], lambda: {'page': shown_page(), 'rules': status()['rules']})
s.client.call('ui.click', id='rule:boss-dummy-half')
time.sleep(.6)
check('rule-off-again', lambda: 'boss-dummy-half' not in status()['rules'], lambda: status()['rules'])

# Close and reopen: back on the page it was left on.
key('escape', pause=1)
wait_for(lambda: not settings_open(), 'the window closes')
key(',', command)
wait_for(settings_open, 'the window reopens')
check('reopens-on-saved-page', lambda: shown_page() == 'rules' and saved('settings_page') == 'rules', lambda: {'page': shown_page(), 'saved': saved('settings_page')})

# The controller: hints follow it, L1/R1 turn, B closes, View opens.
pad('r1')
check('r1-turns', lambda: shown_page() == 'controls', lambda: {'page': shown_page()})
check('pad-hints', lambda: any('L1 / R1 切换分类' in row.get('text', '') for row in nodes()))
shot('settings-pad-controls-960.png')
pad('l1')
pad('l1')
# Controls has nothing to choose, so the focus sat on the tabs and stays there.
check('l1-turns-back', lambda: shown_page() == 'interface' and focused() == 'settings-page:interface', lambda: {'page': shown_page(), 'focus': focused()})
pad('down')
pad('down')
pad('right')
check('pad-moves', lambda: focused() == 'intermission-ui:original', lambda: {'focus': focused()})
pad('b', pause=1)
wait_for(lambda: not settings_open(), 'B closes')
pad('view', pause=1)
wait_for(settings_open, 'View opens')
check('view-reopens', lambda: shown_page() == 'interface', lambda: {'page': shown_page()})

# A large window.
s.client.call('window', width=1600, height=1000)
time.sleep(1)
for page in ('general', 'rules', 'controls'):
    s.client.call('ui.click', id=f'settings-page:{page}')
    time.sleep(.6)
    shot(f'settings-zh-Hans-{page}-1600.png')
pad('view', pause=1)
wait_for(lambda: not settings_open(), 'View closes')
print('EXIT', s.quit().get('exit_code'), flush=True)
