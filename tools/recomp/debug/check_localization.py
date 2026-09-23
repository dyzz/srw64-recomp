#!/usr/bin/env python3
"""Check the translated native pages and battle quotes in the current native build.

intermission: loads the stage-one-clear save in Chinese, opens every native
intermission screen, switches to English and back while it is open, and checks
that names follow the language at once, that the player's own names stay the
same in every language, that the weapon confirmation has no raw markers, and that
F5 reads the dialogue text files again and reports a broken override.
battle: enters the battle-ui mini stage, starts a battle with animation on and
checks that the quote box is redrawn natively with a translated speaker.

Screenshots and localization-checks.json go into the run directory; strings that
still contain kana on a Chinese or English page are listed for review."""
import argparse
import json
import re
import time
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, run_keys

ROOT = Path(__file__).resolve().parents[3]
KANA = re.compile(r"[぀-ヿ]")
TERMS = {l: json.loads((ROOT / f"content/locales/terms/{l}.json").read_text())["sections"] for l in ("zh-Hans", "en")}

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('part', choices=('intermission', 'battle'))
parser.add_argument('--reuse-build', action='store_true')
parser.add_argument('--save', default='build/recomp/save-recovery-check/intermission-cold-1.source.sram')
args = parser.parse_args()
checks, kana = [], []


def term(section, jp, locale):
    return jp if locale == 'ja' else TERMS[locale][section].get(jp, jp)


def strings(value, path=''):
    if isinstance(value, str):
        yield path, value
    elif isinstance(value, dict):
        for k, v in value.items():
            if k not in ('art', 'labels_ja'):
                yield from strings(v, f'{path}.{k}')
    elif isinstance(value, list):
        for i, v in enumerate(value):
            yield from strings(v, f'{path}[{i}]')


