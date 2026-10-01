#!/usr/bin/env python3
"""Check the short skip (R + START) in the native build (docs/native/script-skip.md).

Starts a new game in Chinese, takes the default protagonist and names, and on the
first story dialogue presses R + START (E + Enter). From the dialogue log and the
host log it reports how many VIs the skip took, where it stopped, whether any skipped
dialogue page was shown, and which commands ran their own time (SRW64_SCRIPT_SKIP
wait lines: the ones worth finishing at once). Screenshots before, during and after.

Results go to script-skip-checks.json in the run directory."""
import argparse
import json
import re
import time
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reuse-build', action='store_true')
parser.add_argument('--pages', type=int, default=2, help='pages to read with A before skipping')
parser.add_argument('--skips', type=int, default=2, help='skips in a row, each on the next dialogue read')
args = parser.parse_args()
s = Session.launch(language='zh-Hans', reuse_build=args.reuse_build)
print('RUN', s.run, flush=True)
checks = []


def check(name, passed, detail):
    checks.append({'check': name, 'passed': bool(passed), 'detail': detail})
    (s.run / 'script-skip-checks.json').write_text(json.dumps({'checks': checks}, ensure_ascii=False, indent=2) + '\n')
    print(name, 'PASS' if passed else 'FAIL', json.dumps(detail, ensure_ascii=False)[:400], flush=True)


def status():
    return s.client.call('status')


def keys(**step):
    return s.client.call('keys', **step)


def rows(since, kinds):
    return s.events('dialogue', since, list(kinds))[0]


def cursor():
    return s.events('dialogue', 0, ['-'])[1]


def until_reading(timeout=240):
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
            return st
        if (st.get('intro') or {}).get('loaded'):
            keys(press='z', hold_ms=80)
            time.sleep(.3)
            continue
        time.sleep(.2)
    raise SystemExit('no dialogue to read')


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

# Read a few pages normally first, like a player who then decides to skip.
for _ in range(args.pages):
    until_reading()
    keys(press='z', hold_ms=80)
    time.sleep(.8)
def skip_once(n):
    """R + START on the dialogue being read; checks and timings for skip n."""
    st = until_reading()
    start = cursor()
    s.client.call('screenshot', path=str(s.run / f'skip{n}-before.png'))
    keys(down='e')
    time.sleep(.15)
    keys(press='return', hold_ms=120)
    keys(up='e')
    t0 = time.monotonic()
    shots = 0
    while time.monotonic() - t0 < 120:
        if not s.alive():
            raise SystemExit('the host exited')
        log = rows(start, ('skip_start', 'boundary', 'skip_unavailable', 'skip_cancelled'))
        if any(r['kind'] in ('boundary', 'skip_unavailable', 'skip_cancelled') for r in log):
            break
        if shots < 3 and time.monotonic() - t0 > .3 + shots * .5:
            s.client.call('screenshot', path=str(s.run / f'skip{n}-during-{shots}.png'))
            shots += 1
        time.sleep(.1)
    time.sleep(1.5)
    s.client.call('screenshot', path=str(s.run / f'skip{n}-after.png'))
    log = rows(start, ('skip_start', 'boundary', 'skip_unavailable', 'skip_cancelled', 'fragment', 'turn'))
    began = next((r for r in log if r['kind'] == 'skip_start'), None)
    ended = next((r for r in log if r['kind'] in ('boundary', 'skip_cancelled')), None)
    shown = [r for r in log if r['kind'] == 'fragment' and began and ended and began['vi'] < r['vi'] < ended['vi']]
    check(f'skip{n}-started', began is not None, {'skip_start': began, 'first_rows': log[:4]})
    check(f'skip{n}-stopped', ended is not None, {'end': ended})
    check(f'skip{n}-dialogue-not-shown', began and ended and not shown, {'shown_fragments': shown[:5]})
    if began and ended:
        check(f'skip{n}-duration', True, {'vis': ended['vi'] - began['vi'], 'seconds': round((ended['vi'] - began['vi']) / 60, 2),
                                          'reason': ended.get('reason'), 'overlay': ended.get('overlay')})


for n in range(args.skips):
    skip_once(n + 1)
native_log = Path(str(s.run) + '.native.log')
waits = []
if native_log.exists():
    for line in native_log.read_text(errors='replace').splitlines():
        m = re.match(r'SRW64_SCRIPT_SKIP wait handler=([0-9A-F]+) pc=([0-9A-F]+) vi=(\d+)', line)
        if m:
            waits.append({'handler': m[1], 'pc': m[2], 'vi': int(m[3])})
check('commands-at-own-time', True, {'count': len(waits), 'handlers': sorted({w['handler'] for w in waits}), 'waits': waits[:60]})
d = status().get('dialogue') or {}
check('reading-after-skip', True, {'active': d.get('active'), 'skipping': d.get('skipping'), 'event': d.get('event'),
                                   'boxes': [b.get('text') for b in d.get('boxes', [])]})
print('CHECKS', len(checks), 'FAILED', [c['check'] for c in checks if not c['passed']], flush=True)
print('EXIT', s.quit().get('exit_code'), flush=True)
