#!/usr/bin/env python3
"""Prepare the dialogue and UI fonts: HarmonyOS Sans from the official archive.

The licence allows shipping HarmonyOS Sans with the game but not distributing
the font separately or modified, so the repository holds only
content/fonts/harmonyos-sans.json. This tool checks the official archive and
each extracted file against it and writes the fonts, the symbol font and both
licences into one directory, which the host reads through SRW64_FONT_DIR and
the app bundle ships in Contents/Resources/fonts."""
import argparse
import hashlib
import json
import shutil
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "content/fonts/harmonyos-sans.json"
ARCHIVE = ROOT / "assets/hd-ai/dialogue-polish/HarmonyOS-Sans.zip"
OUTPUT = ROOT / "build/fonts"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def prepared(output: Path) -> bool:
    """True when every font file is present with the expected hash."""
    manifest = json.loads(MANIFEST.read_text())
    for name, row in manifest["files"].items():
        path = output / name
        if not path.is_file() or sha256(path.read_bytes()) != row["sha256"]:
            return False
    return all((output / name).is_file() for name in manifest["bundled"])


def prepare(archive: Path = ARCHIVE, output: Path = OUTPUT) -> Path:
    manifest = json.loads(MANIFEST.read_text())
    if not archive.is_file():
        raise SystemExit(f"HarmonyOS Sans is missing: download the official archive from\n  {manifest['url']}\n"
                         f"and put it at {archive} (SHA-256 {manifest['archive_sha256']}), then run this tool again.")
    data = archive.read_bytes()
    if sha256(data) != manifest["archive_sha256"]:
        raise SystemExit(f"{archive} is not the official HarmonyOS Sans archive (SHA-256 mismatch)")
    output.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as bundle:
        for name, row in manifest["files"].items():
            content = bundle.read(row["member"])
            if sha256(content) != row["sha256"]:
                raise SystemExit(f"{row['member']} in the archive does not match its recorded SHA-256")
            (output / name).write_bytes(content)
    for name in manifest["bundled"]:
        shutil.copyfile(MANIFEST.parent / name, output / name)
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, default=ARCHIVE)
    parser.add_argument("--output", type=Path, default=OUTPUT)
    parser.add_argument("--check", action="store_true", help="only report whether OUTPUT is prepared")
    args = parser.parse_args()
    if args.check:
        ready = prepared(args.output)
        print("prepared" if ready else "not prepared", args.output)
        return 0 if ready else 1
    print("prepared", prepare(args.archive, args.output))
    return 0


if __name__ == "__main__":
    sys.exit(main())
