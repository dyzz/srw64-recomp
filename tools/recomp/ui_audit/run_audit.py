#!/usr/bin/env python3
"""Offline layout audit of the shared UI pages (docs/guide/ui-layout-audit.md).

Builds tools/recomp/ui_audit/layout_audit.cpp against the current frontend.cpp with the
compile flags and libraries of the graphics host build (build/recomp/gfx-build), turns
the recorded page states in states.json into fixtures for every reading language (plus
the longest names, weapon names and figures the catalogs hold), lays every page out at
the Steam Deck size and a few other window sizes, and lists text that runs out of its
box. No ROM run and no game thread: only a small window that draws the pages.
Screenshots and audit.json go into build/recomp/ui-audit/<size>/. Exit status 1 when
anything overflows."""
import argparse
import copy
import json
import re
import shlex
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'src'))
from srw64_native.catalog import source_catalog  # noqa: E402

HERE = Path(__file__).resolve().parent
GFX = ROOT / 'build/recomp/gfx-build'
OUT = ROOT / 'build/recomp/ui-audit'
LOCALES = ('zh-Hans', 'en', 'ja')
# name: window points and interface size (Steam Deck: 1280 x 800 at Largest).
SIZES = {'deck': (1280, 800, 'largest'), 'deck-standard': (1280, 800, 'standard'),
         'wide': (1920, 1080, 'standard'), 'smallest': (960, 720, 'standard')}
LIBS = ['ui-probe/libsrw64_ui_pages.a', 'ui-probe/libsrw64_ui_renderer.a', 'rt64/rt64.a', 'rt64/src/contrib/re-spirv/libre-spirv.a',
        'rt64/src/contrib/nativefiledialog-extended/src/libnfd.a', 'rt64/src/contrib/zstd/build/cmake/lib/libzstd.a',
        'rt64/src/contrib/plume/libplume.a', 'ui-probe/rmlui/Source/Core/librmlui.a']
FRAMEWORKS = ['AppKit', 'Metal', 'QuartzCore', 'CoreGraphics', 'IOKit']


def build():
    """Compile with frontend.cpp's own flags from the host build; relink when a source is newer."""
    command = subprocess.run(['ninja', '-C', str(GFX), '-t', 'commands'], capture_output=True, text=True, check=True).stdout
    frontend = ' -c ' + str(ROOT / 'src/native/ui/frontend.cpp')
    words = shlex.split(next(line for line in command.splitlines() if line.endswith(frontend)))
    flags = words[1:words.index('-MD')]
    flags += ['-I' + str(ROOT / 'src/native/ui'), '-I' + str(ROOT / 'src/native'), '-O1', '-g', '-w',
              '-I' + str(ROOT / 'build/recomp/upstream/RT64/src/contrib/plume/contrib/metal-cpp')]
    obj = OUT / 'obj'
    obj.mkdir(parents=True, exist_ok=True)
    sources = [ROOT / 'src/native/ui/frontend.cpp', ROOT / 'src/native/localization/catalog.cpp', HERE / 'layout_audit.cpp',
               ROOT / 'src/native/ui/probe_surface_macos.cpp']
    headers = max(p.stat().st_mtime for d in ('src/native/ui', 'src/host', 'src/native/text', 'src/native/localization') for p in (ROOT / d).glob('*.hpp'))
    jobs, objects = [], []
    for source in sources:
        target = obj / (source.stem + '.o')
        objects.append(target)
        if not target.exists() or target.stat().st_mtime < max(source.stat().st_mtime, headers):
            print('compile', source.relative_to(ROOT))
            jobs.append(subprocess.Popen(['clang++', *flags, '-c', str(source), '-o', str(target)]))
    if any(job.wait() for job in jobs):
        sys.exit('compile failed')
    binary = OUT / 'srw64-ui-audit'
    if jobs or not binary.exists():
        link = ['clang++', '-arch', 'arm64', '-Wl,-rpath,/opt/homebrew/lib', *map(str, objects), '-o', str(binary),
                *[str(GFX / lib) for lib in LIBS], '/opt/homebrew/lib/libSDL2-2.0.0.dylib', '/opt/homebrew/lib/libfreetype.dylib']
        for framework in FRAMEWORKS:
            link += ['-framework', framework]
        subprocess.run(link, check=True)
    return binary


