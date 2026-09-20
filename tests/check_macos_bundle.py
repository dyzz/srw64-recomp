#!/usr/bin/env python3
"""macOS CI smoke: real Cocoa adapter + synthetic dylib, never the game or ROM."""
from __future__ import annotations
import argparse
import hashlib
import os
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def snapshot(root: Path) -> dict[str, str]:
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob("*") if p.is_file()}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    if sys.platform != "darwin" or binary.name != "srw64-macos-smoke":
        parser.error("Requires the macOS smoke target, not a game binary")
    with tempfile.TemporaryDirectory(prefix="srw64-bundle-smoke-") as work:
        root = Path(work)
        staged = root / "staged/SRW64 Recompiled.app"
        subprocess.run([sys.executable, str(ROOT / "tools/release/package_macos.py"),
                        "--binary", str(binary), "--output", str(staged),
                        "--minimum-macos", "14.0"], check=True)
        # Remove the original library location, not just the cwd/PATH. A binary
        # still linked to its build-tree dylib must fail this actual execution.
        source = binary.parent
        hidden = source.with_name(source.name + "-temporarily-hidden")
        if hidden.exists():
            raise RuntimeError("A prior smoke directory exists; refusing to overwrite it")
        source.rename(hidden)
        relocated = root / "搬移 application/SRW64 Recompiled.app"
        relocated.parent.mkdir()
        staged.rename(relocated)
        readonly = []
        try:
            info = plistlib.loads((relocated / "Contents/Info.plist").read_bytes())
            executable = relocated / "Contents/MacOS" / info["CFBundleExecutable"]
            before = snapshot(relocated)
            # No runtime may rely on writing inside the application bundle.
            for path in [*relocated.rglob("*"), relocated]:
                if not path.is_symlink():
                    readonly.append((path, path.stat().st_mode))
                    path.chmod(path.stat().st_mode & ~0o222)
            cwd = root / "unrelated cwd"
            cwd.mkdir()
            env = {k: v for k, v in os.environ.items() if not k.startswith(("DYLD_", "SRW64_"))}
            env["PATH"] = "/usr/bin:/bin"
            result = subprocess.run([str(executable), "--help"], cwd=cwd, env=env,
                                    check=True, text=True, capture_output=True, timeout=30)
            if result.stdout.strip() != "SRW64_BUNDLE_SMOKE_OK":
                raise RuntimeError(f"Unexpected relocated output: {result.stdout}")
            subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(relocated)], check=True)
            if before != snapshot(relocated):
                raise RuntimeError("Application wrote into its bundle")
            if not any(p.suffix == ".dylib" for p in relocated.rglob("*")):
                raise RuntimeError("Linked test dependency was not embedded")
        finally:
            for path, mode in reversed(readonly):
                path.chmod(mode)
            hidden.rename(source)
    print("Relocated, read-only, ad-hoc signed bundle smoke passed (synthetic host; no game/GUI acceptance).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
