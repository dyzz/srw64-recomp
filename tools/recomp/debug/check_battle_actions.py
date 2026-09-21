#!/usr/bin/env python3
"""Current native build: attack weapons, paid spirits, counter spirits and mirror layout."""
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session


def nodes(value):
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from nodes(child)
    elif isinstance(value, list):
        for child in value:
            yield from nodes(child)


def main():
    s = Session.launch(language='zh-Hans', rules='fixed',
                       mini_stage='config/recomp/mini-stages/battle-ui-skills.json')
    print('RUN', s.run, flush=True)
    checks = []

    def page():
        return s.client.call('status')['battle_page']

    def wait_page(serial=0):
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            p = page()
            if p.get('visible') and p['serial'] > serial:
                return p
            time.sleep(.1)
        s.client.call('screenshot', path=str(s.run / 'actions-timeout.png'))
        raise AssertionError('Battle page timeout')

    def click(action):
        s.client.call('ui.click', id='battle-' + action)
        time.sleep(.4)

    def buttons(*values):
        for value in values:
            s.client.call('buttons', buttons=value, vis=6)
            time.sleep(.7)

    def check(name, passed):
        checks.append({'check': name, 'passed': bool(passed), 'page': page()})
        (s.run / 'action-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2) + '\n')
        assert passed, name
        print(name, 'PASS', flush=True)

    def cast(spirit):
        before = page()
        option = next(o for o in before['spirit_options'] if o['id'] == spirit and o['enabled'])
        if not before.get('spirit_menu'):
            click('spirits')
        click(f"cast:{option['crew']}:{option['slot']}")
        after = wait_page(before['serial'])
        remaining = next(o['sp'] for o in after['spirit_options'] if o['crew'] == option['crew'])
        check('sp-cost-' + str(spirit), remaining == option['sp'] - option['cost'])
        return after

    s.enter_mini_stage()
    buttons('down', 'down', 'right', 'right', 'a', 'down', 'a', 'a', 'left', 'a')
    p = wait_page()
    check('attack-entry', p['mode'] == 1 and p['attacker']['weapon'] == 677)
    click('spirits')
    for _ in range(8):
        s.client.call('ui.key', key='tab')
        focus = (s.client.call('status')['ui']['focus'] or {}).get('id', '')
        assert focus == 'battle-spirit-back' or focus.startswith('battle-cast:'), focus
    check('spirit-keyboard-focus-contained', page()['spirit_menu'])
    s.client.call('screenshot', path=str(s.run / 'spirits-menu.png'))
    s.client.call('ui.key', key='escape')
    time.sleep(.4)
    check('spirit-cancel-keeps-battle', page()['visible'] and not page()['spirit_menu'] and page()['attacker']['sp'] == 83)
    p = cast(7)
    check('sure-hit-refresh', p['attacker']['sp'] == 58 and p['attacker']['hit'] == 100 and p['attacker']['spirits'] & 128)
    check('no-repeat-sure-hit', not next(o for o in p['spirit_options'] if o['id'] == 7)['enabled'])
    p = cast(13)
    check('heal-refreshes-potential', p['attacker']['hp'] == 3000 and not p['attacker']['pilot_effects'][0]['active'])
    check('sp-shortage-disabled', not next(o for o in p['spirit_options'] if o['id'] == 11)['enabled'])
    click('spirit-back')
    for language, width, height in [('en', 800, 600), ('ja', 1100, 760), ('zh-Hans', 960, 720)]:
        s.client.call('settings', locale=language)
        s.client.call('window', width=width, height=height)
        time.sleep(.4)
        tree = s.client.call('ui.tree')
        by_id = {n['id']: n for n in nodes(tree) if n.get('id')}
        left, right = [by_id['battle-card-' + side]['frame'] for side in ['left', 'right']]
        confirm = by_id['battle-confirm']['frame']
        pilots = [by_id['battle-pilot-' + side]['frame'] for side in ['left', 'right']]
        # Banners and pilot panels mirror each other; the confirm button sits
        # between the pilot panels, below the banners.
        check('mirror-layout-' + language, all(abs(a[i] - b[i]) < 1 for a, b in [(left, right), pilots]
              for i in [1, 2, 3]) and left[1] + left[3] < confirm[1] and
              confirm[1] + confirm[3] <= pilots[0][1] + pilots[0][3] and
              pilots[0][1] + pilots[0][3] <= height)
        (s.run / ('actions-tree-' + language + '.json')).write_text(json.dumps(tree, ensure_ascii=False, indent=2))
        s.client.call('screenshot', path=str(s.run / ('actions-' + language + '.png')))
    before = page()
    click('weapon')
    check('attack-weapon-picker', not page()['visible'])
    s.client.call('screenshot', path=str(s.run / 'attack-weapon-picker.png'))
    buttons('down', 'a', 'left', 'a')
    p = wait_page(before['serial'])
    check('attack-weapon-changed', p['attacker']['weapon'] == 678 and p['attacker']['sp'] == 18)
    if p['animation']:
        click('animation')
    before_vi = s.client.call('status')['vi']
    click('confirm')
    s.wait(vi=before_vi + 600, timeout=60)
    # Move beyond all deployed units before opening the map's end-phase menu.
    buttons('right', 'right', 'right', 'right', 'a', 'a', 'a')
    # Which enemy attacks whom first depends on the RNG state at stage entry, so
    # each defender's checks run whenever that defender comes up.
    pending = {181, 165}
    for _ in range(6):
        p = wait_page(p['serial'])
        check('counter-entry-' + str(p['defender']['pilot']), p['mode'] == 2)
        pilot = p['defender']['pilot']
        if pilot == 181 and pilot in pending:
            p = cast(2)
            check('counter-cast-keeps-weapon', p['mode'] == 2 and p['response'] == 0 and p['defender']['weapon'] >= 0 and
                  p['defender']['sp'] == 52 and p['defender']['spirits'] & 4)
            check('copilot-sp-unchanged', next(o for o in p['spirit_options'] if o['crew'] == 1)['sp'] == 32)
            click('spirit-back')
        elif pilot == 165 and pilot in pending:
            click('evade')
            p = wait_page(p['serial'])
            p = cast(5)
            check('flash-keeps-evade-response', p['response'] == 1 and p['defender']['weapon'] == -1 and
                  p['defender']['sp'] == 3 and p['attacker']['hit'] == 0)
            click('spirit-back')
            s.client.call('screenshot', path=str(s.run / 'counter-flash.png'))
            click('counter')
            check('counter-segment-opens-weapons', not page()['visible'])
            # The original list rejects out-of-range weapons; walk down until one is accepted.
            q = None
            for _ in range(4):
                buttons('a')
                deadline = time.monotonic() + 3
                while time.monotonic() < deadline and not (page().get('visible') and page()['serial'] > p['serial']):
                    time.sleep(.1)
                if page().get('visible') and page()['serial'] > p['serial']:
                    q = page()
                    break
                buttons('down')
            check('counter-weapon-after-spirit', q and q['response'] == 0 and q['defender']['weapon'] >= 0 and
                  q['defender']['sp'] == 3 and q['attacker']['hit'] == 0)
            s.client.call('screenshot', path=str(s.run / 'counter-ready.png'))
            p = q
        pending.discard(pilot)
        if not pending:
            break
        click('confirm')
    check('both-defenders-seen', not pending)
    probe = s.run / 'battle-ui-probe.json'
    if probe.exists():
        data = json.loads(probe.read_text())
        check('original-function-probe', data['passed'] and data['gameplay_and_rng_unchanged'] and
              len(data['skills']) == 2400 and all(row['match'] for row in data['skills']))
    print('DONE', s.run, flush=True)
    exit_code = s.quit().get('exit_code')
    print('EXIT', exit_code, flush=True)
    assert exit_code == 0, f'Native host exit: {exit_code}'


if __name__ == '__main__':
    main()
