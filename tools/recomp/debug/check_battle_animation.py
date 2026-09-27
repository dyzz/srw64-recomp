#!/usr/bin/env python3
"""Verify the battle-animation control: X aborts the battle.

Baseline (no input) was recorded in build/recomp/debug/20260925T015448.967379Z:
    0 1 2 3 4 5 7 8 9 10 11 12 13 14 15 16 18 19 20 21
Pressing X should jump to the wind-down (21), return to the map, and the map
must then present the result the way it does with the animation off: the host
replays the animation-off branch on the resume frame (note replay-animation-off),
the 0x4B chain draws the figure, and the roster ends at HP - damage.

Note: check_battle_ui.py toggles the animation flag OFF as one of its
assertions and never restores it, which stops overlay load_00121560 from
loading at all. This driver keeps the flag ON.

Run with SRW64_BATTLE_ANIMATION_PROBE=1 so the host writes the state trace.
"""
import sys,time,json
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session
import argparse

BASELINE=[0,1,2,3,4,5,7,8,9,10,11,12,13,14,15,16,18,19,20,21]

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('run',type=Path,nargs='?',help='attach an existing run')
parser.add_argument('--mode',choices=['abort','baseline'],default='abort')
parser.add_argument('--no-anim',dest='no_anim',action='store_true',
                    help='turn the battle animation OFF and watch how the game shows damage on the map')
parser.add_argument('--pad',action='store_true',
                    help='end the battle with the controller R2 (host-only bit) instead of keyboard X')
parser.add_argument('--audio',action='store_true',
                    help='launch with sound so a human can hear whether a skipped/aborted battle '
                         'leaves an effect or BGM loop playing on the map')
parser.add_argument('--hold',type=int,default=0,help='keep the session alive this many seconds at the end')
parser.add_argument('--binary',help='run a prebuilt host snapshot instead of rebuilding; use this when other '
                                    'sessions are editing src/host so their rebuilds cannot break the run')
parser.add_argument('--at',type=int,default=4,help='press once the machine reaches this state')
parser.add_argument('--player',action='store_true',
                    help='attack with the player unit under the cursor (unit menu, 攻撃, first weapon, first target) '
                         'instead of ending the phase and letting the enemy attack; covers the other fork caller')
args=parser.parse_args()

if args.run:
    s=Session(args.run.resolve())
else:
    s=Session.launch(language='zh-Hans',mini_stage='config/recomp/mini-stages/battle-ui.json',
                     audio=args.audio,binary=args.binary)
    print('RUN',s.run,flush=True)
    s.enter_mini_stage()
    # At ready the cursor sits on a player unit with an enemy to its right. The
    # default ends the phase from the empty cell above (phase-end menu, yes), so
    # the enemy attacks and the defender-response page confirms; --player opens
    # the unit menu, picks 攻撃, the first weapon, then moves the target cursor
    # onto the enemy to the right and confirms.
    # A held direction auto-repeats on the map, so the cursor step is a short tap.
    for key in (['z','down','z','z','right','z'] if args.player else ['up','z','z','z']):
        s.client.call('keys',press=key,hold_ms=30 if key=='right' else 100);time.sleep(.8)

import atexit
@atexit.register
def _shutdown():
    if s.run is None:return
    try:
        s.quit()
    except Exception as error:
        print('quit failed:',error,flush=True)

run=s.run
trace=run/'battle-animation-states.json'
checks=[]
def state():return s.client.call('status')
def states():
    if not trace.exists():return []
    try:return [r['state'] for r in json.loads(trace.read_text()) if 'state' in r]
    except Exception:return []
def rows():
    if not trace.exists():return []
    try:return json.loads(trace.read_text())
    except Exception:return []
def shoot(name):
    """A screenshot that says the host died rather than raising a socket error."""
    try:
        s.client.call('screenshot',path=str(run/name))
        return True
    except Exception as error:
        print('HOST GONE while shooting',name,'-',error,flush=True)
        return False
def check(name,cond,extra=None):
    checks.append({'check':name,'passed':bool(cond),'extra':extra})
    (run/'animation-checks.json').write_text(json.dumps(checks,ensure_ascii=False,indent=2))
    print(name,'PASS' if cond else 'FAIL',extra if extra is not None else '',flush=True)
    assert cond,name
def wait(fn,seconds=120,poll=.1):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        st=state()
        if fn(st):return st
        time.sleep(poll)
    raise AssertionError('state wait timeout')

st=wait(lambda x:x['battle_page'].get('visible',False))
page=st['battle_page']
want=not args.no_anim
if page.get('animation',False)!=want:
    s.client.call('ui.click',id='battle-animation');time.sleep(.3)
    page=state()['battle_page']
check('animation-flag',page.get('animation')==want,page.get('animation'))

