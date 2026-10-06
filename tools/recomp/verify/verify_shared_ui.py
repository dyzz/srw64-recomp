#!/usr/bin/env python3
"""Drive a fresh, isolated SRW64_DEBUG=1 game through the SDL/RmlUi frontend.

Uses real semantic actions and GPU screenshots. Never attach this to a player's
session: it starts a new game.
"""
import argparse
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Client


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', type=Path, required=True)
    args = parser.parse_args()
    run = args.run.resolve()
    client = Client(run)
    checks = []
    window_points = (960, 720)

    def wait(predicate, timeout=20):
        until = time.monotonic() + timeout
        while time.monotonic() < until:
            state = client.call('status')
            if predicate(state):
                return state
            time.sleep(.1)
        raise AssertionError('Timed out: ' + json.dumps(state, ensure_ascii=False))

    def press(key):
        client.call('keys', down=key)
        time.sleep(.15)
        client.call('keys', up=key)
        time.sleep(.15)

    def click(id):
        client.call('ui.click', text=id)

    def shot(name):
        capture = client.call('screenshot', path=str(run / (name + '.png')))
        scale = client.call('ui.tree')['windows'][0]['scale']
        expected = tuple(round(size * scale) for size in window_points)
        actual = (capture['width'], capture['height'])
        assert actual == expected, f'UI/display pixel mismatch: {actual} != {expected}'
        return {**capture, 'window_points': window_points, 'display_scale': scale}

    def field(id):
        """The text an element shows: its own, or its text nodes'."""
        def text(node):
            return node['text'] if 'text' in node else ''.join(text(c) for c in node.get('children', []))

        def find(node):
            if isinstance(node, dict):
                if node.get('id') == id:
                    return text(node)
                for value in node.values():
                    result = find(value)
                    if result is not None:
                        return result
            elif isinstance(node, list):
                for value in node:
                    result = find(value)
                    if result is not None:
                        return result
            return None
        return find(client.call('ui.tree'))

    # Manami's route: the review shows both default names in the reading language
    # (docs/native/default-names.md), rebuilt at once by a language switch.
    terms = {l: json.loads((ROOT / f'content/locales/terms/{l}.json').read_text())['sections']['default_names']
             for l in ('zh-Hans', 'en')}
    separator = {'ja': '・', 'zh-Hans': '·', 'en': ' '}

    def expected(locale, full, nick):
        if locale == 'ja':
            return f'{full} / {nick}'
        given, family = terms[locale][full].split(separator[locale])
        return f'{given}{separator[locale]}{family} / {terms[locale][nick]}'

    def review_names():
        locale = client.call('status')['locale']
        return locale, field('name0'), field('name1')

    # Intro is skipped through the game's existing control path.
    wait(lambda s: s['intro']['loaded'] and s['vi'] >= 600)
    for _ in range(5):
        if client.call('status')['intro'].get('step', {}).get('active'):
            break
        press('return'); press('z'); time.sleep(.6)
    wait(lambda s: s['intro']['step']['active'])
    press('e+return')
    wait(lambda s: s['name_page']['active'] and s['name_page']['person'] == 3)
    shots = [shot('shared-select')]
    click('route1'); click('next')
    state = wait(lambda s: s['name_page']['active'] and s['name_page']['person'] == 2)
    serial = state['name_page']['serial']
    seen = {}
    for _ in range(3):
        locale, protagonist, partner = review_names()
        seen[locale] = [protagonist, partner]
        assert protagonist == expected(locale, 'マナミ・ハミル', 'マナミ'), (locale, protagonist)
        assert partner == expected(locale, 'アイシャ・リッジモンド', 'アイシャ'), (locale, partner)
        client.call('ui.key', key='f7')
        wait(lambda s: s['locale'] != locale)
    assert len(seen) == 3 and client.call('status')['name_page']['serial'] == serial
    checks.append('selection commits the defaults; the review shows them in each language: ' + json.dumps(seen, ensure_ascii=False))
    assert 'settings-open' not in json.dumps(client.call('ui.tree')), 'Options must not overlay the game'
    menu = client.call('menu')
    if sys.platform == 'darwin':
        assert menu['native_menu'], 'Application menu entry is missing'
    client.call('menu', path=[menu['settings']])
    wait(lambda s: s['ui']['input_owners']['settings'])
    checks.append('application menu opens shared settings; no persistent Options button')
    click('preset:rules_defaults')
    wait(lambda s: 'esp-level' in s['rules'])
    shots.append(shot('shared-settings'))
    client.call('keys', down='w')
    click('settings-close')
    assert client.call('status')['ui']['input_owners']['settings']
    client.call('keys', up='w')
    wait(lambda s: not s['ui']['input_owners']['settings'])
    checks.append('settings close waits for held analog-direction keys to release')
    client.call('ui.key', key=',', modifiers=['cmd' if sys.platform == 'darwin' else 'control'])
    wait(lambda s: s['ui']['input_owners']['settings'])
    client.call('ui.key', key='esc')
    wait(lambda s: not s['ui']['input_owners']['settings'])
    assert client.call('status')['name_page']['serial'] == serial
    checks.append('Ctrl/Cmd+comma opens settings and Escape returns to the review')
    client.call('window', width=800, height=600)
    window_points = (800, 600)
    time.sleep(.3)
    shots.append(shot('shared-small'))
    client.call('window', width=1100, height=760)
    window_points = (1100, 760)
    time.sleep(.3)
    shots.append(shot('shared-review'))
    client.call('ui.key', key='esc')
    wait(lambda s: s['name_page']['active'] and s['name_page']['person'] == 3)
    checks.append('Escape on the review returns to the selection')
    client.call('ui.key', key='return')
    wait(lambda s: s['name_page']['active'] and s['name_page']['person'] == 2)
    client.call('keys', down='return')
    wait(lambda s: not s['name_page']['visible'])
    assert client.call('status')['ui']['input_owners']['names']
    client.call('keys', up='return')
    wait(lambda s: not s['ui']['input_owners']['names'])
    checks.append('story confirmation retains modal ownership until Return is released')
    events = [json.loads(line) for line in (run / 'name-entry-events.jsonl').read_text().splitlines()]
    kinds = [e['kind'] for e in events]
    assert kinds.count('selected') == 2 and 'back' in kinds and kinds[-1] == 'started', kinds
    assert not any(e['kind'] == 'defaults-rejected' for e in events)
    checks.append('guest log: defaults committed on selection, back, then story start')
    state = wait(lambda s: s['intro']['step'].get('active', False))
    skips = state['intro']['skips']
    press('e+return')
    wait(lambda s: s['intro']['skips'] > skips)
    checks.append('game keyboard resumes after the modal closes: route intro skip accepted')
    checks.append('GPU captures match UI display pixels at initial and resized window sizes')
    state = client.call('status')
    (run / 'shared-ui-verification.json').write_text(json.dumps({
        'schema': 'srw64.shared-ui-verification.v1', 'status': 'passed',
        'scope': 'live macOS SDL/RmlUi game adapters and GPU readback',
        'checks': checks, 'screenshots': shots, 'final': state,
    }, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({'passed': checks, 'run': str(run)}, ensure_ascii=False))


if __name__ == '__main__':
    main()
