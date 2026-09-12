import copy
import json
from pathlib import Path
import unittest

from srw64_native.catalog import source_catalog
from srw64_native.original_data import extract_gameplay, snapshot_observation
from srw64_native.original_profiles import attach_profiles, skill_ranks

ROOT = Path(__file__).resolve().parents[1]


class SkillRankViewTests(unittest.TestCase):
    def test_counting_semantics_handles_zero_holes_unsorted_and_tied_levels(self):
        self.assertEqual(skill_ranks([5, 0, 3, 3, 12, 0, 0, 0, 0]),
                         [3, 3, 5, 12, None, None, None, None, None])


@unittest.skipUnless((ROOT / "rom.z64").exists(), "Local original ROM required")
class OriginalProfileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sources, _, _ = source_catalog(ROOT, ROOT / "rom.z64")
        layout = json.loads((ROOT / "config/data/original-jp-v1.json").read_text())
        cls.original = extract_gameplay((ROOT / "rom.z64").read_bytes(), layout, cls.sources)

    def setUp(self):
        self.data = copy.deepcopy(self.original)
        attach_profiles(self.data, self.sources)

    def test_pilot_joins_by_mapping_instead_of_actor_index(self):
        p = self.data["actors"][28]["profile"]
        self.assertEqual(p["full_name"], self.sources["base:t00_04771"].replace("<END>", ""))
        self.assertEqual(p["stats"][0]["source_key"], "base:pilot_stats:0018")
        self.assertEqual([s["value"] for s in p["stats"]], [147, 128, 98, 97, 97, 100, 100])
        self.assertEqual([(s["command_name"], s["level"]) for s in p["spirits"]],
                         [("幸運", 1), ("ひらめき", 3), ("必中", 7), ("熱血", 11), ("気合", 16), ("魂", 40)])
        skills = {s["name"]: s["ranks"] for s in p["skills"]}
        self.assertEqual(skills["底力"], [5, 12, 21, 29, 34, 45, 52, None, None])
        self.assertEqual(skills["切り払い"], [6, 12, 19, 26, 34, 39, 52, 60, None])

    def test_shared_weapon_rows_keep_current_and_other_forms_distinct(self):
        p = self.data["units"][216]["profile"]
        self.assertEqual(len(p["weapons"]), 11)
        self.assertEqual([w["key"] for w in p["weapons"] if not w["matches_form"]],
                         ["base:weapons:0853", "base:weapons:0854"])
        self.assertEqual({f["key"] for f in p["related_forms"]}, {"base:units:0217", "base:units:0218"})
        sword = self.data["units"][36]["profile"]["weapons"][0]
        self.assertEqual(sword["label"], "ライトニングソード")
        self.assertEqual({s["id"]: s["value"] for s in sword["stats"]}["ammo"], -1)

    def test_missing_negative_and_overlapping_mappings_do_not_fabricate_data(self):
        self.assertFalse(self.data["actors"][0]["profile"]["stats"])
        self.assertFalse(self.data["actors"][0]["profile"]["spirits"])
        self.assertTrue(any("-3" in n for n in self.data["actors"][6]["profile"]["notices"]))
        self.assertFalse(self.data["actors"][360]["profile"]["stats"])
        self.assertFalse(self.data["actors"][360]["profile"]["spirits"])
        anomaly = self.data["actors"][284]["profile"]
        self.assertTrue(anomaly["stats"])
        self.assertTrue(all(s["ranks"] is None for s in anomaly["skills"]))
        self.assertTrue(any("256" in n for n in anomaly["notices"]))
        self.assertFalse(any(p["profile"]["observed_pilots"] for p in self.data["units"]))

    def test_raw_identities_and_shared_sources_survive_projection(self):
        for category, originals in self.original.items():
            self.assertEqual([r["key"] for r in originals], [r["key"] for r in self.data[category]])
            for before, after in zip(originals, self.data[category]):
                for field in ("raw_hex", "fields", "source_sha256", "weapon_list", "label"):
                    self.assertEqual(before.get(field), after.get(field), (category, before["key"], field))
        spirit = self.data["spirits"][0]
        self.assertTrue(spirit["related_entities"])
        for pilot in spirit["related_entities"]:
            self.assertIn(pilot["key"], [l["key"] for l in spirit["links"]])
        a = self.data["actors"][28]
        self.assertIn("魂", a["search_terms"])
        self.assertIn("ライトニングソード", self.data["units"][36]["search_terms"])
        # Projection values are independent, even when a source row is shared.
        a["profile"]["stats"][0]["value"] = -123
        self.assertEqual(self.data["pilot_stats"][18]["fields"][0]["value"], 147)

    @unittest.skipUnless((ROOT / "build/recomp/gfx-probes/female-map-audio-1/latest-gfx-rdram.bin").exists(), "Optional historical snapshot")
    def test_observed_pairings_are_bidirectional_and_scoped(self):
        ram = (ROOT / "build/recomp/gfx-probes/female-map-audio-1/latest-gfx-rdram.bin").read_bytes()
        self.data["observations"] = [snapshot_observation(ram, self.data, {})]
        attach_profiles(self.data, self.sources)
        pilots = self.data["units"][36]["profile"]["observed_pilots"]
        self.assertEqual([p["key"] for p in pilots], ["base:actors:0028", "base:actors:0024"])
        self.assertTrue(all(p["confidence"] == "snapshot-observed" for p in pilots))
        self.assertTrue(all(p["observation_key"] == "observation:female-stage1" for p in pilots))
        units = self.data["actors"][165]["profile"]["observed_units"]
        self.assertEqual({u["key"] for u in units}, {"base:units:0216", "base:units:0217", "base:units:0218"})
        before = copy.deepcopy(self.data)
        attach_profiles(self.data, self.sources)
        self.assertEqual(self.data, before)


if __name__ == "__main__":
    unittest.main()
