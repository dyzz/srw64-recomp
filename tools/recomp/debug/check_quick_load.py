#!/usr/bin/env python3
"""Verify the quick load of the suspend save (docs/design/quick-load-and-unified-weapons.md §1).

The quick-load mini stage (タケル at 8,8 with three foes next to him) with a fresh save
library whose card has no suspend save:

1. The stage info page's load button says there is no suspend save.
2. The map menu's 中断 (保存) with はい, then はい to go on: the map stays, and the card's
   suspend record is this stage's, turn 1.
3. With the stage's title changed in RAM the record is another stage's: refused.
4. タケル attacks ロゼ; in the middle of the animation the stage info page's button asks,
   はい: the host presses Z + START, the original fades out and reloads the suspend save,
   タケル has not acted.
5. He attacks again to the end; on the idle map the same: back before the attack.

Writes quick-load-checks.json and screenshots to the run."""
import argparse
import json
from pathlib import Path
import shutil
import struct
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, run_keys

ROOT = Path(__file__).resolve().parents[3]
STAGE = ROOT / 'config/recomp/mini-stages/quick-load.json'
SOURCE = ROOT / 'build/recomp/save-recovery-check/intermission-cold-1.source.sram'
MODE, MAP_STATE, PHASE, TURN, TITLE = 0x8015DA02, 0x80172EB0, 0x8010F5E8, 0x8010F5EA, 0x8010F5F1
BATTLE_STATE = 0x80250000
ROSTER, SIDE, SLOT = 0x8015E100, 0x258, 0x14
TAKERU, ROZE = (0, 0), (1, 0)

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--language', default='zh-Hans')
parser.add_argument('--reuse-build', action='store_true')
args = parser.parse_args()

work = ROOT / 'build/recomp/quick-load-check' / time.strftime('%Y%m%dT%H%M%S')
(work / 'saves').mkdir(parents=True)
shutil.copyfile(SOURCE, work / 'saves' / 'cartridge.sram')
s = Session.launch(language=args.language, images='original', mini_stage=str(STAGE), reuse_build=args.reuse_build,
                   env={'SRW64_SAVE_LIBRARY': str(work / 'saves')})
out = Path(s.run)
print('RUN', s.run, flush=True)
checks = []


