#!/usr/bin/env python3
"""Builds a MOD package from its source folder: the package folder and the .srw64mod file.

    tools/mods/pack.py config/recomp/campaigns/sample -o build/mods

The source folder holds mod.json (docs/design/mod-packages.md §2) and the content as the
package carries it, with one exception: a campaign source (campaign.json of
srw64.campaign.v1 with its stages/, docs/design/custom-campaign.md §3) is compiled here
into campaign/campaign.json, the image the game plays. Everything that is not a package
folder or file (the campaign's sources, notes) stays out.

Writes <out>/<id>/ (the package unpacked, as the game keeps it in mods/) and
<out>/<id>-<version>.srw64mod (the file to list and share; reproducible: fixed dates,
sorted entries), then checks the file with tools/mods/validate.py and prints its
summary. Exit code 1 when the check fails.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "tools"))
import validate  # noqa: E402

CAMPAIGN_SOURCE_SCHEMA = "srw64.campaign.v1"
# zip dates start at 1980; one fixed date keeps the file the same for the same content.
FIXED_DATE = (1980, 1, 1, 0, 0, 0)


def read_manifest(source: Path) -> dict:
    """mod.json, by the host's rules; raises on a problem."""
    path = source / "mod.json"
    if not path.is_file():
        raise ValueError(f"{source}: no mod.json")
    manifest, problem = validate.parse_manifest(json.loads(path.read_text(encoding="utf-8")))
    if problem:
        raise ValueError(f"{path}: {problem.code} {problem.detail}".rstrip())
    return manifest


def compile_campaign_source(source: Path, out: Path, mod_id: str) -> None:
    """A campaign source compiled into out/campaign/campaign.json; its id must be the package's."""
    from recomp.script_lab.mini_stage import compile_campaign
    image = compile_campaign(source / "campaign.json")
    if image.get("id") != mod_id:
        raise ValueError(f"{source / 'campaign.json'}: id {image.get('id')!r} is not the package's {mod_id!r}")
    (out / "campaign").mkdir(parents=True, exist_ok=True)
    (out / "campaign" / "campaign.json").write_text(json.dumps(image, ensure_ascii=False) + "\n", encoding="utf-8")


def _copy_tree(src: Path, dst: Path) -> None:
    for item in sorted(src.rglob("*")):
        rel = item.relative_to(src).as_posix()
        if validate._is_junk(rel) or any(part.startswith(".") for part in item.relative_to(src).parts):
            continue
        target = dst / rel
        if item.is_dir():
            target.mkdir(parents=True, exist_ok=True)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(item, target)


def build_folder(source: Path, out_dir: Path) -> tuple[Path, dict]:
    """The package folder <out_dir>/<id>/, built afresh from the source. Returns it and the manifest."""
    manifest = read_manifest(source)
    folder = out_dir / manifest["id"]
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    shutil.copyfile(source / "mod.json", folder / "mod.json")
    campaign_source = source / "campaign.json"
    is_campaign_source = campaign_source.is_file() and json.loads(campaign_source.read_text(encoding="utf-8")).get("schema") == CAMPAIGN_SOURCE_SCHEMA
    if is_campaign_source:
        compile_campaign_source(source, folder, manifest["id"])
    for name in validate.CONTENT_DIRS:
        if name == "campaign" and is_campaign_source:
            continue
        if (source / name).is_dir():
            _copy_tree(source / name, folder / name)
    for name in validate.TOP_FILES:
        if name != "mod.json" and (source / name).is_file():
            shutil.copyfile(source / name, folder / name)
    return folder, manifest


def archive_name(manifest: dict) -> str:
    return f"{manifest['id']}-{validate.version_text(manifest['version'])}{validate.PACKAGE_EXTENSION}"


def build_archive(folder: Path, path: Path) -> Path:
    """The folder zipped with its entries at the top, in order, with fixed dates."""
    files = sorted(p for p in folder.rglob("*") if p.is_file())
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for file in files:
            info = zipfile.ZipInfo(file.relative_to(folder).as_posix(), date_time=FIXED_DATE)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, file.read_bytes())
    return path


def build_package(source: Path, out_dir: Path, *, archive: bool = True) -> tuple[Path, Path | None, dict]:
    """The package folder and (unless archive=False) the .srw64mod file."""
    folder, manifest = build_folder(source, out_dir)
    path = build_archive(folder, out_dir / archive_name(manifest)) if archive else None
    return folder, path, manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", type=Path, help="the source folder with mod.json")
    parser.add_argument("-o", "--out", type=Path, default=ROOT / "build" / "mods", help="where the package folder and file go (default build/mods)")
    parser.add_argument("--no-check", action="store_true", help="skip tools/mods/validate.py on the result")
    parser.add_argument("--license-optional", action="store_true", help="let the check pass without a license")
    args = parser.parse_args(argv)
    try:
        folder, path, manifest = build_package(args.source, args.out)
    except ValueError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    assert path is not None
    print(f"package folder: {folder}")
    print(f"package file:   {path} ({path.stat().st_size} bytes, sha256 {hashlib.sha256(path.read_bytes()).hexdigest()})")
    if args.no_check:
        return 0
    problems, summary = validate.check(path, require_license=not args.license_optional)
    for p in problems:
        print(f"{p.code}: {validate.wording(p, None)}")
    if not problems:
        print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
