#!/usr/bin/env python3
"""Line up a fan translation with our story records, for polishing by comparison.

The reference is Balcerzak's Let's Play on Serenes Forest (one playthrough, Manami route),
saved chapter by chapter under assets/translation-runs/reference-en/raw/chNN.txt with
`Speaker: line` dialogue and `<stage directions>`. It stays out of git and out of the
shipped files: it is a second opinion for meaning and tone, never a source text.

    PYTHONPATH=src .venv/bin/python -B tools/translation/align_reference.py --chapters 1-3
    PYTHONPATH=src .venv/bin/python -B tools/translation/align_reference.py            # all raw chapters

For every chapter the script finds the scene (by title, walking the route graph), aligns the
reference lines with the scene's dialogue records (speaker + similarity of the two English
renderings, Needleman-Wunsch with gaps), and writes
assets/translation-runs/reference-en/aligned/scene-NNNN.jsonl with one row per record:
text_id, speaker, ja, en (ours), zh (ours), ref (theirs, or null when unmatched).
"""
from __future__ import annotations

import argparse
import difflib
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "src"))
from srw64_native.dialogue_text import parse  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
REF = ROOT / "assets/translation-runs/reference-en"
STORY = ROOT / "assets/original-data/story"
DIALOGUE = ROOT / "content/dialogue"

HEADER = re.compile(r'^=== Chapter (\d+): "(.*)" ===')
SPEAKER_LINE = re.compile(r"^([^:<(]{1,40}): (.+)$")
PLACEHOLDER = re.compile(r"\{[A-Za-z]+\}")
# The reference plays Manami with Aisha as the rival; our lines carry placeholders instead.
PLACEHOLDERS = {"HeroName": "Manami", "HeroSurname": "Hamill", "HeroNick": "Manami", "HeroFull": "Manami Hamill",
                "PartnerName": "Aisha", "PartnerSurname": "Ridgemond", "PartnerNick": "Aisha",
                "PartnerFull": "Aisha Ridgemond", "HeroMech": "Simurgh"}
WORD = re.compile(r"[a-z0-9']+")


def norm_name(name: str) -> str:
    name = name.lower()
    name = re.sub(r"\(.*?\)", "", name)
    name = re.sub(r"[^a-z]", "", name)
    for long, short in (("ou", "o"), ("uu", "u"), ("ii", "i"), ("ee", "e"), ("oo", "o"), ("aa", "a")):
        name = name.replace(long, short)
    return name


def words(text: str) -> set[str]:
    return {w for w in WORD.findall(text.lower()) if len(w) > 1}


def jaccard(a: set[str], b: set[str]) -> float:
    if not a or not b:
        return 0.0
    return len(a & b) / len(a | b)


def parse_chapter(path: Path) -> dict:
    number, title, lines = None, "", []
    for raw in path.read_text(encoding="utf-8").split("\n"):
        line = raw.rstrip()
        head = HEADER.match(line)
        if head:
            number, title = int(head.group(1)), head.group(2)
            continue
        if not line or line.startswith("#") or line.startswith("<"):
            continue
        m = SPEAKER_LINE.match(line)
        if not m:
            continue
        speaker, text = m.group(1).strip(), m.group(2).strip()
        hint = None
        h = re.match(r"^\?+\s*\((.+)\)$", speaker)
        if h:
            speaker, hint = "???", h.group(1)
        lines.append({"speaker": speaker, "hint": hint, "text": text, "line": len(lines)})
    if number is None:
        raise SystemExit(f"{path}: no '=== Chapter N: \"title\" ===' header")
    return {"number": number, "title": title, "lines": lines, "path": str(path)}


