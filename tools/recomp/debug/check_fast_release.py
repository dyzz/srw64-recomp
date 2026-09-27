#!/usr/bin/env python3
"""Check letting go of dialogue fast-forward in the native build (docs/native/native-dialogue-ui.md).

Starts a new game in Chinese, takes the default protagonist and names, and on the
first dialogue presses fast-forward the ways a player does: E + Z held and let go,
Z tapped while E stays down, the controller's R2 held and let go, R2 tapped, R2
over automatic reading, and R2 + Menu. From the dialogue log's `fast` and `turn`
rows it checks that
- held, fast-forward keeps turning pages; from the update it lets go no page turns
  until the next Z / A, and that one turns exactly one page;
- a press shorter than Reader::fast_hold_vis (0.3 s) turns exactly one page;
- letting go turns automatic reading off;
- R2 + Menu starts the segment skip, and letting go of R2 leaves it on.

Results go to fast-release-checks.json in the run directory."""
import argparse
import json
import time
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session

FAST_STEP_VIS, FAST_HOLD_VIS = 6, 18   # Reader::fast_step_vis / fast_hold_vis
TAP_MS = 200                           # about 12 VI: over one step, under the hold delay

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reuse-build', action='store_true')
args = parser.parse_args()
s = Session.launch(language='zh-Hans', reuse_build=args.reuse_build, diagnostics='light')
print('RUN', s.run, flush=True)
checks = []


def check(name, passed, detail):
    checks.append({'check': name, 'passed': bool(passed), 'detail': detail})
    (s.run / 'fast-release-checks.json').write_text(json.dumps({'checks': checks}, ensure_ascii=False, indent=2) + '\n')
    print(name, 'PASS' if passed else 'FAIL', flush=True)


def status():
    return s.client.call('status')


def dialogue():
    return status().get('dialogue') or {}


def keys(**step):
    return s.client.call('keys', **step)


def pad(**step):
    return s.client.call('pad', **step)


def cursor():
    return s.events('dialogue', 0, ['-'])[1]


def rows(since, kinds=('fast', 'turn', 'skip_start', 'boundary')):
    return s.events('dialogue', since, list(kinds))[0]


def until_reading(timeout=240):
    """Drive name pages and intro text until a record waits for its next page."""
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if not s.alive():
            raise SystemExit('the host exited')
        st = status()
        if (st.get('name_page') or {}).get('visible'):
            s.client.call('ui.key', key='return')
            time.sleep(1.2)
            continue
        d = st.get('dialogue') or {}
        if d.get('active') and not (d.get('pending') or d.get('history_open') or d.get('skipping')):
            time.sleep(.6)
            return dialogue()
        if (st.get('intro') or {}).get('loaded'):
            keys(press='z', hold_ms=80)
            time.sleep(.3)
            continue
        time.sleep(.2)
    raise SystemExit('no dialogue to read')


def split(log):
    """The turns while the chord was held and after it let go, and the release row."""
    release = next((i for i, r in enumerate(log) if r['kind'] == 'fast' and not r['held']), None)
    turns = [r for r in log if r['kind'] == 'turn']
    if release is None:
        return turns, [], None
    return ([r for r in log[:release] if r['kind'] == 'turn'],
            [r for r in log[release + 1:] if r['kind'] == 'turn'], log[release])


def held_vis(log):
    press = next((r for r in log if r['kind'] == 'fast' and r['held']), None)
    release = next((r for r in log if r['kind'] == 'fast' and not r['held']), None)
    return release['vi'] - press['vi'] if press and release else None


