#!/usr/bin/env python3
"""Switch the pre-battle confirmation between its three forms: the original screen
(translated text, original images), the redesigned page and the HD original (the
original layout redrawn natively, docs/native/native-battle-ui.md). C-down toggles the
animation on each; the choice persists. The HD original is followed into the enemy
phase: its response menu, 防御 redrawing the panels, and B opening the weapon list."""
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session


def main():
    s = Session.launch(language='zh-Hans', rules='fixed', images='hd',
                       mini_stage='config/recomp/mini-stages/battle-ui-skills.json')
    print('RUN', s.run, flush=True)
    settings = s.run / 'presentation-settings.json'  # run_host_probe's default location
    checks = []

    def page():
        return s.client.call('status')['battle_page']

    def focus():
        return (s.client.call('status')['ui']['focus'] or {}).get('id', '')

    def check(name, passed):
        checks.append({'check': name, 'passed': bool(passed), 'page': page()})
        (s.run / 'switch-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2) + '\n')
        assert passed, name
        print(name, 'PASS', flush=True)

    def buttons(*values):
        for value in values:
            s.client.call('buttons', buttons=value, vis=6)
            time.sleep(.7)

    def wait(predicate, timeout=30):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            p = page()
            if predicate(p):
                return p
            time.sleep(.1)
        s.client.call('screenshot', path=str(s.run / 'switch-timeout.png'))
        raise AssertionError('timeout')

    def choose(ui):
        s.client.call('settings', battle_ui=ui)
        time.sleep(.3)
        saved = json.loads(settings.read_text())
        check(f'{ui}-persisted', saved['battle_ui'] == ui and saved['locale'] == 'zh-Hans')

    def toggle_animation(p, shown, name):
        animation = p['animation']
        buttons('c_down')
        wait(lambda p: p.get(shown) and p['animation'] != animation)
        check(f'{name}-cdown-toggles-animation', True)
        buttons('c_down')
        wait(lambda p: p.get(shown) and p['animation'] == animation)

    def attack():  # after backing out of a confirmation: weapon, target, confirm
        buttons('a', 'a', 'left', 'a')

    s.enter_mini_stage()

    # The original screen: no native page; its images are the original ones.
    choose('original')
    buttons('down', 'down', 'right', 'right', 'a', 'down', 'a', 'a', 'left', 'a')
    p = wait(lambda p: p.get('original'))
    check('original-screen-no-native-page', not p.get('visible') and p.get('original_images'))
    s.client.call('screenshot', path=str(s.run / 'original-confirm.png'))
    toggle_animation(p, 'original', 'original')
    buttons('b', 'b')  # back to target selection, cancel
    time.sleep(.5)
    check('original-screen-closed', not page().get('original') and not page().get('original_images'))

    # The redesigned page.
    choose('native')
    attack()
    p = wait(lambda p: p.get('visible'))
    check('native-page', p['mode'] == 1 and p['style'] == 'native' and not p.get('original_images'))
    s.client.call('screenshot', path=str(s.run / 'native-confirm.png'))
    toggle_animation(p, 'visible', 'native')
    buttons('b', 'b')
    time.sleep(.5)

    # The HD original: a native page in the original layout; B goes back as there.
    choose('hd')
    attack()
    p = wait(lambda p: p.get('visible'))
    check('hd-page', p['mode'] == 1 and p['style'] == 'hd' and not p.get('original_images'))
    tree = s.client.call('ui.tree')
    (s.run / 'hd-confirm-tree.json').write_text(json.dumps(tree, ensure_ascii=False, indent=2))
    s.client.call('screenshot', path=str(s.run / 'hd-confirm.png'))
    toggle_animation(p, 'visible', 'hd')
    buttons('b')
    wait(lambda p: not p.get('visible'))
    check('hd-back', page().get('action') == 'back')
    buttons('b')
    time.sleep(.5)

    # Start a battle from it (animation off), then end the turn for an enemy attack.
    attack()
    p = wait(lambda p: p.get('visible') and p['style'] == 'hd')
    if p['animation']:
        buttons('c_down')
        p = wait(lambda p: p.get('visible') and not p['animation'])
    before = s.client.call('status')['vi']
    buttons('a')
    check('hd-a-starts', page().get('action') == 'confirm')
    s.wait(vi=before + 600, timeout=60)
    buttons('right', 'right', 'right', 'right', 'a', 'a', 'a')
    p = wait(lambda p: p.get('visible') and p['mode'] == 2, timeout=120)
    check('hd-menu', p['style'] == 'hd' and focus() == 'battle-confirm')
    s.client.call('screenshot', path=str(s.run / 'hd-menu.png'))
    buttons('down', 'down', 'down')
    check('hd-menu-cursor', focus() == 'battle-defend')
    buttons('a')
    q = wait(lambda q: q.get('visible') and q['serial'] > p['serial'])
    check('hd-defend-redraws', q['mode'] == 2 and q['response'] == 2 and q['defender']['weapon'] == -1 and
          focus() == 'battle-confirm')
    s.client.call('screenshot', path=str(s.run / 'hd-defend.png'))
    buttons('b')
    check('hd-b-weapon-list', not page()['visible'] and page().get('action') == 'weapon')
    # The original list rejects out-of-range weapons; walk down until one is accepted.
    r = None
    for _ in range(4):
        buttons('a')
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline and not (page().get('visible') and page()['serial'] > q['serial']):
            time.sleep(.1)
        if page().get('visible') and page()['serial'] > q['serial']:
            r = page()
            break
        buttons('down')
    check('hd-counter-weapon', r and r['style'] == 'hd' and r['response'] == 0 and r['defender']['weapon'] >= 0)
    s.client.call('screenshot', path=str(s.run / 'hd-counter.png'))
    print('DONE', s.run, flush=True)
    exit_code = s.quit().get('exit_code')
    print('EXIT', exit_code, flush=True)
    assert exit_code == 0, f'Native host exit: {exit_code}'


if __name__ == '__main__':
    main()
