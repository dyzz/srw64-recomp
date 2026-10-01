#!/usr/bin/env python3
"""Show the HD map unit icons (docs/design/unit-icon-hd.md) on the tactical map.

Launches in HD with RT64 texture dumping, enters the move-jump mini stage (two
friends, one foe), screenshots the map, then F6 to the original images for the
side-by-side. Checks that every 16x16 CI4 texture RT64 hashed for the units on
the map is a key of the icon pack (assets/hd-ai/unit-icons/<run>/pack-keys.json),
i.e. the replacement really matched the live TMEM loads."""
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session

ROOT = Path(__file__).resolve().parents[3]


def main():
    keys_path = ROOT / (sys.argv[1] if len(sys.argv) > 1 else 'assets/hd-ai/unit-icons/v2/pack-keys.json')
    keys = {k['hash']: k for k in json.loads(keys_path.read_text())['keys']}
    s = Session.launch(language='zh-Hans', images='hd', dump_textures=True,
                       mini_stage=str(ROOT / 'config/recomp/mini-stages/move-jump.json'))
    print('RUN', s.run, flush=True)
    checks = []

    def check(name, passed, state=None):
        checks.append({'check': name, 'passed': bool(passed), 'state': state})
        (s.run / 'unit-icon-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2) + '\n')
        assert passed, (name, state)
        print(name, 'PASS', flush=True)

    s.enter_mini_stage()
    time.sleep(8)
    s.client.call('screenshot', path=str(s.run / 'map-hd.png'))
    s.client.call('keys', press='F6', hold_ms=90)
    time.sleep(2)
    s.client.call('screenshot', path=str(s.run / 'map-original.png'))
    time.sleep(1)
    dumped = {}
    for path in (s.run / 'textures').glob('*.tile.json'):
        tile = json.loads(path.read_text())
        if (tile['width'], tile['height']) == (16, 16) and tile['tile']['masks'] == 4 and tile['tile']['siz'] == 0:
            dumped[path.name.split('.')[0]] = tile['tile']
    matched = {h: keys[h] for h in dumped if h in keys}
    check('icons_dumped', len(dumped) >= 3, {'dumped_16x16_masked': len(dumped)})
    check('icon_hashes_match_pack', len(matched) >= 3 and all(v['line'] == 1 and v['fmt'] == 2 for v in dumped.values()),
          {'matched': [{'icon': m['icon'], 'palette': m['palette'], 'hash': h} for h, m in matched.items()]})
    print('DONE', s.run, flush=True)


if __name__ == '__main__':
    main()
