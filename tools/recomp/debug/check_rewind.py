#!/usr/bin/env python3
"""Verify rewind in the running game (docs/design/rewind.md, issue #6).

The rewind mini stage: タケル at 8,8 with three foes next to him. Rewind on, then:

1. At the first idle there is one state and nothing before it: the button says so.
2. タケル attacks ロゼ; in the middle of the animation the button goes back through the
   original's abort: the map returns before the attack, タケル has not acted.
3. He attacks again and the battle ends; at the idle after it the button goes back to the
   state before the attack, and a second press finds nothing earlier.
4. The phase ends. Once the first enemy has acted, in the middle of the next enemy's battle
   the button goes back to the enemy phase after the first: still phase 2, the same turn,
   the same units acted.
5. The enemy phase plays out to the player's next turn; there the button goes back into the
   enemy phase's end, which plays out to that turn again.

Local: launches its own session with a save library. --run RUN attaches to a game started
elsewhere (the Windows desktop through an ssh forward) and --stage-image (a path on that
machine) loads the stage from the title menu; --enemy-only starts at 4 on a game standing
idle in the player's phase. Writes rewind-checks.json and screenshots to --out (default the run)."""
import argparse
import json
from pathlib import Path
import shutil
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, run_keys

ROOT = Path(__file__).resolve().parents[3]
STAGE = ROOT / 'config/recomp/mini-stages/rewind.json'
MODE, MAP_STATE, PHASE, TURN = 0x8015DA02, 0x80172EB0, 0x8010F5E8, 0x8010F5EA
BATTLE_STATE = 0x80250000
ROSTER, SIDE, SLOT = 0x8015E100, 0x258, 0x14
TAKERU, ROZE, GALARIA, MINIFO = (0, 0), (1, 0), (1, 1), (1, 2)
ENEMIES = (ROZE, GALARIA, MINIFO)

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--run', help='attach to this run instead of launching')
parser.add_argument('--stage-image', help="with --run: the compiled mini stage on the game's machine")
parser.add_argument('--enemy-only', action='store_true', help='start at the enemy phase checks')
parser.add_argument('--out', help='where checks and screenshots go')
parser.add_argument('--reuse-build', action='store_true')
args = parser.parse_args()

if args.run:
    s = Session.attach(args.run)
else:
    work = ROOT / 'build/recomp/rewind-check' / time.strftime('%Y%m%dT%H%M%S')
    (work / 'saves').mkdir(parents=True)
    s = Session.launch(language='zh-Hans', images='original', mini_stage=str(STAGE), reuse_build=args.reuse_build,
                       env={'SRW64_SAVE_LIBRARY': str(work / 'saves')})
out = Path(args.out) if args.out else Path(s.run)
out.mkdir(parents=True, exist_ok=True)
print('RUN', s.run, 'OUT', out, flush=True)
checks = []