class Text:
    """Record texts per language, and the recorded Japanese or Chinese strings in another language."""
    def __init__(self):
        sources, _, _ = source_catalog(ROOT, ROOT / 'rom.z64')
        clean = lambda s: re.sub(r'<END>$', '', s).replace('<BR>', '\n')
        self.ja = {k: clean(v) for k, v in sources.items() if k.startswith('base:t00_')}
        self.by = {'ja': self.ja}
        for locale in ('zh-Hans', 'en'):
            entries = json.loads((ROOT / f'content/locales/{locale}.json').read_text())['entries']
            self.by[locale] = {**self.ja, **{e['key']: clean(e['target']) for e in entries}}
        # A string back to its record: Japanese through the ROM, Chinese through the catalog.
        self.key = {}
        for locale in ('ja', 'zh-Hans'):
            for key, text in self.by[locale].items():
                self.key.setdefault(text, key)
        from PIL import ImageFont
        fonts = ROOT / 'build/fonts'
        sc = ImageFont.truetype(str(fonts / 'HarmonyOS_Sans_SC.ttf'), 100)
        self.fonts = {'zh-Hans': sc, 'ja': sc, 'en': ImageFont.truetype(str(fonts / 'HarmonyOS_Sans_Condensed.ttf'), 100)}

    def record(self, locale, number):
        return self.by[locale][f'base:t00_{number:05d}']

    def convert(self, value, locale, keep=('screen', 'kind', 'mode', 'window', 'medium', 'context', 'style', 'locale', 'gauge', 'terrain')):
        if isinstance(value, dict):
            return {k: v if k in keep else self.convert(v, locale) for k, v in value.items()}
        if isinstance(value, list):
            return [self.convert(v, locale) for v in value]
        if isinstance(value, str) and value.strip() in self.key:
            return value.replace(value.strip(), self.by[locale][self.key[value.strip()]])
        return value

    def longest(self, locale, first, last, count=7):
        """The widest distinct texts of a record range in the page font."""
        font, seen, out = self.fonts[locale], set(), []
        for text in sorted((self.record(locale, n) for n in range(first, last + 1)),
                           key=lambda t: -max(font.getlength(line) for line in t.split('\n'))):
            if text.strip() and text not in seen:
                seen.add(text)
                out.append(text)
            if len(out) == count:
                break
        return out


def marked(row):
    """upgrade_page.cpp weapon_markers: a list name's 格／射 prefix and P／B／MAP suffix become icons."""
    name, marks = row.get('name', ''), []
    for prefix in ('格', '射'):
        if name.startswith(prefix):
            marks.append(prefix)
            name = name[len(prefix):]
            break
    for suffix in ('MAP', 'P', 'B'):
        if name.endswith(suffix) and len(name) > len(suffix):
            marks.append(suffix)
            name = name[:-len(suffix)].rstrip()
            break
    return {**row, 'display_name': name, 'markers': marks}


