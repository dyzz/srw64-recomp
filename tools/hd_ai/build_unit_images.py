"""Whole HD unit poses for the native pages, indexed by their ROM triplet.

The pages (battle confirm, upgrade, ability, swap) draw the unit's basic battle
pose from battle_assets.units, one PNG per (scene, atlas, palette). This tool
takes the settled ESRGAN blend (esrgan_pose.py --blend, 8x masters with alpha)
and writes the pack folder: unit-<scene>-<atlas>-<palette>.png plus units.json,
the index compile_art copies into the art pack as srw64-units-hd.json. The
launcher and profile.py then attach each unit's `hd` file by that triplet.

    .venv/bin/python -m tools.hd_ai.build_unit_images --run assets/hd-ai/unit-poses/all-1 \
        --output assets/hd-ai/unit-poses/whole-v1 --bind

The images stay PNG (they carry alpha; the pages crop by it); the release pack
stores them as JPEG plus an alpha PNG (tools/release/compress_hd.py).
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageFilter

from tools.hd_ai.aliyun import ROOT

WORK = 8


def clean_alpha(result: Image.Image, source: Image.Image, band: int = 12, floor: int = 8) -> Image.Image:
    """Keep the model's alpha only within `band` px of the original mask and drop
    faint values: the models leave stray alpha over the whole canvas, which made
    the pages' alpha-bounds crop (`rect`) the entire file (2026-09-26)."""
    mask = source.getchannel('A').point(lambda v: 255 if v else 0).resize(result.size, Image.Resampling.NEAREST)
    mask = mask.filter(ImageFilter.MaxFilter(2 * band + 1))
    alpha = Image.eval(result.getchannel('A'), lambda v: 0 if v < floor else v)
    alpha = Image.composite(alpha, Image.new('L', result.size, 0), mask)
    cleaned = result.copy()
    cleaned.putalpha(alpha)
    return cleaned


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--run', type=Path, required=True, help='esrgan_pose.py --blend output (report.json, hd/)')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--bind', action='store_true', help='point content/art/stage1-hd.json at these poses')
    args = parser.parse_args()
    report = json.loads((args.run / 'report.json').read_text())
    if not report.get('blend'):
        raise SystemExit('the run is not a --blend run')
    args.output.mkdir(parents=True, exist_ok=False)
    rows = []
    for key, row in sorted(report['results'].items()):
        if 'recipe' not in row:
            continue  # the per-model intermediates
        image = Image.open(row['path']).convert('RGBA')
        # Runs before 2026-09-26 carried stray alpha over the whole canvas; clean it against the source mask.
        source = Image.open(args.run / 'inputs' / f'{key}-source.png').convert('RGBA')
        image = clean_alpha(image, source)
        name = f'{key}.png'
        image.save(args.output / name, optimize=True)
        rows.append({'scene': row['scene'], 'atlas': row['atlas'], 'palette': row['palette'], 'units': row['units'],
                     'file': name, 'sha256': sha(args.output / name), 'width': image.width, 'height': image.height})
    index = {'schema': 'srw64.unit-images.v1', 'scale': WORK, 'recipe': report['results'][rows[0]['file'][:-4]]['recipe'],
             'images': rows}
    (args.output / 'units.json').write_text(json.dumps(index, indent=2) + '\n')
    print({'poses': len(rows), 'bytes': sum((args.output / r['file']).stat().st_size for r in rows)})
    if args.bind:
        path = ROOT / 'content/art/stage1-hd.json'
        manifest = json.loads(path.read_text())
        manifest['units'] = {'path': str(args.output.resolve().relative_to(ROOT)), 'manifest_sha256': sha(args.output / 'units.json')}
        path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
        print('bound', path.relative_to(ROOT), len(rows), 'whole unit poses')


if __name__ == '__main__':
    main()
