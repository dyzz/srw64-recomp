"""Record the same battle, with MOD voices, at each game speed, as MP4s with the game's sound
(docs/design/audio-mods.md §11.8: at 2x a line is spoken 1.75x faster, at 3x/4x 2x faster).

    .venv/bin/python tools/recomp/debug/record_voice_speed.py [--speeds 1,2,3,4] [--out DIR]

One fresh battle-ui mini-stage per speed, so every clip shows the same first battle (ショウ vs
ガラリア counterattack, both with lines in the disc voice package). Needs the voice package at
build/mods/srw64.battle-voices-disc (https://github.com/dyzz/srw64-recomp-disc-voice-mod, -o build/mods), the host in build/recomp/gfx-build and ffmpeg. Clips land
in local/voice-speed-samples/ by default, with a lines.json of the voice lines each clip played.
"""
import sys, time, json, os, argparse, atexit
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools'))
from recomp.debug.session import Session

ap = argparse.ArgumentParser()
ap.add_argument('--speeds', default='1,2,3,4')
ap.add_argument('--out', default=str(ROOT / 'local/voice-speed-samples'))
args = ap.parse_args()
out = Path(args.out); out.mkdir(parents=True, exist_ok=True)
summary = {}

def record_one(speed: int) -> dict:
    s = Session.launch(language='zh-Hans', mini_stage='config/recomp/mini-stages/battle-ui.json', audio=True,
                       binary=str(ROOT / 'build/recomp/gfx-build/srw64-gfx-host'),
                       env={'SRW64_BATTLE_ANIMATION_PROBE': '1'})
    print('RUN', speed, s.run, flush=True)
    try:
        mods = s.run.parent / (s.run.name + '.mods'); mods.mkdir(exist_ok=True)
        os.symlink(ROOT / 'build/mods/srw64.battle-voices-disc', mods / 'srw64.battle-voices-disc')
        s.enter_mini_stage(); time.sleep(1)
        st = s.client.call('status')
        top = st.get('game_speed', {}).get('top')
        if speed > 1:
            print('settings', s.client.call('settings', game_speed=speed), 'top', top, flush=True)
        console = s.run / 'console.log'; trace = s.run / 'battle-animation-states.json'
        def voice_lines():
            return [l for l in console.read_text(errors='replace').splitlines() if 'SRW64_VOICE' in l] if console.exists() else []
        def states():
            try: return [r['state'] for r in json.loads(trace.read_text()) if 'state' in r]
            except Exception: return []
        before = len(voice_lines()); nstates = len(states())
        for key in ['up', 'z', 'z', 'z']:
            s.client.call('keys', press=key, hold_ms=100); time.sleep(.8)
        end = time.monotonic() + 60
        while time.monotonic() < end:
            st = s.client.call('status')
            if st['battle_page'].get('visible'): break
            time.sleep(.1)
        else: raise SystemExit('no battle page')
        if not st['battle_page'].get('animation', False):
            s.client.call('ui.click', id='battle-animation'); time.sleep(.3)
        s.record_start(width=960)
        t0 = time.monotonic()
        s.client.call('ui.click', id='battle-confirm')
        speeds_seen = set(); lines_seen = []
        end = time.monotonic() + 90; done_at = None
        while time.monotonic() < end:
            st = s.client.call('status'); v = st.get('music', {}).get('voice')
            if v and v.get('line') is not None: speeds_seen.add(v.get('speed'))
            seq = states()[nstates:]
            if done_at is None and seq and 21 in seq and not v: done_at = time.monotonic()
            if done_at and time.monotonic() - done_at > 2.5: break
            time.sleep(.05)
        clip = s.record_stop(out / f'voice-{speed}x.mp4', fps=30)
        lines_seen = voice_lines()[before:]
        info = {'speed': speed, 'top': top, 'clip': clip, 'voice_speeds': sorted(x for x in speeds_seen if x is not None),
                'lines': lines_seen, 'battle_seconds': round((done_at or time.monotonic()) - t0, 2)}
        print(json.dumps(info, ensure_ascii=False, indent=1), flush=True)
        return info
    finally:
        try: s.quit()
        except Exception as e: print('quit failed', e)

for sp in [int(x) for x in args.speeds.split(',') if x]:
    summary[sp] = record_one(sp)
(out / 'lines.json').write_text(json.dumps(summary, ensure_ascii=False, indent=1))
print('DONE', *(f"{k}x: {v['clip']['path']} {v['clip']['seconds']}s voice_speeds={v['voice_speeds']}" for k, v in summary.items()), sep='\n  ')
