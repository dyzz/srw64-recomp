"""The intermission backgrounds widened to 16:9 (docs/design/deck-16x10.md).

The HD pictures (background_hd.py, 1920x1440 for the original's 320x240) cover 4:3. A
picture as wide as 16:9 needs 320 more pixels on each side (53.3 original pixels). The
sides come from wanx2.1-imageedit's expand function (image editors such as qwen-image kept
grey or blurred sides as they were): `prepare` freezes one request per bright picture,
`run` sends each once (the request record is written first, so an interrupted run cannot
pay twice), `compose` keeps the HD picture untouched in the middle and takes only the
sides from the expanded picture, feathered over the join, and `build` derives the dark
versions with background_hd's colour table and writes a background set whose rows carry
their size and extent (native_background.cpp).

    .venv/bin/python -m tools.hd_ai.background_wide prepare --output assets/hd-ai/backgrounds/wide-run-3
    .venv/bin/python -m tools.hd_ai.background_wide run --output ... --env-file path/to/.env [--only 5470]
    .venv/bin/python -m tools.hd_ai.background_wide compose --output ...
    .venv/bin/python -m tools.hd_ai.background_wide build --output ... --images assets/hd-ai/backgrounds/whole-v3
"""
from __future__ import annotations

import argparse
import base64
import fcntl
import io
import json
from pathlib import Path
import shutil
import time
import urllib.error
import urllib.parse
import urllib.request

from PIL import Image, ImageDraw

from srw64_rom.resources import ResourceTable
from tools.hd_ai.aliyun import ROOT, digest, load_env
from tools.hd_ai.background_hd import BRIGHT, DARK, IMAGES, SOURCE, colors, dark_filter, sha

SOURCE_SET = ROOT / 'assets/hd-ai/backgrounds/whole-v2'
HD = (1920, 1440)                     # the HD picture, 6x the original
WIDE = (2560, 1440)                   # 16:9 at the same scale
SIDE = (WIDE[0] - HD[0]) // 2         # 320 HD pixels, 53.3 original pixels
REQUEST = (1536, 1152)                # what the model gets; it returns 2048x1152
MODEL = 'wanx2.1-imageedit'
PRICE = 0.14                          # CNY per picture (docs/data/hd-asset-inventory.md)
BUDGET = 3.0                          # CNY reserved at most per run folder
SCALE = (WIDE[0] / HD[0] + 1) / 2     # each side's scale: the width grows by both less one
FEATHER = 64                          # HD pixels over which the model's side fades into the picture
PROMPT = ('1999年游戏的三维CG插画，向左右两侧自然延伸画面：延续原画的背景、太空、地面、天空、建筑和光影，'
          '风格、质感、色调与原画一致，不增加新的机体、人物、文字或标志。')
# A second candidate for pictures whose first sides grew objects, text or doubled parts.
PROMPT_BACKGROUND_ONLY = ('1999年游戏的三维CG插画。只向左右两侧延伸背景本身：天空、云、太空、星星、山、地面和光影，'
                          '与原画无缝衔接、风格一致。新延伸出的两侧只有背景，不要出现机体或它的任何部件、'
                          '不要重复画面中的物体，不要任何文字、数字、标志或新的物体。')


EDGE = 4                              # HD columns at each edge of a picture not trusted


def trim_edges(picture: Image.Image) -> Image.Image:
    """The picture with its outermost EDGE columns repeated from the next one in."""
    picture = picture.copy()
    w, h = picture.size
    left, right = picture.crop((EDGE, 0, EDGE + 1, h)), picture.crop((w - EDGE - 1, 0, w - EDGE, h))
    for x in range(EDGE):
        picture.paste(left, (x, 0)); picture.paste(right, (w - 1 - x, 0))
    return picture


def prepare(args: argparse.Namespace) -> None:
    out = args.output
    (out / 'inputs').mkdir(parents=True, exist_ok=False)
    samples = []
    for image in IMAGES:
        bright = image + BRIGHT
        picture = Image.open(SOURCE_SET / f'background-{image}-{bright}.png').convert('RGB')
        if picture.size != HD:
            raise ValueError(f'{image}: HD picture is not {HD}')
        # The HD pictures' outermost columns are off (a light line the model would carry on).
        picture = trim_edges(picture)
        name = f'inputs/{image}.jpg'
        picture.resize(REQUEST, Image.Resampling.LANCZOS).save(out / name, quality=95)
        samples.append({'id': str(image), 'image': image, 'bright': bright, 'dark': image + DARK, 'input': name,
                        'prompt': PROMPT})
    (out / 'samples.json').write_text(json.dumps({'schema': 'srw64.hd-ai-samples.v1', 'samples': samples},
                                                 ensure_ascii=False, indent=2) + '\n')
    print(len(samples), 'samples in', out)