def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (out / 'rewind-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2) + '\n')
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


def enemies():
    return {str(u): (acted(u) if present(u) else 'gone') for u in ENEMIES}


def where():
    return {'mode': mem(MODE), 'state': mem(MAP_STATE), 'phase': mem(PHASE), 'turn': mem(TURN, 2),
            'rewind': status()['rewind'], 'takeru_acted': acted(TAKERU)}


def shot(name):
    """A screenshot into OUT; another machine's comes over the connection. A missing one is noted."""
    try:
        taken = s.client.call('screenshot')
        shutil.copy(s.local_file(taken['path']), out / f'{name}.png')
    except Exception as error:
        print('screenshot failed', name, error, flush=True)


def keys(*names, pause=.6):
    for name in names:
        run_keys(s.client, [{'press': name}])
        time.sleep(pause)


def rewind_button():
    s.client.call('pad', press='l4', hold_ms=100)
    time.sleep(.3)


def until(test, timeout, what, fight=False, every=.4):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        value = test()
        if value:
            return value
        if fight:   # the enemy's attack response, its dialogue
            st = status()
            if st.get('battle_page', {}).get('visible') or st.get('dialogue', {}).get('visible'):
                keys('z', pause=.2)
        time.sleep(every)
    raise AssertionError(f'timed out: {what} {json.dumps(where(), ensure_ascii=False)[:300]}')


def idle(phase=1):
    return lambda: mem(MODE) in (3, 0xB) and mem(MAP_STATE) == 5 and mem(PHASE) == phase and status()['rewind']['stage'] == 'idle'


def outcomes():
    return status()['rewind']['outcomes']


def answered(before, key):
    """The button's outcome after `before`: rewind_notice_done / _none / _off."""
    return lambda: outcomes() > before and status()['rewind']['last'] == key and status()['rewind']['stage'] == 'idle'


def kept(phase, turn):
    return [k for k in status()['rewind']['kept'] if k['phase'] == phase and k['turn'] == turn]


def attack():
    """タケル (cursor on him) attacks ロゼ to his right: unit menu, 攻撃, the first weapon, the target, 戦闘開始.
    A key lost on the way leaves a menu up: back out to the idle map and go again."""
    for attempt in range(3):
        until(idle(), 30, 'the idle before attacking')
        keys('z', 'down', 'z', 'z', pause=.8)
        keys('right', 'z', pause=1.2)
        try:
            until(lambda: status().get('battle_page', {}).get('visible'), 10, 'the pre-battle page')
            break
        except AssertionError:
            print('attack retry', attempt, where()['state'], flush=True)
            for _ in range(6):
                if mem(MAP_STATE) == 5:
                    break
                keys('x', pause=.8)
    else:
        raise AssertionError('the pre-battle page never came up')
    shot('pre-battle')
    keys('z', pause=1)


def setup():
    if args.run and args.stage_image:
        # A stage loads from the title menu (title_major 3) and enters at once.
        until(lambda: status()['intro'].get('title_major') == 3 or keys('return', pause=.6), 120, 'the title menu')
        time.sleep(2)
        s.client.call('mini_stage.load', path=args.stage_image)
    elif not args.run:
        s.enter_mini_stage(timeout=180)
    s.client.call('settings', rewind=True)
    until(idle(), 240, 'the idle')
    until(lambda: len(status()['rewind']['kept']) >= 1, 120, 'a kept state (after the opening event)')
    shot('01-idle')
    check('rewind-on', status()['rewind']['on'] and status()['rewind']['available'], status()['rewind'])


def player_phase():
    # 1. Nothing before the first state.
    seen = outcomes()
    rewind_button()
    until(answered(seen, 'rewind_notice_none'), 10, 'nothing earlier')
    check('nothing-earlier', mem(MAP_STATE) == 5 and not acted(TAKERU), where())

    # 2. In the middle of the animation.
    attack()
    until(lambda: mem(MODE) == 2 and 3 <= mem(BATTLE_STATE) <= 9, 60, 'the animation')
    shot('02-animation')
    seen = outcomes()
    rewind_button()
    until(lambda: status()['rewind']['stage'] == 'leaving_battle', 10, 'leaving the battle')
    until(answered(seen, 'rewind_notice_done'), 60, 'back on the map')
    time.sleep(1)
    shot('03-after-battle-rewind')
    check('battle-rewind', not acted(TAKERU) and mem(PHASE) == 1 and mem(TURN, 2) == 0 and present(ROZE), where())

    # 3. After the battle, then once more.
    attack()
    until(lambda: mem(MODE) == 2, 60, 'the animation')
    until(idle(), 240, 'the idle after the battle')
    time.sleep(1)
    check('attack-spent', acted(TAKERU), where())
    shot('04-after-attack')
    seen = outcomes()
    rewind_button()
    until(answered(seen, 'rewind_notice_done'), 60, 'back before the attack')
    time.sleep(1)
    shot('05-after-idle-rewind')
    check('idle-rewind', not acted(TAKERU) and present(ROZE) and len(status()['rewind']['kept']) == 1, where())
    seen = outcomes()
    rewind_button()
    until(answered(seen, 'rewind_notice_none'), 10, 'nothing earlier')
    check('second-press-nothing-earlier', not acted(TAKERU), where())


def enemy_phase():
    turn = mem(TURN, 2)
    # 4. フェイズ終了 from the empty square above タケル (the cursor is on him), はい.
    keys('up', 'z', pause=1)
    keys('z', pause=1)
    keys('z', pause=1)
    until(lambda: mem(PHASE) == 2, 30, 'the enemy phase')
    # The phase's first idle and the one after its first unit: two states kept this turn.
    until(lambda: len(kept(2, turn)) >= 2, 300, 'the first enemy done', fight=True)
    after_first = enemies()
    check('enemy-states-kept', after_first, {'kept': status()['rewind']['kept'], 'enemies': after_first})
    until(lambda: mem(MODE) == 2 and 3 <= mem(BATTLE_STATE) <= 9, 300, 'the second enemy battle', fight=True)
    shot('06-second-enemy-battle')
    seen = outcomes()
    rewind_button()
    until(answered(seen, 'rewind_notice_done'), 60, 'back in the enemy phase')
    time.sleep(.5)
    back = enemies()
    shot('07-enemy-rewind')
    check('enemy-rewind', mem(PHASE) == 2 and mem(TURN, 2) == turn and back == after_first,
          {'enemies': back, 'before': after_first, **where()})

    # 5. On to the next turn; from there back into the enemy phase's end, and on again.
    until(lambda: mem(PHASE) == 1 and mem(TURN, 2) == turn + 1 and mem(MAP_STATE) == 5 and kept(1, turn + 1), 600,
          'the next turn', fight=True)
    time.sleep(1)
    shot('08-next-turn')
    last_enemy = enemies()
    seen = outcomes()
    rewind_button()
    until(answered(seen, 'rewind_notice_done'), 60, 'back into the enemy phase')
    # The enemies' acted flags are cleared when the phase changes; who is on the map stays.
    alive = lambda e: {k for k, v in e.items() if v != 'gone'}
    check('into-enemy-phase', mem(PHASE) == 2 and mem(TURN, 2) == turn and alive(enemies()) == alive(last_enemy), {'enemies': enemies(), **where()})
    until(lambda: mem(PHASE) == 1 and mem(TURN, 2) == turn + 1 and mem(MAP_STATE) == 5, 300, 'the next turn again', fight=True)
    time.sleep(1)
    shot('09-next-turn-again')
    check('next-turn-again', status()['rewind']['stage'] == 'idle', where())


try:
    if args.enemy_only:
        s.client.call('settings', rewind=True)
    else:
        setup()
        player_phase()
    enemy_phase()
finally:
    if not args.run:
        print('EXIT', s.quit().get('exit_code'), flush=True)
