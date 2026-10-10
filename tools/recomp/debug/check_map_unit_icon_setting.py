#!/usr/bin/env python3
"""Verify independent map icon selection, global image switching and saved reload.

Uses an isolated debug session; screenshots keep the map in HD while switching
only its unit icons. Pass --binary to test an already built host.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, ROOT, PROFILE, has_endpoint


def mode(session, **expected):
    # The debug endpoint can listen before the graphics thread creates this file.
    end = time.monotonic() + 60
    state = None
    while time.monotonic() < end:
        path = session.run / 'image-mode.json'
        if path.exists():
            state = json.loads(path.read_text())
            if all(state.get(k) == v for k, v in expected.items()):
                return state
        time.sleep(.1)
    raise AssertionError((expected, state))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary')
    args = parser.parse_args()
    binary = str(Path(args.binary).resolve()) if args.binary else None
    s = Session.launch(language='zh-Hans', images='hd', binary=binary, dump_textures=True,
                       mini_stage=str(ROOT / 'config/recomp/mini-stages/move-jump.json'))
    print('RUN', s.run, flush=True)
    checks = []

    def check(name, condition):
        checks.append({'check': name, 'passed': bool(condition)})
        (s.run / 'map-unit-icon-setting-checks.json').write_text(json.dumps(checks, indent=2) + '\n')
        assert condition, name
        print(name, 'PASS', flush=True)

    def key(name, *modifiers):
        s.client.call('ui.key', key=name, modifiers=list(modifiers))
        time.sleep(.3)

    def shot(name):
        time.sleep(1)
        s.client.call('screenshot', path=str(s.run / name))

    restarted = None
    try:
        s.enter_mini_stage()
        initial = mode(s, mode='hd', map_unit_icons='hd')
        check('icon-family-loaded', initial['map_unit_icon_hashes'] > 1000)
        shot('map-hd-icons.png')
        art = s.run.parent / (s.run.name + '.content') / 'art' / 'rt64.json'
        icon_keys = {r['hashes']['rt64'] for r in json.loads(art.read_text())['textures']
                     if r.get('kind') == 'icon'}
        live_keys = {p.name.split('.')[0] for p in (s.run / 'textures').glob('*.tile.json')}
        check('live-map-icons-match-family', len(icon_keys & live_keys) >= 3)
        key(',', 'cmd' if sys.platform == 'darwin' else 'control')
        s.client.call('ui.click', id='map-unit-icons:original')
        mode(s, mode='hd', map_unit_icons='original', map_unit_icons_preference='original')
        shot('settings-original-icons.png')
        settings = s.run / 'presentation-settings.json'
        check('original-choice-saved', json.loads(settings.read_text())['map_unit_icons'] == 'original')
        key('escape')
        shot('map-hd-original-icons.png')
        s.client.call('settings', images='original')
        mode(s, mode='original', map_unit_icons='original')
        s.client.call('settings', images='hd')
        mode(s, mode='hd', map_unit_icons='original')
        check('global-toggle-keeps-icon-choice', True)
        key(',', 'cmd' if sys.platform == 'darwin' else 'control')
        s.client.call('ui.click', id='map-unit-icons:hd')
        mode(s, mode='hd', map_unit_icons='hd')
        key('escape')
        shot('map-hd-icons-restored.png')
        check('hd-choice-saved', json.loads(settings.read_text())['map_unit_icons'] == 'hd')
        key(',', 'cmd' if sys.platform == 'darwin' else 'control')
        s.client.call('ui.click', id='map-unit-icons:original')
        mode(s, mode='hd', map_unit_icons='original')
        check('clean-exit', s.quit().get('exit_code') == 0)

        # Load the exact file written through the UI in a second host process.
        run = s.run / 'reload'
        command = [sys.executable, str(ROOT / 'tools/recomp/run/run_host_probe.py'),
                   '--graphics', '--interactive', '--profile', str(PROFILE),
                   '--language', 'zh-Hans', '--images', 'hd', '--output', str(run),
                   '--presentation-settings', str(settings), '--binary',
                   binary or str(ROOT / 'build/recomp/gfx-build/srw64-gfx-host')]
        with (s.run / 'reload.log').open('w') as log:
            process = subprocess.Popen(command, cwd=ROOT, env=dict(os.environ, SRW64_DEBUG='1'),
                                       stdout=log, stderr=subprocess.STDOUT)
        restarted = Session(run, process)
        end = time.monotonic() + 180
        while not has_endpoint(run):
            assert process.poll() is None, 'reload host exited; see reload.log'
            assert time.monotonic() < end, 'reload endpoint timed out'
            time.sleep(.2)
        mode(restarted, mode='hd', map_unit_icons='original', map_unit_icons_preference='original')
        check('saved-original-choice-reloaded', True)
        restarted.wait(vi=60, timeout=60)
        check('reload-clean-exit', restarted.quit().get('exit_code') == 0)
    finally:
        if s.process.poll() is None:
            s.quit()
        if restarted and restarted.process.poll() is None:
            restarted.quit()
    print('DONE', s.run, flush=True)


if __name__ == '__main__':
    main()
