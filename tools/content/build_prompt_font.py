#!/usr/bin/env python3
"""Build content/fonts/SRW64Prompts.ttf: button icons for UI hints, from PromptFont.

PromptFont (Yukari "Shinmera" Hafner, https://shinmera.com/promptfont, SIL Open Font
Licence 1.1) draws controller buttons and keyboard keys, but on ordinary Unicode code
points (arrows, maths symbols) that HarmonyOS Sans also has: as a fallback font those
would never be reached, and ⇓ would stay an arrow. So this keeps only the glyphs the
hints use and moves them into the Private Use Area from U+E800 (HarmonyOS Sans and
SRW64Symbols have nothing there), where both text engines reach them as the last font
of the chain. UI strings write tokens such as {A} or {Esc}; src/native/text/
button_prompts.hpp turns them into these characters for the controller in use.

The glyphs are scaled by 700/660, HarmonyOS Sans's cap height over PromptFont's, and
then enlarged about their middle to stand as tall as a Chinese character (about 930 of
1000, centred on 380): at PromptFont's own size a face button was a size smaller than
the text beside it and a shoulder button a flat sliver, too small to read on a Steam
Deck (user, 2026-09-28). A flat glyph grows by at most 1.4, and none shrinks. Vertical
metrics copy
HarmonyOS Sans SC, so the fallback never raises a line. Being a Modified Version under
the OFL, the font has its own name; PromptFont declares no Reserved Font Name.

The source is the PromptFont release Zelda64Recomp ships (assets/promptfont, 2023-12-29),
which the pinned upstream checkout holds; its SHA-256 is checked. Needs fontTools; the
built font is tracked, so this runs only when the glyph list changes.

    python tools/content/build_prompt_font.py [--source path/to/promptfont.ttf]
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

from fontTools import subset
from fontTools.misc.timeTools import timestampFromString
from fontTools.ttLib import TTFont, newTable
from fontTools.ttLib.tables._c_m_a_p import cmap_format_4
from fontTools.ttLib.tables._g_l_y_f import Glyph

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "build/recomp/upstream/Zelda64Recomp-reference/assets/promptfont/promptfont.ttf"
SOURCE_SHA256 = "7206e62fdb7e07fdf137e52464d7fa862f2f8dc79931fa6500e6ceb8ed1f06b9"
LICENCE = SOURCE.with_name("LICENSE.txt")
LICENCE_SHA256 = "91db5ab511830ae7b393be382e64423c5a783da51faf6211b3d42ac836ac0ce3"
OUTPUT = ROOT / "content/fonts/SRW64Prompts.ttf"
LICENCE_OUTPUT = ROOT / "content/fonts/LICENSE-SRW64Prompts.txt"
SCALE = 700 / 660                        # HarmonyOS Sans cap height / PromptFont's (H)
TALL, MIDDLE, GROW = 930, 380, 1.4       # a CJK character's height and middle; the most a glyph grows
ASCENT, DESCENT = 928, -244              # HarmonyOS Sans SC hhea
BUILT = timestampFromString("Mon Sep 28 00:00:00 2026")

# (Private Use Area code point, PromptFont code point, PromptFont name). The code
# points are fixed: button_prompts.hpp and tests/test_button_prompts.py use them. The
# keyboard arrows follow the drawings, not promptfont.h's names (U+23F5 is right, U+23F6 up).
GLYPHS = [
    (0xE800, 0x21D3, "xbox-a"), (0xE801, 0x21D2, "xbox-b"), (0xE802, 0x21D0, "xbox-x"), (0xE803, 0x21D1, "xbox-y"),
    (0xE804, 0x21E3, "sony-cross"), (0xE805, 0x21E2, "sony-circle"), (0xE806, 0x21E0, "sony-square"), (0xE807, 0x21E1, "sony-triangle"),
    (0xE808, 0x21A7, "gamepad-a (bottom)"), (0xE809, 0x21A6, "gamepad-b (right)"), (0xE80A, 0x21A4, "gamepad-x (left)"), (0xE80B, 0x21A5, "gamepad-y (top)"),
    (0xE810, 0x2198, "xbox-lb"), (0xE811, 0x2199, "xbox-rb"), (0xE812, 0x2196, "xbox-lt"), (0xE813, 0x2197, "xbox-rt"),
    (0xE814, 0x21B0, "sony-l1"), (0xE815, 0x21B1, "sony-r1"), (0xE816, 0x21B2, "sony-l2"), (0xE817, 0x21B3, "sony-r2"),
    (0xE818, 0x219C, "nintendo-l"), (0xE819, 0x219D, "nintendo-r"), (0xE81A, 0x219A, "nintendo-zl"), (0xE81B, 0x219B, "nintendo-zr"),
    (0xE820, 0x21FA, "xbox-view"), (0xE821, 0x21FB, "xbox-menu"), (0xE822, 0x2206, "dualsense-create"), (0xE823, 0x2208, "dualsense-options"),
    (0xE824, 0x21FD, "nintendo-minus"), (0xE825, 0x21FE, "nintendo-plus"),
    (0xE830, 0x21CE, "dpad"), (0xE831, 0x219F, "dpad-up"), (0xE832, 0x21A1, "dpad-down"), (0xE833, 0x219E, "dpad-left"),
    (0xE834, 0x21A0, "dpad-right"), (0xE835, 0x21A3, "dpad-up-down"), (0xE836, 0x21A2, "dpad-left-right"),
    (0xE838, 0x21CB, "analog-l"), (0xE839, 0x21CC, "analog-r"), (0xE83A, 0x21BF, "analog-r-up"), (0xE83B, 0x21C3, "analog-r-down"),
    (0xE83C, 0x21F5, "analog-r-up-down"), (0xE83D, 0x21BA, "analog-l-click"), (0xE83E, 0x21BB, "analog-r-click"),
    (0xE840, 0x242F, "keyboard-escape"), (0xE841, 0x242E, "keyboard-enter"), (0xE842, 0x242B, "keyboard-tab"), (0xE843, 0x243A, "keyboard-space"),
    (0xE844, 0x2427, "keyboard-control"), (0xE845, 0x23F4, "keyboard-left"), (0xE846, 0x23F6, "keyboard-up"), (0xE847, 0x23F5, "keyboard-right"),
    (0xE848, 0x23F7, "keyboard-down"), (0xE849, 0x2424, "keyboard-arrows"), (0xE84A, 0x2423, "keyboard-wasd"), (0xE84B, 0x2425, "keyboard-ijkl"),
    (0xE84C, 0x2464, "keyboard-f5"), (0xE84D, 0x2465, "keyboard-f6"), (0xE84E, 0x2466, "keyboard-f7"),
    # Letter and digit keys (PromptFont draws them on the fullwidth letters and digits),
    # so a hint's bound key is an icon whatever the player bound (user, 2026-09-30).
    (0xE850, 0xFF21, "key-a"),     (0xE851, 0xFF22, "key-b"),     (0xE852, 0xFF23, "key-c"),     (0xE853, 0xFF24, "key-d"),
    (0xE854, 0xFF25, "key-e"),     (0xE855, 0xFF26, "key-f"),     (0xE856, 0xFF27, "key-g"),     (0xE857, 0xFF28, "key-h"),
    (0xE858, 0xFF29, "key-i"),     (0xE859, 0xFF2A, "key-j"),     (0xE85A, 0xFF2B, "key-k"),     (0xE85B, 0xFF2C, "key-l"),
    (0xE85C, 0xFF2D, "key-m"),     (0xE85D, 0xFF2E, "key-n"),     (0xE85E, 0xFF2F, "key-o"),     (0xE85F, 0xFF30, "key-p"),
    (0xE860, 0xFF31, "key-q"),     (0xE861, 0xFF32, "key-r"),     (0xE862, 0xFF33, "key-s"),     (0xE863, 0xFF34, "key-t"),
    (0xE864, 0xFF35, "key-u"),     (0xE865, 0xFF36, "key-v"),     (0xE866, 0xFF37, "key-w"),     (0xE867, 0xFF38, "key-x"),
    (0xE868, 0xFF39, "key-y"),     (0xE869, 0xFF3A, "key-z"),
    (0xE870, 0xFF10, "key-0"),     (0xE871, 0xFF11, "key-1"),     (0xE872, 0xFF12, "key-2"),     (0xE873, 0xFF13, "key-3"),     (0xE874, 0xFF14, "key-4"),
    (0xE875, 0xFF15, "key-5"),     (0xE876, 0xFF16, "key-6"),     (0xE877, 0xFF17, "key-7"),     (0xE878, 0xFF18, "key-8"),     (0xE879, 0xFF19, "key-9"),
    (0xE87A, 0x242D, "keyboard-backspace"), (0xE87B, 0x2429, "keyboard-shift"), (0xE87C, 0x2428, "keyboard-alt"), (0xE87D, 0x2437, "keyboard-delete"),
]

LICENCE_HEADER = """SRW64 Prompts (content/fonts/SRW64Prompts.ttf)
==============================================

