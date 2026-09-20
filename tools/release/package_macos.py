#!/usr/bin/env python3
"""Build-side macOS bundle staging. Never used by the player's application.

Copies only a named executable, its linked dependencies and explicit notices.
Never copy the repository, ROM, imported content cache, saves or font files.
An ad-hoc signature permits local testing; it is NOT Developer ID/notarization.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile

MACHO_MAGIC = {bytes.fromhex(value) for value in (
    "feedface", "cefaedfe", "feedfacf", "cffaedfe", "cafebabe", "bebafeca", "cafebabf", "bfbafeca")}
EXECUTABLE = "srw64-gfx-host"


def run(command: list[str]) -> str:
    return subprocess.run(command, check=True, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT).stdout


def version_tuple(value: str) -> tuple[int, int, int]:
    if not re.fullmatch(r"[0-9]+(?:\.[0-9]+){0,2}", value):
        raise ValueError(f"Invalid numeric version: {value}")
    pieces = [int(item) for item in value.split(".")]
    return tuple((pieces + [0, 0])[:3])


def macho_files(bundle: Path) -> list[Path]:
    files = []
    for path in sorted(bundle.rglob("*")):
        if path.is_symlink():
            if not path.resolve().is_relative_to(bundle.resolve()):
                raise ValueError(f"Bundle symlink escapes: {path}")
            continue
        if path.is_file():
            with path.open("rb") as stream:
                if stream.read(4) in MACHO_MAGIC:
                    files.append(path)
    return files


def load_commands(text: str) -> tuple[list[str], list[str]]:
    """Read LC_RPATH and deployment versions from every Mach-O slice."""
    paths, versions = [], []
    command = ""
    for line in text.splitlines():
        value = line.strip()
        if value.startswith("cmd "):
            command = value[4:]
        elif command == "LC_RPATH" and value.startswith("path "):
            paths.append(value[5:].rsplit(" (offset ", 1)[0])
        elif command == "LC_BUILD_VERSION" and value.startswith("minos "):
            versions.append(value.split()[1])
        elif command == "LC_VERSION_MIN_MACOSX" and value.startswith("version "):
            versions.append(value.split()[1])
    return paths, versions


def check_deployment(files: list[Path], minimum: str) -> None:
    declared = version_tuple(minimum)
    for binary in files:
        _, versions = load_commands(run(["/usr/bin/otool", "-l", str(binary)]))
        if not versions:
            raise ValueError(f"Cannot establish macOS deployment target: {binary}")
        if any(version_tuple(value) > declared for value in versions):
            raise ValueError(f"{binary.name} requires macOS {max(versions, key=version_tuple)}; "
                             f"bundle declares {minimum}. Rebuild with the matching deployment target.")


def strip_external_rpaths(files: list[Path]) -> None:
    for binary in files:
        paths, _ = load_commands(run(["/usr/bin/otool", "-l", str(binary)]))
        # fixup_bundle first rewrites linked dependencies to the embedded copies.
        # Remove build/Homebrew RPATHs rather than leaving loader search paths
        # pointing at the developer's machine. verify_app runs again afterwards.
        for path in dict.fromkeys(paths):
            if not path.startswith(("@loader_path", "@executable_path")):
                run(["/usr/bin/install_name_tool", "-delete_rpath", path, str(binary)])


def sign_bundle(bundle: Path, files: list[Path], identity: str) -> None:
    flags = ["--timestamp=none"] if identity == "-" else ["--timestamp", "--options", "runtime"]
    nested = [path for path in bundle.rglob("*") if path.is_dir() and not path.is_symlink()
              and path.suffix in (".framework", ".bundle", ".xpc", ".app")]
    # Sign inside out. --deep is for final verification, not a substitute for
    # explicitly signing every nested object after install_name_tool changes.
    for path in sorted(files, key=lambda p: len(p.parts), reverse=True):
        run(["/usr/bin/codesign", "--force", "--sign", identity, *flags, str(path)])
    for path in sorted(nested, key=lambda p: len(p.parts), reverse=True):
        run(["/usr/bin/codesign", "--force", "--sign", identity, *flags, str(path)])
    run(["/usr/bin/codesign", "--force", "--sign", identity, *flags, str(bundle)])
    run(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(bundle)])


def stage_bundle(binary: Path, output: Path, *, version: str = "0.2.0", minimum: str = "14.0",
                 identity: str = "-", notices: tuple[Path, ...] = (), cmake: str = "cmake") -> Path:
    if sys.platform != "darwin":
        raise ValueError("macOS packaging must run on macOS")
    version_tuple(version)
    version_tuple(minimum)
    binary = binary.resolve(strict=True)
    output = output.absolute()
    if output.suffix != ".app" or any(c in str(output) + str(binary) for c in ";\n\r"):
        raise ValueError("Output must be a .app path without CMake list/control separators")
    if output.exists() or output.is_symlink():
        raise FileExistsError(f"Refusing to overwrite {output}")
    if not binary.is_file():
        raise ValueError("Input must be a compiled Mach-O executable, not a script")
    with binary.open("rb") as stream:
        if stream.read(4) not in MACHO_MAGIC:
            raise ValueError("Input must be a compiled Mach-O executable, not a script")
    resolved_notices = [path.resolve(strict=True) for path in notices]
    if any(not path.is_file() or path.suffix.lower() not in (".txt", ".md", "") for path in resolved_notices):
        raise ValueError("Notices must be explicit plain-text license files")
    script = Path(__file__).with_suffix(".cmake")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".srw64-stage-", dir=output.parent) as work:
        staged = Path(work) / output.name
        macos = staged / "Contents/MacOS"
        resources = staged / "Contents/Resources"
        macos.mkdir(parents=True)
        resources.mkdir()
        executable = macos / EXECUTABLE
        shutil.copy2(binary, executable)
        executable.chmod(0o755)
        info = {
            "CFBundleExecutable": EXECUTABLE, "CFBundleName": "SRW64 Recompiled",
            "CFBundleDisplayName": "SRW64 Recompiled", "CFBundleIdentifier": "io.github.dyzz.srw64-recomp",
            "CFBundlePackageType": "APPL", "CFBundleVersion": version, "CFBundleShortVersionString": version,
            "LSMinimumSystemVersion": minimum, "NSHighResolutionCapable": True,
            "NSPrincipalClass": "NSApplication",
        }
        (staged / "Contents/Info.plist").write_bytes(plistlib.dumps(info))
        (resources / "Distribution.txt").write_text(
            "SRW64 Recompiled experimental application. ROM not included.\n"
            "Imported game content and saves remain in your private user directory.\n"
            "Hold Option when launching to choose another ROM, or use --choose-rom.\n"
            "Public distribution requires dependency-license review and Developer ID notarization.\n",
            encoding="utf-8")
        for index, notice in enumerate(resolved_notices):
            licenses = resources / "licenses"
            licenses.mkdir(exist_ok=True)
            shutil.copyfile(notice, licenses / f"{index:02d}-{notice.name}")
        run([cmake, f"-DBUNDLE:PATH={staged}", f"-DSEARCH_DIRS:STRING={binary.parent}", "-P", str(script)])
        payloads = macho_files(staged)
        if executable not in payloads:
            raise ValueError("Bundle has no native main executable")
        check_deployment(payloads, minimum)
        strip_external_rpaths(payloads)
        run([cmake, f"-DBUNDLE:PATH={staged}", "-DVERIFY_ONLY:BOOL=ON", "-P", str(script)])
        sign_bundle(staged, payloads, identity)
        if output.exists() or output.is_symlink():
            raise FileExistsError(f"Output appeared during packaging: {output}")
        staged.rename(output)
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", default="0.2.0")
    parser.add_argument("--minimum-macos", default="14.0")
    parser.add_argument("--sign-identity", default="-", help="'-' is local ad-hoc testing, not notarization")
    parser.add_argument("--license-file", type=Path, action="append", default=[])
    parser.add_argument("--cmake", default="cmake")
    args = parser.parse_args()
    try:
        result = stage_bundle(args.binary, args.output, version=args.version, minimum=args.minimum_macos,
                              identity=args.sign_identity, notices=tuple(args.license_file), cmake=args.cmake)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        detail = error.stdout if isinstance(error, subprocess.CalledProcessError) else str(error)
        parser.exit(1, f"Bundle staging failed: {detail}\n")
    print(f"Staged {result}; signature={'ad-hoc (local tests only)' if args.sign_identity == '-' else 'Developer-supplied identity'}.")
    print("Not notarized. No ROM, imported game content, saves or fonts were bundled.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
