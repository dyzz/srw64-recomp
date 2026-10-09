#!/usr/bin/env python3
"""The 武器改造 crash of 0.4.2/0.4.3: a press on the frame the weapon list opens is latched
before the native page takes the pad, and the original step that runs on that frame
(801CD268) freed D_801DD148 (+1) text handles from D_801DD568 to redraw details it never
drew. That count is another overlay's leftover; a negative one looped out of RDRAM
(access violation, SIGBUS on a Mac). Here the count is made negative, a machine is chosen
and an arrow is pressed on the frames around the list's opening: the game must keep
running. --binary runs a prebuilt host (one from before the fix dies in the first round)."""
import argparse
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, run_keys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reuse-build', action='store_true')
parser.add_argument('--binary')
parser.add_argument('--rounds', type=int, default=8)
parser.add_argument('--save', default='build/recomp/save-recovery-check/intermission-cold-1.source.sram')
args = parser.parse_args()
s = Session.launch(language='ja', images='original', save=args.save, reuse_build=args.reuse_build, binary=args.binary)
print('RUN', s.run, flush=True)
checks = []
def status():
    return s.client.call('status')
def page():
    return status()['upgrade_page']
def wait_page(screen, timeout=30):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        p = page()
        if p.get('visible') and p.get('screen') == screen:
            return p
        time.sleep(.1)
    raise AssertionError(f'upgrade page {screen} did not open')
def keys(*names, pause=.4):
    for name in names:
        run_keys(s.client, [{'press': name}])
        time.sleep(pause)
def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (s.run/'upgrade-latch-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2)+'\n')
    print(name, 'PASS' if passed else 'FAIL', flush=True)
    assert passed, name

# Title ring to the load screen, as check_upgrade.py does.
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

s.client.call('ui.click', text='intermission:2')
wait_page('list')
time.sleep(1.5)
def vi():
    return s.client.call('status')['vi']
# Choose the machine and press an arrow on the frame the weapon list opens (32 VIs after
# the list closed, the fade): that frame's input is read before the page opens, and the
# step that follows the build in the same frame reads it. The rounds sweep the offset.
events = s.run/'upgrade-page-events.jsonl'
def last_close():
    rows = [json.loads(line) for line in events.read_text().splitlines() if line.strip()]
    return max((r['vi'] for r in rows if r['kind'] == 'close'), default=0)
survived = 0
for attempt in range(args.rounds):
    s.client.call('memory.write', address=0x801DD148, hex='FFF0')
    before_close = last_close()
    run_keys(s.client, [{'press': 'z'}])
    end = time.monotonic() + 5
    while last_close() == before_close and time.monotonic() < end:
        time.sleep(.005)
    closed = last_close()
    offset = 28 + attempt % 8
    s.client.call('wait_vi', vi=closed + offset)
    s.client.call('buttons', buttons='down', vis=2)
    time.sleep(2.5)
    before = vi()
    time.sleep(2)
    moving = vi() - before
    p = page()
    print('round', attempt, 'offset', offset, 'vi moved', moving, 'screen', p.get('screen'), 'count', s.client.call('memory.read', address=0x801DD148, size=2)['hex'], flush=True)
    if moving < 30:
        break
    survived += 1
    wait_page('weapons')
    time.sleep(1)
    keys('x', pause=2)
    wait_page('list')
    time.sleep(1)
check('arrow-on-open-frame-survives', survived == args.rounds, {'survived': survived, 'rounds': args.rounds})
print('EXIT', s.quit().get('exit_code'), flush=True)
