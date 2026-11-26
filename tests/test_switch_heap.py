"""The Switch heap's usable stretch and the thread-stack records (src/host/switch/heap_layout.hpp).

The program itself only runs on a Switch; what it decides is plain C++, tested here against
the odd answers svcQueryMemory can give (tests/native_switch_heap.cpp), and the platform code
is checked to go through it.
"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / "src/host/switch/switch_platform.cpp"


class SwitchHeapTest(unittest.TestCase):
    def test_layout_and_records_under_sanitizers(self):
        compiler = shutil.which("clang++") or shutil.which("c++") or shutil.which("g++")
        if not compiler:
            self.skipTest("no C++ compiler")
        with tempfile.TemporaryDirectory() as temporary:
            program = Path(temporary) / "native_switch_heap"
            flags = ["-std=c++20", "-O1", "-g", "-Wall", "-Wextra", "-Wpedantic", "-Werror"]
            sanitize = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
            build = subprocess.run([compiler, *flags, *sanitize, "-I", str(ROOT / "src/host"),
                                    str(ROOT / "tests/native_switch_heap.cpp"), "-o", str(program)],
                                   capture_output=True, text=True)
            if build.returncode != 0 and "sanitize" in build.stderr:
                build = subprocess.run([compiler, *flags, "-I", str(ROOT / "src/host"),
                                        str(ROOT / "tests/native_switch_heap.cpp"), "-o", str(program)],
                                       capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            result = subprocess.run([str(program)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("switch heap: ok", result.stdout)

    def test_platform_goes_through_the_tested_code(self):
        source = PLATFORM.read_text(encoding="utf-8")
        self.assertIn('#include "heap_layout.hpp"', source)
        initheap = source[source.index("void __libnx_initheap(void)"):]
        initheap = initheap[:initheap.index("\n}\n")]
        # The walk is the tested one, nothing usable stops the program, and newlib gets
        # exactly the stretch it chose.
        self.assertIn("srw64::switch_heap::choose(", initheap)
        self.assertIn("if (!heap_layout.usable()) diagAbortWithResult(", initheap)
        self.assertIn("fake_heap_start = reinterpret_cast<char*>(heap_layout.start);", initheap)
        self.assertNotIn("for (u64 at", initheap)
        self.assertIn("static_assert(srw64::switch_heap::kTypeHeap == MemType_Heap", source)
        # init.c sets the heap up before the C++ constructors run, which would wipe a layout
        # that needed one.
        self.assertIn("constinit srw64::switch_heap::Layout heap_layout{};", source)
        # The ring: a slot from pick_slot only, and the join lets its record go under the lock
        # after reading what it reports.
        record = source[source.index("void record_stack("):]
        record = record[:record.index("\n}\n")]
        self.assertIn("srw64::switch_heap::pick_slot(", record)
        self.assertIn("if (index == kStackRecords)", record)
        join = source[source.index("void* __wrap___syscall_thread_join("):]
        join = join[:join.index("\n}\n")]
        after = join[join.index("__real___syscall_thread_join(thread)"):]
        self.assertLess(after.index("stack = r->stack"), after.index("r->state = 4"))
        self.assertNotIn("r->stack, r->size", after)


if __name__ == "__main__":
    unittest.main()
