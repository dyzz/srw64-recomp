#!/usr/bin/env python3
"""Verify skill effects and depleted resource bars in the current native build.

An optional run path attaches to the fixture's already-open Banjo/Roze page.
Otherwise build, enter battle-ui-skills from the native title menu, and select
the encounter through controller input. Leaves the page open for inspection.
"""
import argparse
import json
from pathlib import Path
import sys
import time

from PIL import Image

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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path, nargs='?')
    args = parser.parse_args()
    if args.run:
        s = Session(args.run.resolve())
    else:
        s = Session.launch(language='zh-Hans', rules='fixed',
                           mini_stage='config/recomp/mini-stages/battle-ui-skills.json')
        print('RUN', s.run, flush=True)
        s.enter_mini_stage()
        for button in ['down', 'down', 'right', 'right', 'a', 'down', 'a', 'a', 'left', 'a']:
            s.client.call('buttons', buttons=button, vis=6)
            time.sleep(.7)
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        st = s.client.call('status')
        if st['battle_page'].get('visible'):
            break
        time.sleep(.1)
    else:
        raise AssertionError('Battle page did not open')

    checks = []

    def check(name, passed):
        checks.append({'check': name, 'passed': bool(passed)})
        (s.run / 'skill-checks.json').write_text(json.dumps(checks, indent=2) + '\n')
        assert passed, name
        print(name, 'PASS', flush=True)

    p = st['battle_page']
    (s.run / 'skill-page.json').write_text(json.dumps(st, ensure_ascii=False, indent=2) + '\n')
    a, d = p['attacker'], p['defender']
    check('player-confirm', p['mode'] == 1 and p['can_cancel'])
    check('initial-resources', (a['hp'], a['max_hp'], a['en'], a['max_en'], d['hp'], d['en']) ==
          (1050, 3000, 75, 150, 4875, 132))
    potential = next(e for e in a['pilot_effects'] if e['id'] == 'potential')
    check('potential-current-band', potential['active'] and
          (potential['level'], potential['hit'], potential['evade'], potential['critical'], potential['threshold']) ==
          (4, 5, 5, 10, 40))
    esp = next(e for e in d['pilot_effects'] if e['id'] == 'esp')
    check('enemy-esp', esp['active'] and (esp['level'], esp['hit'], esp['evade']) == (2, 14, 14))
    check('equipment-required', all(any(e['id'] == skill and e['chance'] == 0 and
          e['reason'] == 'no_equipment' for e in a['defensive_effects']) for skill in ['parry', 'shield']))

    for language, width, height, skill_name in [('en', 800, 600, 'Potential'),
            ('ja', 1100, 760, '底力'), ('zh-Hans', 960, 720, '底力')]:
        s.client.call('settings', locale=language)
        s.client.call('window', width=width, height=height)
        time.sleep(.5)
        tree = s.client.call('ui.tree')
        flat = list(nodes(tree))
        text = ' '.join(str(n.get('text', '')) for n in flat)
        check('visible-skill-' + language, skill_name + ' L4' in text)
        buttons = [n['frame'] for n in flat if n.get('id') in
                   ['battle-confirm', 'battle-animation', 'battle-back']]
        check('buttons-in-window-' + language, len(buttons) == 3 and
              all(x >= 0 and y >= 0 and x + w <= width and y + h <= height
                  for x, y, w, h in buttons))
        path = s.run / ('skill-' + language + '.png')
        s.client.call('screenshot', path=str(path))
        (s.run / ('skill-tree-' + language + '.json')).write_text(
            json.dumps(tree, ensure_ascii=False, indent=2) + '\n')
        # All four bars have missing resources. Sample the depleted end of
        # each mirrored bar in the native GPU screenshot, not a CSS mockup.
        scale = next(w['scale'] for w in tree['windows'] if w['game'])
        tracks = []
        for n in flat:
            children = n.get('children', [])
            if (len(children) == 3 and children[0].get('class') == 'span' and
                    children[1].get('class') == 'div' and
                    any(v.get('text') in ['HP', 'EN'] for v in nodes(children[0]))):
                tracks.append(children[1]['frame'])
        with Image.open(path) as im:
            rgb = im.convert('RGB')
            colors = [rgb.getpixel((round((x + w * (.95 if x < width / 2 else .05)) * scale), round((y + h / 2) * scale)))
                      for x, y, w, h in tracks]
        check('lost-resources-red-' + language, len(colors) == 4 and
              all(r > 180 and g < 90 and b < 90 for r, g, b in colors))
    probe = s.run / 'battle-ui-probe.json'
    if probe.exists():
        data = json.loads(probe.read_text())
        check('original-function-probe', data['passed'] and data['gameplay_and_rng_unchanged'] and
              len(data['skills']) == 2400 and all(row['match'] for row in data['skills']))
    print('DONE', s.run, flush=True)


if __name__ == '__main__':
    main()