def expand_one(out: Path, sample: dict, config: dict, candidate: int = 1) -> dict:
    """One expand request, sent once; the asynchronous task is polled until it ends."""
    folder = out / 'runs' / f"{sample['id']}--{MODEL}--{candidate}"
    folder.mkdir(parents=True, exist_ok=True)
    report_path = folder / 'request.json'
    if report_path.exists():
        report = json.loads(report_path.read_text())
        if report.get('status') != 'task_submitted':
            return report
    else:
        report = None
    data = (out / sample['input']).read_bytes()
    prompt = sample['prompt'] if candidate == 1 else PROMPT_BACKGROUND_ONLY
    parameters = {'n': 1, 'left_scale': SCALE, 'right_scale': SCALE, 'top_scale': 1.0, 'bottom_scale': 1.0,
                  'seed': 640900 + candidate}
    url = urllib.parse.urlsplit(config['DASHSCOPE_BASE_URL'])
    if url.scheme != 'https' or not url.hostname.endswith('.aliyuncs.com') or url.username or url.query or url.fragment:
        raise RuntimeError('unsupported configured API origin')
    endpoint = lambda path: urllib.parse.urlunsplit((url.scheme, url.netloc, path, '', ''))
    headers = {'Authorization': 'Bearer ' + config['DASHSCOPE_API_KEY'], 'Content-Type': 'application/json'}

    def save() -> None:
        temporary = report_path.with_suffix('.tmp')
        temporary.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
        temporary.replace(report_path)
    if report is None:
        report = {'schema': 'srw64.ali-image-request.v1', 'sample_id': sample['id'], 'model': MODEL, 'function': 'expand',
                  'candidate': candidate, 'parameters': parameters, 'input_sha256': digest(data), 'prompt': prompt,
                  'reserved_cny': PRICE, 'price_is_estimate': True, 'status': 'request_started',
                  'started_at_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}
        with (out / '.budget.lock').open('a') as guard:
            fcntl.flock(guard, fcntl.LOCK_EX)
            reserved = sum(json.loads(f.read_text())['reserved_cny'] for f in (out / 'runs').glob('*/request.json'))
            if reserved + PRICE > BUDGET + 1e-8:
                raise RuntimeError(f'spending reservation would exceed CNY {BUDGET}')
            save()   # before the POST, so an interruption cannot send it twice
        body = {'model': MODEL, 'input': {'function': 'expand', 'prompt': prompt,
                                          'base_image_url': 'data:image/jpeg;base64,' + base64.b64encode(data).decode()},
                'parameters': parameters}
        request = urllib.request.Request(endpoint('/api/v1/services/aigc/image2image/image-synthesis'),
                                         data=json.dumps(body).encode(), headers={**headers, 'X-DashScope-Async': 'enable'})
        try:
            with urllib.request.urlopen(request, timeout=120) as response:
                result = json.load(response)
            report.update(status='task_submitted', task_id=result['output']['task_id'], request_id=result.get('request_id'))
        except urllib.error.HTTPError as error:
            report.update(status='http_error', http_status=error.code)
            try:
                report['error_code'] = json.loads(error.read()).get('code')
            except (ValueError, UnicodeError):
                pass
        save()
        if report['status'] != 'task_submitted':
            return report
    start = time.monotonic()
    while time.monotonic() - start < 600:
        time.sleep(5)
        with urllib.request.urlopen(urllib.request.Request(endpoint(f"/api/v1/tasks/{report['task_id']}"), headers=headers), timeout=60) as response:
            result = json.load(response)
        state = result.get('output', {}).get('task_status')
        if state == 'SUCCEEDED':
            results = result['output'].get('results', [])
            with urllib.request.urlopen(results[0]['url'], timeout=90) as response:   # signed URL: not recorded
                output = response.read()
            picture = Image.open(io.BytesIO(output)); picture.load()
            path = folder / f"output.{ {'PNG': 'png', 'JPEG': 'jpg', 'WEBP': 'webp'}[picture.format] }"
            path.write_bytes(output)
            report.update(status='completed', output=str(path.relative_to(out)), output_sha256=digest(output),
                          dimensions=list(picture.size), usage=result.get('usage'))
            break
        if state in ('FAILED', 'CANCELED', 'UNKNOWN'):
            report.update(status='task_' + state.lower(), error_code=result.get('output', {}).get('code'))
            break
    save()
    return report


def run(args: argparse.Namespace) -> None:
    config = load_env(args.env_file)
    for sample in json.loads((args.output / 'samples.json').read_text())['samples']:
        if args.only and sample['id'] not in args.only:
            continue
        report = expand_one(args.output, sample, config, args.candidate)
        print(sample['id'], report.get('status'), report.get('dimensions'), report.get('error_code', ''), flush=True)


def compose(args: argparse.Namespace) -> None:
    out = args.output
    (out / 'compose').mkdir(exist_ok=True)
    for sample in json.loads((out / 'samples.json').read_text())['samples']:
        candidate = dict(item.split('=') for item in (args.choose or [])).get(sample['id'], '1')
        request = out / 'runs' / f"{sample['id']}--{MODEL}--{candidate}" / 'request.json'
        if not request.exists() or json.loads(request.read_text()).get('status') != 'completed':
            continue
        generated = Image.open(out / json.loads(request.read_text())['output']).convert('RGB').resize(WIDE, Image.Resampling.LANCZOS)
        picture = Image.open(SOURCE_SET / f"background-{sample['image']}-{sample['bright']}.png").convert('RGB')
        # The HD picture wins in the middle; the model's sides fade in over FEATHER pixels
        # inside each edge, so the join follows the picture rather than a hard line.
        mask = Image.new('L', WIDE, 0)
        draw = ImageDraw.Draw(mask)
        draw.rectangle((SIDE + EDGE + FEATHER, 0, SIDE + HD[0] - EDGE - FEATHER - 1, WIDE[1]), fill=255)
        for step in range(FEATHER):
            value = round(255 * (step + 1) / (FEATHER + 1))
            draw.line((SIDE + EDGE + step, 0, SIDE + EDGE + step, WIDE[1]), fill=value)
            draw.line((SIDE + HD[0] - EDGE - 1 - step, 0, SIDE + HD[0] - EDGE - 1 - step, WIDE[1]), fill=value)
        placed = generated.copy()
        placed.paste(picture, (SIDE, 0))
        result = Image.composite(placed, generated, mask)
        result.save(out / 'compose' / f"{sample['image']}.png")
        preview = result.resize((WIDE[0] // 4, WIDE[1] // 4), Image.Resampling.LANCZOS)
        guide = ImageDraw.Draw(preview)
        for x in (SIDE // 4, (SIDE + HD[0]) // 4):
            guide.line((x, 0, x, 8), fill=(255, 0, 255))
        preview.save(out / 'compose' / f"{sample['image']}-preview.png")
        print('composed', sample['image'])


def build(args: argparse.Namespace) -> None:
    out, target = args.output, args.images
    target.mkdir(parents=True, exist_ok=False)
    table = ResourceTable((ROOT / 'rom.z64').read_bytes())
    old = json.loads((SOURCE_SET / 'backgrounds.json').read_text())
    rows = []
    side = SIDE * SOURCE[0] / HD[0]
    for sample in json.loads((out / 'samples.json').read_text())['samples']:
        bright = Image.open(out / 'compose' / f"{sample['image']}.png").convert('RGB')
        dark = bright.filter(dark_filter(colors(table, sample['bright']), colors(table, sample['dark'])))
        for palette, picture in ((sample['bright'], bright), (sample['dark'], dark)):
            name = f"background-{sample['image']}-{palette}.png"
            picture.save(target / name)
            rows.append({'image': sample['image'], 'palette': palette, 'file': name, 'sha256': sha(target / name),
                         'model': MODEL, 'size': list(WIDE), 'extent': [-side, 0, SOURCE[0] + side, SOURCE[1]]})
    # Pictures not widened here keep their 4:3 rows (the starfield widens by mirroring).
    for row in old['images']:
        if not any(r['image'] == row['image'] and r['palette'] == row['palette'] for r in rows):
            shutil.copy2(SOURCE_SET / row['file'], target / row['file'])
            rows.append(row)
    index = {'schema': 'srw64.background-images.v1', 'size': old['size'], 'source_size': old['source_size'], 'images': rows}
    (target / 'backgrounds.json').write_text(json.dumps(index, indent=2) + '\n')
    print(len(rows), 'rows in', target)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    for name in ('prepare', 'run', 'compose', 'build'):
        command = commands.add_parser(name)
        command.add_argument('--output', type=Path, required=True)
        if name == 'run':
            command.add_argument('--env-file', type=Path, required=True)
            command.add_argument('--only', nargs='*')
            command.add_argument('--candidate', type=int, default=1)
        if name == 'compose':
            command.add_argument('--choose', nargs='*', help='ID=CANDIDATE for a picture not using candidate 1')
        if name == 'build':
            command.add_argument('--images', type=Path, required=True)
    args = parser.parse_args()
    {'prepare': prepare, 'run': run, 'compose': compose, 'build': build}[args.command](args)


if __name__ == '__main__':
    main()
