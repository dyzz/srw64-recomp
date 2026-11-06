#!/usr/bin/env python3
"""Check a mod submitted through the GitHub Issue form and write its entry for the website.

docs/design/mod-packages.md §13.3. It reads the Issue (.github/ISSUE_TEMPLATE/mod.yml), downloads
the file from the author's GitHub Release, runs validate.py against the listed packages and the
owners, and prints what a person still has to review. Nothing is written without --write:

- web/src/data/mods/<id>.json, the entry the MOD download page lists;
- web/src/data/mod-owners.json, the first time a prefix (the part of the id before the dot) is seen.

    .venv/bin/python tools/mods/intake.py 12                 check Issue 12
    .venv/bin/python tools/mods/intake.py 12 --write         check it and write the entry
    .venv/bin/python tools/mods/intake.py --url URL --author NAME    check a Release file, no Issue

The file stays on the author's Release; nothing here uploads it anywhere.
"""
import argparse
from datetime import date
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "mods"))
import validate  # noqa: E402

REPO = "dyzz/srw64-recomp"
DATA = ROOT / "web/src/data/mods"
OWNERS = ROOT / "web/src/data/mod-owners.json"
HASHES = ROOT / "tools/mods/original-hashes.txt"
URL_HEADING = "Release 文件链接 / Release file link"
ABOUT_HEADING = "内容与素材来源 / What it does and where its material comes from"
CONFIRM_HEADING = "确认 / Confirm"
CONFIRMATIONS = 3
RELEASE_FILE = re.compile(r"https://github\.com/([A-Za-z0-9-]+)/([A-Za-z0-9._-]+)/releases/download/([^/?#\s]+)/([^/?#\s]+)")


def sections(body: str) -> dict[str, str]:
    """An Issue form's body: `### label`, then what was filled in."""
    out: dict[str, str] = {}
    label = None
    for line in body.replace("\r\n", "\n").split("\n"):
        if line.startswith("### "):
            label = line[4:].strip()
            out[label] = ""
        elif label is not None:
            out[label] += line + "\n"
    return {k: v.strip() for k, v in out.items()}


def read_form(body: str) -> tuple[str, str, list[str]]:
    """The link, the description and what is wrong with the form."""
    form = sections(body)
    problems = []
    url, about = form.get(URL_HEADING, ""), form.get(ABOUT_HEADING, "")
    if not url:
        problems.append("the form has no Release file link")
    if not about or about == "_No response_":
        problems.append("the form does not say what the package does")
    ticked = len(re.findall(r"^- \[[xX]\]", form.get(CONFIRM_HEADING, ""), re.M))
    if ticked != CONFIRMATIONS:
        problems.append(f"{ticked} of {CONFIRMATIONS} confirmations ticked")
    return url, about, problems


def release_problem(url: str, author: str) -> str:
    match = RELEASE_FILE.fullmatch(url)
    if not match:
        return "the link is not a GitHub Release file"
    if match[1].lower() != author.lower():
        return f"the Release belongs to {match[1]}, the submission to {author}"
    return ""


def listed() -> dict:
    return {"packages": [json.loads(p.read_text(encoding="utf-8")) for p in sorted(DATA.glob("*.json"))]}


def owners() -> dict:
    return json.loads(OWNERS.read_text(encoding="utf-8"))


def review(path: Path, author: str, url: str, index: dict, known: dict, hashes: set[str] | None) -> tuple[list[str], dict, dict]:
    """What is wrong with the downloaded file, its entry and the owners with this author added."""
    owner = f"github:{author}"
    _, summary = validate.check(path, host_only=True)
    package = summary.get("id", "")
    updated = dict(known)
    if package:
        updated.setdefault(package.split(".", 1)[0], owner)
    problems, summary = validate.check(path, original_hashes=hashes, index=index, owners=updated, owner=owner)
    words = [validate.wording(p, None) for p in problems]
    if "id" not in summary or "sha256" not in summary:
        return words, {}, known
    match = RELEASE_FILE.fullmatch(url)
    entry = {k: v for k, v in summary.items() if k != "path"}
    entry.update({"url": url, "page": f"/mods/#{entry['id']}",
                  "source": f"https://github.com/{match[1]}/{match[2]}/releases/tag/{match[3]}",
                  "published": date.today().isoformat(), "yanked": None, "community": True})
    return words, entry, updated


def download(url: str, folder: Path) -> Path:
    path = folder / RELEASE_FILE.fullmatch(url)[4]
    subprocess.run(["curl", "-fsSL", "--proto", "=https", "--proto-redir", "=https", "--max-filesize", str(validate.MAX_SUBMISSION),
                    "-o", str(path), url], check=True)
    return path


def fetch_issue(number: int) -> dict:
    result = subprocess.run(["gh", "issue", "view", str(number), "--repo", REPO, "--json", "author,body,state,title,url"],
                            capture_output=True, text=True, check=True)
    return json.loads(result.stdout)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("issue", type=int, nargs="?", help="the Issue number")
    parser.add_argument("--url", help="check this Release file instead of an Issue")
    parser.add_argument("--author", help="the GitHub user submitting, with --url")
    parser.add_argument("--write", action="store_true", help="write the entry and the owner when everything passes")
    args = parser.parse_args(argv)
    if (args.issue is None) == (args.url is None) or bool(args.url) != bool(args.author):
        parser.error("give an Issue number, or --url and --author")

    problems: list[str] = []
    about = ""
    if args.issue is not None:
        issue = fetch_issue(args.issue)
        author = issue["author"]["login"]
        url, about, problems = read_form(issue["body"])
        print(f"{issue['title']} ({issue['state']}) by {author}\n{issue['url']}")
    else:
        url, author = args.url, args.author
    wrong = release_problem(url, author) if url else ""
    if wrong:
        problems.append(wrong)
    entry: dict = {}
    updated: dict = {}
    if url and not wrong:
        hashes = validate.load_original_hashes(HASHES) if HASHES.exists() else None
        if hashes is None:
            print("note: tools/mods/original-hashes.txt does not exist, so the original's data was not looked for")
        with tempfile.TemporaryDirectory() as folder:
            try:
                path = download(url, Path(folder))
            except subprocess.CalledProcessError as error:
                problems.append(f"the download failed (curl exit {error.returncode})")
            else:
                words, entry, updated = review(path, author, url, listed(), owners(), hashes)
                problems += words

    if entry:
        name = entry["name"].get("zh-Hans") or entry["name"].get("en") or next(iter(entry["name"].values()), "")
        print(f"\n{entry['id']} {entry['version']}  {name}\n"
              f"contents {', '.join(entry['contents'])}; license {entry['license']}; "
              f"game {entry['game']}; {entry['bytes'] / 1048576:.1f} MB\nsha256 {entry['sha256']}")
    if about:
        print(f"\nThe author says:\n{about}")
    if problems:
        print("\nNot accepted:\n  " + "\n  ".join(problems))
        return 1
    print("\nThe checks passed. Still for a person (§13.5): the description matches the contents, no third "
          "party's art or music, no commercial music or soundtrack audio.")
    if args.write:
        path = DATA / f"{entry['id']}.json"
        old = json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}
        path.write_text(json.dumps(entry, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        OWNERS.write_text(json.dumps(updated, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"wrote {path.relative_to(ROOT)}")
        if old.get("netdisk"):
            print("the old version's net disk links were dropped: upload the new file and add netdisk again")
    return 0


if __name__ == "__main__":
    sys.exit(main())