def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (out / 'quick-load-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2) + '\n')
    print(name, 'PASS' if passed else 'FAIL', json.dumps(state, ensure_ascii=False)[:400], flush=True)
    assert passed, name


def status():
    return s.client.call('status')


def mem(address, size=1):
    return int(s.client.call('memory.read', address=address, size=size)['hex'], 16)


def acted(unit):
    side, slot = unit
    return bool(mem(ROSTER + side * SIDE + slot * SLOT + 0xB) & 0x80)


def present(unit):
    side, slot = unit
    return mem(ROSTER + side * SIDE + slot * SLOT) == 1


def where():
    return {'mode': mem(MODE), 'state': mem(MAP_STATE), 'phase': mem(PHASE), 'turn': mem(TURN, 2),
            'quick_load': status()['quick_load'], 'takeru_acted': acted(TAKERU)}


def shot(name):
    try:
        taken = s.client.call('screenshot')
        shutil.copy(s.local_file(taken['path']), out / f'{name}.png')
    except Exception as error:
        print('screenshot failed', name, error, flush=True)


def keys(*names, pause=.6):
    for name in names:
        run_keys(s.client, [{'press': name}])
        time.sleep(pause)


def until(test, timeout, what, every=.4):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        value = test()
        if value:
            return value
        time.sleep(every)
    raise AssertionError(f'timed out: {what} {json.dumps(where(), ensure_ascii=False)[:300]}')


def idle():
    return lambda: mem(MODE) in (3, 0xB, 0x11) and mem(MAP_STATE) == 5 and mem(PHASE) == 1 and status()['quick_load']['stage'] == 'idle'


def notice_count():
    """Now, in VIs: the notices posted after it are the ones looked for."""
    return status()['vi']


def noticed(before, key_text):
    """A notice since `before` that begins as the catalog text does (up to its first {...})."""
    head = key_text.split('{')[0]
    return lambda: any(n['vi'] > before and n['text'].startswith(head) for n in status()['notices'])


def ask():
    """The stage info page (Y), then its load button."""
    s.client.call('pad', press='y', hold_ms=100)
    until(lambda: status()['settings_window'], 10, 'the stage info page')
    time.sleep(.8)
    s.client.call('ui.click', id='quick-load-ask')
    time.sleep(.8)


def confirm():
    shot('confirm')
    s.client.call('ui.click', id='quick-load:yes')


def cursor():
    """The map cursor's tile: f32 pixels at 0x80102308 / 0C, a tile is (v - 32) >> 4."""
    raw = bytes.fromhex(s.client.call('memory.read', address=0x80102308, size=8)['hex'])
    x, y = struct.unpack('>ff', raw)
    return (int(x) - 32) >> 4, (int(y) - 32) >> 4


def attack():
    """タケル (cursor on him) attacks ロゼ to his right: unit menu (8), 攻撃, the weapon list
    (0x17), the first weapon, the target, 戦闘開始. Each key waits for the state it leads
    to (tactical-ui-states); a lost key is pressed again."""
    def step(key, test, what):
        for _ in range(4):
            keys(key, pause=.5)
            try:
                return until(test, 3, what, every=.2)
            except AssertionError:
                pass
        shot('stuck')
        raise AssertionError(f'{what} {json.dumps(where(), ensure_ascii=False)[:300]}')
    until(idle(), 30, 'the idle before attacking')
    keys('e', pause=1)   # R: the cursor to the next unit yet to act, タケル
    until(idle(), 10, 'the cursor on タケル')
    step('z', lambda: mem(MAP_STATE) == 8, 'the unit menu')
    keys('down', pause=.5)
    step('z', lambda: mem(MAP_STATE) == 0x17, 'the weapon list')
    # The weapon chosen, the target choice stays in 0x17 with the range drawn.
    keys('z', pause=.8)
    # The target cursor onto ロゼ (9,8), wherever the list put it.
    for _ in range(6):
        x, y = cursor()
        if (x, y) == (9, 8):
            break
        keys('right' if x < 9 else 'left' if x > 9 else 'down' if y < 8 else 'up', pause=.6)
    for _ in range(3):
        keys('z', pause=.8)
        try:
            until(lambda: status().get('battle_page', {}).get('visible'), 10, 'the pre-battle page', every=.3)
            break
        except AssertionError:
            pass
    else:
        shot('stuck')
        raise AssertionError(f'the pre-battle page {json.dumps(where(), ensure_ascii=False)[:300]}')
    keys('z', pause=1)


def loaded(before):
    return lambda: noticed(before, text('quick_load_done'))() and status()['quick_load']['stage'] == 'idle'


CATALOG = json.loads((ROOT / f'content/locales/{args.language}.json').read_text())['ui']
def text(key):
    return CATALOG[key]


try:
    s.enter_mini_stage(timeout=180)
    until(idle(), 240, 'the idle')
    time.sleep(1)
    shot('01-idle')

    # 1. No suspend save yet.
    before = notice_count()
    check('usable-on-map', status()['quick_load']['usable'], status()['quick_load'])
    ask()
    until(noticed(before, text('quick_load_none')), 10, 'no suspend save')
    check('none', not status()['quick_load']['saved']['exists'], status()['quick_load'])
    if status()['settings_window']:
        keys('x', pause=.8)
    until(idle(), 20, 'the idle again')

    # 2. 中断 from the map menu (the last item), はい, はい to go on.
    keys('up', 'z', pause=1)
    keys('up', 'z', pause=1)
    keys('z', pause=2)
    shot('02-saved')
    keys('z', pause=1.5)
    until(idle(), 20, 'the map after saving')
    saved = status()['quick_load']['saved']
    check('suspend-saved', saved['exists'] and saved['this_stage'] and saved['turn'] == 1, saved)

    # 3. Another stage's record: refused.
    title = mem(TITLE)
    s.client.call('memory.write', address=TITLE, hex=f'{(title + 1) & 0xFF:02x}')
    time.sleep(.3)
    before = notice_count()
    ask()
    until(noticed(before, text('quick_load_other_stage')), 10, 'another stage')
    check('other-stage', not status()['quick_load']['saved']['this_stage'], status()['quick_load'])
    if status()['settings_window']:
        keys('x', pause=.8)
    s.client.call('memory.write', address=TITLE, hex=f'{title:02x}')
    until(idle(), 20, 'the idle again')

    # 4. In the middle of the animation.
    attack()
    until(lambda: mem(MODE) == 2 and 3 <= mem(BATTLE_STATE) <= 9, 60, 'the animation')
    shot('03-animation')
    before = notice_count()
    ask()
    until(lambda: status()['settings_window'], 5, 'the question')
    confirm()
    until(lambda: status()['quick_load']['stage'] != 'idle', 10, 'the load starting')
    until(loaded(before), 60, 'back on the map')
    time.sleep(1)
    shot('04-after-battle-load')
    check('battle-load', not acted(TAKERU) and present(ROZE) and mem(TURN, 2) == 0, where())

    # 5. After a whole battle, from the idle map.
    attack()
    until(lambda: mem(MODE) == 2, 60, 'the animation')
    until(idle(), 240, 'the idle after the battle')
    time.sleep(1)
    check('attack-spent', acted(TAKERU), where())
    before = notice_count()
    ask()
    confirm()
    until(loaded(before), 60, 'back before the attack')
    time.sleep(1)
    shot('05-after-idle-load')
    check('idle-load', not acted(TAKERU) and present(ROZE) and mem(TURN, 2) == 0, where())
finally:
    print('EXIT', s.quit().get('exit_code'), flush=True)
