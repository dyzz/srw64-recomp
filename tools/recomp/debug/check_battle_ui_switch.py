#!/usr/bin/env python3
"""Switch between the native pre-battle page and the original HUD; toggle the
animation on the original HUD with C-down; the choice persists."""
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session


def main():
    s = Session.launch(language='zh-Hans', rules='fixed',
                       mini_stage='config/recomp/mini-stages/battle-ui-skills.json')
    print('RUN', s.run, flush=True)
    settings = s.run / 'presentation-settings.json'  # run_host_probe's default location
    checks = []

    def page():
        return s.client.call('status')['battle_page']

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

    s.enter_mini_stage()
    s.client.call('settings', battle_ui='original')
    time.sleep(.3)
    saved = json.loads(settings.read_text())
    check('setting-persisted', saved['battle_ui'] == 'original' and saved['locale'] == 'zh-Hans')
    buttons('down', 'down', 'right', 'right', 'a', 'down', 'a', 'a', 'left', 'a')
    p = wait(lambda p: p.get('original'))
    check('original-hud-no-native-page', not p.get('visible'))
    animation = p['animation']
    s.client.call('screenshot', path=str(s.run / 'original-confirm.png'))
    buttons('c_down')
    p = wait(lambda p: p.get('original') and p['animation'] != animation)
    check('cdown-toggles-animation', True)
    s.client.call('screenshot', path=str(s.run / 'original-confirm-toggled.png'))
    buttons('c_down')
    wait(lambda p: p.get('original') and p['animation'] == animation)
    buttons('b', 'b')  # back to target selection, cancel
    time.sleep(.5)
    check('original-hud-closed', not page().get('original'))
    s.client.call('settings', battle_ui='native')
    time.sleep(.3)
    check('setting-native-persisted', json.loads(settings.read_text())['battle_ui'] == 'native')
    buttons('a', 'a', 'left', 'a')
    p = wait(lambda p: p.get('visible'))
    check('native-page-back', p['mode'] == 1)
    s.client.call('screenshot', path=str(s.run / 'native-again.png'))
    print('DONE', s.run, flush=True)
    exit_code = s.quit().get('exit_code')
    print('EXIT', exit_code, flush=True)
    assert exit_code == 0, f'Native host exit: {exit_code}'


if __name__ == '__main__':
    main()