Button and key icons for the SRW64 hints. Every glyph comes from PromptFont by
Yukari "Shinmera" Hafner, available at https://shinmera.com/promptfont
(PromptFont is based on the Xolonium font by Severin Meyer).

This font is a Modified Version of PromptFont under the SIL Open Font License 1.1
below: it keeps {count} of PromptFont's glyphs, moves them to the Private Use Area
from U+E800, scales them by 700/660 and up to a Chinese character's height, and
takes the vertical metrics of HarmonyOS Sans SC. It is built by
tools/content/build_prompt_font.py from the PromptFont release shipped with
Zelda64Recomp (assets/promptfont, 2023-12-29, SHA-256
{sha}). Included trademarks belong to their respective owners.

The PromptFont licence follows unchanged.

"""


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(source: Path = SOURCE, output: Path = OUTPUT, licence_output: Path = LICENCE_OUTPUT) -> None:
    if sha256(source) != SOURCE_SHA256:
        raise SystemExit(f"{source} is not the PromptFont release this font is built from (SHA-256 mismatch)")
    if sha256(LICENCE if source == SOURCE else source.with_name("LICENSE.txt")) != LICENCE_SHA256:
        raise SystemExit("PromptFont's LICENSE.txt beside the font does not match")
    font = TTFont(source)
    names = {pua: font.getBestCmap()[code] for pua, code, _ in GLYPHS}
    options = subset.Options()
    options.layout_features = []
    options.name_IDs = []
    options.notdef_outline = False
    options.glyph_names = True
    options.recalc_timestamp = False
    subsetter = subset.Subsetter(options)
    subsetter.populate(glyphs=sorted(set(names.values())))
    subsetter.subset(font)
    for tag in ("GSUB", "GPOS", "GDEF", "kern", "FFTM"):
        if tag in font:
            del font[tag]

    glyf, hmtx = font["glyf"], font["hmtx"]
    for name in font.getGlyphOrder():
        glyph = glyf[name]
        if glyph.isComposite():
            raise SystemExit(f"{name} is a composite glyph; scale its components first")
        grow = 1.0
        if glyph.numberOfContours:
            glyph.coordinates.scale((SCALE, SCALE))
            glyph.recalcBounds(glyf)
            grow = max(1.0, min(GROW, TALL / (glyph.yMax - glyph.yMin)))
            middle = (glyph.yMax + glyph.yMin) / 2
            glyph.coordinates.transform(((grow, 0), (0, grow)))
            glyph.coordinates.translate((0, MIDDLE - middle * grow))
            glyph.coordinates.toInt()
            glyph.recalcBounds(glyf)
        advance, _ = hmtx[name]
        hmtx[name] = (round(advance * SCALE * grow), glyph.xMin if glyph.numberOfContours else 0)
    # An empty .notdef, so a character outside the table falls through to nothing.
    glyf[".notdef"] = Glyph()
    hmtx[".notdef"] = (500, 0)

    table = cmap_format_4(4)
    table.platformID, table.platEncID, table.language = 3, 1, 0
    table.cmap = {pua: names[pua] for pua in names}
    unicode = cmap_format_4(4)
    unicode.platformID, unicode.platEncID, unicode.language = 0, 3, 0
    unicode.cmap = dict(table.cmap)
    font["cmap"] = newTable("cmap")
    font["cmap"].tableVersion = 0
    font["cmap"].tables = [unicode, table]

    font["hhea"].ascent, font["hhea"].descent, font["hhea"].lineGap = ASCENT, DESCENT, 0
    os2 = font["OS/2"]
    os2.sTypoAscender, os2.sTypoDescender, os2.sTypoLineGap = ASCENT, DESCENT, 0
    os2.usWinAscent, os2.usWinDescent = ASCENT, -DESCENT
    os2.usFirstCharIndex, os2.usLastCharIndex = min(names), max(names)
    os2.ulUnicodeRange1 = os2.ulUnicodeRange2 = os2.ulUnicodeRange3 = os2.ulUnicodeRange4 = 0
    os2.setUnicodeRanges({60})  # Private Use Area
    font["head"].created = font["head"].modified = BUILT

    name = font["name"]
    name.names = []
    for number, text in {
        0: 'Glyphs from PromptFont by Yukari "Shinmera" Hafner (https://shinmera.com/promptfont); modified for SRW64',
        1: "SRW64 Prompts", 2: "Regular", 3: "SRW64 Prompts Regular 1.0", 4: "SRW64 Prompts Regular",
        5: "Version 1.0", 6: "SRW64Prompts-Regular",
        13: "This Font Software is licensed under the SIL Open Font License, Version 1.1.",
        14: "https://openfontlicense.org",
    }.items():
        name.setName(text, number, 3, 1, 0x409)
    font.save(output)
    licence = (LICENCE if source == SOURCE else source.with_name("LICENSE.txt")).read_text()
    licence_output.write_text(LICENCE_HEADER.format(count=len(GLYPHS), sha=SOURCE_SHA256) + licence)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", type=Path, default=SOURCE)
    args = parser.parse_args()
    build(args.source)
    print(OUTPUT, OUTPUT.stat().st_size, "bytes,", len(GLYPHS), "glyphs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
