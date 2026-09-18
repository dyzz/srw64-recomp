#!/usr/bin/env python3
"""Exercise F7 locale switches in a running, isolated first-dialogue QA host.

Requires SRW64_WINDOW_CONTROL=1 and SRW64_STATE_PROBE=1. Uses the real settings
hotkey events, never edits guest memory. The host needs the female intro input fixture.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
import time


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--locale-order', nargs='+', default=['ja', 'zh-Hans', 'en'])
    args = parser.parse_args()
    output = args.output.resolve()
    deadline = time.monotonic() + 180
    sequence = 0

    def read(name: str) -> dict:
        try:
            return json.loads((output / name).read_text())
        except (FileNotFoundError, json.JSONDecodeError):
            return {}

    def wait(predicate):
        while time.monotonic() < deadline:
            value = predicate()
            if value:
                return value
            if (output / 'native-counters.json').exists():
                raise RuntimeError('Host ended before locale verification completed')
            time.sleep(.05)
        raise RuntimeError('Locale verification timed out')

    def command(**values) -> dict:
        nonlocal sequence
        sequence += 1
        temporary = output / 'settings-request.tmp'
        temporary.write_text(json.dumps({'schema': 'srw64.language-hotkey.v1',
            'sequence': sequence, **values}))
        temporary.replace(output / 'language-control.json')
        return wait(lambda: (d if (d := read('language-key.json')).get('sequence') == sequence else None))

    def pulse(button: str) -> None:
        subprocess.run([sys.executable, str(Path(__file__).resolve().parents[1] / "run" / "control_host.py"),
                        str(output), '--buttons', button, '--duration', '3'],
                       check=True, stdout=subprocess.DEVNULL)
        time.sleep(.15)

    state = wait(lambda: (d if (d := read('dialogue-state.json')).get('active') and
                         any(b['text_id'] == 17410 for b in d['boxes']) else None))
    first_event = state['event']
    # Complete one fragment through the normal confirm flow to make a genuine
    # completed history entry. No future script fragments are manufactured.
    for _ in range(10):
        pulse('a')
        state = read('dialogue-state.json')
        if state.get('event') != first_event:
            break
    else:
        raise RuntimeError('Did not reach the next dialogue fragment')
    pulse('l')
    state = wait(lambda: (d if (d := read('dialogue-state.json')).get('history_open') else None))
    event, count = state['event'], state['history_entries']
    assert any(e['complete'] and e['text'] for e in state['history'])
    results = []
    initial_locale = state['locale']
    order = args.locale_order
    start = order.index(initial_locale)
    expected = [order[(start + i + 1) % len(order)] for i in range(2 * len(order))]
    histories = {}
    for index, locale in enumerate(expected):
        previous_request = read('settings-result.json').get('request', 0)
        ui = command()
        assert not ui['sheet_open']
        result = wait(lambda: (d if (d := read('settings-result.json')).get('request', 0) > previous_request else None))
        assert result['saved'] and not result['error'] and result['locale'] == locale, result
        assert read('presentation-settings.json')['locale'] == locale
        state = wait(lambda: (d if (d := read('dialogue-state.json')).get('locale') == locale else None))
        assert state['event'] == event and state['history_entries'] == count
        assert state['history_open'] and not state['automatic'] and not state['skipping']
        histories[locale] = next(e['text'] for e in state['history'] if e['complete'] and e['text'])
        present = wait(lambda: (d if (d := read('dialogue-present.json')).get('locale') == locale and d.get('history_open') else None))
        assert present['reading_event'] == event
        (output / f'locale-{index}-{locale}-reader.json').write_text(json.dumps(state, ensure_ascii=False, indent=2))
        (output / f'locale-{index}-{locale}-present.json').write_text(json.dumps(present, ensure_ascii=False, indent=2))
        frames = wait(lambda: [p for p in output.glob('present-*.json')
                     if read(p.name).get('native_vi_at_draw', 0) >= present['native_vi'] + 120])
        assert read('dialogue-present.json')['locale'] == locale
        frame = max(frames, key=lambda p: int(p.stem.split('-')[1]))
        (output / f'locale-{index}-{locale}.png').write_bytes(frame.with_suffix('.png').read_bytes())
        if index == 0:
            subprocess.run(['screencapture', '-x', '-l', str(ui['window_id']),
                            str(output / f'hotkey-{locale}-window.png')], check=True)
        results.append({'locale': locale, 'event': event, 'history_entries': count,
                        'request': result['request'], 'source_frame': frame.name})
    assert len(set(histories.values())) == len(order), histories
    # Auto-repeat and application shortcuts are not fresh bare F7 presses.
    previous = read('presentation-settings.json')
    request = read('settings-result.json')['request']
    for options in [{'repeat': True}, {'command_modifier': True}]:
        ui=command(**options)
        assert ui['request']==request and not ui['sheet_open']
    assert read('presentation-settings.json') == previous
    assert read('dialogue-state.json')['locale'] == initial_locale
    before = {d['argument']: d for p in output.glob('state-*-locale-before.json') if (d := read(p.name))}
    after = {d['argument']: d for p in output.glob('state-*-locale-after.json') if (d := read(p.name))}
    assert len(before) == len(after) == len(results)
    assert all(before[k]['regions'] == after[k]['regions'] for k in before)
    verification = {'schema': 'srw64.hot-locale-verification.v1', 'switches': results,
        'hotkey': 'F7', 'no_popup': True, 'repeat_and_modified_hotkeys_ignored': True,
        'locale_order': order, 'each_locale_persisted': True,
        'same_fragment_and_history_count': True,
        'completed_history_retranslated': True, 'observed_game_regions_unchanged_during_commit': True,
        'full_game_state_coverage': False, 'gameplay_clock_rule': 'original timing preserved'}
    (output / 'hot-locale-verification.json').write_text(json.dumps(verification, ensure_ascii=False, indent=2))
    subprocess.run([sys.executable, str(Path(__file__).resolve().parents[1] / "run" / "control_host.py"), str(output), '--quit'], check=True)
    print(json.dumps(verification, ensure_ascii=False, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
