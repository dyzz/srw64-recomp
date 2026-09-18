#!/usr/bin/env python3
"""Build isolated, pinned recomp analysis tools (macOS/Linux)."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import platform
import struct
import subprocess
import sys
import zlib

ROOT = Path(__file__).resolve().parents[3]
WORK = ROOT / "build/recomp"


def run(command: list[str], cwd: Path = ROOT) -> None:
    subprocess.run(command, cwd=cwd, check=True)


def output(command: list[str], cwd: Path = ROOT) -> str:
    return subprocess.check_output(command, cwd=cwd, text=True).strip()


def main() -> int:
    lock = json.loads((ROOT / "config/recomp/toolchain.json").read_text())
    if lock.get("schema") != "srw64.recomp-toolchain.v1":
        raise RuntimeError("invalid toolchain schema")
    WORK.mkdir(parents=True, exist_ok=True)
    venv = WORK / "venv"
    if not (venv / "bin/python").exists():
        run([sys.executable, "-m", "venv", str(venv)])
    run([str(venv / "bin/python"), "-m", "pip", "install", "-r", str(ROOT / "config/recomp/requirements.lock")])
    source_report: dict = {}
    for name, source in lock["sources"].items():
        if source.get("component") == "graphics":
            continue  # prepare_rt64.py owns this optional dependency group.
        directory = WORK / "upstream" / name
        if not directory.exists():
            run(["git", "clone", "--no-checkout", source["url"], str(directory)])
            run(["git", "checkout", "--detach", source["commit"]], directory)
        actual = output(["git", "rev-parse", "HEAD"], directory)
        if actual != source["commit"]:
            raise RuntimeError(f"{name} checkout differs from lock: {actual}")
        run(["git", "diff", "--quiet", "HEAD"], directory)
        if source["recursive"]:
            run(["git", "submodule", "update", "--init", "--recursive"], directory)
        source_report[name] = {
            "commit": actual,
            "submodules": output(["git", "submodule", "status", "--recursive"], directory),
        }
    recomp = WORK / "upstream/N64Recomp"
    tool_build = WORK / "tool-build"
    run(["cmake", "-S", str(recomp), "-B", str(tool_build), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++"])
    run(["cmake", "--build", str(tool_build), "--target", "N64RecompCLI", "RSPRecomp", "-j", "6"])

    # Upstream n64sym's Makefile assumes ELF/static linking. Embed the identical
    # signature bytes with the host's symbol spelling without patching upstream.
    n64sym = WORK / "upstream/n64sym"
    sig = (n64sym / "src/builtin_signatures.sig").read_bytes()
    compressed = zlib.compress(sig)
    asset = tool_build / "builtin_signatures.sig.defl"
    asset.write_bytes(struct.pack("=II", len(sig), len(compressed)) + compressed + b"\0" * 4)
    symbol = "_gBuiltinSignatureFile" if platform.system() == "Darwin" else "gBuiltinSignatureFile"
    assembly = tool_build / "builtin_signatures.s"
    assembly.write_text(f'.data\n.p2align 2\n.global {symbol}\n{symbol}:\n.incbin "{asset}"\n')
    names = ["n64sym_main", "n64sym", "arutil", "elfutil", "pathutil", "crc32", "signaturefile", "threadpool"]
    run(["clang++", "-std=c++11", "-O2", "-Wno-deprecated-declarations", "-I" + str(n64sym / "include"), "-I" + str(n64sym / "src"), *[str(n64sym / "src" / (name + (".c" if name == "crc32" else ".cpp"))) for name in names], str(assembly), "-o", str(tool_build / "n64sym")])
    report = {
        "schema": "srw64.recomp-toolchain-build.v1",
        "status": "compiled",
        "platform": platform.platform(),
        "sources": source_report,
        "python_packages": output([str(venv / "bin/python"), "-m", "pip", "freeze"]).splitlines(),
        "clang": output(["clang", "--version"]),
        "binaries": {name: hashlib.sha256((tool_build / name).read_bytes()).hexdigest() for name in ["N64Recomp", "RSPRecomp", "n64sym"]},
        "signature_sha256": hashlib.sha256(sig).hexdigest(),
    }
    (WORK / "toolchain-build.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
