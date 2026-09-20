#!/usr/bin/env python3
"""Fetch two explicit, immutable upstream test fixtures; never package fonts.

Pins include upstream commit, size, and Git blob identity (not a SHA-256).
Only CI/test setup uses this network helper. The C++ runtime has no downloader.
"""
from __future__ import annotations
import hashlib
import pathlib
import sys
import urllib.request

FIXTURES = (
    ("notofonts/noto-cjk", "f8d157532fbfaeda587e826d4cd5b21a49186f7c",
     "Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf",
     16437364, "dc15562470b4f842321894787a0d066879ccff8b"),
    ("notofonts/noto-fonts", "ffebf8c1ee449e544955a7e813c54f9b73848eac",
     "hinted/ttf/NotoSansArabic/NotoSansArabic-Black.ttf",
     254936, "fa98830116eabbf99902fb57f744efc574bd496e"),
)

def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: prepare_fonts.py BUILD_DIRECTORY")
    destination = pathlib.Path(sys.argv[1])
    destination.mkdir(parents=True, exist_ok=True)
    for repository, commit, path, size, expected in FIXTURES:
        target = destination / pathlib.PurePosixPath(path).name
        if target.exists():
            data = target.read_bytes()
        else:
            url = f"https://raw.githubusercontent.com/{repository}/{commit}/{path}"
            with urllib.request.urlopen(url, timeout=60) as response:
                data = response.read(size + 1)
        actual = hashlib.sha1(f"blob {len(data)}\0".encode() + data).hexdigest()
        if len(data) != size or actual != expected:
            raise SystemExit(f"Font fixture identity mismatch: {target.name}")
        if not target.exists():
            target.write_bytes(data)
        print(f"Verified {target.name}: {len(data)} bytes, git-blob {actual}")

if __name__ == "__main__":
    main()