def fixtures(text):
    states = json.loads((HERE / 'states.json').read_text())
    out = []
    for loc in LOCALES:
        def add(name, pages, **extra):
            out.append({'name': f'{name}-{loc}', 'locale': loc, 'pages': pages, **extra})
        at = lambda state: text.convert(copy.deepcopy(state), loc)
        units, pilots, stages = text.longest(loc, 527, 889), text.longest(loc, 4382, 4742), text.longest(loc, 281, 423, 1)
        menus, weapons, skills = text.longest(loc, 2699, 4027), text.longest(loc, 1370, 2698, 1), text.longest(loc, 4028, 4043, 1)
        # Intermission menu, with the longest episode title and figures.
        menu = at(states['intermission'])
        add('intermission', {'intermission_page': menu})
        add('intermission-long', {'intermission_page': {**menu, 'scene_title': stages[0], 'funds': 9999999, 'turns': 999, 'episode': 60}})
        add('intermission-swap', {'intermission_page': {**menu, 'submenu': True}})
        # データセーブ / ロード
        for name, state in states['save'].items():
            add(f'save-{name}', {'save_page': at(state)})
        slots = at(states['save']['slots'])
        for slot in slots['slots']:
            if slot.get('used'):
                slot.update(title=stages[0], funds=9999999, turns=999, episode=60, level=99)
        add('save-slots-long', {'save_page': slots})
        add('load-choice', {'save_page': {**at(states['save']['choice']), 'context': 'title'}})
        # のりかえ, 能力, 強化パーツ: recorded rows, then seven rows of the longest names.
        for name, state in states['swap'].items():
            add(f'swap-{name}', {'swap_page': at(state)})
        pilot_list = at(states['swap']['pilots'])
        pilot_list['rows'] = [{**pilot_list['rows'][0], 'name': pilots[i], 'unit': units[i], 'index': i} for i in range(7)]
        add('swap-pilots-long', {'swap_page': pilot_list})
        targets = at(states['swap']['targets'])
        targets['rows'] = [{**targets['rows'][0], 'name': units[i], 'pilot': pilots[i], 'slot': i} for i in range(7)]
        add('swap-targets-long', {'swap_page': targets})
        for name, state in states['ability'].items():
            state = at(state)
            if name == 'weapons':
                state['rows'] = [marked(r) for r in state['rows']]
            add(f'ability-{name}', {'ability_page': state})
        unit_list = at(states['ability']['unit-list'])
        unit_list['rows'] = [{**unit_list['rows'][0], 'name': units[i], 'pilot': pilots[i], 'slot': i} for i in range(7)]
        add('ability-unit-list-long', {'ability_page': unit_list})
        pilot_rows = at(states['ability']['pilot-list'])
        pilot_rows['rows'] = [{**pilot_rows['rows'][0], 'name': pilots[i], 'unit': units[i], 'index': i} for i in range(7)]
        add('ability-pilot-list-long', {'ability_page': pilot_rows})
        # 機体／パイロット 能力 pages: the longest names and the most lines the pages hold.
        unit_page = at(states['ability']['unit-page'])
        unit_page.update(abilities=at(['変形', '分身', 'オーラバリア']),   # ビルバイン: the most any unit shows
                         parts=text.longest(loc, 1129, 1148, 4), hp=99999, hp_max=99999, en=999,
                         en_max=999, armor=9999, mobility=999, limit=999, repair=999999, types=at(['陸', '空', '海']))   # four move bits show three types at most (one bit has no text)
        unit_page['unit'] = {**unit_page['unit'], 'name': units[0]}
        add('ability-unit-page-long', {'ability_page': unit_page})
        add('ability-unit-page-longest-abilities', {'ability_page': {**unit_page, 'abilities': text.longest(loc, 1019, 1030, 3)}})
        pilot_page = at(states['ability']['pilot-page'])
        pilot_page.update(skills=text.longest(loc, 1031, 1099, 6), spirits=text.longest(loc, 969, 998, 6), sp=999, sp_max=999, next=99999, morale=150,
                          stats={k: 999 for k in pilot_page['stats']})
        pilot_page['pilot'] = {**pilot_page['pilot'], 'full_name': text.longest(loc, 4743, 5103, 1)[0], 'name': pilots[0], 'level': 99}
        pilot_page['unit'] = {**pilot_page['unit'], 'name': units[0]}
        add('ability-pilot-page-long', {'ability_page': pilot_page})
        weapon_rows = at(states['ability']['weapons'])
        weapon_rows['rows'] = [marked({**weapon_rows['rows'][0], 'name': menus[i], 'index': i}) for i in range(6)]
        add('ability-weapons-long', {'ability_page': weapon_rows})
        for name, state in states['parts'].items():
            add(f'parts-{name}', {'parts_page': at(state)})
        parts = at(states['parts']['list-rows'])
        parts['rows'] = [{**parts['rows'][0], 'name': units[i], 'pilot': pilots[i], 'slot': i} for i in range(7)]
        add('parts-list-long', {'parts_page': parts})
        # Title menus: オプション and the song list (upgrade_page.cpp and title_page.cpp field names).
        add('title-options', {'title_page': at(states['title_options'])})
        songs = [{'text': text.record(loc, n), 'song': n - 232, 'playing': n == 235} for n in range(232, 281)]
        for top, current in ((0, 3), (39, 45)):
            add(f'title-songs-{top}', {'title_page': {'visible': True, 'serial': 1, 'screen': 'sound', 'title': text.record(loc, 226),
                                                      'exit': text.record(loc, 230), 'songs': songs, 'current': current, 'top': top, 'rows': 10}})
        # 改造: no recorded state, built as upgrade_page.cpp publishes it.
        r = lambda n: text.record(loc, n)
        labels = {'funds': r(0x1019), 'price': r(0xFEF), 'pilot': r(0xFEC), 'question': r(0xFED), 'cap': r(0xFEE), 'maxed': r(0x102F),
                  'poor': r(0x1030), 'ask': r(0xFE4), 'yes': r(0xFE5), 'no': r(0xFE6), 'hp': r(0xFE7), 'en': r(0xFE8), 'mobility': r(0xFE9),
                  'armor': r(0xFEA), 'limit': r(0xFEB)}
        keys = ['weapon', 'power', 'range', 'hit', 'ammo', 'terrain', 'air', 'land', 'sea', 'space', 'morale', 'en', 'skill', 'critical']
        base = {'visible': True, 'serial': 1, 'funds': 9999999, 'labels': labels, 'weapon_labels': {**{k: r(0xFF1 + i) for i, k in enumerate(keys)}, 'icons': {}},
                'bonus_labels': [r(0x10AC), r(0x10AD), r(0x10AE)]}
        rows = [{'slot': i, 'number': 216, 'name': units[i], 'hp': 99999, 'en': 999, 'mobility': 999, 'armor': 9999, 'limit': 999, 'pilot': pilots[i]} for i in range(7)]
        add('upgrade-list', {'upgrade_page': {**base, 'screen': 'list', 'kind': 'stats', 'title': r(0xFCE), 'cursor': 0, 'page': 0, 'pages': 3, 'rows': rows}})
        stats = [{'name': labels[k], 'value': v, 'level': 7, 'cap': 15, 'original_cap': 10, 'price': 999999, 'preview': v + 500, 'gauge': '>>>>>>>...**ooo'}
                 for k, v in (('hp', 99999), ('en', 999), ('mobility', 999), ('armor', 9999), ('limit', 999))]
        unit = {'slot': 0, 'number': 216, 'name': units[0], 'art': {}}
        for window in ('', 'confirm', 'poor'):
            add('upgrade-stats' + ('-' + window if window else ''), {'upgrade_page': {**base, 'screen': 'stats', 'kind': 'stats', 'title': r(0xFCE), 'unit': unit,
                                                                                     'cap': 15, 'cursor': 0, 'rows': stats, 'window': window}})
        weapon_list = [{**marked({'name': menus[i]}), 'index': i, 'number': 100 + i, 'power': 9999, 'range_min': 1, 'range_max': 9, 'hit': 30, 'critical': 50,
                        'en': 999, 'morale': 150, 'skill': 2, 'skill_name': skills[0], 'ammo': 99, 'ammo_max': 99, 'terrain': 'AAAA', 'level': 7, 'type': 1,
                        'cap': 15, 'original_cap': 10, 'price': 999999, 'preview': 9999, 'gauge': '>>>>>>>...**ooo'} for i in range(6)]
        armed = {**unit, 'en': 999, 'morale': 150}
        add('upgrade-weapons', {'upgrade_page': {**base, 'screen': 'weapons', 'kind': 'weapons', 'title': r(0xFCF), 'unit': armed, 'cursor': 0,
                                                 'page': 0, 'pages': 2, 'rows': weapon_list, 'window': ''}})
        add('upgrade-weapon', {'upgrade_page': {**base, 'screen': 'weapon', 'kind': 'weapons', 'title': r(0xFCF), 'unit': armed, 'cursor': 0,
                                                'weapon': weapon_list[0], 'window': 'maxed'}})
        # 戦闘前確認: the recorded encounters in both styles, then four- and five-digit damage with the longest names.
        for i, battle in enumerate(states['battle']):
            for style in ('native', 'hd'):
                add(f'battle-{i}-{style}', {'battle_page': {**at(battle), 'style': style}}, battle_ui=style)
        long = at(states['battle'][0])
        for side in ('attacker', 'defender'):
            c = long[side]
            c.update(unit_name=units[0], pilot_name=pilots[0], weapon_name=weapons[0], hp=99999, sp=999, max_sp=999, morale=150)
            for k in ('damage', 'critical_damage', 'damage_raw', 'damage_if_shield'):
                c[k] = 12345
        add('battle-long-native', {'battle_page': long})
        add('battle-long-hd', {'battle_page': {**long, 'style': 'hd'}}, battle_ui='hd')
        four = copy.deepcopy(long)
        for side in ('attacker', 'defender'):
            four[side].update(damage=8888, critical_damage=8888)
        add('battle-4digit-native', {'battle_page': four})
        for page in ('general', 'interface', 'rules', 'controls', 'about'):
            add(f'settings-{page}', {}, settings=page)
        add('link', {'link_page': {'visible': True}})
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--sizes', default=','.join(SIZES), help='comma-separated: ' + ', '.join(SIZES))
    parser.add_argument('--only', help='regular expression on fixture names (e.g. "zh-Hans$" or "^battle")')
    parser.add_argument('--shrunk', type=float, default=0.85, metavar='SCALE',
                        help='also list text the fit class set smaller than this share of its face (default 0.85; 1 lists every shrink, 0 none)')
    args = parser.parse_args()
    binary = build()
    all_fixtures = fixtures(Text())
    if args.only:
        all_fixtures = [f for f in all_fixtures if re.search(args.only, f['name'])]
    failed = False
    for size in args.sizes.split(','):
        width, height, ui_size = SIZES[size]
        directory = OUT / size
        directory.mkdir(parents=True, exist_ok=True)
        path = directory / 'fixtures.json'
        path.write_text(json.dumps([{**f, 'ui_size': ui_size} for f in all_fixtures], ensure_ascii=False))
        run = subprocess.run([str(binary), str(ROOT / 'content/locales'), str(path), str(directory), str(width), str(height)],
                             cwd=ROOT, env={'SRW64_FONT_DIR': str(ROOT / 'build/fonts'), 'PATH': '/usr/bin:/bin'}, capture_output=True, text=True)
        if run.returncode:
            print(run.stderr[-2000:])
            sys.exit(f'{size}: the audit did not finish')
        report = json.loads((directory / 'audit.json').read_text())
        problems = [(r['name'], issue) for r in report for issue in r.get('issues', [])] + [(r['name'], {'kind': 'error', 'text': r['error']}) for r in report if 'error' in r]
        print(f'{size} ({width}x{height}, {ui_size}): {len(report)} pages, {len(problems)} problem(s) — {directory.relative_to(ROOT)}')
        for name, issue in problems:
            failed = True
            print(f"  {name}: {issue['kind']} {issue.get('over_dp', 0):.1f} dp {issue.get('text', '')!r} ({issue.get('path', '')})")
        # Shrinks are not failures (the fit class exists to shrink), but the worst ones say where a face is set too large.
        shrunk = sorted(((r['name'], s) for r in report for s in r.get('shrunk', []) if s['scale'] < args.shrunk), key=lambda ns: ns[1]['scale'])
        if shrunk:
            print(f'  {len(shrunk)} line(s) shrunk below {args.shrunk:.0%}:')
        for name, s in shrunk:
            print(f"    {name}: {s['from_dp']:.1f} → {s['to_dp']:.1f} dp ({s['scale']:.0%}) {s['text']!r} ({s['path']})")
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
