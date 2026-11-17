#!/usr/bin/env python3
"""Wraps a .srw64mod for Thunderstore: the zip its upload page takes.

    tools/mods/thunderstore.py build/mods/srw64.space-a-1.0.2.srw64mod --icon icon.png \
        --team Marchwind64 --dep srw64.base=Marchwind64-Base-1.0.0 -o build/thunderstore

Thunderstore wants manifest.json, a 256x256 icon.png and README.md at the top of the zip
(wiki.thunderstore.io/mods/creating-a-package). The .srw64mod goes in beside them as made, so
its SHA-256 still holds; the game installs the packages inside such a zip (mod_library.cpp
bundled_packages), dropped on the window, chosen, or placed in mods/.

manifest.json comes from mod.json: the name from the id (or --name), version_number from the
version, the description from the English one (or the first), cut to Thunderstore's 250
characters. Each of mod.json's `requires` needs a --dep giving its Thunderstore package
string (Team-Name-1.2.3); srw64.hd is the game's HD pack and is left out. `optional` and
`conflicts` have no Thunderstore form and are stated in the README.

The icon may be any square image; it is scaled to 256x256. README.md is written from
mod.json unless --readme gives one. The package is checked with tools/mods/validate.py
first. Writes <out>/<Team>-<Name>-<version>.zip (without --team: <Name>-<version>.zip),
reproducible (fixed dates, fixed order). Exit code 1 on a problem.
"""
from __future__ import annotations

import argparse
import io
import json
import re
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import validate  # noqa: E402

FIXED_DATE = (1980, 1, 1, 0, 0, 0)
ICON_SIZE = 256
DESCRIPTION_LIMIT = 250
NAME_LIMIT = 128
# The game's HD pack, a dependency the game resolves itself (mod-packages.md §3).
VIRTUAL = {"srw64.hd"}
NAME_RE = re.compile(r"^[A-Za-z0-9_]+$")
DEPENDENCY_RE = re.compile(r"^[A-Za-z0-9_]+-[A-Za-z0-9_]+-\d+\.\d+\.\d+$")
LANGUAGE_NAMES = {"en": "English", "ja": "日本語", "zh-Hans": "简体中文", "zh-Hant": "繁體中文", "ko": "한국어"}


def default_name(mod_id: str) -> str:
    """srw64.space-a -> space_a: the part after the author's prefix, in Thunderstore's letters."""
    rest = mod_id.split(".", 1)[1] if "." in mod_id else mod_id
    return re.sub(r"[^A-Za-z0-9_]", "_", rest)[:NAME_LIMIT]


def text_in(value, language: str = "en") -> str:
    """A text field (a string, or {language: text}) in one language, else the first given."""
    if isinstance(value, str):
        return value
    if isinstance(value, dict) and value:
        return value.get(language) or next(iter(value.values()))
    return ""


def short_description(manifest: dict) -> str:
    text = " ".join(text_in(manifest.get("description")).split()) or text_in(manifest.get("name"))
    if len(text) <= DESCRIPTION_LIMIT:
        return text
    return text[:DESCRIPTION_LIMIT - 1].rstrip() + "…"


def dependency_strings(manifest: dict, deps: dict[str, str]) -> list[str]:
    """mod.json's requirements as Thunderstore package strings; raises for one without a --dep."""
    out, missing = [], []
    for mod_id in manifest.get("requires", {}):
        if mod_id in VIRTUAL:
            continue
        if mod_id not in deps:
            missing.append(mod_id)
            continue
        out.append(deps[mod_id])
    if missing:
        raise ValueError("no --dep for " + ", ".join(missing) + " (give it as id=Team-Name-1.2.3)")
    return out


def thunderstore_manifest(manifest: dict, name: str, website: str, deps: dict[str, str]) -> dict:
    if not NAME_RE.match(name) or len(name) > NAME_LIMIT:
        raise ValueError(f"name {name!r}: Thunderstore takes letters, digits and _ only, up to {NAME_LIMIT}")
    return {
        "name": name,
        "version_number": validate.version_text(manifest["version"]),
        "website_url": website,
        "description": short_description(manifest),
        "dependencies": dependency_strings(manifest, deps),
    }


def readme(manifest: dict, package_file: str) -> str:
    """README.md from mod.json: the names and descriptions in every language it has."""
    name = text_in(manifest.get("name"))
    lines = [f"# {name}", ""]
    description = manifest.get("description")
    if isinstance(description, str):
        lines += [description, ""]
    names = manifest.get("name") if isinstance(manifest.get("name"), dict) else {}
    if isinstance(description, dict):
        for language, text in description.items():
            title = names.get(language, "")
            heading = LANGUAGE_NAMES.get(language, language)
            lines += [f"## {heading}" + (f": {title}" if title and title != name else ""), "", text, ""]
    lines += ["## Installing", "",
              "This is a mod for Marchwind64, the Super Robot Wars 64 recompilation.",
              "Download it with **Manual Download**, then drag the zip onto the game window, choose it on the MOD page,",
              "or put it in the `mods` folder of the game's data folder. Mod managers (r2modman, Gale) are not needed or supported.",
              "",
              f"Version 0.5.3 and earlier do not open this zip: take `{package_file}` out of it and use that file the same way.",
              ""]
    facts = [f"- Package: `{manifest['id']}` {validate.version_text(manifest['version'])} (`{package_file}`)"]
    if manifest.get("game"):
        facts.append(f"- Game version: `{manifest['game']}`")
    for key, label in (("requires", "Requires"), ("optional", "Works with"), ("conflicts", "Conflicts with")):
        if manifest.get(key):
            facts.append(f"- {label}: " + ", ".join(("the HD pack" if i in VIRTUAL else f"`{i}`") + f" {r}" for i, r in manifest[key].items()))
    if manifest.get("authors"):
        facts.append("- Authors: " + ", ".join(manifest["authors"]))
    if manifest.get("license"):
        facts.append(f"- License: {manifest['license']}")
    lines += ["## Details", "", *facts, ""]
    return "\n".join(lines)


