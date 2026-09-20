#!/usr/bin/env python3
"""Fetch immutable, identity-checked test fonts; never package or upload them."""
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
            request = urllib.request.Request(url, headers={"Accept-Encoding": "identity"})
            with urllib.request.urlopen(request, timeout=60) as response:
                print(f"Fetch {target.name}: HTTP {response.status}, "
                      f"length={response.headers.get('Content-Length')}, "
                      f"range={response.headers.get('Content-Range')}, "
                      f"encoding={response.headers.get('Content-Encoding')}", flush=True)
                data = response.read(size + 1)
        actual = hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\x00" + data).hexdigest()
        if len(data) != size or actual != expected:
            raise SystemExit(f"Font fixture identity mismatch: {target.name}; "
                             f"expected {size} bytes / {expected}, "
                             f"received {len(data)} bytes / {actual}; magic={data[:8].hex()}")
        if not target.exists():
            target.write_bytes(data)
        print(f"Verified {target.name}: {len(data)} bytes, git-blob {actual}")

if __name__ == "__main__":
    main()
