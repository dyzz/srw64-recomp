"""MOD battle voices follow the game speed (docs/design/audio-mods.md §11.8): at 2x a line is
spoken 1.75x faster and runs to its stretched length; X during a line fades it out with the
picture (300 ms) instead of cutting it 200 ms later.

    .venv/bin/python tools/recomp/debug/check_voice_speed.py

Needs the disc voice package built at build/mods/srw64.battle-voices-disc (make_voice_pack.py of
https://github.com/dyzz/srw64-recomp-disc-voice-mod, -o build/mods) and the host built in build/recomp/gfx-build; the battle-ui mini-stage's
ショウ and マリア have lines in it. Run 2026-11-01: 12334 0.76 s → 0.43 s, 12313 1.08 s → 0.59 s
at speed=1.75; X at state 9 removed the line within 2 ms and the tail faded over 300 ms.
"""
import sys, time, json, os, struct, atexit
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools'))
from recomp.debug.session import Session

s = Session.launch(language='zh-Hans', mini_stage='config/recomp/mini-stages/battle-ui.json', audio=True,
                   binary=str(ROOT / 'build/recomp/gfx-build/srw64-gfx-host'),
                   env={'SRW64_BATTLE_ANIMATION_PROBE': '1'})
print('RUN', s.run, flush=True)
@atexit.register
def _quit():
    try: s.quit()
    except Exception as e: print('quit failed', e)
mods = s.run.parent / (s.run.name + '.mods')
mods.mkdir(exist_ok=True)
os.symlink(ROOT / 'build/mods/srw64.battle-voices-disc', mods / 'srw64.battle-voices-disc')
s.enter_mini_stage()
time.sleep(1)
print('settings', s.client.call('settings', game_speed=2), flush=True)
console = s.run / 'console.log'
trace = s.run / 'battle-animation-states.json'
def status(): return s.client.call('status')
def voice_lines():
    if not console.exists(): return []
    return [l for l in console.read_text(errors='replace').splitlines() if 'SRW64_VOICE' in l]
def states():
    try: return [r['state'] for r in json.loads(trace.read_text()) if 'state' in r]
    except Exception: return []
def ogg_seconds(record):
    b = (ROOT / f'build/mods/srw64.battle-voices-disc/audio/voice/battle/{record}.ogg').read_bytes()
    i = b.find(b'\x01vorbis'); rate = struct.unpack_from('<I', b, i + 12)[0]
    j = b.rfind(b'OggS'); return struct.unpack_from('<q', b, j + 6)[0] / rate
results = []
def check(name, ok, extra=None):
    results.append({'check': name, 'passed': bool(ok), 'extra': extra}); print(name, 'PASS' if ok else 'FAIL', extra, flush=True)

def start_enemy_battle():
    for key in ['up', 'z', 'z', 'z']:
        s.client.call('keys', press=key, hold_ms=100); time.sleep(.8)
    end = time.monotonic() + 60
    while time.monotonic() < end:
        st = status()
        if st['battle_page'].get('visible'): break
        time.sleep(.1)
    else: raise SystemExit('no battle page')
    if not st['battle_page'].get('animation', False):
        s.client.call('ui.click', id='battle-animation'); time.sleep(.3)
    s.client.call('ui.click', id='battle-confirm')

# ---- battle 1: watch every line at 2x
before = len(voice_lines()); nstates = len(states())
start_enemy_battle()
timeline = []   # (t, line, speed, position, running)
end = time.monotonic() + 60; seen_map_back = False; t0 = time.monotonic()
while time.monotonic() < end:
    st = status(); m = st.get('music', {}); v = m.get('voice'); gs = st.get('game_speed', {})
    timeline.append((round(time.monotonic() - t0, 3), v.get('line') if v else None, v.get('speed') if v else None,
                     v.get('position') if v else None, v.get('decoded') if v else None, gs.get('running'), m.get('voice_fading')))
    seq = states()[nstates:]
    if seq and seq[-1] == 21 and st.get('mode') not in (2,) and not v: 
        # out of the battle once the map state machine moved on
        pass
    if time.monotonic() - t0 > 8 and not v and seq and 21 in seq and st.get('scene', {}).get('mode', 2) != 2:
        seen_map_back = True; break
    time.sleep(.05)
lines = voice_lines()[before:]
print('VOICE LINES', *lines, sep='\n  ', flush=True)
check('voice-played-at-2x', any('speed=1.75' in l for l in lines), lines[:3])
check('game-speed-running-during-battle', any(r[5] for r in timeline), None)
# per line: how long the voice was in the status vs its file length / 1.75
durations = {}
for t, line, speed, pos, decoded, running, fading in timeline:
    if line is not None:
        d = durations.setdefault(line, {'first': t, 'last': t, 'speed': speed}); d['last'] = t
for line, d in durations.items():
    d['status_seconds'] = round(d['last'] - d['first'], 2); d['file_seconds'] = round(ogg_seconds(line), 2)
    d['expected'] = round(d['file_seconds'] / (d['speed'] or 1), 2)
print('DURATIONS', json.dumps(durations, indent=1), flush=True)
finished = [d for d in durations.values() if d['status_seconds'] >= d['expected'] - 0.25]
check('at-least-one-line-ran-to-its-stretched-length', any(abs(d['status_seconds'] - d['expected']) < 0.35 for d in durations.values()), durations)
# A tail only exists when the next line cut this one short; lines that ran out leave none.
interrupted = [l for l, d in durations.items() if d['status_seconds'] < d['expected'] - 0.35]
if interrupted: check('tail-fade-seen-after-an-interrupted-line', any(r[6] for r in timeline), interrupted)
(s.run / 'voice-speed-timeline.json').write_text(json.dumps(timeline))

# ---- battle 2: abort with X while a line plays
time.sleep(6)   # the enemy phase ends, the player phase begins
nstates = len(states()); before = len(voice_lines())
start_enemy_battle()
end = time.monotonic() + 60; pressed = None
while time.monotonic() < end:
    st = status(); v = st.get('music', {}).get('voice')
    seq = states()[nstates:]
    if v and v.get('decoded') and seq and 2 <= seq[-1] <= 20:
        s.client.call('keys', press='x', hold_ms=100); pressed = (time.monotonic(), v, seq[-1]); break
    time.sleep(.03)
check('pressed-x-while-a-line-played', pressed is not None, pressed and pressed[1:])
fade_seen = []; gone_at = None
if pressed:
    t_press = pressed[0]
    for _ in range(80):
        st = status(); m = st.get('music', {})
        fade_seen.append((round(time.monotonic() - t_press, 3), m.get('voice', {}).get('line') if m.get('voice') else None, m.get('voice_fading')))
        if gone_at is None and not m.get('voice'): gone_at = time.monotonic() - t_press
        time.sleep(.025)
    print('AFTER X', fade_seen[:30], flush=True)
    check('voice-stopped-within-100ms-of-x', gone_at is not None and gone_at < 0.15, gone_at)
    check('voice-faded-not-cut', any(f[2] for f in fade_seen), [f for f in fade_seen if f[2]][:5])
    fading_values = [f[2] for f in fade_seen if f[2]]
    check('fade-lasts-about-300ms', fading_values and fading_values[0] > 44100 * 0.2, fading_values[:3])
time.sleep(8)
seq = states()[nstates:]
check('abort-reached-winddown', 21 in seq, seq)
(s.run / 'voice-speed-checks.json').write_text(json.dumps(results, ensure_ascii=False, indent=1))
print('DONE', sum(not r['passed'] for r in results), 'failed', flush=True)