def icon_png(path: Path) -> bytes:
    """Any square image as a 256x256 PNG. Scaled with the colour weighted by alpha, so
    transparent edges do not darken (Pillow's RGBA resize spreads their black)."""
    from PIL import Image
    with Image.open(path) as image:
        if image.width != image.height:
            raise ValueError(f"{path}: the icon is {image.width}x{image.height}; it must be square")
        image = image.convert("RGBA")
        if image.size != (ICON_SIZE, ICON_SIZE):
            image = image.convert("RGBa").resize((ICON_SIZE, ICON_SIZE), Image.LANCZOS).convert("RGBA")
        out = io.BytesIO()
        image.save(out, "PNG", optimize=True)
        return out.getvalue()


def build(package: Path, icon: Path, out_dir: Path, *, team: str = "", name: str = "", website: str = "",
          deps: dict[str, str] | None = None, readme_path: Path | None = None) -> tuple[Path, dict]:
    """The Thunderstore zip; returns its path and its manifest.json. Raises ValueError on a problem."""
    problems, _ = validate.check(package, require_license=True)
    if problems:
        raise ValueError("; ".join(f"{p.code}: {validate.wording(p, None)}" for p in problems))
    with zipfile.ZipFile(package) as z:
        manifest, problem = validate.parse_manifest(json.loads(z.read(_manifest_entry(z))))
    if problem:
        raise ValueError(f"mod.json: {problem.code} {problem.detail}")
    for value in (deps or {}).values():
        if not DEPENDENCY_RE.match(value):
            raise ValueError(f"--dep {value!r} is not a Thunderstore package string (Team-Name-1.2.3)")
    if team and not NAME_RE.match(team):
        raise ValueError(f"team {team!r}: letters, digits and _ only")
    ts = thunderstore_manifest(manifest, name or default_name(manifest["id"]), website, deps or {})
    text = readme_path.read_text(encoding="utf-8") if readme_path else readme(manifest, package.name)
    files = [
        ("manifest.json", (json.dumps(ts, ensure_ascii=False, indent=2) + "\n").encode("utf-8")),
        ("icon.png", icon_png(icon)),
        ("README.md", text.encode("utf-8")),
        (package.name, package.read_bytes()),
    ]
    out_dir.mkdir(parents=True, exist_ok=True)
    stem = "-".join(p for p in (team, ts["name"], ts["version_number"]) if p)
    path = out_dir / f"{stem}.zip"
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for entry, data in files:
            info = zipfile.ZipInfo(entry, date_time=FIXED_DATE)
            info.compress_type = zipfile.ZIP_STORED if entry.endswith(validate.PACKAGE_EXTENSION) else zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data)
    return path, ts


def _manifest_entry(z: zipfile.ZipFile) -> str:
    names = [n for n in z.namelist() if not validate._is_junk(n)]
    if "mod.json" in names:
        return "mod.json"
    tops = {n.split("/", 1)[0] for n in names}
    if len(tops) == 1 and f"{next(iter(tops))}/mod.json" in names:
        return f"{next(iter(tops))}/mod.json"
    raise ValueError("no mod.json in the package")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("package", type=Path, help="the .srw64mod file (tools/mods/pack.py makes it)")
    parser.add_argument("--icon", type=Path, required=True, help="a square image; scaled to 256x256")
    parser.add_argument("--team", default="", help="the Thunderstore team, for the file's name")
    parser.add_argument("--name", default="", help="the Thunderstore package name (default: from the id)")
    parser.add_argument("--website", default="", help="website_url (default empty)")
    parser.add_argument("--dep", action="append", default=[], metavar="ID=Team-Name-1.2.3",
                        help="the Thunderstore package of one of mod.json's requires")
    parser.add_argument("--readme", type=Path, help="README.md to use instead of the one written from mod.json")
    parser.add_argument("-o", "--out", type=Path, default=ROOT / "build" / "thunderstore", help="where the zip goes")
    args = parser.parse_args(argv)
    deps = {}
    for item in args.dep:
        key, sep, value = item.partition("=")
        if not sep:
            print(f"error: --dep {item!r}: give it as id=Team-Name-1.2.3", file=sys.stderr)
            return 1
        deps[key] = value
    try:
        path, ts = build(args.package, args.icon, args.out, team=args.team, name=args.name, website=args.website,
                         deps=deps, readme_path=args.readme)
    except (ValueError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(f"thunderstore package: {path} ({path.stat().st_size} bytes)")
    print(json.dumps(ts, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
