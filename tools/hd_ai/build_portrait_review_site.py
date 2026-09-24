"""Local review page: every portrait, original beside its whole HD image.

Originals are decoded from the ROM under the base palette and cropped to the
96x96 the game draws; HD images are linked from the whole-image set. The page
stays local because the art comes from the ROM and is not distributed.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import json
from pathlib import Path
import struct

from srw64_rom.resources import ResourceTable
from tools.hd_ai.portrait_batch import ACTORS, FACE_TABLE, SILHOUETTE, decode, portraits
from tools.hd_ai.aliyun import ROOT


def names() -> tuple[list[str], dict[str, str]]:
    labels = [json.loads(line)['label'] for line in (ROOT / 'assets/original-data/records/actors.jsonl').open()]
    terms = json.loads((ROOT / 'content/locales/terms/zh-Hans.json').read_text())['sections']['pilots']
    return labels, {k: v for k, v in terms.items() if isinstance(v, str)}


def metrics(batch: Path, reviewed: Path) -> dict[int, dict]:
    found = {}
    for grid_id, grid in json.loads((batch / 'report.json').read_text())['results'].items():
        for cell in grid.get('cells', []):
            if cell['status'] == 'completed':
                m = cell['matte']
                found[cell['resource_id']] = {'source': f'2×2 拼图 {grid_id}', 'fidelity': cell['fidelity'],
                                              'iou': round(m['silhouette_iou_against_original'], 3),
                                              'warnings': m.get('warnings', [])}
    for row in json.loads((reviewed / 'report.json').read_text())['portraits']:
        found[row['resource_id']] = {'source': '第一话已审单张', 'fidelity': None,
                                     'iou': round(row['matte']['silhouette_iou_against_original'], 3), 'warnings': []}
    return found


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--whole', type=Path, required=True, help='build_portrait_images output (portraits.json)')
    parser.add_argument('--batch', type=Path, required=True)
    parser.add_argument('--reviewed', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    (args.output / 'original').mkdir(parents=True, exist_ok=True)
    rom = (ROOT / 'rom.z64').read_bytes()
    table = ResourceTable(rom)
    labels, zh = names()
    actors, silhouettes = defaultdict(list), set()
    for actor in range(ACTORS):
        image, palette = struct.unpack_from('>2H', rom, FACE_TABLE + 4 * actor)
        if palette == SILHOUETTE:
            silhouettes.add(image)
        elif actor < len(labels) and labels[actor] not in actors[image]:
            actors[image].append(labels[actor])
    info = metrics(args.batch, args.reviewed)
    index = json.loads((args.whole / 'portraits.json').read_text())
    whole = {row['image']: row for row in index['images']}
    rows = []
    for row in portraits(rom):
        image_id = row['resource_id']
        source = decode(table, image_id, row['palette_id'])
        name = f'original/portrait-{image_id}.png'
        source.crop((0, 0, 96, 96)).save(args.output / name)
        jp = actors.get(image_id, [])
        rows.append({'id': image_id, 'size': source.width, 'palette': row['palette_id'],
                     'jp': jp, 'zh': [zh.get(n, '') for n in jp], 'protagonist': bool(row.get('protagonist')),
                     'silhouette': image_id in silhouettes, 'original': name,
                     'hd': str(Path('..') / args.whole.name / whole[image_id]['file']),
                     **info.get(image_id, {'source': '', 'fidelity': None, 'iou': None, 'warnings': []})})
    page = (Path(__file__).with_name('portrait_review.html')).read_text()
    page = page.replace('/*DATA*/[]', json.dumps(rows, ensure_ascii=False))
    (args.output / 'index.html').write_text(page)
    print({'portraits': len(rows), 'output': str(args.output / 'index.html')})


if __name__ == '__main__':
    main()
