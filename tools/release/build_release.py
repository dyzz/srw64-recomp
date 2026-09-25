#!/usr/bin/env python3
"""Build the release downloads from one commit, apart from the shared worktree.

Checks the commit out into OUTPUT/src (a detached git worktree), links the local
ROM and assets/ into it, clones the pinned upstream sources and built tools from
this checkout's build/recomp (copy-on-write), then regenerates everything the
game build reads from that commit alone: the CPU code, the RT64 patches, the
frontend adapter, the audio microcode, the fonts and the two model packs. Other
sessions' uncommitted work in the main worktree never reaches the build.

Writes to OUTPUT:
  SRW64-<version>-macos14-arm64.zip  the app (Original images; HD when a pack is installed)
  SRW64-<version>-HD.zip             the HD pack: unzip into the user directory as hd/
  release.json, release-notes.md, logs/
Publishing is a separate, manual step: release.json holds the gh command."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import tomllib

ROOT = Path(__file__).resolve().parents[2]
DEPS = ROOT / "build/macos-deps/14.0-arm64/prefix"
NOTES = ROOT / "tools/release/release-notes.md"
HD_NOTICE = ROOT / "tools/release/hd-notice.txt"
# Built once by make recomp-bootstrap / recomp-scan; they depend on the pinned
# toolchain and the ROM, not on the project's sources.
CLONED = ("upstream", "tool-build", "cpu-scan")
CLONED_ASSETS = ("fonts", "hd-ai", "models")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


class Steps:
    def __init__(self, logs: Path):
        self.logs, self.count = logs, 0
        logs.mkdir(parents=True)

    def run(self, name: str, command: list[str], cwd: Path, env: dict | None = None) -> None:
        self.count += 1
        log = self.logs / f"{self.count:02d}-{name}.log"
        started = time.monotonic()
        print(f"[{self.count:02d}] {name}", flush=True)
        with log.open("w") as stream:
            result = subprocess.run(command, cwd=cwd, env=env, stdout=stream, stderr=subprocess.STDOUT)
        if result.returncode:
            raise SystemExit(f"{name} failed ({result.returncode}); see {log}")
        print(f"     {time.monotonic() - started:.0f} s", flush=True)


def zip_folder(steps: Steps, name: str, parent: Path, folder: str, archive: Path) -> None:
    # Signature and symlinks survive; extended attributes (quarantine, provenance) stay out.
    steps.run(name, ["/usr/bin/ditto", "-c", "-k", "--norsrc", "--noextattr", "--keepParent", folder, str(archive)], parent)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--commit", default="HEAD")
    parser.add_argument("--version", default=tomllib.loads((ROOT / "pyproject.toml").read_text())["project"]["version"])
    parser.add_argument("--output", type=Path, help="new directory (default build/release/<version>-<commit>)")
    parser.add_argument("--keep-source", action="store_true", help="keep OUTPUT/src and its build after success")
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args()

    commit = subprocess.check_output(["git", "rev-parse", "--verify", f"{args.commit}^{{commit}}"], cwd=ROOT, text=True).strip()
    output = (args.output or ROOT / f"build/release/{args.version}-{commit[:7]}").absolute()
    if output.exists():
        raise SystemExit(f"{output} exists; choose another --output")
    for needed in [ROOT / "rom.z64", ROOT / "assets", DEPS, *(ROOT / "build/recomp" / name for name in CLONED)]:
        if not needed.exists():
            raise SystemExit(f"Missing {needed}: prepare the main checkout first (make all, macOS dependencies)")
    python = sys.executable
    steps = Steps(output / "logs")
    source = output / "src"
    steps.run("checkout", ["git", "worktree", "add", "--detach", str(source), commit], ROOT)
    (source / "rom.z64").symlink_to(ROOT / "rom.z64")
    # assets/ is untracked apart from its README. The art, font and model sources are
    # cloned (compile_art refuses paths that resolve outside the checkout); the rest is linked.
    for entry in sorted((ROOT / "assets").iterdir()):
        target = source / "assets" / entry.name
        if target.exists():
            continue
        if entry.name in CLONED_ASSETS:
            steps.run(f"clone-assets-{entry.name}", ["/bin/cp", "-cR", str(entry), str(target)], ROOT)
        else:
            target.symlink_to(entry)
    # The docs' screenshots are not in git (.gitignore *.png); test_docs checks their links.
    if (ROOT / "docs/native/images").is_dir() and not (source / "docs/native/images").exists():
        (source / "docs/native/images").symlink_to(ROOT / "docs/native/images")
    work = source / "build/recomp"
    work.mkdir(parents=True)
    for name in CLONED:
        steps.run(f"clone-{name}", ["/bin/cp", "-cR", str(ROOT / "build/recomp" / name), str(work / name)], ROOT)
    # This checkout's modules, never the main worktree's (the editable install points there).
    env = {**os.environ, "PYTHONPATH": f"{source / 'src'}:{source / 'tools'}"}
    for key in [k for k in env if k.startswith("SRW64_")]:
        del env[key]
    steps.run("generate-cpu", [python, "tools/recomp/toolchain/generate_cpu.py"], source, env)
    steps.run("prepare-rt64", [python, "tools/recomp/toolchain/prepare_rt64.py"], source, env)
    steps.run("prepare-frontend", [python, "tools/recomp/toolchain/prepare_frontend.py", "--fetch"], source, env)
    steps.run("audio-microcode", [str(work / "tool-build/RSPRecomp"), "config/recomp/audio-probe.toml"], source, env)
    steps.run("fonts", [python, "tools/content/prepare_fonts.py"], source, env)
    steps.run("tests", [python, "-m", "unittest", "discover", "-s", "tests"], source, env)

    build = work / "macos14-app-build"
    steps.run("configure", ["cmake", "-S", "src/host", "-B", str(build), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_C_COMPILER=/usr/bin/clang", "-DCMAKE_CXX_COMPILER=/usr/bin/clang++",
        "-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0", "-DCMAKE_OSX_ARCHITECTURES=arm64",
        f"-DCMAKE_PREFIX_PATH={DEPS}", "-DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew;/usr/local",
        f"-DSDL2_DIR={DEPS}/lib/cmake/SDL2", f"-DICU_ROOT={DEPS}", f"-Dharfbuzz_DIR={DEPS}/lib/cmake/harfbuzz",
        "-DSRW64_ENABLE_RT64=ON", "-DSRW64_METAL_SOURCE_SHADERS=ON", f"-DPython3_EXECUTABLE={python}"], source, env)
    steps.run("build", ["cmake", "--build", str(build), "--target", "srw64-gfx-host", "--parallel", str(args.jobs)], source, env)
    app_dir = output / "app"
    app_dir.mkdir()
    steps.run("package", [python, "tools/release/package_macos.py", "--binary", str(build / "srw64-gfx-host"),
        "--output", str(app_dir / "SRW64 Recompiled.app"), "--version", args.version, "--minimum-macos", "14.0",
        "--search-dir", str(DEPS / "lib"), "--runtime-library", str(DEPS / "lib/libSDL3.dylib"),
        "--fonts", str(source / "build/fonts"), "--dialogue", str(source / "content/dialogue")], source, env)
    app_zip = output / f"SRW64-{args.version}-macos14-arm64.zip"
    zip_folder(steps, "zip-app", app_dir, "SRW64 Recompiled.app", app_zip)

    # The HD pack, from this commit's tools and manifests and the local assets.
    steps.run("marker-pack", [python, "tools/recomp/model5600/prepare_native_marker.py",
                              "--output", str(work / "native-marker/assets")], source, env)
    steps.run("model-pack", [python, "tools/models/build_native_models.py", "--output", str(work / "native-models/assets")], source, env)
    pack_dir = output / "pack"
    pack_dir.mkdir()
    steps.run("hd-pack", [python, "tools/release/prepare_hd_bundle.py", "--output", str(pack_dir / "hd")], source, env)
    if (source / HD_NOTICE.relative_to(ROOT)).is_file():
        shutil.copyfile(source / HD_NOTICE.relative_to(ROOT), pack_dir / "hd/NOTICE.txt")
    hd_zip = output / f"SRW64-{args.version}-HD.zip"
    zip_folder(steps, "zip-hd", pack_dir, "hd", hd_zip)

    artifacts = {path.name: {"bytes": path.stat().st_size, "sha256": sha256(path)} for path in (app_zip, hd_zip)}
    record = {"schema": "srw64.release-build.v1", "version": args.version, "tag": f"v{args.version}", "commit": commit,
              "artifacts": artifacts, "hd": json.loads((pack_dir / "hd/hd.json").read_text()),
              # Not run here. --target pins the tag to the built commit.
              "publish": ["gh", "release", "create", f"v{args.version}", "--repo", "dyzz/srw64-recomp",
                          "--target", commit, "--title", f"SRW64 Recompiled {args.version}",
                          "--notes-file", str(output / "release-notes.md"), str(app_zip), str(hd_zip)]}
    (output / "release.json").write_text(json.dumps(record, indent=2, ensure_ascii=False) + "\n")
    notes = source / NOTES.relative_to(ROOT)
    if notes.is_file():
        mib = {name: f"{row['bytes'] / 1048576:.0f} MB" for name, row in artifacts.items()}
        text = notes.read_text().format(version=args.version, tag=record["tag"], commit=commit, short=commit[:7],
                                        app_zip=app_zip.name, hd_zip=hd_zip.name,
                                        app_size=mib[app_zip.name], hd_size=mib[hd_zip.name],
                                        app_sha=artifacts[app_zip.name]["sha256"], hd_sha=artifacts[hd_zip.name]["sha256"])
        (output / "release-notes.md").write_text(text)
    if not args.keep_source:
        subprocess.run(["git", "worktree", "remove", "--force", str(source)], cwd=ROOT, check=True)
    print(json.dumps(record["artifacts"], indent=2))
    print(f"Ready in {output}. Nothing was published.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
