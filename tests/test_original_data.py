import copy
import json
from pathlib import Path
import struct
import unittest

from srw64_native.catalog import source_catalog
from srw64_native.original_data import (checked_slice, check_layout, extract_gameplay,
                                       numeric_field, pilot_skill_fields, skill_threshold_fields,
                                       snapshot_observation, weapon_list)

ROOT = Path(__file__).resolve().parents[1]


class OriginalDataBoundsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.layout = json.loads((ROOT / "config/data/original-jp-v1.json").read_text())

    def test_span_never_silently_truncates(self):
        for offset, size in [(-1, 1), (0, -1), (3, 2)]:
            with self.assertRaises(ValueError):
                checked_slice(b"1234", offset, size)

    def test_pointer_list_preserves_sentinel_and_rejects_corruption(self):
        config = {"pointer_count": 1, "rom_offset": 0, "first_payload_relative": 4, "region_end": 28}
        rom = struct.pack(">I6H6H", 4, 7, 0, 0xffff, 0xffff, 0xffff, 0xffff, *([0xffff]*6))
        result = weapon_list(rom, config, 0, 10)
        self.assertEqual(result["rows"][0]["remaining_u16"], [0, 65535, 65535, 65535, 65535])
        self.assertEqual(result["rows"][0]["eligible_unit_ids"], [0])
        self.assertEqual(bytes.fromhex(result["raw_hex"]), rom[4:])
        for bad, count in [(b"\0\0\0\0" + rom[4:], 10), (rom, 7), (rom[:16], 10)]:
            with self.assertRaises(ValueError):
                weapon_list(bad, config, 0, count)
        with self.assertRaisesRegex(ValueError, "Unterminated"):
            weapon_list(rom[:16] + rom[4:16], config, 0, 10)
        with self.assertRaisesRegex(ValueError, "eligible unit"):
            weapon_list(rom[:6] + b"\0\1" + rom[8:], config, 0, 10)

    def test_signed_values_scaling_and_sentinel_are_distinct(self):
        specs = {s["id"]: s for s in self.layout["numeric_fields"]["weapons"]}
        raw = bytearray(16)
        raw[1], raw[4], raw[5], raw[13] = 15, 246, 255, 128
        values = {key: numeric_field(raw, spec) for key, spec in specs.items()}
        self.assertEqual((values["power"]["encoded_value"], values["power"]["value"]), (15, 1500))
        self.assertEqual(values["hit_modifier"]["value"], -10)
        self.assertEqual(values["critical_modifier"]["value"], -128)
        self.assertEqual(values["ammo"]["value"], -1)
        self.assertEqual(values["ammo"]["sentinel_meaning"], "无弹数限制")
        raw[5] = 10
        self.assertNotIn("sentinel_meaning", numeric_field(raw, specs["ammo"]))
        with self.assertRaises(ValueError):
            numeric_field(raw[:4], specs["ammo"])

    def test_threshold_padding_is_preserved_but_not_a_tenth_rank(self):
        raw = bytes(range(30))
        fields = skill_threshold_fields(raw, self.layout["pilot_skills"])
        self.assertEqual([f["value"] for f in fields[::2]],
                         [list(range(9)), list(range(10, 19)), list(range(20, 29))])
        self.assertEqual([f["value"] for f in fields[1::2]], [[9], [19], [29]])
        self.assertEqual(bytes(v for f in fields for v in f["value"]), raw)
        with self.assertRaises(ValueError):
            skill_threshold_fields(raw[:-1], self.layout["pilot_skills"])

    def test_skill_name_priority_and_unknown_bits(self):
        config = self.layout["pilot_skills"]
        for flags, expected in [(0, None), (12, "底力"), (24, "NT"), (48, "強化人間"),
                                (96, "聖戦士"), (64, "超能力"), (255, "底力")]:
            raw = bytes(15) + bytes([flags])
            value = pilot_skill_fields(raw, config)[0]["value"]
            self.assertEqual(value["shared_group_name"], expected)
            self.assertEqual(value["unknown_bits"], flags & 128)
            self.assertEqual(sum(s["mask"] for s in value["enabled"]) | value["unknown_bits"], flags)


