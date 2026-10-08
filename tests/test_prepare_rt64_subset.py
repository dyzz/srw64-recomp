"""prepare_rt64.py accepts a graphics source patched by an earlier revision of its lists:
the original with some of the patches applied in order, and nothing else."""
from __future__ import annotations

import itertools
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from recomp.toolchain.prepare_rt64 import patched_subset  # noqa: E402

ORIGINAL = "alpha();\nbeta();\ngamma();\ndelta();\n"
STEPS = [
    ("alpha();", "alpha();\n#ifdef X\nextra();\n#endif"),   # inserts after its context
    ("beta();\ngamma();", "gamma();"),                       # deletes, keeping context
    ("gamma();", "gamma(1);"),                              # replaces
    ("extra();", "extra(2);"),                              # changes an earlier step's text
    ("delta();", "delta(); // kept"),                       # inserts after, on the same line
]


def apply(steps) -> str:
    text = ORIGINAL
    for before, after in steps:
        if text.count(before) != 1:
            return None
        text = text.replace(before, after)
    return text


class PatchedSubsetTest(unittest.TestCase):
    def test_every_subset_in_order_is_accepted(self):
        for count in range(len(STEPS) + 1):
            for subset in itertools.combinations(STEPS, count):
                text = apply(subset)
                if text is not None:
                    self.assertTrue(patched_subset(text, ORIGINAL, STEPS), subset)

    def test_other_changes_are_refused(self):
        self.assertFalse(patched_subset(ORIGINAL + "local();\n", ORIGINAL, STEPS))
        self.assertFalse(patched_subset(apply(STEPS).replace("gamma(1);", "gamma(3);"), ORIGINAL, STEPS))
        self.assertFalse(patched_subset(ORIGINAL.replace("delta();", "delta(9);"), ORIGINAL, STEPS))

    def test_many_steps_stay_fast(self):
        # Listing subsets doubled per step: 17 on plume_vulkan.cpp ran CI out of memory.
        original = "".join(f"line{i}();\n" for i in range(100)) + "x" * 200_000
        steps = [(f"line{i}();", f"line{i}(); // {i}") for i in range(40)]
        text = original
        for before, after in steps[::2]:
            text = text.replace(before, after)
        self.assertTrue(patched_subset(text, original, steps))
        self.assertFalse(patched_subset(text + "y", original, steps))


if __name__ == "__main__":
    unittest.main()
