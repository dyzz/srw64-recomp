#!/usr/bin/env python3
"""Verify the return to the title screen (docs/design/quick-load-and-unified-weapons.md §1.6).

The quick-load mini stage with a fresh save library. The host presses the original's
Z + START for the player and lets go as soon as the game takes it, so the fade ends at
the BANPRESTO logo and the title, never at a load of the suspend save:

1. On the idle map the stage info page's button asks; はい: the game fades, the mode goes
   to 1 (the logo) and then 7 (the title), the host's combination is idle again.
2. Entered again: the blank-tile menu's seventh row (ours, before the original's 中断,
   which moves to the eighth) asks the same; はい: the title.
3. Entered again: the map menu's 中断 (保存) stores a suspend save; the
   return still ends at the title, not at the suspend save.
4. Entered again: タケル attacks ロゼ; in the middle of the animation the page's button
   asks; はい: the title again.
5. On the title the function is refused ("not here").

With --intermission (the stage-one-clear save loaded through the title ring, check_intermission.py):
5. On the インターミッション menu its row before 次のマップへ asks; はい: the title. The stage
   info page's button too.
6. Loaded again, on the 改造 sub-screen: the title again.
7. With the original intermission screens: the menu's labels (ours where 次のマップへ was, it
   and the chapter line 16 lower), our row asks and いいえ stays, and 次のマップへ below it
   still goes on to the next map.

With --two-item: the menu as a （前） scene leaves it (データセーブ, ours, 次のマップへ), built again
on the way back from ユニット改造, on the native page and the original screens.

Local: launches its own session with a fresh save library. --run RUN attaches to a game
started elsewhere, and --stage (a path on that machine) loads the stage from the title
menu. Writes title-return-checks.json and screenshots to the run."""
import argparse
import base64
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
MODE, MAP_STATE, PHASE = 0x8015DA02, 0x80172EB0, 0x8010F5E8
BATTLE_STATE = 0x80250000
INTERMISSION = (4, 0x12, 0x13, 0x14, 0x15)   # after a clear; after a load from the title

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--language', default='zh-Hans')
parser.add_argument('--images', default='original', help='original or hd: the window frames drawn from the ROM or redrawn in HD')
parser.add_argument('--reuse-build', action='store_true')
parser.add_argument('--run', help='attach to this run instead of launching')
parser.add_argument('--stage', help="with --run: the stage definition on the game's machine")
parser.add_argument('--intermission', action='store_true', help='the stage-one-clear save through the title ring: the intermission menu and a sub-screen instead of the mini stage')
parser.add_argument('--two-item', action='store_true', help='the two-item intermission menu (after a （前） scene), on the native page and the original screens')
args = parser.parse_args()

if args.run:
    s = Session.attach(args.run)
    s.client.call('settings', locale=args.language)
else:
    work = ROOT / 'build/recomp/title-return-check' / time.strftime('%Y%m%dT%H%M%S')
    (work / 'saves').mkdir(parents=True)
    shutil.copyfile(SOURCE, work / 'saves' / 'cartridge.sram')
    if args.intermission or args.two_item:
        s = Session.launch(language=args.language, images=args.images, save=str(SOURCE), reuse_build=args.reuse_build)
    else:
        s = Session.launch(language=args.language, images=args.images, mini_stage=str(STAGE), reuse_build=args.reuse_build,
                           env={'SRW64_SAVE_LIBRARY': str(work / 'saves')})
out = Path(s.run)
print('RUN', s.run, flush=True)
checks = []
CATALOG = json.loads((ROOT / f'content/locales/{args.language}.json').read_text())['ui']


