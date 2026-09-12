"""Shared polling and shutdown observations for live native verification.

An exit code describes the process outcome. It does not establish that guest
threads were joined before RDRAM was released; preserve that evidence separately.
"""
from __future__ import annotations

from collections.abc import Callable
import json
from pathlib import Path
import re
import time
from typing import TypeVar

T = TypeVar("T")


def wait_for(read: Callable[[], T], predicate: Callable[[T], bool], seconds: float = 12,
             *, description: str = "native verification condition") -> T:
    """Retry missing/partly written snapshots, while propagating actual errors."""
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            value = read()
            if predicate(value):
                return value
        except (FileNotFoundError, json.JSONDecodeError):
            pass
        time.sleep(.04)
    raise TimeoutError(f"Timed out after {seconds:g}s waiting for {description}")


def wait_for_report(run: Path, seconds: float = 30) -> dict:
    return wait_for(lambda: json.loads((run / "report.json").read_text()),
                    lambda report: report.get("status") not in (None, "incomplete"),
                    seconds, description=f"final host report in {run}")


def write_control(path: Path, text: str) -> None:
    """Publish a complete request; readers must never consume a partial line."""
    temporary = path.with_name(path.name + ".pending")
    temporary.write_text(text)
    temporary.replace(path)


def shutdown_observation(log: str) -> dict[str, list[int]]:
    """Empty lists mean unobserved, not zero live guest threads."""
    return {phase: [int(value) for value in re.findall(
        rf"SRW64_SHUTDOWN_BOUNDARY phase={phase} guest_threads=(\d+)", log)]
        for phase in ("before_rdram_free", "after_rdram_free")}


def shutdown_verified(log: str) -> bool:
    """Require actual joins, successful OS observations and their ordering."""
    joined = re.search(r"SRW64_GUEST_THREADS_JOINED created=(\d+) joined=(\d+) remaining=0", log)
    observed = shutdown_observation(log)
    if not joined or joined[1] != joined[2] or any(values != [0] for values in observed.values()):
        return False
    before = log.index("SRW64_SHUTDOWN_BOUNDARY phase=before_rdram_free")
    after = log.index("SRW64_SHUTDOWN_BOUNDARY phase=after_rdram_free")
    return joined.start() < before < after
