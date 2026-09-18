"""Documentation links and the repository paths the docs name must exist.

Docs moved into topic folders under docs/; this keeps relative links and
`src/...`-style paths from going stale when files move again. Links into
build/ point at local run evidence that is not in the repository.
"""
from __future__ import annotations

from pathlib import Path
import os
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
MARKDOWN = sorted([*ROOT.glob("*.md"), *(ROOT / "docs").rglob("*.md"), ROOT / "assets/README.md"])
LINK = re.compile(r"\]\(([^)\s#]+)(?:#[^)\s]*)?\)")
REPO_PATH = re.compile(r"`((?:src|tools|tests|config|content|scripts|docs)/[^`\s*<>{}|]+)`")
# Paths that deliberately name something outside this repository or not yet written.
ELSEWHERE = {
    "src/config.cpp",   # N64Recomp's config parser (docs/design/recomp-plan.md)
    "src/host/text",    # proposed module layout (docs/design/native-enhancements-plan.md)
}


def local_evidence(path: Path) -> bool:
    return path.is_relative_to(ROOT / "build") or (path.is_relative_to(ROOT / "assets") and path.name != "README.md")


class DocsTests(unittest.TestCase):
    def test_relative_links_resolve(self):
        broken = []
        for doc in MARKDOWN:
            for target in LINK.findall(doc.read_text()):
                if re.match(r"[a-z]+:", target):
                    continue
                path = Path(os.path.normpath(doc.parent / target))
                if not local_evidence(path) and not path.exists():
                    broken.append(f"{doc.relative_to(ROOT)} -> {target}")
        self.assertEqual(broken, [])

    def test_named_repository_paths_exist(self):
        missing = []
        for doc in MARKDOWN:
            for name in REPO_PATH.findall(doc.read_text()):
                name = name.rstrip("/.,:;").split(":")[0]
                if name in ELSEWHERE or (ROOT / name).exists() or any(ROOT.glob(name)):
                    continue
                missing.append(f"{doc.relative_to(ROOT)}: {name}")
        self.assertEqual(missing, [])

    def test_docs_live_in_topic_folders(self):
        loose = [p.name for p in (ROOT / "docs").glob("*.md") if p.name != "README.md"]
        self.assertEqual(loose, [], "put new docs in a topic folder and list them in docs/README.md")
        index = (ROOT / "docs/README.md").read_text()
        unlisted = [str(p.relative_to(ROOT / "docs")) for p in (ROOT / "docs").glob("*/*.md")
                    if f"({p.relative_to(ROOT / 'docs')})" not in index]
        self.assertEqual(unlisted, [])


if __name__ == "__main__":
    unittest.main()