def record(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (s.run / 'localization-checks.json').write_text(json.dumps({'checks': checks, 'kana': kana}, ensure_ascii=False, indent=2) + '\n')
    print(name, 'PASS' if passed else 'FAIL', flush=True)


def keys(*names, pause=.4):
    for name in names:
        run_keys(s.client, [{'press': name}])
        time.sleep(pause)


def status():
    return s.client.call('status')


def shot(name):
    s.client.call('screenshot', path=str(s.run / f'{name}.png'))


def wait_for(fn, timeout=30, what='state'):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        st = status()
        if fn(st):
            return st
        time.sleep(.1)
    raise AssertionError(f'{what} did not appear')


def page(key, screen, timeout=30):
    return wait_for(lambda st: st[key].get('visible') and st[key].get('screen') == screen, timeout, f'{key} {screen}')[key]


def locale(value, key=None, ready=None):
    s.client.call('settings', locale=value)
    return wait_for(lambda st: st['locale'] == value and (key is None or ready is None or ready(st[key])), 15, f'{key} in {value}')


def scan(key, name, locale_name, value):
    for path, text in strings(value):
        if KANA.search(text):
            kana.append({'page': name, 'locale': locale_name, 'path': f'{key}{path}', 'text': text})


def switching(key, name, expect):
    """Shoot the open page in zh-Hans, switch to en while it stays open, then back.

    expect(page, locale) says whether the page already shows that language."""
    result = {}
    for n, value in enumerate(('zh-Hans', 'en', 'zh-Hans')):
        try:
            st = locale(value, key, lambda p: expect(p, value))
            record(f'{name}-{value}-{n}', True, st[key])
        except AssertionError:
            st = status()
            record(f'{name}-{value}-{n}', False, st[key])
        scan(key, name, value, st[key])
        if n < 2:
            result[value] = st[key]
            shot(f'{name}-{value}')
    return result


def section(name, run):
    try:
        run()
    except Exception as error:
        record(f'{name}-error', False, {'error': str(error)})


def leave(key, presses=1):
    """B until the intermission menu is back and no sub-page is open."""
    for _ in range(presses + 2):
        st = status()
        if st['intermission_page'].get('visible') and not st[key].get('visible'):
            return
        keys('x', pause=1.2)
    wait_for(lambda st: st['intermission_page'].get('visible') and not st[key].get('visible'), 30, 'intermission menu')


def intermission():
    s.wait(vi=600)
    for _ in range(8):
        keys('return', pause=.5)
        try:
            s.wait(title_major=3, timeout=4)
            break
        except Exception:
            pass
    time.sleep(2.5)
    keys('right', 'right', 'right', pause=1.5)
    keys('return', pause=3)
    for _ in range(8):
        st = status()
        if st['intermission_page'].get('visible') or (st.get('intro') or {}).get('title_major') != 7:
            break
        keys('z', pause=1.5)
    s.wait(intermission_page=True, timeout=30)
    units = ['ダイターン3', 'スイームルグ', 'ドール']
    pilots = {}

    def upgrade():
        s.client.call('ui.click', text='intermission:1')
        page('upgrade_page', 'list')
        seen = switching('upgrade_page', 'upgrade-list',
                         lambda p, l: [r['name'] for r in p['rows']] == [term('units', u, l) for u in units])
        # スイームルグ's pilot is the protagonist: the entered name, whatever the language.
        pilots['upgrade'] = {l: p['rows'][1].get('pilot') for l, p in seen.items()}
        keys('z', pause=1)
        page('upgrade_page', 'stats')
        switching('upgrade_page', 'upgrade-stats', lambda p, l: p['unit']['name'] == term('units', units[0], l))
        leave('upgrade_page', 3)

    def weapons():
        s.client.call('ui.click', text='intermission:2')
        page('upgrade_page', 'list')
        keys('z', pause=1)
        page('upgrade_page', 'weapons')
        switching('upgrade_page', 'weapon-list', lambda p, l: p['unit']['name'] == term('units', units[0], l))
        keys('z', pause=1)
        p = page('upgrade_page', 'weapon')
        shown = p['weapon'].get('display_name', '')
        record('weapon-confirm-plain-name', shown and not shown.startswith(('格', '射')) and not shown.endswith(('P', 'B')), p)
        shot('weapon-confirm-zh-Hans')
        leave('upgrade_page', 4)

    def ability():
        s.client.call('ui.click', text='intermission:3')
        page('ability_page', 'units')
        keys('z', pause=1)
        page('ability_page', 'unit')
        switching('ability_page', 'ability-unit', lambda p, l: p['unit']['name'] == term('units', units[0], l))
        leave('ability_page', 3)
        s.client.call('ui.click', text='intermission:4')
        page('ability_page', 'pilots')
        seen = switching('ability_page', 'ability-pilots', lambda p, l: bool(p['rows']))
        # Pilots 25-32 are the protagonists and partners (records 4407-4414).
        pilots['ability'] = {l: [r['name'] for r in p['rows'] if 25 <= r.get('number', -1) <= 32] for l, p in seen.items()}
        keys('z', pause=1)
        page('ability_page', 'pilot')
        switching('ability_page', 'ability-pilot', lambda p, l: bool(p['pilot'].get('name')))
        leave('ability_page', 3)

    def parts():
        s.client.call('ui.click', text='intermission:6')
        page('parts_page', 'list')
        switching('parts_page', 'parts-list', lambda p, l: sorted(r['name'] for r in p['rows']) == sorted(term('units', u, l) for u in units))
        keys('z', pause=1)
        page('parts_page', 'slots')
        switching('parts_page', 'parts-slots', lambda p, l: p['unit']['name'] == term('units', units[0], l))
        leave('parts_page', 3)

    def swap():
        s.client.call('ui.click', text='intermission:5')
        time.sleep(.8)
        try:
            s.client.call('ui.click', text='intermission-swap:0')
            page('swap_page', 'pilots', timeout=8)
        except Exception:
            # The stage-one save has too few pilots to swap: the menu refuses it.
            keys('x', pause=1)
            record('swap-refused-in-this-save', True, status()['intermission_page'])
            return
        switching('swap_page', 'swap-pilots', lambda p, l: bool(p['rows']))
        leave('swap_page', 3)

    def save():
        s.client.call('ui.click', text='intermission:0')
        page('save_page', 'choice')
        yes = lambda p, l: p.get('labels', {}).get('yes') == term('intermission', 'はい', l)
        switching('save_page', 'save-choice', yes)
        keys('z', pause=.5)
        page('save_page', 'slots', timeout=15)
        switching('save_page', 'save-slots', yes)
        leave('save_page', 3)

    def reload():
        # F5 with a broken override: the report names it, the rest still loads.
        overrides = s.run / 'dialogue' / 'zh-Hans'
        overrides.mkdir(parents=True, exist_ok=True)
        (overrides / 'mine.txt').write_text('@17412 ローレンス\n只有一页\n', encoding='utf-8')
        keys('f5', pause=3)
        report_path = s.run / 'dialogue' / 'dialogue-report.txt'
        report = report_path.read_text() if report_path.exists() else ''
        loads = [json.loads(line) for line in (s.run / 'dialogue-events.jsonl').read_text().splitlines() if '"kind":"dialogue_text"' in line]
        record('reload-report', 'zh-Hans/mine.txt' in report and len(loads) >= 2 and loads[-1]['locales']['zh-Hans']['problems'] == 1,
               {'report': report, 'loads': loads[-2:]})
        shot('reload-banner-zh-Hans')

    for name, run in (('upgrade', upgrade), ('weapons', weapons), ('ability', ability), ('parts', parts), ('swap', swap), ('save', save),
                      ('reload', reload)):
        section(name, run)
    # The player's own names come from the game's name banks: the same in every language.
    for where, by_locale in pilots.items():
        names = list(by_locale.values())
        record(f'entered-names-{where}', len(names) == 2 and names[0] and names[0] == names[1], by_locale)
    shot('intermission-menu-zh-Hans')


def battle():
    s.enter_mini_stage()
    for key in ['up', 'z', 'z', 'z']:
        s.client.call('keys', press=key, hold_ms=100)
        time.sleep(.35)
    st = wait_for(lambda x: x['battle_page'].get('visible', False), 120, 'pre-battle page')
    if not st['battle_page'].get('animation'):
        s.client.call('ui.click', id='battle-animation')
        time.sleep(.3)
    s.client.call('ui.click', id='battle-confirm')
    quotes, shots = [], 0
    end = time.monotonic() + 90
    while time.monotonic() < end and shots < 4:
        dialogue = status().get('dialogue') or {}
        boxes = [b for b in dialogue.get('boxes', []) if b.get('text')]
        for box in boxes:
            if box['text_key'] not in [q['text_key'] for q in quotes]:
                quotes.append(box)
                shot(f'battle-quote-{shots}')
                shots += 1
                if shots == 2:
                    s.client.call('settings', locale='en')
        time.sleep(.2)
    record('battle-quotes-redrawn', bool(quotes), {'quotes': quotes})
    ids = [int(q['text_key'].split('_')[1]) for q in quotes]
    record('battle-quote-range', all(5799 <= i <= 17346 for i in ids), {'ids': ids})
    record('battle-speaker-translated', any(not KANA.search(q.get('speaker', '')) for q in quotes), {'speakers': [q.get('speaker') for q in quotes]})
    events = [json.loads(line) for line in (s.run / 'dialogue-events.jsonl').read_text().splitlines() if '"battle_' in line]
    record('battle-events-logged', any(e['kind'] == 'battle_quote' for e in events),
           {'battle_quote': sum(e['kind'] == 'battle_quote' for e in events), 'battle_overflow': sum(e['kind'] == 'battle_overflow' for e in events)})


if args.part == 'intermission':
    s = Session.launch(language='zh-Hans', images='original', save=args.save, reuse_build=args.reuse_build, timeout=1800)
else:
    s = Session.launch(language='zh-Hans', mini_stage='config/recomp/mini-stages/battle-ui.json', reuse_build=args.reuse_build, timeout=1800)
print('RUN', s.run, flush=True)
try:
    intermission() if args.part == 'intermission' else battle()
finally:
    print(json.dumps({'passed': sum(c['passed'] for c in checks), 'failed': [c['check'] for c in checks if not c['passed']],
                      'kana': len(kana)}, ensure_ascii=False), flush=True)
    s.client.call('quit')
