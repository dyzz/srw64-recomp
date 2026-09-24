"""Cover the glyph rows corrected against the ROM font bitmaps.

The upstream seed marked every row confirmed, but 138 rows disagreed with the
tiles the game actually draws. These tests pin the reviewed readings and the
structural invariants that exposed the misreadings, so a future re-import of the
seed cannot quietly reintroduce them. See docs/guide/provenance.md.
"""
from pathlib import Path
import collections
import csv
import unittest

from srw64_native.catalog import source_catalog
from srw64_rom.glyphs import glyph_map_path, load_glyph_map
from srw64_rom.resources import ResourceTable, decode_i4_texture

ROOT = Path(__file__).resolve().parents[1]
ROM = ROOT / "rom.z64"
NARROW_END = 0x13B      # ids below this are 8x14 tiles, 63 per row
RESOURCE_1_BASE = 0x597  # the second font resource restarts the 14x14 grid

# Readings taken from the tiles; the seed value is in the row's note.
CORRECTED = {457: "艦", 458: "闘", 450: "船", 226: "Ⅲ", 227: "Ⅱ", 316: "防", 398: "努",
             468: "獄", 912: "視", 1516: "潰", 1635: "抹", 1180: "判", 1200: "父",
             217: "%", 218: "～", 2047: "（前）",
             # tiles that hold two compressed characters, like 1454-1459 and 1888
             1462: "真・", 1463: "天馬", 1464: "翔覇",
             1004: "靭", 1562: "悛",
             # spacing compound tiles: each holds the kana it draws
             1934: "バ）", 1946: "ニン", 1950: "ルライト", 1935: "（ビル",
             258: "🔧", 1318: "殲", 1919: "黷"}

# Most of the font is laid out in Japanese reading order, which independently
# places several corrections: e.g. 左 (サ) cannot be 右 (ウ) inside the サ run.
READING_RUNS = {874: "困婚恨混佐左差査才済",    # コン → サ → サイ
                1401: "印右鋭液円往荷",        # イン ウ エイ エキ エン オウ カ
                1106: "徴町眺直沈鎮墜追通",    # チョウ → チョク → チン → ツイ
                1196: "夫婦怖普父腐負赴",      # フ
                1091: "綻胆団断男談",          # タン → ダン
                1000: "辛刃甚尽靭図推",        # シン ジン ズ スイ
                1559: "衆蹴粛悛純照"}          # シュウ シュク シュン ジュン ショウ

# Sentences whose decoding the corrections change; keys from the ROM text tables.
DECODED = {"base:t00_00358": "銀河帝国軍先遣艦隊<END>",
           "base:t00_00318": "スウィートウォーター防衛戦<END>",
           "base:t00_00337": "トレーズ抹殺指令<END>",
           "base:t00_00330": "月は地獄だ!<END>",
           "base:t00_00308": "理想、潰えて<END>",
           "base:t00_00663": "トールギスⅢ<END>",
           "base:t00_00517": "パーツ<END>",
           # compound tiles 1923-1953 must join into the attack names they spell
           "base:t00_02592": "ダブルバーニングファイヤー（グレート）<END>",
           "base:t00_02593": "ダブルライトニングバスター<END>",
           "base:t00_02600": "ツインオーラアタック（ビルバイン）<END>",
           "base:t00_02607": "ダブルバーニングファイヤー（マジンガー）<END>",
           "base:t00_02608": "ダブルバーニングファイヤー（ミネルバ）<END>",  # no X in tile 1934
           "base:t00_02610": "ダブルライトニングバスター（量グレート）<END>",
           "base:t00_02611": "ツインオーラアタック（ダンバイン）<END>",
           "base:t00_02612": "ツインオーラアタック（サーバイン）<END>"}

# The font draws characters in four proportions: 8x14 half-width tiles, 14x14
# full-width tiles, 14x14 tiles that squeeze several characters together, and
# icons. The same character in two proportions (half-width バ and the バ inside
# a compound tile) is expected; within one proportion only these repeat.
FORMS = {"half", "full", "compound", "icon"}
COMPOUND = ({632} | set(range(1454, 1460)) | set(range(1462, 1465)) | {1815, 1816}
            | set(range(1886, 1889)) | set(range(1923, 1954)) | {2047, 2048, 2049})
SAME_FORM_REPEATS = {("皆", "full"): [710, 1823],      # redrawn copies, 3 stroke pixels apart
                     ("討", "full"): [1144, 1853],     # 1 stroke pixel apart
                     ("還", "full"): [1597, 1826],     # 26 stroke pixels apart
                     ("ダブ", "compound"): [1944, 1949],
                     ("イン）", "compound"): [1937, 1940, 1943],
                     ("ニン", "compound"): [1946, 1951]}  # same kana, different edge slivers

