"""Allow WARP enumeration in a BUILD-DIRECTORY copy of pinned Plume for CI.

Only the two adapter-selection filters change. The compositor, shaders, D3D12
commands, driver and readback are real. Never edit the dependency checkout or
apply this to the game build. WARP is software evidence, not hardware coverage.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path


def prepare(source: Path, output: Path) -> None:
    raw = source.read_bytes().replace(b"\r\n", b"\n")
    blob = hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest()
    if blob != "2cdcf18f3b2e1729c2ff63684329c9c26770748c":
        raise ValueError("WARP fixture requires the pinned Plume D3D12 source")
    if source.resolve() == output.resolve():
        raise ValueError("Refusing to modify the dependency checkout")
    text = raw.decode("utf-8")
    replacements = (
        ("if (adapterDesc.Flags & (DXGI_ADAPTER_FLAG_REMOTE | DXGI_ADAPTER_FLAG_SOFTWARE)) {",
         "if (adapterDesc.Flags & DXGI_ADAPTER_FLAG_REMOTE) {"),
        ("if ((adapterDesc.Flags & (DXGI_ADAPTER_FLAG_REMOTE | DXGI_ADAPTER_FLAG_SOFTWARE)) == 0) {",
         "if ((adapterDesc.Flags & DXGI_ADAPTER_FLAG_REMOTE) == 0) {"),
    )
    for before, after in replacements:
        if text.count(before) != 1:
            raise ValueError("Pinned adapter filter differs")
        text = text.replace(before, after)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text, encoding="utf-8", newline="\n")
    print("WARP test: software-adapter selection enabled in generated copy; production source unchanged")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    prepare(args.source, args.output)
