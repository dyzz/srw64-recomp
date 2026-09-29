#!/usr/bin/env python3
"""Offline check of the original screens' text overlay (src/host/ui_text.cpp) for every
data-text record 0-5643 (docs/guide/ui-layout-audit.md).

A translated label may use its original width, or up to 1.35 times that plus 4 where nothing
follows on its line; to fit it is narrowed (Chinese and Japanese to 70 %, English 80 %) and
set up to 1.5 points smaller. The original width comes from the ROM glyph cells (code 0 and
below 0x13B narrow 8, the rest 14). Lists what cannot fit even with nothing after it
("overflows") and what fits only when nothing follows closely ("depends"). Japanese is the
control: text shown as the ROM has it always fits. Runs of two or more spaces hold places for
numbers: such a line counts as fitting when either its parts fit or, with a number moved into
the translated sentence as ui_text does, the whole line fits."""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'src'))
from srw64_native.catalog import source_catalog  # noqa: E402
from PIL import ImageFont  # noqa: E402

FONTS = ROOT / 'build/fonts'
# locale: font, smallest size (the label size less 1.5), narrowest squeeze
STYLE = {'zh-Hans': ('HarmonyOS_Sans_SC.ttf', 11.0, .7), 'ja': ('HarmonyOS_Sans_SC.ttf', 11.0, .7),
         'en': ('HarmonyOS_Sans_Condensed.ttf', 10.5, .8)}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--locales', default='zh-Hans,en,ja')
    parser.add_argument('--json', help='write every finding here')
    args = parser.parse_args()
    sources, _, glyphs = source_catalog(ROOT, ROOT / 'rom.z64')
    code = {}
    for key, char in glyphs.items():
        if len(char) == 1 and int(key) > 0:
            code.setdefault(char, int(key))
    cell = lambda c: 8 if c == ' ' else 14 if code.get(c, 0x13B) >= 0x13B else 8
    sections = json.loads((ROOT / 'content/locales/terms/sections.json').read_text())['sections']
    section = lambda n: next((s['name'] for s in sections if any(a <= n <= b for a, b in s['ranges'])), '-')
    clean = lambda s: re.sub(r'<END>$', '', s)
    parts = lambda line: [p for p in re.split(r' {2,}', line) if p.strip()]
    rows = []
    for locale in args.locales.split(','):
        name, size, squeeze = STYLE[locale]
        font = ImageFont.truetype(str(FONTS / name), 100)
        entries = {} if locale == 'ja' else {e['key']: clean(e['target']) for e in json.loads((ROOT / f'content/locales/{locale}.json').read_text())['entries']}
        for n in range(5644):
            key = f'base:t00_{n:05d}'
            if key not in sources:
                continue
            src = clean(sources[key])
            if re.search(r'<G:|<[A-Z]', src.replace('<BR>', '')):
                continue
            dst = entries.get(key, src)
            translated = dst != src
            for src_line, dst_line in zip(src.split('<BR>'), dst.split('<BR>')):
                sp, dp = parts(src_line.strip()), parts(dst_line.strip())
                pairs = list(zip(sp, dp)) if len(sp) == len(dp) > 1 else [(src_line.strip(), dst_line.strip())]
                if len(pairs) > 1:
                    # When a printed number sits in the gap, ui_text moves it into the sentence and
                    # draws the line whole: that fitting is enough.
                    whole = re.sub(r' {2,}', '99', dst_line.strip())
                    if font.getlength(whole) * size / 100 * squeeze <= sum(cell(c) for c in src_line.strip()) + .5:
                        continue
                for s, d in pairs:
                    if not s or not d:
                        continue
                    width = sum(cell(c) for c in s)
                    need = font.getlength(d) * size / 100 * squeeze
                    surely = width if translated else width + 2
                    at_most = width * 1.35 + 4 if translated else width + 2
                    if need > surely + .5:
                        rows.append({'locale': locale, 'record': n, 'section': section(n), 'original': s, 'text': d, 'width': width,
                                     'needs': round(need, 1), 'verdict': 'overflows' if need > at_most + .5 else 'depends'})
    for verdict in ('overflows', 'depends'):
        found = [r for r in rows if r['verdict'] == verdict]
        print(f'{verdict}: {len(found)}')
        for r in found:
            print(f"  {r['locale']} {r['record']} {r['section']}: {r['original']!r} → {r['text']!r} (width {r['width']}, needs {r['needs']})")
    if args.json:
        Path(args.json).write_text(json.dumps(rows, ensure_ascii=False, indent=1) + '\n')
    sys.exit(1 if any(r['verdict'] == 'overflows' and r['locale'] == 'zh-Hans' for r in rows) else 0)


if __name__ == '__main__':
    main()
