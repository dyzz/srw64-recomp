from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

from tools.recomp.libretro_runner import (
    BUTTON_IDS,
    INPUT_SCRIPT_SCHEMA,
    RunnerError,
    _compile_events,
    _load_script,
)


ROOT = Path(__file__).resolve().parents[1]


class LibretroRunnerTests(unittest.TestCase):
    def test_compile_events_maps_and_overlaps_buttons(self) -> None:
        states = _compile_events(
            {
                "events": [
                    {"frame": 1, "button": "start", "duration": 2},
                    {"frame": 2, "button": "n64_a", "duration": 2},
                ]
            },
            frame_count=4,
        )

        self.assertEqual(states[1], {BUTTON_IDS["start"]})
        self.assertEqual(
            states[2], {BUTTON_IDS["start"], BUTTON_IDS["n64_a"]}
        )
        self.assertEqual(states[3], {BUTTON_IDS["n64_a"]})

    def test_compile_events_rejects_unknown_button_and_bad_range(self) -> None:
        invalid_documents = [
            {"events": [{"frame": 0, "button": "power", "duration": 1}]},
            {"events": [{"frame": 3, "button": "start", "duration": 2}]},
        ]
        for document in invalid_documents:
            with self.subTest(document=document), self.assertRaises(RunnerError):
                _compile_events(document, frame_count=4)

    def test_load_script_requires_versioned_schema(self) -> None:
        path = ROOT / "config/recomp/reference-load-continue.json"
        self.assertEqual(_load_script(path)["schema"], INPUT_SCRIPT_SCHEMA)
        with tempfile.TemporaryDirectory() as directory:
            invalid = Path(directory) / "input.json"
            invalid.write_text('{"events": [], "screenshots": []}\n', encoding="utf-8")
            with self.assertRaises(RunnerError):
                _load_script(invalid)

    def test_checked_in_input_scripts_compile_for_their_run_lengths(self) -> None:
        scripts = {"reference-load-continue.json": 3000}
        for filename, frame_count in scripts.items():
            with self.subTest(filename=filename):
                document = json.loads(
                    (ROOT / "config/recomp" / filename).read_text(encoding="utf-8")
                )
                self.assertEqual(document["schema"], INPUT_SCRIPT_SCHEMA)
                _compile_events(document, frame_count)
                self.assertTrue(
                    all(
                        0 <= int(frame) < frame_count
                        for frame in document["screenshots"]
                    )
                )


if __name__ == "__main__":
    unittest.main()
