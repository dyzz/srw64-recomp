#!/usr/bin/env python3
"""Drive a fresh, isolated SRW64_DEBUG=1 game through the SDL/RmlUi frontend.

Uses real semantic actions and GPU screenshots. Synthetic composition exercises
SDL_TEXTEDITING, not the desktop input method's candidate window. Never attach
this to a player's session: it starts a new game and edits character names.
"""
import argparse
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Client


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', type=Path, required=True)
    args = parser.parse_args()
    run = args.run.resolve()
    client = Client(run / 'debug.sock')
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
        def find(node):
            if isinstance(node, dict):
                if node.get('id') == id:
                    return node.get('text', '')
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

    def edit(text):
        click('field0')
        client.call('ui.key', key='a', modifiers=['cmd' if sys.platform == 'darwin' else 'control'])
        client.call('ui.type', text=text)

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
    state = wait(lambda s: s['name_page']['active'] and s['name_page']['person'] == 0)
    serial = state['name_page']['serial']
    edit('🙂'); click('next')
    assert client.call('status')['name_page']['serial'] == serial
    assert field('field0') == '🙂'
    checks.append('unencodable name remains on the editor')
    edit('マナミ')
    client.call('ui.key', key='a', modifiers=['cmd' if sys.platform == 'darwin' else 'control'])
    client.call('ui.type', text='ナナ', marked=True)
    before = client.call('status')['locale']
    client.call('ui.key', key='f7')
    assert client.call('status')['locale'] == before
    client.call('ui.key', key='return')
    assert client.call('status')['name_page']['serial'] == serial
    client.call('ui.type', text='ナナ')
    assert field('field0') == 'ナナ'
    checks.append('IME preedit owns Return and F7; commit preserves UTF-8')
    client.call('ui.key', key='f7')
    wait(lambda s: s['locale'] != before)
    assert field('field0') == 'ナナ'
    checks.append('locale rebuild preserves edited names')
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
    assert field('field0') == 'ナナ'
    client.call('ui.key', key=',', modifiers=['cmd' if sys.platform == 'darwin' else 'control'])
    wait(lambda s: s['ui']['input_owners']['settings'])
    client.call('ui.key', key='esc')
    wait(lambda s: not s['ui']['input_owners']['settings'])
    assert field('field0') == 'ナナ'
    checks.append('Ctrl/Cmd+comma opens settings and Escape returns to the edited name')
    client.call('window', width=800, height=600)
    window_points = (800, 600)
    time.sleep(.3)
    shots.append(shot('shared-small'))
    client.call('window', width=1100, height=760)
    window_points = (1100, 760)
    time.sleep(.3)
    click('next')
    wait(lambda s: s['name_page']['active'] and s['name_page']['person'] == 1)
    shots.append(shot('shared-partner'))
    click('next')
    wait(lambda s: s['name_page']['active'] and s['name_page']['person'] == 2)
    shots.append(shot('shared-review'))
    client.call('keys', down='return')
    wait(lambda s: not s['name_page']['visible'])
    assert client.call('status')['ui']['input_owners']['names']
    client.call('keys', up='return')
    wait(lambda s: not s['ui']['input_owners']['names'])
    checks.append('story confirmation retains modal ownership until Return is released')
    checks.append('real game accepts player, partner and review; returns to game')
    events = [json.loads(line) for line in (run / 'name-entry-events.jsonl').read_text().splitlines()]
    assert any(e['kind'] == 'committed' and e['person'] == 0 and e['values'][0] == 'ナナ' for e in events)
    assert any(e['kind'] == 'started' for e in events)
    checks.append('guest writeback log contains the edited name and story start')
    state = wait(lambda s: s['intro']['step'].get('active', False))
    skips = state['intro']['skips']
    press('e+return')
    wait(lambda s: s['intro']['skips'] > skips)
    checks.append('game keyboard resumes after the modal closes: route intro skip accepted')
    checks.append('GPU captures match UI display pixels at initial and resized window sizes')
    state = client.call('status')
    (run / 'shared-ui-verification.json').write_text(json.dumps({
        'schema': 'srw64.shared-ui-verification.v1', 'status': 'passed',
        'scope': 'live macOS SDL/RmlUi game adapters and GPU readback; synthetic IME only',
        'checks': checks, 'screenshots': shots, 'final': state,
    }, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({'passed': checks, 'run': str(run)}, ensure_ascii=False))


if __name__ == '__main__':
    main()