# The font draws zero and capital O with one tile; the game picks by ASCII input.
SHARED_TILE = [[1, 25], [1937, 1940, 1943]]


def font_tiles() -> dict:
    table = ResourceTable(ROM.read_bytes())
    return {resource: decode_i4_texture(table.extract(resource)[0])[0] for resource in (0, 1)}


def tile_pixels(images: dict, glyph: int) -> bytes:
    """Font resource 0: 8x14 narrow glyphs, 63 a row, then 14x14 wide ones (as battle_assets.font_tile)."""
    if glyph < NARROW_END:
        image, x, y, width = images[0], glyph % 63 * 8, glyph // 63 * 14, 8
    elif glyph < RESOURCE_1_BASE:
        index = glyph - NARROW_END
        image, x, y, width = images[0], index % 36 * 14, 70 + index // 36 * 14, 14
    else:
        index = glyph - RESOURCE_1_BASE
        image, x, y, width = images[1], index % 36 * 14, index // 36 * 14, 14
    if y + 14 > image.height:
        raise ValueError(f"glyph {glyph} is outside the font resources")
    return image.crop((x, y, x + width, y + 14)).tobytes()


class GlyphMapTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mapping = load_glyph_map(ROOT)
        with glyph_map_path(ROOT).open(encoding="utf-8-sig", newline="") as source:
            cls.rows = list(csv.DictReader(source))

    def test_reviewed_readings_stay_put(self):
        for glyph, character in CORRECTED.items():
            self.assertEqual(self.mapping[glyph], character, f"glyph {glyph}")

    def test_reading_sorted_runs_hold(self):
        for start, run in READING_RUNS.items():
            found = "".join(self.mapping[glyph] for glyph in range(start, start + len(run)))
            self.assertEqual(found, run, f"run from glyph {start}")

    def test_corrected_rows_record_their_evidence(self):
        notes = {int(row["glyph_id"]): row for row in self.rows}
        for glyph in (457, 316, 1635, 217, 2047):
            row = notes[glyph]
            self.assertTrue(row["note"].startswith("bitmap-corrected"), row["note"])
            self.assertEqual(row["status"], "confirmed")

    def test_rows_that_are_not_pinned_down_are_not_confirmed(self):
        hedges = ("unverified", "unconfirmed", "too compressed")
        for row in self.rows:
            if row["status"] != "confirmed":
                self.assertEqual(row["status"], "probable", row)
                self.assertTrue(any(h in row["note"] for h in hedges),
                                f"a probable row must say what is unsettled: {row}")

    def test_every_tile_records_its_proportion(self):
        for row in self.rows:
            glyph, form = int(row["glyph_id"]), row["form"]
            self.assertIn(form, FORMS, row)
            if form == "compound":
                self.assertIn(glyph, COMPOUND, row)
            elif form != "icon":
                self.assertEqual(form, "half" if glyph < NARROW_END else "full", row)
        self.assertEqual({int(r["glyph_id"]) for r in self.rows if r["form"] == "compound"}, COMPOUND)

    def test_repeats_within_one_proportion_are_only_the_documented_ones(self):
        repeated = collections.defaultdict(list)
        for row in self.rows:
            repeated[(row["char"], row["form"])].append(int(row["glyph_id"]))
        self.assertEqual({key: sorted(glyphs) for key, glyphs in repeated.items() if len(glyphs) > 1},
                         SAME_FORM_REPEATS)

    @unittest.skipUnless(ROM.exists(), "Local original ROM required")
    def test_tiles_drawn_alike_read_alike(self):
        images = font_tiles()
        shared = collections.defaultdict(list)
        for glyph in self.mapping:
            shared[tile_pixels(images, glyph)].append(glyph)
        groups = sorted(sorted(glyphs) for glyphs in shared.values() if len(glyphs) > 1)
        self.assertEqual(groups, sorted(SHARED_TILE))
        # 1/25 apart, one tile never carries two readings.
        for glyphs in groups:
            if glyphs != [1, 25]:
                self.assertEqual(len({self.mapping[glyph] for glyph in glyphs}), 1, glyphs)

    @unittest.skipUnless(ROM.exists(), "Local original ROM required")
    def test_corrections_reach_the_decoded_text(self):
        sources, _, _ = source_catalog(ROOT, ROM)
        for key, text in DECODED.items():
            self.assertEqual(sources[key], text)


if __name__ == "__main__":
    unittest.main()
