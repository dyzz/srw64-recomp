#!/usr/bin/env python3
"""Verify the Rules page's cheats (docs/gameplay/cheats.md) on a first-episode save.

Loads build/recomp/save-recovery-check/intermission-cold-1.source.sram through the
title ring, opens the settings window on the Cheats page, raises the protagonist's
level and checks the record against the original's own recomputation, turns on the
five switches and checks what each frame holds (lowering EN, SP and morale by hand
and watching them come back), then saves into slot 2 and reads the level the slot
list derives from the saved experience. Evidence goes to the run directory."""
import argparse
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, run_keys

PILOTS, PILOT_SIZE = 0x80172F40, 0x4C
UNITS, UNIT_SIZE = 0x8016A210, 0x54
FUNDS, PARTS = 0x8010F5F4, 0x8015E990

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reuse-build', action='store_true')
parser.add_argument('--language', default='zh-Hans')
args = parser.parse_args()
s = Session.launch(language=args.language, images='original', save='build/recomp/save-recovery-check/intermission-cold-1.source.sram',
                   reuse_build=args.reuse_build)
print('RUN', s.run, flush=True)
checks = []
def status():
    return s.client.call('status')
def keys(*names, pause=.4):
    for name in names:
        run_keys(s.client, [{'press': name}])
        time.sleep(pause)
def pad(buttons, pause=.8):
    s.client.call('pad', press=buttons, hold_ms=100)
    time.sleep(pause)
