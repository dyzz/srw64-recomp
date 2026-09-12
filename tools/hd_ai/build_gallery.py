"""Build a self-contained, local-only gallery from the frozen HD experiment."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import shutil

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'build/hd-ai/2026-09-08'
DEST = SOURCE / 'gallery'
MODELS = [
    ('qwen-image-3.0-pro', 'Qwen 3.0 Pro'),
    ('qwen-image-3.0', 'Qwen 3.0'),
    ('wan2.7-image-pro', 'Wan 2.7 Pro'),
    ('qwen-image-2.0-pro-2026-06-22', 'Qwen 2.0 Pro · 固定版'),
]
SAMPLE_LABELS = {
    'map-europe': ('欧洲过场地图', '地图'),
    'map-asia': ('亚洲地图图层', '地图'),
    'portrait': ('人物头像', '角色'),
    'ui-border': ('UI 装饰边框', '界面'),
    'mech-icon': ('机械单位图标', '单位'),
    'terrain-forest': ('森林图集裁块', '地形'),
}


def main() -> None:
    (DEST / 'media').mkdir(parents=True, exist_ok=True)
    review = json.loads(Path(__file__).with_name('review_decisions.json').read_text())
    assert review['schema'] == 'srw64.hd-ai-review-decisions.v1'
    samples = json.loads((SOURCE / 'samples.json').read_text())
    qa = json.loads((SOURCE / 'review/qa.json').read_text())
    assert samples['schema'] == 'srw64.hd-ai-samples.v1'
    assert qa['completed_outputs'] == 42
    composites = {(r['sample'], r['model'], r['candidate']): r['composite'] for r in qa['records']}
    images: dict[str, str] = {}

    def asset(path: str, name: str, thumb: bool = False) -> str:
        source = SOURCE / path
        target = DEST / 'media' / (name + ('.webp' if thumb else source.suffix))
        if thumb:
            with Image.open(source) as image:
                image.thumbnail((480, 480), Image.Resampling.LANCZOS)
                image.save(target, format='WEBP', quality=87)
        else:
            shutil.copy2(source, target)
        relative = target.relative_to(DEST).as_posix()
        images[relative] = hashlib.sha256(target.read_bytes()).hexdigest()
        return relative

    data: dict = {'schema': 'srw64.hd-gallery.v1', 'date': '2026.09.08',
                  'reviewSummary': review['summary'], 'default': review['default'],
                  'count': 42, 'estimatedCost': qa['reserved_cost_cny'],
                  'models': [{'id': i, 'label': label} for i, label in MODELS], 'samples': []}
    for sample in samples['samples']:
        key = sample['id']
        title, category = SAMPLE_LABELS[key]
        decision = review['assets'][key]
        status, note = decision['label'], decision['note']
        entry = {'id': key, 'title': title, 'status': status, 'category': category, 'note': note,
                 'size': sample['source_dimensions'], 'canvasSize': sample['canvas_dimensions'],
                 'inputSize': sample['input_dimensions'], 'resourceId': sample['source_record']['resource_id'],
                 'prompt': sample['prompt'],
                 'source': asset(sample['canvas'], key + '-source'),
                 'input': asset(sample['input'], key + '-input'),
                 'baseline': asset(f'review/{key}--lanczos.png', key + '-baseline'),
                 'thumb': asset(sample['input'], key + '-thumb', True), 'runs': []}
        for model, _ in MODELS:
            for candidate in (1, 2):
                request = SOURCE / 'runs' / f'{key}--{model}--{candidate}' / 'request.json'
                if not request.exists():
                    continue
                report = json.loads(request.read_text())
                assert report['status'] == 'completed'
                raw = SOURCE / report['output']
                assert hashlib.sha256(raw.read_bytes()).hexdigest() == report['output_sha256']
                preferred = decision.get('preferred', {})
                is_preferred = (model, candidate) == (preferred.get('model'), preferred.get('candidate'))
                if is_preferred:
                    assert report['output_sha256'] == preferred['output_sha256']
                name = f'{key}-{model}-{candidate}'
                entry['runs'].append({'model': model, 'candidate': candidate, 'size': report['dimensions'],
                    'preferred': is_preferred,
                    'cost': report['reserved_cny'], 'seconds': report['elapsed_seconds'],
                    'raw': asset(report['output'], name + '-raw'),
                    'masked': asset(composites[key, model, candidate], name + '-masked'),
                    'thumb': asset(report['output'], name + '-thumb', True)})
        assert len(entry['runs']) == 7
        data['samples'].append(entry)
    data['runtime'] = {
        'original': asset('replay-reference-native/present-60.png', 'runtime-original'),
        'map': asset('replay-hd-final/present-60.png', 'runtime-map'),
        'combined': asset('replay-map-and-font-final/present-60.png', 'runtime-combined'),
        'protected': asset('map-probe-final/protected-map.png', 'protected-map'),
        'mask': asset('map-probe-final/editable-mask.png', 'map-editable-mask'),
    }
    template = Path(__file__).with_name('gallery.html').read_text()
    encoded = json.dumps(data, ensure_ascii=False).replace('</', '<\\/')
    (DEST / 'index.html').write_text(template.replace('__GALLERY_DATA__', encoded))
    (DEST / 'gallery-manifest.json').write_text(json.dumps({
        'schema': 'srw64.hd-gallery-files.v1', 'outputs': 42, 'images': images}, indent=2) + '\n')
    print(json.dumps({'gallery': str(DEST / 'index.html'), 'outputs': 42, 'local_images': len(images)}))


if __name__ == '__main__':
    main()
