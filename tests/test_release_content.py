"""ROM-free fixtures for the local-only standalone content export."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("export_content", ROOT / "tools/release/export_content.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def sha(data):
    return hashlib.sha256(data).hexdigest()


class StandaloneContentTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.prepared = self.root / "prepared content"
        (self.prepared / "name-entry").mkdir(parents=True)
        self.image = self.prepared / "name-entry/face-27.png"
        self.image.write_bytes(b"synthetic portrait, not game data")
        self.data = {
            "schema": "srw64.native-dialogue-data.v2", "rom_sha256": "a" * 64,
            "name_entry_assets": {"schema": "srw64.name-entry-assets.v1", "portraits": {"27": {
                "original": str(self.image), "original_sha256": sha(self.image.read_bytes()),
                "hd": "/not/copied/hd.png", "hd_sha256": "b" * 64}}},
        }
        self.profile = {
            "schema": "srw64.prepared-profile.v1", "rom_sha256": "a" * 64,
            "profile": {"baseline": "srw64-jp-rev0", "presentation": {"images": "original", "resolution_scale": 2}},
            "dialogue": {},
        }
        self.output = self.root / "relocatable content"
        self.write_input()

    def write_input(self):
        raw = json.dumps(self.data).encode()
        (self.prepared / "dialogue.json").write_bytes(raw)
        self.profile["dialogue"]["sha256"] = sha(raw)
        (self.prepared / "profile.json").write_text(json.dumps(self.profile))

    def test_export_and_relocate(self):
        manifest = MODULE.export_content(self.prepared, self.output)
        moved = self.root / "moved content"
        self.output.rename(moved)
        for relative, expected in manifest["files"].items():
            self.assertEqual(sha((moved / relative).read_bytes()), expected)
        data = json.loads((moved / "dialogue.json").read_text())
        portrait = data["name_entry_assets"]["portraits"]["27"]
        self.assertEqual(portrait["original"], "name-entry/face-27.png")
        self.assertNotIn("hd", portrait)
        self.assertNotIn(str(self.prepared), (moved / "dialogue.json").read_text())
        self.assertFalse((moved / "profile.json").exists())
        self.assertEqual(manifest["distribution"], "local-only-rom-derived-content")

    def test_destination_is_never_overwritten(self):
        self.output.mkdir()
        marker = self.output / "keep.txt"
        marker.write_text("keep")
        with self.assertRaises(FileExistsError):
            MODULE.export_content(self.prepared, self.output)
        self.assertEqual(marker.read_text(), "keep")

    def test_tampered_dialogue_is_rejected(self):
        (self.prepared / "dialogue.json").write_bytes(b"{}")
        with self.assertRaisesRegex(ValueError, "dialogue changed"):
            MODULE.export_content(self.prepared, self.output)
        self.assertFalse(self.output.exists())

    def test_tampered_portrait_is_rejected(self):
        self.image.write_bytes(b"different")
        with self.assertRaisesRegex(ValueError, "portrait changed"):
            MODULE.export_content(self.prepared, self.output)
        self.assertFalse(self.output.exists())

    def test_external_file_is_not_copied(self):
        outside = self.root / "private.txt"
        outside.write_bytes(b"not content")
        row = self.data["name_entry_assets"]["portraits"]["27"]
        row.update(original=str(outside), original_sha256=sha(outside.read_bytes()))
        self.write_input()
        with self.assertRaisesRegex(ValueError, "inside the prepared"):
            MODULE.export_content(self.prepared, self.output)

    def test_portrait_id_cannot_escape(self):
        portraits = self.data["name_entry_assets"]["portraits"]
        portraits["../bad"] = portraits.pop("27")
        self.write_input()
        with self.assertRaisesRegex(ValueError, "portrait id"):
            MODULE.export_content(self.prepared, self.output)

    def test_hd_is_not_silently_downgraded(self):
        self.profile["profile"]["presentation"]["images"] = "hd"
        self.write_input()
        with self.assertRaisesRegex(ValueError, "Original mode only"):
            MODULE.export_content(self.prepared, self.output)

    def test_mismatched_rom_is_rejected(self):
        self.profile["rom_sha256"] = "c" * 64
        self.write_input()
        with self.assertRaisesRegex(ValueError, "identities differ"):
            MODULE.export_content(self.prepared, self.output)

    def test_schema_is_checked(self):
        self.data["schema"] = "unknown"
        self.write_input()
        with self.assertRaisesRegex(ValueError, "v2 native dialogue"):
            MODULE.export_content(self.prepared, self.output)

    def test_resolution_is_checked(self):
        self.profile["profile"]["presentation"]["resolution_scale"] = True
        self.write_input()
        with self.assertRaisesRegex(ValueError, "scale"):
            MODULE.export_content(self.prepared, self.output)


if __name__ == "__main__":
    unittest.main()
