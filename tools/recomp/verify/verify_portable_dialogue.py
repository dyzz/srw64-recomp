#!/usr/bin/env python3
"""Check the portable dialogue path in an isolated, already active debug game.

Start a muted test game, reach a dialogue (verify_shared_ui.py can do this), then
run this script. It changes locale/font/history and advances one host page; do
not attach it to a player's session. Screenshots are actual GPU readback.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Client


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', type=Path, required=True)
    args = parser.parse_args()
    run = args.run.resolve()
    client = Client(run / 'debug.sock')
    records: list[dict] = []

    def wait(predicate, timeout: float = 20) -> dict:
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            state = client.call('status')
            if predicate(state):
                return state
            time.sleep(.05)
        raise AssertionError('Dialogue condition timed out: ' + json.dumps(state, ensure_ascii=False))

    def press(key: str) -> None:
        client.call('keys', down=key)
        time.sleep(.12)
        client.call('keys', up=key)
        time.sleep(.12)

    def capture(label: str) -> None:
        # Allow the immutable dialogue frame to reach GPU presentation.
        time.sleep(.15)
        shot = client.call('screenshot', path=str(run / (label + '.png')))
        records.append({'case': label, 'screenshot': shot, 'state': client.call('status')['dialogue']})

    initial = wait(lambda s: s.get('dialogue', {}).get('active'))['dialogue']
    event = initial['event']
    if initial['automatic'] or initial['skipping'] or initial['history_open']:
        raise AssertionError('Start on a manual dialogue with history closed')
    for locale in ('zh-Hans', 'ja', 'en'):
        client.call('settings', locale=locale)
        state = wait(lambda s: s['locale'] == locale and s['dialogue']['locale'] == locale)
        assert state['dialogue']['event'] == event, 'Locale switch advanced the script'
        wait(lambda s: s['dialogue']['revealed_utf16'] > 4)
        capture('portable-' + locale)
        for size in (10, 18, 13):
            current = client.call('status')['dialogue']['font_size']
            for _ in range(abs(size - current)):
                press('i' if size > current else 'k')
            state = wait(lambda s: s['dialogue']['font_size'] == size)['dialogue']
            assert state['event'] == event and state['page'] < state['pages']
            capture(f'portable-{locale}-{size}')
        press('q')
        wait(lambda s: s['dialogue']['history_open'])
        capture('portable-history-' + locale)
        press('q')
        wait(lambda s: not s['dialogue']['history_open'])
        assert client.call('status')['dialogue']['event'] == event
    for width, height in ((800, 600), (1100, 760)):
        client.call('window', width=width, height=height)
        time.sleep(.3)
        capture(f'portable-window-{width}')
    state = client.call('status')['dialogue']
    assert state['pages'] > 1 and state['page'] == 0, 'Use the first multiline dialogue'
    press('z')
    wait(lambda s: s['dialogue']['page'] == 1 and s['dialogue']['event'] == event)
    capture('portable-next-page')
    result = {'schema': 'srw64.portable-dialogue-verification.v1', 'status': 'passed',
              'scope': 'macOS real game, portable CJK/English text, Plume GPU readback; not Windows/Linux gameplay',
              'records': records}
    (run / 'portable-dialogue-verification.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({'status': 'passed', 'captures': len(records), 'run': str(run)}))


if __name__ == '__main__':
    main()