def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (s.run/'cheat-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2)+'\n')
    assert passed, (name, state)
    print(name, 'PASS', flush=True)
def shot(name):
    s.client.call('screenshot', path=str(s.run/name))
def read(address, size):
    return int(s.client.call('memory.read', address=address, size=size)['hex'], 16)
def write(address, value, size):
    s.client.call('memory.write', address=address, hex=f'{value:0{size*2}X}')
def ids():
    found = []
    def walk(node):
        if isinstance(node, dict):
            if isinstance(node.get('id'), str) and node['id']:
                found.append(node['id'])
            for value in node.values():
                walk(value)
        elif isinstance(node, list):
            for value in node:
                walk(value)
    walk(s.client.call('ui.tree'))
    return found
def level_button(index, level):
    return next(i for i in ids() if i.startswith(f'cheat-level:{index}:{level}:'))
def pilot(index):
    p = PILOTS + index * PILOT_SIZE
    return {'state': read(p, 1), 'number': read(p+2, 2), 'level': read(p+5, 1), 'exp': read(p+0x12, 2),
            'sp': read(p+0x16, 2), 'sp_max': read(p+0x18, 2), 'morale': read(p+0x20, 2),
            'stats': [read(p+0x22+2*i, 2) for i in range(6)], 'spirits': read(p+0x0A, 1)}

# Title ring: ロード is the fourth item; ROM cartridge, slot 1, yes.
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
time.sleep(1.5)
check('intermission', status()['intermission_page'].get('visible'), {})

# The settings window's Cheats page: the five switches, the pilots folded away until
# their row is opened.
pad('view')
check('settings-open', status()['settings_window'], {})
s.client.call('ui.click', id='settings-page:cheats')
time.sleep(1)
check('levels-folded', 'cheat-levels' in ids() and not [i for i in ids() if i.startswith('cheat-level:')], {})
s.client.call('ui.click', id='cheat-levels')
time.sleep(1)
present = ids()
switches = [f'cheat:{name}' for name in ('funds', 'parts', 'en', 'sp', 'morale')]
levels = [i for i in present if i.startswith('cheat-level:')]
check('page', all(i in present for i in switches) and levels and len(set(present)) == len(present), {'levels': levels[:12]})
shot('cheats-page.png')

# Raise the first listed pilot ten levels: level and experience as 800AC220 writes
# them, stats and SP from 800A7F8C (+1 each of four stats, +2 each of two, +2 SP max
# per level).
index = int(levels[0].split(':')[1])
before = pilot(index)
target = min(before['level'] + 10, 99)
s.client.call('ui.click', id=level_button(index, target))
time.sleep(1.5)
after = pilot(index)
grown = target - before['level']
expected = [v + grown * d for v, d in zip(before['stats'], (1, 1, 2, 2, 1, 1))]
check('level', after['level'] == target and after['exp'] == (target - 1) * 500 and after['stats'] == expected
      and after['sp_max'] == before['sp_max'] + 2 * grown and after['sp'] == after['sp_max'],
      {'before': before, 'after': after, 'expected_stats': expected})
# Walk the focus down to the cheats so the screenshot shows them.
for _ in range(40):
    focus = (s.client.call('ui.tree').get('focus') or {}).get('id') or ''
    if focus.startswith('cheat-level:'):
        break
    pad('down', pause=.25)
time.sleep(.5)
shot('cheats-level.png')
# And back down: the recomputation works both ways.
s.client.call('ui.click', id=level_button(index, before['level'] if before['level'] > 1 else 1))
time.sleep(1.5)
down = pilot(index)
check('level-down', down['level'] == before['level'] and down['stats'] == before['stats'] and down['sp_max'] == before['sp_max'], {'down': down})
s.client.call('ui.click', id=level_button(index, target))
time.sleep(1.5)

# The five switches.
for switch in switches:
    s.client.call('ui.click', id=switch)
    time.sleep(.3)
time.sleep(1)
check('funds', read(FUNDS, 4) == 99999999, {'funds': read(FUNDS, 4)})
parts = [(read(PARTS + 2*i, 1), read(PARTS + 2*i + 1, 1)) for i in range(18)]
check('parts', all(held == max(9, fitted) for held, fitted in parts), {'parts': parts})
unit = next(UNITS + i*UNIT_SIZE for i in range(140) if read(UNITS + i*UNIT_SIZE, 1))
write(unit + 8, 5, 2)
p = PILOTS + index * PILOT_SIZE
write(p + 0x16, 1, 2)
write(p + 0x20, 100, 2)
time.sleep(.5)
check('en-held', read(unit + 8, 2) == read(unit + 0xA, 2), {'en': read(unit + 8, 2), 'max': read(unit + 0xA, 2)})
check('sp-held', read(p + 0x16, 2) == read(p + 0x18, 2), {'sp': read(p + 0x16, 2)})
check('morale-held', read(p + 0x20, 2) == 150, {'morale': read(p + 0x20, 2)})
shot('cheats-on.png')
saved = json.loads((s.run/'presentation-settings.json').read_text()) if (s.run/'presentation-settings.json').exists() else None
events = [json.loads(line) for line in (s.run/'cheat-events.jsonl').read_text().splitlines() if line.strip()]
check('events', any(e['kind'] == 'level' for e in events) and any(e['kind'] == 'switches' for e in events), {'events': events[-6:], 'saved': saved})

# Close the settings, save into slot 2 and read the level the slot list shows.
pad('b')
s.client.call('ui.click', text='intermission:0')
def save_page():
    return status()['save_page']
end = time.monotonic() + 20
while not (save_page().get('visible') and save_page().get('screen') == 'choice') and time.monotonic() < end:
    time.sleep(.2)
keys('z', pause=3)
end = time.monotonic() + 20
while not (save_page().get('visible') and save_page().get('screen') == 'slots') and time.monotonic() < end:
    time.sleep(.2)
keys('down')
serial = save_page()['serial']
keys('z', pause=3)
end = time.monotonic() + 20
while save_page().get('serial') == serial and time.monotonic() < end:
    time.sleep(.2)
slot = save_page()['slots'][1]
check('saved-level', slot['used'] and slot['level'] == target and slot['funds'] == 99999999,
      {k: slot.get(k) for k in ('name', 'level', 'funds', 'episode')})
shot('cheats-saved.png')
s.quit()
print('ALL PASS', flush=True)
