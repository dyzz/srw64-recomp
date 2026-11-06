"""tools/mods/intake.py: reading the Issue form and reviewing a submitted package."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "mods"))
import intake  # noqa: E402

URL = "https://github.com/someone/mods/releases/download/v1.1/someone.funnels-1.1.0.srw64mod"
BODY = f"""### {intake.URL_HEADING}

{URL}

### {intake.ABOUT_HEADING}

Funnels after moving. All numbers, no art.

### {intake.CONFIRM_HEADING}

- [X] one
- [X] two
- [X] three
"""


def package(folder: Path, package_id: str, version: str) -> Path:
    """The site's funnels package under another id."""
    source = ROOT / "web/public/mods/srw64.funnels-p-1.0.0.srw64mod"
    path = folder / f"{package_id}-{version}.srw64mod"
    with zipfile.ZipFile(source) as old, zipfile.ZipFile(path, "w") as new:
        for name in old.namelist():
            data = old.read(name)
            if name.endswith("mod.json"):
                manifest = json.loads(data)
                manifest.update({"id": package_id, "version": version})
                data = json.dumps(manifest).encode("utf-8")
            new.writestr(name, data)
    return path


class FormTests(unittest.TestCase):
    def test_a_complete_form(self):
        url, about, problems = intake.read_form(BODY.replace("\n", "\r\n"))
        self.assertEqual(url, URL)
        self.assertEqual(about, "Funnels after moving. All numbers, no art.")
        self.assertEqual(problems, [])

    def test_an_unticked_box(self):
        self.assertEqual(intake.read_form(BODY.replace("- [X] two", "- [ ] two"))[2], ["2 of 3 confirmations ticked"])

    def test_the_template_has_the_headings(self):
        text = (ROOT / ".github/ISSUE_TEMPLATE/mod.yml").read_text(encoding="utf-8")
        for heading in (intake.URL_HEADING, intake.ABOUT_HEADING, intake.CONFIRM_HEADING):
            self.assertIn(heading, text)
        self.assertEqual(text.count("required: true") - 2, intake.CONFIRMATIONS)

    def test_the_release_is_the_authors(self):
        self.assertEqual(intake.release_problem(URL, "Someone"), "")
        self.assertIn("belongs to", intake.release_problem(URL, "other"))
        self.assertIn("not a GitHub", intake.release_problem("https://pan.quark.cn/s/abc", "someone"))


class ReviewTests(unittest.TestCase):
    def review(self, package_id, version, author, url=URL, index=None, known=None):
        with tempfile.TemporaryDirectory() as folder:
            path = package(Path(folder), package_id, version)
            return intake.review(path, author, url, index or {"packages": []}, {"srw64": "github:dyzz"} if known is None else known, None)

    def test_a_new_author(self):
        problems, entry, owners = self.review("someone.funnels", "1.1.0", "someone")
        self.assertEqual(problems, [])
        self.assertEqual(entry["url"], URL)
        self.assertEqual(entry["source"], "https://github.com/someone/mods/releases/tag/v1.1")
        self.assertTrue(entry["community"])
        self.assertNotIn("path", entry)
        self.assertEqual(owners["someone"], "github:someone")

    def test_someone_elses_prefix(self):
        self.assertTrue(self.review("srw64.funnels", "1.1.0", "someone")[0])
        self.assertTrue(self.review("someone.funnels", "1.1.0", "other", known={"someone": "github:someone"})[0])

    def test_an_update_must_be_newer(self):
        index = {"packages": [{"id": "someone.funnels", "version": "1.1.0"}]}
        self.assertTrue(self.review("someone.funnels", "1.1.0", "someone", index=index)[0])
        self.assertEqual(self.review("someone.funnels", "1.2.0", "someone", index=index)[0], [])


if __name__ == "__main__":
    unittest.main()
