#!/usr/bin/env python3
"""Show the whole HD unit poses on the native pre-battle page.

Launches in HD, enters the battle-ui mini stage, walks to the confirm page as
check_battle_ui_switch.py does, and screenshots it; then F6 to the original
images at the same page for the side-by-side. Checks that the page snapshot
carries an `hd` unit pose for both sides while HD is applied."""
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session


def main():
    stage = sys.argv[1] if len(sys.argv) > 1 else 'config/recomp/mini-stages/battle-ui-skills.json'
    s = Session.launch(language='zh-Hans', images='hd', rules='fixed', mini_stage=stage)
    print('RUN', s.run, flush=True)
    checks = []

    def page():
        return s.client.call('status')['battle_page']

    def check(name, passed):
        checks.append({'check': name, 'passed': bool(passed)})
        (s.run / 'unit-pose-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2) + '\n')
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
        s.client.call('screenshot', path=str(s.run / 'unit-pose-timeout.png'))
        raise AssertionError('timeout')

    s.enter_mini_stage()
    s.client.call('settings', battle_ui='native')
    time.sleep(.3)
    buttons('down', 'down', 'right', 'right', 'a', 'down', 'a', 'a', 'left', 'a')
    p = wait(lambda p: p.get('visible'))
    time.sleep(1.5)
    s.client.call('screenshot', path=str(s.run / 'confirm-hd.png'))
    arts = [(p.get(side) or {}).get('unit_art', {}) for side in ('attacker', 'defender')]
    (s.run / 'unit-art.json').write_text(json.dumps(arts, ensure_ascii=False, indent=2) + '\n')
    check('both-sides-carry-hd-pose', all(a.get('hd') for a in arts))
    s.client.call('keys', press='f6')  # original images at the same page
    time.sleep(1.5)
    s.client.call('screenshot', path=str(s.run / 'confirm-original.png'))
    s.client.call('keys', press='f6')
    time.sleep(1.0)
    s.client.call('screenshot', path=str(s.run / 'confirm-hd-again.png'))
    print('DONE', s.run, flush=True)
    exit_code = s.quit().get('exit_code')
    print('EXIT', exit_code, flush=True)
    assert exit_code == 0, f'Native host exit: {exit_code}'


if __name__ == '__main__':
    main()
