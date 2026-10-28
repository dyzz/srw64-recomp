"""Build and serve a local ROM/active-HD portrait and unit-art review desk.

    .venv/bin/python -m tools.hd_ai.art_review build
    .venv/bin/python -m tools.hd_ai.art_review serve --port 8766

Reads the active content manifest, never changes production art. Reviews live in
the output directory; image hashes invalidate old decisions after art changes.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import struct
import threading

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont, ImageOps, ImageStat

from tools.hd_ai.aliyun import ROOT

DEFAULT = ROOT / 'output/art-review'


def read(path):
    return json.loads(Path(path).read_text())


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def inspect(original, hd, expected_hash):
    with Image.open(original) as src, Image.open(hd) as high:
        src, high = src.convert('RGBA'), high.convert('RGBA')
        low = high.resize(src.size, Image.Resampling.LANCZOS)
        m = src.getchannel('A').point(lambda x: 255 if x >= 128 else 0)
        n = low.getchannel('A').point(lambda x: 255 if x >= 128 else 0)
        intersection, union = ImageChops.darker(m, n), ImageChops.lighter(m, n)
        iou = intersection.histogram()[255] / max(1, union.histogram()[255])
        color = sum(ImageStat.Stat(ImageChops.difference(src.convert('RGB'), low.convert('RGB')), intersection).mean) / 3 if intersection.getbbox() else 0
        allowed = src.getchannel('A').point(lambda x: 255 if x else 0).filter(ImageFilter.MaxFilter(5))
        opaque = high.getchannel('A').point(lambda x: 255 if x >= 16 else 0)
        outside = ImageChops.subtract(opaque, allowed.resize(high.size, Image.Resampling.NEAREST)).histogram()[255]
        flags = []
        if sha(hd) != expected_hash:
            flags.append('素材哈希与接入索引不一致')
        if high.size != (src.width * 8, src.height * 8):
            flags.append('HD 尺寸不等于原图 8 倍')
        if iou < .94:
            flags.append('轮廓偏差较大，需对照检查')
        if outside > 64:
            flags.append('原轮廓外有不透明像素，需检查边缘')
        return {'iou': round(iou, 4), 'color_delta': round(color, 2), 'outside_pixels': outside,
                'source_size': list(src.size), 'hd_size': list(high.size), 'flags': flags}


def link_image(output, name, source):
    target = output / 'media' / name
    if target.is_symlink() or target.exists():
        target.unlink()
    target.symlink_to(source.resolve())
    return 'media/' + name


def sheets(output, rows):
    folder = output / 'sheets'
    folder.mkdir(exist_ok=True)
    font_path = '/System/Library/Fonts/Hiragino Sans GB.ttc'
    font = ImageFont.truetype(font_path, 14) if Path(font_path).exists() else ImageFont.load_default()
    for kind in ('unit', 'portrait'):
        subset = [r for r in rows if r['kind'] == kind]
        for start in range(0, len(subset), 20):
            page = Image.new('RGB', (1440, 1190), '#22252c')
            draw = ImageDraw.Draw(page)
            draw.text((12, 8), f'{kind} {start + 1}–{min(start + 20, len(subset))} / {len(subset)} | ROM ← → ACTIVE HD', font=font, fill='white')
            for i, row in enumerate(subset[start:start + 20]):
                x, y = (i % 4) * 360, (i // 4) * 230 + 35
                draw.text((x + 8, y + 4), row['id'], font=font, fill='#b4c9e1')
                draw.text((x + 8, y + 24), row['name'][:23], font=font, fill='white')
                for j, key in enumerate(('original', 'hd')):
                    with Image.open(output / row[key]) as im:
                        im = im.convert('RGBA')
                        im = ImageOps.contain(im, (170, 170), Image.Resampling.NEAREST if key == 'original' else Image.Resampling.LANCZOS)
                        px, py = x + j * 178 + 6, y + 51
                        draw.rectangle((px, py, px + 170, py + 170), fill='#646470')
                        page.paste(im, (px + (170 - im.width) // 2, py + (170 - im.height) // 2), im)
            page.save(folder / f'{kind}-{start // 20 + 1:02d}.jpg', quality=90)


def build(output):
    from srw64_rom.resources import ResourceTable
    from tools.hd_ai.portrait_batch import ACTORS, FACE_TABLE, SILHOUETTE, decode, portraits
    from tools.hd_ai.build_portrait_review_site import metrics
    output.mkdir(parents=True, exist_ok=True)
    (output / 'media').mkdir(exist_ok=True)
    manifest = read(ROOT / 'content/art/stage1-hd.json')
    terms = read(ROOT / 'content/locales/terms/zh-Hans.json')['sections']
    samples = {r['id']: r for r in read(ROOT / 'assets/hd-ai/unit-poses/all-1/samples.json')['samples']}
    rows = []
    binding_checks = {}
    for kind, section, filename in [('unit', 'units', 'units.json'), ('portrait', 'portraits', 'portraits.json')]:
        binding = manifest[section]
        folder = ROOT / binding['path']
        binding_checks[section] = sha(folder / filename) == binding['manifest_sha256']
        for row in read(folder / filename)['images']:
            if kind == 'unit':
                rid = f"unit-{row['scene']}-{row['atlas']}-{row['palette']}"
                sample = samples[rid]
                jp = list(dict.fromkeys(sample['names']))
                zh = [terms['units'].get(n, n) for n in jp]
                source = ROOT / 'assets/hd-ai/unit-poses/all-1' / sample['source']
                if sha(source) != sample['source_sha256']:
                    raise ValueError(f'original snapshot hash changed: {rid}')
                entry = {'id': rid, 'jp': jp, 'name': ' / '.join(dict.fromkeys(zh)), 'identities': row['units'],
                         'palette': row['palette'], 'recipe': read(folder / filename)['recipe']}
            else:
                rid = f"portrait-{row['image']}"
                entry = {'id': rid, 'resource': row['image'], 'palette': row['palette'], 'recipe': 'Qwen 整张人物头像'}
                source = output / 'media' / f'{rid}-rom.png'
            entry.update(kind=kind, hd=link_image(output, f'{rid}-hd.png', folder / row['file']),
                         hd_sha256=sha(folder / row['file']), expected_sha256=row['sha256'],
                         active_path=str((folder / row['file']).relative_to(ROOT)),
                         initial_status='unreviewed', initial_note='', tags=[])
            if kind == 'unit':
                entry['original'] = link_image(output, f'{rid}-rom.png', source)
                entry['metrics'] = inspect(source, folder / row['file'], row['sha256'])
            rows.append(entry)
    rom = (ROOT / 'rom.z64').read_bytes()
    table = ResourceTable(rom)
    labels = [json.loads(line)['label'] for line in (ROOT / 'assets/original-data/records/actors.jsonl').open()]
    actor_names, silhouettes = {}, set()
    for actor in range(ACTORS):
        image, palette = struct.unpack_from('>2H', rom, FACE_TABLE + 4 * actor)
        if palette == SILHOUETTE:
            silhouettes.add(image)
        elif actor < len(labels):
            actor_names.setdefault(image, []).append(labels[actor])
    sources = {r['resource_id']: r for r in portraits(rom)}
    historical = metrics(ROOT / 'assets/hd-ai/portrait-batch/full-1', ROOT / 'assets/hd-ai/portrait-matte/v2')
    for row in rows:
        if row['kind'] != 'portrait':
            continue
        source = sources[row['resource']]
        path = output / 'media' / f"{row['id']}-rom.png"
        decode(table, row['resource'], source['palette_id']).crop((0, 0, 96, 96)).save(path)
        row['original'] = 'media/' + path.name
        row['jp'] = list(dict.fromkeys(actor_names.get(row['resource'], [])))
        row['name'] = ' / '.join(dict.fromkeys(terms['pilots'].get(n, n) for n in row['jp'])) or '主角（存档画面）'
        row['metrics'] = inspect(path, output / row['hd'], row['expected_sha256'])
        row['historical'] = historical.get(row['resource'], {})
        row['silhouette'] = row['resource'] in silhouettes
        if row['historical'].get('warnings'):
            row['metrics']['flags'].append('历史生成报告有提示，需重新核对')
    for row in rows:
        if row['id'] == 'unit-2414-1817-2117':
            row.update(initial_status='issue', initial_note='用户已确认：鼻下阴影与嘴角连成上翘黑缝，嘴角像裂开。2026-10-23 已生成修正候选，未接入。', tags=['五官 / 嘴部'])
            candidate = ROOT / 'assets/hd-ai/unit-poses/face-fix-20261023/unit-2414-1817-2117-face-v2.png'
            if candidate.exists():
                row['candidate'] = link_image(output, row['id'] + '-candidate.png', candidate)
    face_fix = ROOT / 'assets/hd-ai/unit-poses/face-fix-20261024/validation.json'
    if face_fix.exists():
        applied = {r['id']: r for r in read(face_fix)['results'] if r.get('applied')}
        for row in rows:
            fix = applied.get(row['id'])
            if fix and row['hd_sha256'] == fix['after_sha256']:
                row.update(initial_status='pass', initial_note='嘴角黑缝已局部修复并接入；仅静态图片复核。',
                           recipe=row['recipe'] + '；imagegen 脸部局部修正')
                row.pop('candidate', None)
    human_redraw = ROOT / 'assets/hd-ai/unit-poses/human-redraw-20261024/validation.json'
    if human_redraw.exists():
        applied = {r['id']: r for r in read(human_redraw)['results'] if r.get('applied')}
        for row in rows:
            fix = applied.get(row['id'])
            if fix and row['hd_sha256'] == fix['after_sha256']:
                row.update(initial_status='pass', initial_note='动画参考重绘已接入；仅静态图片复核。',
                           recipe='imagegen 动画参考重绘；原姿势、透明画布及占位适配')
    mouth_repairs = ROOT / 'assets/hd-ai/unit-poses/face-repair-20261025/validation.json'
    if mouth_repairs.exists():
        applied = {r['id']: r for r in read(mouth_repairs)['results'] if r.get('applied')}
        for row in rows:
            fix = applied.get(row['id'])
            if fix and row['hd_sha256'] == fix['after_sha256']:
                row.update(initial_status='pass', initial_note='面部局部修复：口线及面甲异常纹路已清理；仅静态图片复核。',
                           recipe=row['recipe'] + '；imagegen 面部局部修复')
    official_repair_files = [ROOT / 'assets/hd-ai/unit-poses' / folder / 'validation.json'
                             for folder in ('official-face-repair-20261025', 'official-face-repair-v2-20261025',
                                            'bastole-perspective-20261025', 'bastole-beak-root-20261025',
                                            'bastole-clean-face-20261025')]
    for official_repairs in official_repair_files:
        if not official_repairs.exists():
            continue
        applied = {r['id']: r for r in read(official_repairs)['results'] if r.get('applied')}
        for row in rows:
            fix = applied.get(row['id'])
            if fix and row['hd_sha256'] == fix['after_sha256']:
                row.update(initial_status='pass' if fix.get('user_confirmed') else 'overview',
                           initial_note=('用户已确认当前修复版；仅静态验收。' if fix.get('user_confirmed')
                                         else '官方设定参考局部重绘已接入，待用户确认；仅静态复核。'),
                           recipe=row['recipe'] + '；官方设定参考的口部及人物局部重绘')
    candidate_files = [ROOT / 'assets/hd-ai/unit-poses' / folder / 'validation.json'
                       for folder in ('gaia-lips-20261025', 'zambot3-mask-20261025')]
    for candidate_file in candidate_files:
        if not candidate_file.exists():
            continue
        candidate = read(candidate_file)
        for row in rows:
            if row['id'] == candidate['id'] and candidate.get('applied') and row['hd_sha256'] == candidate.get('after_sha256'):
                row.update(initial_status='pass', initial_note='用户已确认面甲修复并接入；仅静态验收。',
                           recipe=row['recipe'] + '；官方机体图参考的面甲局部修正')
            if row['id'] == candidate['id'] and row['hd_sha256'] == candidate['based_on_sha256']:
                path = ROOT / candidate['candidate_path']
                if path.exists() and sha(path) == candidate['candidate_sha256']:
                    row['candidate'] = link_image(output, row['id'] + '-lips-candidate.png', path)
    texture_file = ROOT / 'assets/hd-ai/unit-poses/texture-repair-20261025/validation.json'
    if texture_file.exists():
        texture_repairs = {r['id']: r for r in read(texture_file)['results'] if r.get('applied')}
        for row in rows:
            fix = texture_repairs.get(row['id'])
            if fix and row['hd_sha256'] == fix.get('after_sha256'):
                row.update(initial_status=('pass' if fix.get('user_confirmed') else
                                           'issue' if fix.get('user_rejected') else 'overview'),
                           initial_note=('用户已确认本次脏纹修复；仅静态验收。' if fix.get('user_confirmed') else
                                         fix.get('remaining_issue', '脏纹局部修复已接入；待用户逐项确认，仅静态复核。')),
                           recipe=row['recipe'] + '；imagegen 脏纹局部修复')
    cougar_file = ROOT / 'assets/hd-ai/unit-poses/landcougar-mask-20261025/validation.json'
    if cougar_file.exists():
        fix = read(cougar_file)['results'][0]
        for row in rows:
            if row['id'] == fix['id'] and fix.get('applied') and row['hd_sha256'] == fix['after_sha256']:
                row.update(initial_status='pass' if fix.get('user_confirmed') else 'overview',
                           initial_note='豹战机H面甲按厂商立体参考重绘，待确认；仅静态复核。')
    data = {'schema': 'srw64.art-review.v1', 'generated_at': datetime.now(timezone.utc).isoformat(),
            'binding_checks': binding_checks, 'rows': rows,
            'scope': '当前接入的基本机体姿势及人物基础调色板头像；不包括战斗分帧、cut-in、地图图标及借用调色板变体。'}
    (output / 'data.json').write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n')
    template = Path(__file__).with_name('art_review.html').read_text()
    (output / 'index.html').write_text(template.replace('/*REVIEW_DATA*/null', json.dumps(data, ensure_ascii=False).replace('</', '<\\/')))
    for name in ('art_review.js', 'art_review.css'):
        (output / name).write_text(Path(__file__).with_name(name).read_text())
    sheets(output, rows)
    print(json.dumps({'output': str(output), 'units': sum(r['kind'] == 'unit' for r in rows),
                      'portraits': sum(r['kind'] == 'portrait' for r in rows),
                      'technical_flags': sum(bool(r['metrics']['flags']) for r in rows), 'bindings': binding_checks}, ensure_ascii=False))


def serve(output, port):
    known = {r['id']: r for r in read(output / 'data.json')['rows']}
    lock = threading.Lock()
    state_path = output / 'reviews.json'

    class Handler(SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(output), **kwargs)

        def end_headers(self):
            self.send_header('Cache-Control', 'no-store')
            super().end_headers()

        def reply(self, status, data):
            body = json.dumps(data, ensure_ascii=False).encode()
            self.send_response(status)
            self.send_header('Content-Type', 'application/json; charset=utf-8')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            if self.path == '/api/reviews':
                with lock:
                    self.reply(200, read(state_path) if state_path.exists() else {'schema': 'srw64.art-decisions.v1', 'reviews': {}})
            else:
                super().do_GET()

        def do_POST(self):
            # Only same-origin requests can write local decisions.
            origin = self.headers.get('Origin')
            if origin and origin != 'http://' + self.headers.get('Host', ''):
                return self.reply(403, {'error': 'origin mismatch'})
            if self.headers.get('Sec-Fetch-Site') == 'cross-site':
                return self.reply(403, {'error': 'cross-site write denied'})
            if self.path != '/api/reviews':
                return self.reply(404, {'error': 'unknown endpoint'})
            try:
                length = int(self.headers.get('Content-Length', '0'))
                if not 0 < length <= 2_000_000:
                    raise ValueError('invalid body length')
                payload = json.loads(self.rfile.read(length))
                changes = payload['reviews']
                if not isinstance(changes, dict):
                    raise ValueError('reviews must be an object')
                for key, value in changes.items():
                    if key not in known or value.get('status') not in ('unreviewed', 'overview', 'pass', 'suspect', 'issue'):
                        raise ValueError('unknown item or status')
                    if value.get('hd_sha256') != known[key]['hd_sha256']:
                        raise ValueError('art changed; refresh before reviewing')
                    if not isinstance(value.get('note', ''), str) or len(value.get('note', '')) > 10000:
                        raise ValueError('invalid note')
                with lock:
                    data = read(state_path) if state_path.exists() else {'schema': 'srw64.art-decisions.v1', 'reviews': {}}
                    data['reviews'].update(changes)
                    temp = state_path.with_suffix('.tmp')
                    temp.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n')
                    os.replace(temp, state_path)
                self.reply(200, {'saved': len(changes)})
            except (ValueError, KeyError, TypeError, AttributeError) as exc:
                self.reply(400, {'error': str(exc)})

    server = ThreadingHTTPServer(('127.0.0.1', port), Handler)
    print(f'Art review: http://127.0.0.1:{port}', flush=True)
    server.serve_forever()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['build', 'serve'])
    parser.add_argument('--output', type=Path, default=DEFAULT)
    parser.add_argument('--port', type=int, default=8766)
    args = parser.parse_args()
    if args.command == 'build':
        build(args.output.resolve())
    else:
        serve(args.output.resolve(), args.port)


if __name__ == '__main__':
    main()
