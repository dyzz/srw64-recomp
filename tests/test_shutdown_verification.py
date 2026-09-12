"""Do not confuse absent diagnostics, exit zero, or a late join with safety."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/recomp"))
from verification_support import shutdown_verified


class ShutdownVerificationTests(unittest.TestCase):
    def test_requires_join_and_ordered_observations(self) -> None:
        joined = "SRW64_GUEST_THREADS_JOINED created=4 joined=4 remaining=0\n"
        before = "SRW64_SHUTDOWN_BOUNDARY phase=before_rdram_free guest_threads=0\n"
        after = "SRW64_SHUTDOWN_BOUNDARY phase=after_rdram_free guest_threads=0\n"
        self.assertTrue(shutdown_verified(joined + before + after))
        for log in ("", before + after, joined, joined + after, before + joined + after,
                    joined.replace("joined=4", "joined=3") + before + after,
                    joined + before.replace("threads=0", "threads=4") + after,
                    joined + before + after + after):
            self.assertFalse(shutdown_verified(log), log)
