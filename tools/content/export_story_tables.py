"""The scene table for story MOD authors (docs/guide/story-mods/scenes.csv).

A story event names its stage by scene number (docs/guide/story-mods/README.md §2); this
lists every stage's scene number with its title in each language, from the stage metadata
(content/stages/scene-*.json).

    .venv/bin/python tools/content/export_story_tables.py
"""
from __future__ import annotations

import csv
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STAGES = ROOT / 'content/stages'
OUT = ROOT / 'docs/guide/story-mods/scenes.csv'
LOCALES = ('ja', 'zh-Hans', 'zh-Hant', 'en')


def main() -> None:
    rows = []
    for path in sorted(STAGES.glob('scene-*.json')):
        meta = json.loads(path.read_text(encoding='utf-8'))
        title = meta.get('title', {})
        rows.append([meta['scene'], *(' '.join(title.get(l, '').split()) for l in LOCALES)])
    rows.sort()
    with OUT.open('w', encoding='utf-8', newline='') as f:
        out = csv.writer(f, lineterminator='\n')
        out.writerow(['scene', *(f'title_{l}' for l in LOCALES)])
        out.writerows(rows)
    print(OUT.relative_to(ROOT), len(rows), 'scenes')


if __name__ == '__main__':
    main()
