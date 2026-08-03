from __future__ import annotations

import csv
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
WORKS_PATH = ROOT / "reference" / "appearing-works.csv"


class AppearingWorksReferenceTests(unittest.TestCase):
    def setUp(self) -> None:
        with WORKS_PATH.open(encoding="utf-8", newline="") as handle:
            self.rows = list(csv.DictReader(handle))

    def test_ids_and_rom_categories_are_complete_and_unique(self) -> None:
        ids = [row["id"] for row in self.rows]
        self.assertEqual(len(ids), len(set(ids)))

        category_keys = {
            row["rom_category_key"]
            for row in self.rows
            if row["rom_category_key"]
        }
        expected = {f"t00_{text_id:05d}" for text_id in range(60, 85)}
        self.assertEqual(category_keys, expected)

    def test_participation_boundaries_are_explicit(self) -> None:
        licensed = [
            row for row in self.rows if row["entry_type"] == "licensed_work"
        ]
        original = [row for row in self.rows if row["entry_type"] == "original"]
        supplemental = [
            row
            for row in self.rows
            if row["entry_type"] == "supplemental_source"
        ]

        self.assertEqual(len(licensed), 25)
        self.assertEqual(len(original), 1)
        self.assertEqual(len(supplemental), 4)
        self.assertEqual(
            sum(row["availability"] == "native" for row in licensed), 22
        )
        self.assertEqual(
            sum(row["availability"] == "link_battler_only" for row in licensed),
            3,
        )
        self.assertEqual(sum(row["first_srw_in_64"] == "yes" for row in licensed), 3)


if __name__ == "__main__":
    unittest.main()
