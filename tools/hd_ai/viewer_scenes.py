"""The battle viewer's scene pictures from the game itself, in HD: one battle per scene of
its list (cutin_hd.VIEWER_SCENES) in the battle-ui mini stage (three battles a session), the scene forced by writing
both sides' terrain and environment bytes (800F97EA/B), the battle's windows, units and
effects hidden (SRW64_DEV_HIDE_BATTLE_HUD / _SPRITES), and the camera held at rest between
the units (pitch 4.8, distance 260, yaw 0, at.x 0) while the attack plays (camera state 5),
16:9. Writes OUT/scene-<key>.jpg (960x540) and a contact sheet; then

    .venv/bin/python -m tools.hd_ai.cutin_hd scenes --shots OUT --into assets/hd-ai/cutins/pack-<n> --bind

    .venv/bin/python -m tools.hd_ai.viewer_scenes OUT [--only key ...]
"""
from __future__ import annotations

import struct
import sys
import time
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
from recomp.debug.session import Session  # noqa: E402
from tools.hd_ai.cutin_hd import VIEWER_SCENES  # noqa: E402

SIZE = (960, 540)
CAMERA = {0x8015DE08: 4.8, 0x8015DE2C: 260.0, 0x8015DE0C: 0.0, 0x80250228: 0.0}


def capture(out: Path, todo: list) -> None:
    s = Session.launch(language='zh-Hans', images='hd', binary=str(ROOT / 'build/recomp/gfx-build/srw64-gfx-host'),
                       mini_stage=str(ROOT / 'config/recomp/mini-stages/battle-ui.json'),
                       env={'SRW64_DEV_HIDE_BATTLE_HUD': '1', 'SRW64_DEV_HIDE_BATTLE_SPRITES': '1'})
    status = lambda: s.client.call('status')
    read = lambda a: s.client.call('memory.read', address=a, size=1)['hex']
    try:
        s.client.call('window', width=1280, height=720)
        s.wait(vi=600, timeout=300)
        s.enter_mini_stage(); time.sleep(4)
        for key in ['up', 'z', 'z', 'z']:
            s.client.call('keys', press=key, hold_ms=100); time.sleep(0.8)
        for key, env, terrain in todo:
            end = time.monotonic() + 90
            while not status()['battle_page'].get('visible'):
                if time.monotonic() > end: raise SystemExit('no battle page')
                time.sleep(0.2)
            time.sleep(0.8)
            if not status()['battle_page'].get('animation', False):
                s.client.call('ui.click', id='battle-animation'); time.sleep(0.3)
            for side in (0, 1):
                s.client.call('memory.write', address=0x800F97EA + side * 0x1074, hex=f'{terrain:02X}{env:02X}')
            s.client.call('ui.click', id='battle-confirm')
            end = time.monotonic() + 15        # the state stays 5 from the last battle: wait for it to leave
            while read(0x80178D30) == '05' and time.monotonic() < end: time.sleep(0.03)
            end = time.monotonic() + 30
            while not (read(0x80178D30) == '05' and read(0x8015DA02) == '02'):
                if time.monotonic() > end: raise SystemExit(f'{key}: the attack never came')
                time.sleep(0.03)
            for address, value in CAMERA.items():
                s.client.call('memory.write', address=address, hex=struct.pack('>f', value).hex())
            time.sleep(0.4)
            shot = out / f'{key}.png'
            s.client.call('screenshot', path=str(shot))
            Image.open(shot).convert('RGB').resize(SIZE, Image.LANCZOS).save(out / f'scene-{key}.jpg', quality=90)
            shot.unlink()
            print('scene', key, flush=True)
            s.client.call('keys', down='z'); time.sleep(0.1)
            s.client.call('keys', press='return', hold_ms=150); time.sleep(0.3)
            s.client.call('keys', release_all=True)
    finally:
        s.quit()


def main() -> None:
    out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
    only = set(sys.argv[sys.argv.index('--only') + 1:]) if '--only' in sys.argv else None
    todo = [v for v in VIEWER_SCENES if (only is None or v[0] in only) and not (out / f'scene-{v[0]}.jpg').exists()]
    # The battle-ui stage has three battles in it: a session each three scenes.
    for start in range(0, len(todo), 3):
        capture(out, todo[start:start + 3])
    keys = [k for k, _, _ in VIEWER_SCENES if (out / f'scene-{k}.jpg').exists()]
    tw, th, cols = 320, 180, 4
    sheet = Image.new('RGB', (cols * tw, ((len(keys) + cols - 1) // cols) * (th + 16)), 'white')
    draw = ImageDraw.Draw(sheet)
    for i, k in enumerate(keys):
        x, y = (i % cols) * tw, (i // cols) * (th + 16)
        sheet.paste(Image.open(out / f'scene-{k}.jpg').resize((tw, th)), (x, y + 16)); draw.text((x + 2, y + 2), k, fill='black')
    sheet.save(out / 'sheet.png')


if __name__ == '__main__':
    main()
