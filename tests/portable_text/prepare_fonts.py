#!/usr/bin/env python3
"""Read test fonts from immutable Git commits; never package or upload them.

The pinned commit and its tree bind the expected blob, without relying on
hand-copied Contents API metadata or raw-file CDN responses. Git checks
fetched object identities; the extracted bytes are independently checked.
Only developer/CI setup uses Git and the network. The runtime does not.
"""
from __future__ import annotations
import hashlib
import os
import pathlib
import re
import subprocess
import sys
import tempfile

FIXTURES = (
    ("notofonts/noto-cjk", "f8d157532fbfaeda587e826d4cd5b21a49186f7c",
     "Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf", 16437364),
    ("notofonts/noto-fonts", "ffebf8c1ee449e544955a7e813c54f9b73848eac",
     "hinted/ttf/NotoSansArabic/NotoSansArabic-Black.ttf", 254936),
)


def git(directory: pathlib.Path, *arguments: str) -> bytes:
    return subprocess.run(
        ["git", "-c", "fetch.fsckObjects=true", "-C", str(directory), *arguments],
        check=True, stdout=subprocess.PIPE, timeout=180,
        env={**os.environ, "GIT_TERMINAL_PROMPT": "0"},
    ).stdout


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: prepare_fonts.py BUILD_DIRECTORY")
    destination = pathlib.Path(sys.argv[1]).resolve()
    destination.mkdir(parents=True, exist_ok=True)
    for repository, commit, path, size in FIXTURES:
        if not re.fullmatch(r"[0-9a-f]{40}", commit):
            raise SystemExit("Fixture commit must be a full immutable SHA")
        target = destination / pathlib.PurePosixPath(path).name
        with tempfile.TemporaryDirectory(prefix="srw64-font-source-") as temporary:
            source = pathlib.Path(temporary)
            git(source, "init", "--quiet")
            git(source, "remote", "add", "origin", f"https://github.com/{repository}.git")
            # These settings must persist for subsequent cat-file lazy fetches,
            # not merely exist as -c overrides on the first fetch process.
            git(source, "config", "remote.origin.promisor", "true")
            git(source, "config", "remote.origin.partialclonefilter", "blob:none")
            git(source, "fetch", "--quiet", "--depth=1", "--filter=blob:none", "origin", commit)
            actual_commit = git(source, "rev-parse", "FETCH_HEAD^{commit}").decode().strip()
            if actual_commit != commit:
                raise SystemExit("Git returned a different fixture commit")
            expected = git(source, "rev-parse", f"{commit}:{path}").decode().strip()
            if not re.fullmatch(r"[0-9a-f]{40}", expected):
                raise SystemExit("Invalid font blob identity in pinned tree")
            print(f"Reading {target.name} from pinned blob {expected}", flush=True)
            if git(source, "cat-file", "-t", expected).strip() != b"blob":
                raise SystemExit("Pinned font is not a blob")
            if int(git(source, "cat-file", "-s", expected)) != size:
                raise SystemExit(f"Unexpected pinned font size: {target.name}")
            data = git(source, "cat-file", "blob", expected)
            actual = hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\x00" + data).hexdigest()
            if len(data) != size or actual != expected:
                raise SystemExit(f"Extracted font does not match pinned Git object: {target.name}")
            if target.exists():
                if target.stat().st_size != size or target.read_bytes() != data:
                    raise SystemExit(f"Existing font fixture differs: {target.name}")
            else:
                target.write_bytes(data)
            print(f"Verified {target.name}: commit={commit}, blob={actual}, bytes={len(data)}", flush=True)


if __name__ == "__main__":
    main()
