#!/usr/bin/env python3
"""Compare the game state after the short skip with reading the same story (docs/native/script-skip.md).

Two runs one after the other from a new game in Chinese with the default names: one
reads the opening with A page by page, the other presses R + START on its first
dialogue page and again on the first page after each stop. Both go on until the
tactical map waits for the player (script engine idle, player phase). The runs then
compare the roster, unit and pilot records, story variables, money and stage fields,
and the script engine's context, byte by byte.

Results go to script-skip-state.json in the second run's directory."""
import json
import time
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session

REGIONS = {
    'roster': (0x8015E100, 3 * 0x258),       # map slots, three sides
    'units': (0x8016A210, 140 * 0x54),       # unit instances
    'pilots': (0x80172F40, 0x1800),          # pilot instances
    'variables': (0x8015E818, 0x40),         # 200 two-bit story variables
    'stage': (0x8010F5E0, 0x20),             # phase, turn, map, scene, money
    'engine': (0x8015F950 + 0x944, 0x80),    # event context and engine fields
    'excluded': (0x8015F700, 0x40),          # sortie exclusion list
    'carriers': (0x8015E850, 0x40),
}


def run(mode):
    s = Session.launch(language='zh-Hans', reuse_build=(mode == 'skip'), diagnostics='light')
    print(mode, 'RUN', s.run, flush=True)

    def call(method, **params):
        return s.client.call(method, **params)

    def mem(address, size):
        return bytes.fromhex(call('memory.read', address=address, size=size)['hex'])

    def idle():
        engine = mem(0x8015F950, 0xA00)
        state = int.from_bytes(engine[4:6], 'big')
        event = int.from_bytes(engine[0x97C:0x97E], 'big')
        return state == 0xC0 and event == 0x80 and mem(0x8010F5E8, 1)[0] == 1 and mem(0x8015DA02, 1)[0] in (3, 0xB)

    s.wait(vi=600, timeout=300)
    for _ in range(20):
        call('keys', press='return', hold_ms=80)
        time.sleep(.5)
        try:
            s.wait(title_major=3, timeout=6)
            break
        except Exception:
            pass
    time.sleep(2)
    call('keys', press='return', hold_ms=80)
    time.sleep(3)
    skips = 0
    last_event = None
    end = time.monotonic() + 400
    idle_since = None
    while time.monotonic() < end:
        if not s.alive():
            raise SystemExit('the host exited')
        st = call('status')
        if (st.get('name_page') or {}).get('visible'):
            call('ui.key', key='return')
            time.sleep(1.2)
            continue
        if (st.get('intro') or {}).get('loaded'):
            call('keys', press='z', hold_ms=80)
            time.sleep(.3)
            continue
        d = st.get('dialogue') or {}
        if d.get('active') and not d.get('pending') and not d.get('skipping'):
            if mode == 'skip' and d.get('event') != last_event:
                last_event = d.get('event')
                call('keys', down='e')
                time.sleep(.12)
                call('keys', press='return', hold_ms=120)
                call('keys', up='e')
                skips += 1
            else:
                call('keys', press='z', hold_ms=60)
            time.sleep(.25)
            idle_since = None
            continue
        if idle():
            idle_since = idle_since or time.monotonic()
            if time.monotonic() - idle_since > 3:
                break
        else:
            idle_since = None
        time.sleep(.2)
    else:
        raise SystemExit(f'{mode}: the map never became idle')
    state = {name: mem(address, size).hex() for name, (address, size) in REGIONS.items()}
    vi = call('status')['vi']
    s.client.call('screenshot', path=str(s.run / f'{mode}-idle.png'))
    s.quit()
    return s.run, state, vi, skips


read_run, read_state, read_vi, _ = run('read')
skip_run, skip_state, skip_vi, skips = run('skip')
report = {'read_run': str(read_run), 'skip_run': str(skip_run), 'read_idle_vi': read_vi, 'skip_idle_vi': skip_vi,
          'skips': skips, 'regions': {}}
for name, (address, size) in REGIONS.items():
    a, b = bytes.fromhex(read_state[name]), bytes.fromhex(skip_state[name])
    diffs = [{'address': f'{address + i:08X}', 'read': f'{a[i]:02X}', 'skip': f'{b[i]:02X}'} for i in range(size) if a[i] != b[i]]
    report['regions'][name] = {'bytes': size, 'differing': len(diffs), 'first': diffs[:24]}
    print(name, 'same' if not diffs else f'{len(diffs)} bytes differ', flush=True)
(Path(skip_run) / 'script-skip-state.json').write_text(json.dumps(report, indent=2) + '\n')
print('idle at VI', read_vi, '(read) vs', skip_vi, '(skip), skips', skips, flush=True)