def hold_and_let_go(name, down, up, confirm):
    """Held 0.8 s: pages turn; let go: nothing moves; one confirm: one page."""
    until_reading()
    start = cursor()
    down()
    time.sleep(.8)
    up()
    time.sleep(2.5)
    log = rows(start)
    held, late, release = split(log)
    d = dialogue()
    s.client.call('screenshot', path=str(s.run / f'{name}-released.png'))
    check(f'{name}-stops-on-release', len(held) >= 2 and release and not late and not d.get('fast') and not d.get('automatic'),
          {'held_turns': len(held), 'late_turns': late, 'release': release, 'state': {k: d.get(k) for k in
           ('event', 'page', 'pages', 'pending', 'fast', 'automatic')}})
    until_reading()
    start = cursor()
    confirm()
    time.sleep(1.5)
    turns = rows(start, ('turn',))
    check(f'{name}-one-confirm-one-page', len(turns) == 1 and not turns[0]['fast'], {'turns': turns})


def tap(name, press):
    """A press between one fast step and the hold delay turns exactly one page."""
    until_reading()
    start = cursor()
    press()
    time.sleep(1.5)
    log = rows(start)
    turns = [r for r in log if r['kind'] == 'turn']
    vis = held_vis(log)
    meaningful = vis is not None and FAST_STEP_VIS < vis < FAST_HOLD_VIS
    check(f'{name}-one-page', len(turns) == 1 and meaningful,
          {'turns': turns, 'held_vis': vis, 'meaningful': meaningful})


# Title, then New game; the common prologue is intro screens and dialogue.
s.wait(vi=600, timeout=300)
for _ in range(20):
    keys(press='return', hold_ms=80)
    time.sleep(.5)
    try:
        s.wait(title_major=3, timeout=6)
        break
    except Exception:
        pass
time.sleep(2)
keys(press='return', hold_ms=80)
time.sleep(3)

# Keyboard: E + Z held and let go; Z tapped while E stays down.
hold_and_let_go('keys-e+z', lambda: keys(down='e+z'), lambda: keys(up='e+z'),
                lambda: keys(press='z', hold_ms=80))


def e_down_z_tap():
    keys(down='e')
    time.sleep(.3)
    keys(press='z', hold_ms=TAP_MS)
    time.sleep(.5)
    keys(up='e')


tap('keys-z-tap-with-e-down', e_down_z_tap)

# Controller: R2 held and let go, R2 tapped.
hold_and_let_go('pad-r2', lambda: pad(down='r2'), lambda: pad(up='r2'), lambda: pad(press='a', hold_ms=80))
tap('pad-r2-tap', lambda: pad(press='r2', hold_ms=TAP_MS))

# R2 over automatic reading: letting go turns it off and the page waits.
until_reading()
for _ in range(3):
    pad(press='l2', hold_ms=80)
    time.sleep(.4)
    if dialogue().get('automatic'):
        break
    until_reading()
was_auto = dialogue().get('automatic')
start = cursor()
pad(down='r2')
time.sleep(.6)
pad(up='r2')
time.sleep(4)
held, late, release = split(rows(start))
d = dialogue()
check('pad-r2-ends-automatic', was_auto and release and not release['automatic'] and not late and not d.get('automatic'),
      {'automatic_before': was_auto, 'release': release, 'late_turns': late, 'automatic_after': d.get('automatic')})

# R2 + Menu: the segment skip, which letting go of R2 does not stop. Last: it skips the scene.
until_reading()
start = cursor()
pad(down='r2')
time.sleep(.2)
pad(press='menu', hold_ms=150)
pad(up='r2')   # at once: a short segment may reach its end soon after the skip starts
end = time.monotonic() + 90
while time.monotonic() < end and not any(r['kind'] == 'boundary' for r in rows(start)):
    time.sleep(.5)
log = rows(start)
began = next((i for i, r in enumerate(log) if r['kind'] == 'skip_start'), None)
released = next((r for r in log[(began or 0):] if r['kind'] == 'fast' and not r['held']), None)
ended = next((r for r in log[(began or 0):] if r['kind'] == 'boundary'), None)
check('pad-r2-menu-skip-outlasts-r2', began is not None and released and released['skip'] and ended,
      {'skip_start': log[began] if began is not None else None, 'release': released, 'boundary': ended})

failed = [c['check'] for c in checks if not c['passed']]
print('CHECKS', len(checks), 'FAILED', failed, flush=True)
print('EXIT', s.quit().get('exit_code'), flush=True)
