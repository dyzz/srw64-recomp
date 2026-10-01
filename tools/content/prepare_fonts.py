#!/usr/bin/env python3
"""Prepare the dialogue and UI fonts: HarmonyOS Sans, the symbol and prompt fonts.

The licence lets HarmonyOS Sans be redistributed unmodified with software (not on
its own, not modified), so the repository carries the official files, licence
included, in content/fonts next to content/fonts/harmonyos-sans.json. This tool
checks each file against that manifest (or extracts it from the official archive
when the repository copy is absent) and writes the fonts, the symbol font and the
licences into one directory, which the host reads through SRW64_FONT_DIR and the
app bundle ships in Contents/Resources/fonts."""
import argparse
import hashlib
import json
import shutil
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "content/fonts/harmonyos-sans.json"
ARCHIVE = ROOT / "assets/fonts/HarmonyOS-Sans-2.040.zip"
OUTPUT = ROOT / "build/fonts"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def stale(output: Path, manifest: dict) -> list[Path]:
    """Files OUTPUT holds that the package no longer lists (an older HarmonyOS Sans)."""
    keep = set(manifest["files"]) | set(manifest["bundled"])
    return sorted(path for path in output.iterdir() if path.is_file() and path.name not in keep) if output.is_dir() else []


def prepared(output: Path) -> bool:
    """True when every font file is present with the expected hash, and nothing else."""
    manifest = json.loads(MANIFEST.read_text())
    if stale(output, manifest):
        return False
    for name, row in manifest["files"].items():
        path = output / name
        if not path.is_file() or sha256(path.read_bytes()) != row["sha256"]:
            return False
    # The bundled files are tracked: a rebuilt symbol font must replace the prepared copy.
    return all((output / name).is_file() and (output / name).read_bytes() == (MANIFEST.parent / name).read_bytes()
               for name in manifest["bundled"])


def prepare(archive: Path = ARCHIVE, output: Path = OUTPUT, repository: Path = MANIFEST.parent) -> Path:
    manifest = json.loads(MANIFEST.read_text())
    if all((repository / name).is_file() for name in manifest["files"]):
        output.mkdir(parents=True, exist_ok=True)
        for path in stale(output, manifest):
            path.unlink()
        for name, row in manifest["files"].items():
            content = (repository / name).read_bytes()
            if sha256(content) != row["sha256"]:
                raise SystemExit(f"{repository / name} is not the official file (SHA-256 mismatch)")
            (output / name).write_bytes(content)
        for name in manifest["bundled"]:
            shutil.copyfile(MANIFEST.parent / name, output / name)
        return output
    if not archive.is_file():
        raise SystemExit(f"HarmonyOS Sans {manifest['version']} is missing: download the official archive from\n  {manifest['url']}\n"
                         f"and put it at {archive} (SHA-256 {manifest['archive_sha256']}), then run this tool again.")
    data = archive.read_bytes()
    if sha256(data) != manifest["archive_sha256"]:
        raise SystemExit(f"{archive} is not the official HarmonyOS Sans archive (SHA-256 mismatch)")
    output.mkdir(parents=True, exist_ok=True)
    # The app bundle ships the whole directory, so older font files must go.
    for path in stale(output, manifest):
        path.unlink()
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
