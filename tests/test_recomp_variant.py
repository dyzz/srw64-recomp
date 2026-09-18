from __future__ import annotations

import hashlib
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from recomp.toolchain.audit_rom_variant import VariantError, audit


class NativeVariantTests(unittest.TestCase):
    def test_resource_change_passes_but_loader_data_change_is_rejected(self) -> None:
        baseline = bytes(0x100100)
        sections = {"schema": "srw64.recomp-code-sections.v1",
                    "rom_sha256": hashlib.sha256(baseline).hexdigest(),
                    "sections": [{"name": "overlay", "rom_start": 0x100000,
                                  "rom_end": 0x100040, "sha256": hashlib.sha256(bytes(64)).hexdigest()}]}
        candidate = bytearray(baseline)
        candidate[-1] = 1
        report = audit(baseline, candidate, hashlib.sha256(candidate).hexdigest(), sections)
        self.assertEqual(report["status"], "static-compatible")
        candidate[0x100030] = 1  # Initialized data must remain identical too.
        with self.assertRaisesRegex(VariantError, "full loader section"):
            audit(baseline, candidate, hashlib.sha256(candidate).hexdigest(), sections)

    def test_identity_and_initial_load_changes_are_rejected(self) -> None:
        baseline = bytes(0x100100)
        sections = {"schema": "srw64.recomp-code-sections.v1",
                    "rom_sha256": hashlib.sha256(baseline).hexdigest(), "sections": []}
        candidate = bytearray(baseline)
        candidate[0x40] = 1
        with self.assertRaisesRegex(VariantError, "pinned resource variant"):
            audit(baseline, candidate, sections["rom_sha256"], sections)
        with self.assertRaisesRegex(VariantError, "initial 1 MiB"):
            audit(baseline, candidate, hashlib.sha256(candidate).hexdigest(), sections)
        with self.assertRaisesRegex(VariantError, "section list is empty"):
            audit(baseline, baseline, sections["rom_sha256"], sections)