def load_targets(locale: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for path in sorted((DIALOGUE / locale / "story").glob("*.txt")):
        entries, problems = parse(path.read_text(encoding="utf-8"), str(path))
        for entry in entries:
            pages = [" ".join(page.target) for page in entry.pages if page.target]
            if pages:
                out[entry.key] = " ".join(pages)
    return out


class Names:
    def __init__(self) -> None:
        sections = json.loads((ROOT / "content/locales/terms/en.json").read_text(encoding="utf-8"))["sections"]
        self.en = {**sections.get("pilot_full_names", {}), **sections.get("pilots", {})}
        self.stages = sections.get("stages", {})

    def speaker(self, line: dict) -> tuple[str | None, bool]:
        """(english name or None, resolved?) for a scene dialogue line."""
        spk = line.get("speaker") or {}
        label = re.sub(r"（.*?）$", "", spk.get("label") or "").strip()
        if not label:
            return None, False
        resolved = spk.get("status") in ("resolved", "route-resolved-by-scene")
        return self.en.get(label, label), resolved


def speaker_score(ref: dict, ours: str | None, resolved: bool, aliases: dict[str, str] | None = None) -> float:
    if aliases and ours and aliases.get(ref["speaker"]) == ours:
        return 1.0
    if ref["speaker"] == "???":
        if ours is None:
            return 0.5
        return 1.0 if ref["hint"] and norm_name(ref["hint"]) == norm_name(ours) else 0.0
    if ours is None or not resolved:
        return 0.0
    a, b = norm_name(ref["speaker"]), norm_name(ours)
    if a == b or (len(a) > 3 and (a in b or b in a)):
        return 1.0
    ratio = difflib.SequenceMatcher(None, a, b).ratio()
    return 1.0 if ratio >= 0.75 else (0.3 if ratio >= 0.6 else -2.0)


def scene_dialogue(scene: dict) -> list[dict]:
    rows = []
    for event in scene["events"]:
        for line in event["lines"]:
            if line.get("kind") == "dialogue" and line.get("text_key"):
                rows.append({**line, "event": event.get("phase_label") or event.get("phase")})
    return rows


def align(ref_lines: list[dict], ours: list[dict], names: Names, en: dict[str, str],
          aliases: dict[str, str] | None = None) -> list[tuple[int, int, float]]:
    """Needleman-Wunsch over (reference line, scene record); returns accepted (i, j, score).

    Two passes: the first learns which of our speaker names each reference name stands for
    (Brai the Great = Emperor Burai) from confident text matches, the second uses them."""
    if aliases is None:
        votes: dict[str, dict[str, int]] = {}
        for i, j, _ in align(ref_lines, ours, names, en, aliases={}):
            spk = names.speaker(ours[j])[0]
            sim = jaccard(words(ref_lines[i]["text"]), words(PLACEHOLDER.sub("", en.get(ours[j]["text_key"], ""))))
            if spk and ref_lines[i]["speaker"] != "???" and sim >= 0.25:
                votes.setdefault(ref_lines[i]["speaker"], {}).setdefault(spk, 0)
                votes[ref_lines[i]["speaker"]][spk] += 1
        aliases = {}
        for ref_name, counts in votes.items():
            best = max(counts, key=counts.get)
            if counts[best] >= 2 and counts[best] >= 0.6 * sum(counts.values()):
                aliases[ref_name] = best
    n, m = len(ref_lines), len(ours)
    ref_words = [words(r["text"]) for r in ref_lines]
    our_words, our_spk = [], []
    for row in ours:
        text = en.get(row["text_key"], "")
        text = PLACEHOLDER.sub(lambda mt: PLACEHOLDERS.get(mt.group(0)[1:-1], ""), text)
        our_words.append(words(text))
        our_spk.append(names.speaker(row))
    gap = -0.6
    score = [[0.0] * (m + 1) for _ in range(n + 1)]
    back = [[0] * (m + 1) for _ in range(n + 1)]
    for i in range(1, n + 1):
        score[i][0] = i * gap
        back[i][0] = 1
    for j in range(1, m + 1):
        score[0][j] = j * gap
        back[0][j] = 2
    pair = [[0.0] * m for _ in range(n)]
    for i in range(n):
        for j in range(m):
            pair[i][j] = (speaker_score(ref_lines[i], our_spk[j][0], our_spk[j][1], aliases)
                          + 4.0 * jaccard(ref_words[i], our_words[j]))
    for i in range(1, n + 1):
        for j in range(1, m + 1):
            diag = score[i - 1][j - 1] + pair[i - 1][j - 1]
            up = score[i - 1][j] + gap
            left = score[i][j - 1] + gap
            best = max(diag, up, left)
            score[i][j] = best
            back[i][j] = 0 if best == diag else (1 if best == up else 2)
    i, j, diag_steps = n, m, []
    while i > 0 or j > 0:
        move = back[i][j]
        if move == 0 and i > 0 and j > 0:
            i, j = i - 1, j - 1
            spk = speaker_score(ref_lines[i], our_spk[j][0], our_spk[j][1], aliases)
            sim = jaccard(ref_words[i], our_words[j])
            diag_steps.append((i, j, spk, sim))
        elif move == 1 or j == 0:
            i -= 1
        else:
            j -= 1
    diag_steps.reverse()
    strong = {(i, j) for i, j, spk, sim in diag_steps if (spk >= 0 and sim >= 0.12) or sim >= 0.3}
    out = []
    for i, j, spk, sim in diag_steps:
        ok = (i, j) in strong
        if not ok and spk > 0 and sim >= 0.04:
            # a same-speaker line wedged between accepted neighbours is almost surely the same line
            ok = ((i - 1, j - 1) in strong and (i + 1, j + 1) in strong) or \
                 (((i - 1, j - 1) in strong or (i + 1, j + 1) in strong) and sim >= 0.08)
        if ok:
            out.append((i, j, round(spk + 4 * sim, 2)))
    return sorted(out)


def candidates_for(chapter: dict, index: list[dict], names: Names, previous: int | None) -> list[int]:
    """Scenes worth trying: the route successors within two hops, or for the first chapter every
    scene whose title looks alike plus the four protagonists' openings."""
    if previous is None:
        title = chapter["title"].lower()
        out = [s for s in range(len(index))
               if difflib.SequenceMatcher(None, title, names.stages.get(index[s]["title"], "").lower()).ratio() >= 0.5]
        return sorted(set(out) | {0, 1, 2, 3})
    out: list[int] = []
    for hop1 in index[previous]["next_scenes"]:
        if hop1 not in out:
            out.append(hop1)
        for hop2 in index[hop1]["next_scenes"]:
            if hop2 not in out:
                out.append(hop2)
    return out


def parse_range(text: str | None) -> set[int] | None:
    if not text:
        return None
    out: set[int] = set()
    for part in text.split(","):
        a, _, b = part.partition("-")
        out.update(range(int(a), int(b or a) + 1))
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--chapters", help="e.g. 1-3,5; default all raw/chNN.txt")
    parser.add_argument("--out", type=Path, default=REF / "aligned")
    args = parser.parse_args()
    wanted = parse_range(args.chapters)
    chapters = [parse_chapter(p) for p in sorted((REF / "raw").glob("ch*.txt"))]
    chapters = [c for c in chapters if wanted is None or c["number"] in wanted]
    if not chapters:
        raise SystemExit("no chapters")
    index = json.loads((STORY / "index.json").read_text(encoding="utf-8"))["scenes"]
    overrides_path = REF / "chapter-map.json"
    overrides = json.loads(overrides_path.read_text()) if overrides_path.exists() else {}
    names = Names()
    en, zh = load_targets("en"), load_targets("zh-Hans")
    args.out.mkdir(parents=True, exist_ok=True)
    previous = None
    summary = []
    for chapter in sorted(chapters, key=lambda c: c["number"]):
        key = str(chapter["number"])
        if key in overrides:
            trials = [overrides[key]]
        else:
            trials = candidates_for(chapter, index, names, previous)
        best = None
        for s in trials:
            trial_scene = json.loads((STORY / f"{s:04d}.json").read_text(encoding="utf-8"))
            trial_pairs = align(chapter["lines"], scene_dialogue(trial_scene), names, en)
            if best is None or len(trial_pairs) > best[1]:
                best = (s, len(trial_pairs))
        if best is None or best[1] < max(3, len(chapter["lines"]) * 0.2):
            print(f"chapter {chapter['number']} {chapter['title']!r}: no scene matched well "
                  f"(tried {trials}, best {best}); add to {overrides_path.name}")
            previous = None
            continue
        scene_no = best[0]
        previous = scene_no
        scene = json.loads((STORY / f"{scene_no:04d}.json").read_text(encoding="utf-8"))
        ours = scene_dialogue(scene)
        pairs = align(chapter["lines"], ours, names, en)
        by_record = {j: (i, s) for i, j, s in pairs}
        rows = []
        for j, row in enumerate(ours):
            spk, resolved = names.speaker(row)
            hit = by_record.get(j)
            rows.append({
                "text_id": row["text_id"], "key": row["text_key"], "speaker": spk,
                "speaker_ja": (row.get("speaker") or {}).get("label"), "event": row["event"],
                "ja": row["display"], "en": en.get(row["text_key"]), "zh": zh.get(row["text_key"]),
                "ref": chapter["lines"][hit[0]]["text"] if hit else None,
                "ref_speaker": chapter["lines"][hit[0]]["speaker"] if hit else None,
                "score": hit[1] if hit else None,
            })
        out = args.out / f"scene-{scene_no:04d}.jsonl"
        out.write_text("".join(json.dumps(r, ensure_ascii=False) + "\n" for r in rows), encoding="utf-8")
        matched_ref = len(pairs)
        summary.append((chapter["number"], chapter["title"], scene_no, scene["title"], len(chapter["lines"]),
                        len(ours), matched_ref))
        unmatched = [l["speaker"] + ": " + l["text"][:60] for i, l in enumerate(chapter["lines"])
                     if i not in {i for i, _, _ in pairs}]
        print(f"chapter {chapter['number']:2d} -> scene {scene_no:3d} {scene['title']}: "
              f"ref {len(chapter['lines'])} lines, ours {len(ours)} records, matched {matched_ref} "
              f"({matched_ref / max(1, len(chapter['lines'])):.0%} of ref)")
        for u in unmatched[:8]:
            print("    unmatched ref:", u)
        if len(unmatched) > 8:
            print(f"    ... {len(unmatched) - 8} more")
    (args.out / "summary.json").write_text(json.dumps(
        [dict(zip(("chapter", "title", "scene", "scene_title", "ref_lines", "records", "matched"), s)) for s in summary],
        ensure_ascii=False, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