def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (out / 'title-return-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2) + '\n')
    print(name, 'PASS' if passed else 'FAIL', json.dumps(state, ensure_ascii=False)[:400], flush=True)
    assert passed, name


def status():
    return s.client.call('status')


def mem(address, size=1):
    return int(s.client.call('memory.read', address=address, size=size)['hex'], 16)


def where():
    st = status()
    return {'mode': mem(MODE), 'state': mem(MAP_STATE), 'phase': mem(PHASE), 'title_major': st['intro'].get('title_major'),
            'quick_load': st['quick_load']}


def shot(name):
    try:
        if not args.run:
            s.client.call('screenshot', path=str(out / f'title-return-{name}.png'))
            return
        # A game on another machine writes in its own run directory; the picture is read back.
        taken = s.client.call('screenshot')
        data, offset = b'', 0
        while True:
            part = s.client.call('file.read', path=taken['path'], offset=offset)
            data += base64.b64decode(part['data']); offset = len(data)
            if part['eof']:
                break
        (out / f'title-return-{name}.png').write_bytes(data)
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


def noticed(before, head):
    return lambda: any(n['vi'] > before and n['text'].startswith(head) for n in status()['notices'])


def ask():
    """The stage info page (Y), then its title button."""
    s.client.call('pad', press='y', hold_ms=100)
    until(lambda: status()['settings_window'], 10, 'the stage info page')
    time.sleep(.8)
    shot('page')
    s.client.call('ui.click', id='title-return-ask')
    time.sleep(.8)


def confirm():
    shot('confirm')
    s.client.call('ui.click', id='title-return:yes')


def returned():
    """The logo (1) after the fade, then the title (7) with the combination let go."""
    # The logo (1) can be over before a poll sees it: either is leaving.
    until(lambda: mem(MODE) in (1, 7), 15, 'the logo')
    until(lambda: mem(MODE) == 7, 60, 'the title')
    until(lambda: status()['quick_load']['stage'] == 'idle', 10, 'the combination let go')
    return where()


entries = 0


def enter():
    """The first time through the title's own control; after a return to the title the
    stage is loaded again at run time (mini_stage.load enters it from the menu)."""
    global entries
    entries += 1
    if args.run or entries > 1:
        until(lambda: status()['intro'].get('title_major') == 3 or keys('return', pause=.6), 180, 'the title menu')
        time.sleep(2)
        s.client.call('mini_stage.load', path=args.stage if args.run else str(STAGE))
        until(lambda: status()['mini_stage'].get('ready'), 120, 'the stage')
    else:
        s.enter_mini_stage(timeout=180)
    until(idle(), 240, 'the idle')
    time.sleep(1)


def pool():
    """The text engine's live labels: record -> y (the first one of each)."""
    raw = bytes.fromhex(s.client.call('memory.read', address=0x8015CB00, size=60 * 0x34)['hex'])
    found = {}
    for n in range(60):
        e = raw[n * 0x34:(n + 1) * 0x34]
        if 1 <= e[2] <= 3:
            found.setdefault(struct.unpack('>H', e[0:2])[0], struct.unpack('>i', e[8:12])[0])
    return found


def menu_row():
    """Our label's y (record 0xF001, map_menu.hpp), or None."""
    return pool().get(0xF001)


def menu_rows():
    """The y of our label, システム (record 525) and 中断 (526)."""
    found = pool()
    return {'ours': found.get(0xF001), 'system': found.get(525), 'suspend': found.get(526)}


def cursor():
    raw = bytes.fromhex(s.client.call('memory.read', address=0x80102308, size=8)['hex'])
    x, y = struct.unpack('>ff', raw)
    return (int(x) - 32) >> 4, (int(y) - 32) >> 4


def attack():
    """タケル attacks ロゼ to his right (check_quick_load.py)."""
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
    keys('e', pause=1)
    until(idle(), 10, 'the cursor on タケル')
    step('z', lambda: mem(MAP_STATE) == 8, 'the unit menu')
    keys('down', pause=.5)
    step('z', lambda: mem(MAP_STATE) == 0x17, 'the weapon list')
    keys('z', pause=.8)
    # The target cursor starts on the attacker; ロゼ (9,8) is the tile to his right. The
    # pixel-to-tile reading drifts with the map's scroll, so it only confirms, never steers.
    time.sleep(1)
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


def load_intermission(original=False):
    """Title ring: ロード is three to the right of スタート; ROM cartridge, slot 1, yes (check_intermission.py).
    With the original screens the menu is the game's: wait for its build (801DEC50)."""
    until(lambda: status()['intro'].get('title_major') == 3 or keys('return', pause=.5), 180, 'the title menu')
    time.sleep(2.5)
    keys('right', 'right', 'right', pause=1.5)
    keys('return', pause=3)
    for _ in range(8):
        st = status()
        if st['intermission_page'].get('visible') or st['intro'].get('title_major') != 7:
            break
        keys('z', pause=1.5)
    if original:
        until(lambda: mem(MODE) in INTERMISSION and mem(0x801DEC50, 4) == 1 and mem(0x8015E9C5) == 0xFF, 60, 'the original intermission menu')
    else:
        until(lambda: status()['intermission_page'].get('visible'), 60, 'the intermission menu')
    time.sleep(1)


def two_item(original):
    """The menu built again for a （前） scene: into ユニット改造 and back with the cleared scene
    (8010F5F1) set to the first of D_801DC6D4's list, which the build reads."""
    if original:
        s.client.call('settings', intermission_ui='original')
    load_intermission(original=original)
    first = mem(0x801DC6D4)
    if original:
        until(lambda: mem(0x801DD548, 2) == 1 or keys('down', pause=.6), 10, 'ユニット改造 on the original menu')
        keys('z', pause=1)
    else:
        s.client.call('ui.click', text='intermission:1')
    until(lambda: mem(0x801DECCC, 4) != 0, 20, 'the ユニット改造 screen')
    time.sleep(2)
    s.client.call('memory.write', address=0x8010F5F1, hex=f'{first:02X}')
    keys('x', pause=1)
    if original:
        until(lambda: mem(0x801DECCC, 4) == 0 and mem(0x801DEC50, 4) == 1 and mem(0x8015E9C5) == 0xFF, 30, 'the original menu again')
    else:
        until(lambda: status()['intermission_page'].get('visible'), 30, 'the native menu again')
    time.sleep(1)
    tag = 'original' if original else 'native'
    shot(f'08-two-{tag}')
    if original:
        found = pool()
        check('two-original-labels', mem(0x801DECD8, 4) == 10 and (found.get(0xFCD), found.get(0xF001), found.get(0xFD5), found.get(0xFD7)) == (41, 57, 73, 193),
              dict(found, mode=mem(0x801DECD8, 4)))
    else:
        page = status()['intermission_page']
        check('two-native-items', page.get('restricted') and len(page['items']) == 3, page)
    for _ in range(4):
        if mem(0x801DD548, 2) == 1:
            break
        keys('up', pause=.6)
    shot(f'08-two-{tag}-row')
    check(f'two-{tag}-row-reached', mem(0x801DD548, 2) == 1, {'cursor': mem(0x801DD548, 2)})
    keys('z', pause=1)
    until(lambda: status()['settings_window'], 10, f'the title question from the two-item {tag} menu')
    s.client.call('ui.click', id='title-return:no')
    time.sleep(1)
    check(f'two-{tag}-no-stays', mem(MODE) in INTERMISSION and mem(0x801DD540) == 0 and mem(0x801DD548, 2) == 1, where())
    if original:
        # 次のマップへ under our row still goes on.
        keys('down', pause=.8)
        keys('z', pause=.5)
        state = until(lambda: (mem(0x801DD540) == 2 or mem(MODE) == 12) and where(), 20, 'the next map from the two-item menu')
        check('two-original-next-map', True, state)
    else:
        # Back to the title from it, for the original screens' turn.
        keys('z', pause=1)
        until(lambda: status()['settings_window'], 10, 'the title question again')
        confirm()
        state = returned()
        check('two-native-to-title', state['mode'] == 7 and state['quick_load']['stage'] == 'idle', state)


if args.two_item:
    try:
        s.wait(vi=600, timeout=300)
        two_item(original=False)
        two_item(original=True)
        print('ALL PASS', len(checks), flush=True)
    finally:
        shot('end')
        if not args.run:
            s.quit()
    sys.exit(0)

if args.intermission:
    try:
        s.wait(vi=600, timeout=300)
        load_intermission()
        shot('05-intermission')
        check('usable-in-intermission', status()['quick_load']['usable_title'] and mem(MODE) in INTERMISSION, where())
        # Our row before the menu's last: up twice from the first item lands on it, A asks.
        keys('up', 'up', pause=.8)
        shot('05-row')
        keys('z', pause=1)
        until(lambda: status()['settings_window'], 10, 'the title question from the menu row')
        confirm()
        state = returned()
        shot('05-title')
        check('intermission-row-to-title', state['mode'] == 7 and state['quick_load']['stage'] == 'idle', state)
        load_intermission()
        ask()
        confirm()
        state = returned()
        check('intermission-to-title', state['mode'] == 7 and state['quick_load']['stage'] == 'idle', state)
        load_intermission()
        s.client.call('ui.click', text='intermission:1')
        until(lambda: not status()['intermission_page'].get('visible'), 20, 'the sub-screen')
        time.sleep(3)
        shot('06-upgrade')
        check('usable-on-sub-screen', status()['quick_load']['usable_title'] and mem(MODE) in INTERMISSION, where())
        ask()
        confirm()
        state = returned()
        shot('06-title')
        check('sub-screen-to-title', state['mode'] == 7 and state['quick_load']['stage'] == 'idle', state)
        # 7. The original menu, drawn by the game.
        s.client.call('settings', intermission_ui='original')
        load_intermission(original=True)
        found = pool()
        shot('07-original')
        bodies = bytes.fromhex(s.client.call('memory.read', address=0x800FBAB0, size=2 * 0x218)['hex'])
        title_y = next((struct.unpack_from('>i', bodies, n * 0x218 + 8)[0] for n in range(2)
                        if 1 <= bodies[n * 0x218 + 2] <= 3 and struct.unpack_from('>i', bodies, n * 0x218 + 4)[0] == 82), None)
        check('original-labels', (found.get(0xF001), found.get(0xFD5), found.get(0xFD7), found.get(0xFE0), title_y) == (169, 185, 209, 209, 209), dict(found, title=title_y))
        keys('up', 'up', pause=.8)
        check('original-row-reached', mem(0x801DD548, 2) == 8, {'cursor': mem(0x801DD548, 2)})
        shot('07-original-row')
        keys('z', pause=1)
        until(lambda: status()['settings_window'], 10, 'the title question from the original menu')
        s.client.call('ui.click', id='title-return:no')
        time.sleep(1)
        check('original-no-stays', mem(MODE) in INTERMISSION and mem(0x801DD540) == 0 and mem(0x801DD548, 2) == 8, where())
        keys('down', pause=.8)
        keys('z', pause=.5)
        # Exit code 2, cleared again once the next map's mode (12) has begun.
        state = until(lambda: (mem(0x801DD540) == 2 or mem(MODE) == 12) and where(), 20, 'the next map from the original menu')
        check('original-next-map', True, state)
        print('ALL PASS', len(checks), flush=True)
    finally:
        shot('end')
        if not args.run:
            s.quit()
    sys.exit(0)

try:
    # The cursor's tile is read in the original's coordinates: keep the picture 4:3.
    s.wait(vi=300, timeout=300)
    s.client.call('settings', aspect='4:3')
    enter()
    shot('01-idle')
    # 1. From the idle map.
    check('usable-on-map', status()['quick_load']['usable_title'], status()['quick_load'])
    ask()
    confirm()
    state = returned()
    shot('01-title')
    check('map-to-title', state['mode'] == 7 and state['quick_load']['stage'] == 'idle', state)

    # 2. The blank-tile menu's own row: up from フェイズ終了 wraps onto 中断 (the eighth),
    # up again onto ours (the seventh), A asks. First its sub-screens: under 部隊表 and
    # 作戦目的 the row is not drawn, and back on the menu (which can come back lower) it
    # sits between システム and 中断.
    enter()
    keys('up', pause=.8)
    keys('z', pause=1)
    until(lambda: mem(MAP_STATE) == 0x16 and mem(0x80172ED4) == 8, 10, 'the blank-tile menu with eight rows')
    for index, name in ((1, 'units'), (3, 'goal')):
        for _ in range((index - mem(0x80172ED6)) % 8):
            keys('down', pause=.4)
        keys('z', pause=1.5)
        until(lambda: mem(0x80172EB2) != 0, 10, f'the {name} sub-screen')
        time.sleep(.5)
        shot(f'02-{name}')
        check(f'no-row-under-{name}', menu_row() is None, {'row': menu_row(), 'sub': mem(0x80172EB2)})
        keys('x', pause=1.5)
        until(lambda: mem(0x80172EB2) == 0, 10, 'the menu again')
        time.sleep(.5)
        shot(f'02-{name}-back')
        rows = menu_rows()
        check(f'rows-after-{name}', rows['ours'] is not None and rows['system'] is not None and rows['suspend'] is not None
              and rows['ours'] - rows['system'] == 16 and rows['suspend'] - rows['ours'] == 16, rows)
    # Closed with B the row goes with the original's labels; opened again it is back.
    keys('x', pause=1)
    until(lambda: mem(MAP_STATE) == 5, 10, 'the map after closing the menu')
    check('row-freed-on-close', menu_row() is None and 526 not in pool(), menu_rows())
    keys('z', pause=1)
    until(lambda: mem(MAP_STATE) == 0x16 and mem(0x80172ED4) == 8, 10, 'the menu again')
    time.sleep(.5)
    rows = menu_rows()
    check('rows-reopened', rows['ours'] is not None and rows['suspend'] - rows['ours'] == 16 and rows['ours'] - rows['system'] == 16, rows)
    # Back to フェイズ終了, then up past 中断 onto ours.
    for _ in range(mem(0x80172ED6)):
        keys('up', pause=.4)
    keys('up', 'up', pause=.8)
    check('menu-row-reached', mem(0x80172ED6) == 6, {'cursor': mem(0x80172ED6), 'rows': mem(0x80172ED4)})
    shot('02-menu')
    keys('z', pause=1)
    until(lambda: status()['settings_window'], 10, 'the title question from the menu')
    confirm()
    state = returned()
    shot('02-menu-title')
    check('menu-to-title', state['mode'] == 7 and state['quick_load']['stage'] == 'idle', state)

    # 3. With a suspend save on the card the return still ends at the title. 中断 (保存) is
    # the eighth row now, after ours: up once from the first.
    enter()
    keys('up', 'z', pause=1)
    shot('02-menu-suspend')
    keys('up', 'z', pause=1)
    keys('z', pause=2)
    keys('z', pause=1.5)
    until(idle(), 20, 'the map after saving')
    saved = status()['quick_load']['saved']
    check('suspend-saved', saved['exists'] and saved['this_stage'], saved)
    ask()
    confirm()
    state = returned()
    shot('02-title')
    check('saved-still-title', state['mode'] == 7 and state['quick_load']['stage'] == 'idle', state)

    # 3. In the middle of a battle animation.
    enter()
    attack()
    until(lambda: mem(MODE) == 2 and 2 <= mem(BATTLE_STATE) <= 20 and status()['quick_load']['usable_title'], 60, 'the animation')
    time.sleep(1)
    shot('03-battle')
    ask()
    confirm()
    state = returned()
    shot('03-title')
    check('battle-to-title', state['mode'] == 7 and state['quick_load']['stage'] == 'idle', state)

    # 4. Refused on the title.
    check('not-on-title', not status()['quick_load']['usable_title'], status()['quick_load'])
    print('ALL PASS', len(checks), flush=True)
finally:
    shot('end')
    if not args.run:
        s.quit()
