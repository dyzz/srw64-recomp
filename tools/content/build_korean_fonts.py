#!/usr/bin/env python3
"""Build the Korean faces in content/fonts from their official files (docs/design/korean.md).

The translator chose them; the host draws both texts itself, so each is subset to what it
shows and renamed, as a Modified Version under the SIL OFL 1.1 must be when the font has a
Reserved Font Name; the copyright and licence records stay.

- SRW64BrushKO.ttf, the chapter title cards: Nanum Brush Script (NHN, OFL 1.1, Reserved
  Font Names Nanum and NanumBrush among others; github.com/google/fonts, ofl/nanumbrushscript),
  subset to the 2,350 Hangul syllables of KS X 1001, every Korean chapter title, ASCII and
  the punctuation. A title outside the subset falls back to Droid Sans Fallback.
- SRW64BannerKO.ttf, the battle banners (critical, barriers, decoys, shield defence) and the
  response badges: Gmarket Sans Bold (eBay Korea, OFL 1.1, Reserved Font Name Gmarket Sans
  Font; corp.gmarket.com/fonts, GmarketSansTTF.zip), subset the same way to KS X 1001 Hangul,
  the Korean banner and badge texts, ASCII and the punctuation.

Needs fontTools; the built fonts are tracked, so this runs only when the titles or banners change.

    python tools/content/build_korean_fonts.py --nanum NanumBrushScript-Regular.ttf --gmarket GmarketSansTTFBold.ttf
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_title_fonts import FONTS, ROOT, chapter_titles, subset_font  # noqa: E402

PUNCTUATION = "·…—–「」『』“”‘’（）《》！？，、。：；~"
# The banner and badge texts (sprite_text.cpp kBannerRecords, kBannerLabels, hud_dummy, the
# badges' records) in Korean.
BANNER_RECORDS = ("01019", "01020", "01021", "01022", "01023", "01024", "01025", "01026", "01100", "01101")
BANNER_LABELS = ("hud_shield_defense", "hud_critical", "hud_dummy")


def hangul() -> str:
    """The 2,350 syllables of KS X 1001 (EUC-KR): the everyday Hangul."""
    return "".join(c for c in map(chr, range(0xAC00, 0xD7A4)) if len(c.encode("euc_kr", "ignore")) == 2)


def banners() -> str:
    document = json.loads((ROOT / "content/locales/ko.json").read_text(encoding="utf-8"))
    records = {row["key"]: row["target"] for row in document["entries"]}
    text = "".join(records.get(f"base:t00_{key}", "").replace("<END>", "") for key in BANNER_RECORDS)
    return text + "".join(document["ui"].get(key, "") for key in BANNER_LABELS)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--nanum", type=Path, help="NanumBrushScript-Regular.ttf from github.com/google/fonts (ofl/nanumbrushscript)")
    parser.add_argument("--gmarket", type=Path, help="GmarketSansTTFBold.ttf from corp.gmarket.com/fonts (GmarketSansTTF.zip)")
    args = parser.parse_args()
    ascii_ = "".join(map(chr, range(0x20, 0x7F)))
    if args.nanum:
        text = hangul() + chapter_titles("ko") + ascii_ + PUNCTUATION
        print(f"SRW64BrushKO.ttf: {subset_font(args.nanum, FONTS / 'SRW64BrushKO.ttf', text, 'SRW64 Brush KO', 'Nanum Brush Script')} characters")
    if args.gmarket:
        text = hangul() + banners() + ascii_ + PUNCTUATION
        print(f"SRW64BannerKO.ttf: {subset_font(args.gmarket, FONTS / 'SRW64BannerKO.ttf', text, 'SRW64 Banner KO', 'Gmarket Sans TTF Bold')} characters")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