if args.no_anim:
    # Reference capture: what the original does on the map with animation off.
    before=len(rows())
    s.client.call('ui.click',id='battle-confirm')
    for i,d in enumerate([0.3,0.6,0.9,1.2,1.6,2.2,3.0]):
        time.sleep(d)
        if not shoot(f'noanim-t{i}.png'):break
        print('shot t%d'%i,flush=True)
    print('DONE',flush=True);raise SystemExit(0)

before=len(rows())
s.client.call('ui.click',id='battle-confirm')

if args.mode=='baseline':
    time.sleep(35)
    seq=states()[before:]
    check('baseline-sequence',seq==BASELINE,seq)
    print('DONE',flush=True);raise SystemExit(0)

# Wait for the machine to reach the press state, then send one press.
# Shoot the frame just before the press so there is visual proof the battle was
# actually on screen, not the confirmation page.
end=time.monotonic()+40;pressed_at=None
while time.monotonic()<end:
    seq=states()[before:]
    # The trace file is polled, so by the time a state is seen the machine may
    # already have moved on. Press as soon as the battle is under way at or past
    # the requested state rather than demanding an exact match.
    if seq and seq[-1]>=args.at:
        shoot(f'anim-{args.mode}-before.png')
        if args.pad:
            s.client.call('pad',press='r2',hold_ms=100)
        else:
            s.client.call('keys',press='x',hold_ms=100)
        pressed_at=seq[-1]
        print('pressed',('R2' if args.pad else 'X'),'at state',pressed_at,flush=True)
        break
    time.sleep(.05)
check('reached-press-state',pressed_at is not None and pressed_at>=args.at,pressed_at)

# Follow what happens next frame by frame instead of only looking at the end.
for i,delay in enumerate([0.35,0.35,0.35,0.5,2,8]):
    time.sleep(delay)
    alive=shoot(f'anim-{args.mode}-t{i}.png')
    print('shot t%d state=%s'%(i,(states()[before:] or ['-'])[-1]),flush=True)
    if not alive:
        check('host-survived-the-abort',False,'the host exited during the wind-down')
seq=states()[before:]
notes=[r.get('note') for r in rows()[before:] if r.get('note')]
print('SEQUENCE',seq,flush=True)
print('NOTES',notes,flush=True)

check('abort-note','abort-to-winddown' in notes,notes)
# The whole point: end early but still leave through the normal wind-down,
# so the map comes back rather than the title screen.
check('jumped-to-winddown',21 in seq,seq)
check('counter-round-skipped',not [x for x in (13,14,15,16,18,19,20) if x in seq],seq)
# The point of the abort is that the player still sees what the battle did, the
# same figure an animation-off battle shows. ui-text.jsonl records each one.
figures=[]
log=run/'ui-text.jsonl'
if log.exists():
    for line in log.read_text().splitlines():
        try:row=json.loads(line)
        except Exception:continue
        if row.get('kind')=='damage':figures.append(row.get('figure'))
print('FIGURES',figures,flush=True)
check('damage-redrawn',bool(figures),figures)

# The host must have taken over the resume frame (state 24 sub-state 4) with the
# animation-off branch, and the chain must have run to its end (state 24 again).
replay=[r for r in rows()[before:] if r.get('note')=='replay-animation-off']
check('replayed-animation-off',len(replay)==1,replay)
replay=replay[0]
print('REPLAY',json.dumps(replay,ensure_ascii=False),flush=True)
check('rounds-queued',bool(replay.get('rounds')),replay.get('rounds'))
# At the fork the roster held the pre-battle HP and the aborted animation must
# not have touched it: the chain subtracts from what it finds.
check('roster-untouched-by-animation',
      all(h['at_fork']==h['now'] for h in replay['hp_before']),replay['hp_before'])
done=[r for r in rows()[before:] if r.get('note')=='chain-done']
check('chain-done',len(done)==1,done)
# The chain drains exactly the queued damage out of the roster, clamped at 0,
# for each participant it targets: no double application, nothing skipped.
expected=[h['now'] for h in replay['hp_before']]
handles=[h['handle'] for h in replay['hp_before']]
for r in replay['rounds']:
    if r['target'] in handles:
        i=handles.index(r['target'])
        expected[i]=max(expected[i]-r['damage'],0)
print('HP expected',expected,'after',done[0]['hp_after'],flush=True)
check('hp-settled-like-animation-off',done[0]['hp_after']==expected,
      {'expected':expected,'after':done[0]['hp_after']})

if args.player:
    # A player attack is followed by nothing until the phase ends; the map just
    # has to come back alive after the rewards box (chain-done proved state 24).
    time.sleep(8)
    check('host-alive-after-rewards',shoot('animation-'+args.mode+'-after.png'))
else:
    st=wait(lambda x:x['battle_page'].get('visible',False),60)
    check('next-encounter-on-map',st['battle_page'].get('visible'),st['vi'])

shoot('animation-'+args.mode+'.png')
if args.hold:
    print('HOLDING %ds — listen for leftover sound on the map, then it exits'%args.hold,flush=True)
    time.sleep(args.hold)
print('DONE',flush=True)
