"""The one compiler of story events, campaigns and maps is the level editor's, in TypeScript
(web/src/lib/compiler). The command line runs it with node: story.py, stage.py and
map_tool.py hand it their files through the scripts in web/scripts and read back JSON.

node 23 or later runs the TypeScript as it is (22.18 and later too).
"""
from __future__ import annotations

import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "web" / "scripts"


class CompileError(ValueError):
    pass


def _node() -> str:
    node = shutil.which("node")
    if not node:
        raise CompileError("编译器是 web/src/lib/compiler 的 TypeScript，要装 node（23 以上）")
    version = subprocess.run([node, "--version"], capture_output=True, text=True).stdout.strip().lstrip("v")
    major, minor = (int(x) for x in (version.split(".") + ["0"])[:2])
    if (major, minor) < (22, 18):
        raise CompileError(f"node {version} 太旧：编译器要 node 23 以上（直接运行 TypeScript）")
    return node


def run(script: str, *args: str | Path) -> dict:
    """web/scripts/<script> with these arguments: its JSON, or CompileError with what it said."""
    done = subprocess.run([_node(), str(SCRIPTS / script), *map(str, args)], capture_output=True, text=True, encoding="utf-8")
    if done.returncode != 0:
        raise CompileError(f"{script} 运行失败：{done.stderr.strip()[-800:]}")
    try:
        out = json.loads(done.stdout)
    except ValueError:
        raise CompileError(f"{script} 的输出不是 JSON：{done.stdout[:200]}") from None
    if isinstance(out, dict) and "error" in out and len(out) == 1:
        raise CompileError(out["error"])
    return out