@unittest.skipUnless((ROOT / "rom.z64").is_file(), "Local original ROM required")
class OriginalDataROMTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rom = (ROOT / "rom.z64").read_bytes()
        cls.layout = json.loads((ROOT / "config/data/original-jp-v1.json").read_text())
        sources, _, _ = source_catalog(ROOT, ROOT / "rom.z64")
        cls.data = extract_gameplay(cls.rom, cls.layout, sources)

    def test_table_records_reassemble_to_original_bytes(self):
        for spec in self.layout["tables"]:
            data = b"".join(bytes.fromhex(r["raw_hex"]) for r in self.data[spec["id"]])
            start = spec["rom_offset"]
            self.assertEqual(data, self.rom[start:start+spec["count"]*spec["stride"]], spec["id"])

    def test_all_weapon_menu_markers_parse_without_changing_original_names(self):
        self.assertEqual(len(self.data["weapons"]), 1329)
        self.assertTrue(all(w["weapon_traits"]["parsed"] for w in self.data["weapons"]))
        net = self.data["weapons"][0]
        self.assertEqual((net["label"], net["menu_label"]), ("アイアンネット", "格アイアンネットP"))
        self.assertEqual([m["token"] for m in net["weapon_traits"]["markers"]], ["格", "P"])

    def test_drift_in_rom_layout_or_code_evidence_fails_closed(self):
        with self.assertRaisesRegex(ValueError, "pinned"):
            check_layout(self.rom[:-1]+bytes([self.rom[-1]^1]), self.layout)
        for collection in ["tables", "evidence"]:
            layout = copy.deepcopy(self.layout)
            layout[collection][0]["sha256"] = "0" * 64
            with self.assertRaisesRegex(ValueError, "changed"):
                check_layout(self.rom, layout)

    def test_real_aliases_and_index_anomalies_survive(self):
        # Distinct pointer slots may deliberately share the same payload.
        self.assertEqual(self.data["units"][1]["weapon_list"]["raw_hex"], self.data["units"][2]["weapon_list"]["raw_hex"])
        for actor in [284, 360]:
            self.assertTrue(any(f["confidence"] == "unknown" for f in self.data["actors"][actor]["fields"]))
        self.assertFalse(any(l["key"].startswith("base:pilot_stats:") for l in self.data["actors"][360]["links"]))
        self.assertEqual(self.data["actor_spirits_map"][6]["fields"][0]["value"], -3)

    def test_stage1_unit_weapon_and_actor_references(self):
        unit = self.data["units"][36]
        self.assertEqual(unit["label"], "スイームルグ")
        self.assertEqual([r["weapon_id"] for r in unit["weapon_list"]["rows"]], [160, 161, 162, 163])
        actor = self.data["actors"][28]
        self.assertEqual(actor["label"], "マナミ")
        self.assertIn("base:pilot_stats:0018", [l["key"] for l in actor["links"]])
        self.assertIn("base:spirits:0013", [l["key"] for l in actor["links"]])
        self.assertEqual(unit["label_confidence"], "code-confirmed")

    def test_names_match_independent_display_code_and_table_boundaries(self):
        # Check display instructions independently of names generated from the layout.
        for offset, instruction in [(0x91d94, 0x24a5020f), (0x92764, 0x24a50a8b),
                                    (0x216b60, 0x24a5055a), (0x94d64, 0x24a503c9)]:
            self.assertEqual(struct.unpack_from(">I", self.rom, offset)[0], instruction)
        for index, plain, menu in [(0, "アイアンネット", "格アイアンネットP"),
                                   (160, "ライトニングソード", "格ライトニングソードP"),
                                   (1328, "格闘", "格格闘P")]:
            row = self.data["weapons"][index]
            self.assertEqual(row["label"], plain)
            self.assertEqual(row["menu_label"], menu)
        commands = {c["command_id"]: c for r in self.data["spirits"] for c in r["fields"][0]["value"]}
        self.assertEqual(set(commands), set(range(30)))
        self.assertEqual(commands[0]["command_name"], "自爆")
        self.assertEqual(commands[29]["command_name"], "復活")
        self.assertEqual(commands[0]["text_key"], "base:t00_00969")

    def test_shared_weapon_lists_preserve_distinct_form_membership(self):
        unit = self.data["units"][216]
        rows = unit["weapon_list"]["rows"]
        self.assertTrue(any(216 in r["eligible_unit_ids"] for r in rows))
        self.assertTrue(any(216 not in r["eligible_unit_ids"] for r in rows))
        for u in self.data["units"]:
            for r in u["weapon_list"]["rows"]:
                self.assertEqual(r["eligible_unit_ids"], [v for v in r["remaining_u16"] if v != 65535])
                self.assertTrue(all(0 <= v < len(self.data["units"]) for v in r["eligible_unit_ids"]))
        self.assertEqual(next(f["value"] for f in unit["fields"] if f["name"] == "修理费用"), 14000)

    def test_confirmed_base_values_and_growth(self):
        def values(category, index):
            return {f["id"]: f["value"] for f in self.data[category][index]["fields"] if "id" in f}
        self.assertEqual(values("units", 36), {"hp": 5300, "en": 160, "movement": 5,
                         "mobility": 70, "armor": 1400, "limit": 260, "repair_cost": 6000})
        self.assertEqual(values("weapons", 160), {"power": 1500, "range_min": 1, "range_max": 1,
                         "hit_modifier": 10, "ammo": -1, "en_cost": 0, "will_required": 0, "critical_modifier": 10})
        self.assertEqual(values("pilot_stats", 18), {"melee": 147, "ranged": 128, "evasion": 98,
                         "accuracy": 97, "reaction": 97, "skill": 100, "sp": 100})
        row = self.data["pilot_stats"][18]
        self.assertEqual({f["id"]: f["growth_per_level"] for f in row["fields"] if "id" in f},
                         {"melee": 1, "ranged": 1, "evasion": 2, "accuracy": 2, "reaction": 1, "skill": 1, "sp": 2})
        self.assertEqual([s["name"] for s in row["fields"][-1]["value"]["enabled"]], ["切り払い", "底力"])

    def test_status_labels_match_independent_rom_coordinates(self):
        for base, count, expected in [
            (0x518c0, 18, [(914, 160, 157), (915, 160, 173), (916, 160, 189), (917, 160, 205)]),
            (0x51968, 18, [(926, 28, 109), (929, 28, 125), (927, 116, 109), (930, 116, 125),
                           (928, 228, 109), (931, 228, 125), (924, 182, 70)]),
            (0x51a80, 15, [(4082, 174, 20), (4083, 230, 20), (4084, 270, 20), (4085, 28, 164),
                           (4091, 28, 188), (4092, 28, 205), (4094, 172, 205)])]:
            rows = [struct.unpack_from(">4H", self.rom, base + i * 8)[:3] for i in range(count)]
            for label in expected:
                self.assertIn(label, rows)

    def test_stage_to_map_and_resource_chain_preserves_boundaries(self):
        stage = self.data["stage_maps"][1]
        self.assertEqual(stage["raw_hex"], "1403")
        self.assertIn("base:map_assets:0020", [l["key"] for l in stage["links"]])
        assets = self.data["map_assets"][20]
        self.assertEqual([f["value"] for f in assets["fields"]], [6284, 6228, 6243, 6422, 6429, 0, 44])
        self.assertEqual([l["key"] for l in assets["links"]],
                         [f"base:resources:{i:04d}" for i in [6284, 6228, 6243, 6422, 6429]])
        self.assertEqual(len(self.data["map_assets"][1]["links"]), 3)  # Zero auxiliary slots are not resource 0.
        self.assertEqual(self.data["stage_maps"][-1]["fields"][-1]["confidence"], "unknown")
        for row in self.data["map_assets"]:
            self.assertTrue(all(0 <= int(l["key"].split(":")[-1]) < 6436 for l in row["links"]))
        # Test the loader instructions independently of metadata and generated labels.
        self.assertEqual(struct.unpack_from(">I", self.rom, 0xf28d4)[0], 0x00021040)
        self.assertEqual(struct.unpack_from(">I", self.rom, 0xf28e0)[0], 0x902295b0)

    def test_overlay_evidence_addresses_belong_to_the_named_load_section(self):
        sections = {s["name"]: s for s in json.loads((ROOT / "config/recomp/code-sections.json").read_text())["sections"]}
        for item in self.layout["evidence"]:
            section = sections[item.get("section", "resident")]
            start = item["rom_offset"]
            self.assertGreaterEqual(start, section["rom_start"])
            end = section["rom_end"] if item.get("kind") == "data" else section["text_end"]
            self.assertLessEqual(start + item["byte_size"], end)
            self.assertEqual(int(item["vram"], 16), section["vram"] + start - section["rom_start"])

    @unittest.skipUnless((ROOT / "build/recomp/gfx-probes/female-map-audio-1/latest-gfx-rdram.bin").exists(), "Optional historical snapshot")
    def test_historical_snapshot_distinguishes_shared_transformations(self):
        ram = (ROOT / "build/recomp/gfx-probes/female-map-audio-1/latest-gfx-rdram.bin").read_bytes()
        observation = snapshot_observation(ram, self.data, {})
        self.assertEqual(observation["scene_indices"], {"stage_index": 1, "map_index": 20})
        assets_spec = next(s for s in self.layout["tables"] if s["id"] == "map_assets")
        start, size = assets_spec["rom_offset"], assets_spec["stride"] * assets_spec["count"]
        self.assertEqual(ram[0x219b1c:0x219b1c+size], self.rom[start:start+size])
        allies = [u for u in observation["instances"] if u["bank"] == 0]
        self.assertEqual([u["unit_key"] for u in allies], ["base:units:0036", "base:units:0216", "base:units:0218", "base:units:0217"])
        self.assertEqual([p["actor_key"] for p in allies[0]["pilots"]], ["base:actors:0028", "base:actors:0024"])
        self.assertEqual(len({u["pilots"][0]["pointer"] for u in allies[1:]}), 1)
        damaged = bytearray(ram)
        damaged[0x16A248:0x16A24C] = b"\xff"*4
        with self.assertRaisesRegex(ValueError, "pointer"):
            snapshot_observation(bytes(damaged), self.data, {})


if __name__ == "__main__":
    unittest.main()
