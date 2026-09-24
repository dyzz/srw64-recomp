#!/usr/bin/env python3
"""Pack authored HD meshes for the host's native model replacement.

Each model replaces one original ROM model resource. The pack carries the
resource exactly as the game loads it into RDRAM (recognised through segment 4),
the offsets of all of its triangle commands (the first one is drawn natively,
the rest are suppressed) and the authored mesh as float position/normal plus
RGBA8 colour. See docs/native/native-ship-model.md.

  .venv/bin/python tools/models/build_native_models.py            # build
  .venv/bin/python tools/models/build_native_models.py --check    # validate only
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'src'))
from srw64_rom.resources import ResourceTable  # noqa: E402

ROM_SHA256 = 'ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e'
DEFAULT_OUTPUT = ROOT / 'build/recomp/native-models/assets'
# World-map model table 801C5670. Authored meshes stay local like the other HD assets:
# tools/models/<key>_blender.py writes assets/models/<key>/mesh.json, and a model whose
# mesh is missing is left out of the pack (the original keeps drawing).
# display_scale is a presentation choice applied when packing (travelling ships are
# drawn larger); the authored mesh keeps the design proportions in original units.
# display_list is the descriptor replaced; name plates and other lists stay original.
SHIP = 1.3
MODELS = [
    {'resource_id': 5584, 'name': 'Albion', 'key': 'albion', 'display_scale': SHIP,
     'original_sha256': 'ff9a705eb896af3fd7b01422a15f790a74f730bfb6d95ed01cac95a4fd0083da'},
    {'resource_id': 5585, 'name': 'Argama', 'key': 'argama', 'display_scale': SHIP,
     'original_sha256': 'c87040d6d9b862406178dc73e62f086d3e52f6b9f57025b53304c02298e78b36'},
    {'resource_id': 5586, 'name': 'Audhumla', 'key': 'audhumla', 'display_scale': SHIP,
     'original_sha256': '21456e5bf07742b32536eeb2fabd0d3d8b9ef7bad4548773f6f92653b733625b'},
    {'resource_id': 5587, 'name': 'Medea', 'key': 'medea', 'display_scale': SHIP,
     'original_sha256': '85a2e5f71684c23658dca835ce1d683edf4ab6331a0db57232f41dd0ee010ad5'},
    {'resource_id': 5588, 'name': 'Nahel Argama', 'key': 'nahel-argama', 'display_scale': SHIP,
     'original_sha256': 'f6be0b1c4e520ba5ae6fee003089d625784b1dac15d8e6947af46b031ca508f6'},
    {'resource_id': 5589, 'name': 'Peacemillion', 'key': 'peacemillion', 'display_scale': SHIP,
     'original_sha256': 'eab47cf55eec4b9f1b778e871258582bf9388ca75524899da61e6e24ded67595'},
    {'resource_id': 5590, 'name': 'Libra', 'key': 'libra', 'display_list': 0,
     'original_sha256': '5c5206d62048d2ce9f80295a227864bd7b60e395041c38a044c334847f266a07'},
    {'resource_id': 5591, 'name': 'Ra Cailum', 'key': 'ra-cailum', 'display_scale': SHIP,
     'original_sha256': '9f1b7dd2161f3196ff24c80cdd81e3d91712d71e3ae423011c5569070fc3d441'},
    {'resource_id': 5592, 'name': 'La Vie en Rose', 'key': 'la-vie-en-rose', 'display_list': 0,
     'original_sha256': '4a4a554876bba1426081d9a702f02d0a42338e2a1f5508299fd16597564a7c4c'},
    {'resource_id': 5593, 'name': 'Goraon', 'key': 'goraon', 'display_scale': SHIP,
     'original_sha256': 'fe4771a143241f205e248f1362e617f5cbc6fc9b515bf5e355f5dd2c6ad4e364'},
    {'resource_id': 5594, 'name': 'Gran Garan', 'key': 'gran-garan', 'display_scale': SHIP,
     'original_sha256': '09b0b1a36166b65e2f49e536a36a12c3a4aa9d3070d4312bd98238ddd4fc738c'},
    {'resource_id': 5595, 'name': 'Gandall', 'key': 'gandall', 'display_scale': SHIP,
     'original_sha256': 'a732cef2ac9f3754c4b65bfc9d7c30d0e3f9ab9b24a26a5ebc487b9526a038bf'},
    {'resource_id': 5596, 'name': 'Barge', 'key': 'barge', 'display_list': 0,
     'original_sha256': '036e2dddd099ea685cc51f24b76b611b5f182a0940239c0c2af3bdc11f6d7e55'},
    {'resource_id': 5598, 'name': 'Axis', 'key': 'axis', 'display_list': 1,
     'original_sha256': '4e4542dbd1a289773701a65c205c300787e08e1d6843d814aebcd7a046ae2ad8'},
    {'resource_id': 5607, 'name': 'Fifth Luna', 'key': 'fifth-luna', 'display_list': 0,
     'original_sha256': 'efaae5560b4655bae2d10febb84e1519f1bd567237da2885d81d0b0091033c63'},
]
# Not handled for now: 5597, index 21, plate 「デビルアクシズ」 (the game's Devil Axis), which
# no scene places — the デビルアクシズ scenes place the plain Axis; 5601, index 16, a red
# copy of the 5600 marker nothing creates.
NOT_HANDLED = {5597: 'デビルアクシズ (Devil Axis), not placed by any scene',
               5601: 'red marker, never created'}
# Name plates (type-5 billboards: green frame, black board, white italic katakana) are
# redrawn per language from the term tables, so F7 changes them with the rest of the text.
PLATES = {5590: (1, 'リーブラ', 'リーブラ'), 5592: (1, 'ラビアンローズ', 'ラビアンローズ'),
          5596: (1, 'バルジ', 'バルジ'), 5598: (0, 'アクシズ', 'アクシズ'),
          5607: (1, 'フィフスルナ', 'フィフス・ルナ')}  # resource: (display list, plate text, term key)
# Plates on resources whose geometry stays original: the space region 5599 labels six
# colonies and Sweetwater with the same boards (type-5 lists 6-12). They pack as
# plate-only entries. Per plate: (display list, plate text, term key, suffix in every language).
BOARDS = {5599: {'name': 'Space region',
                 'original_sha256': 'ab6e5ec7314094c539aeded4b879e9405f5ec9847c14a4ad24df5b834b1e21b6',
                 'plates': [(6, 'サイド3', 'サイド', ' 3'), (7, 'サイド2', 'サイド', ' 2'), (8, 'サイド5', 'サイド', ' 5'),
                            (9, 'サイド7', 'サイド', ' 7'), (10, 'サイド1', 'サイド', ' 1'), (11, 'サイド6', 'サイド', ' 6'),
                            (12, 'スウィートウォーター', 'スウィートウォーター', '')]}}
PLATE_LOCALES = ('ja', 'zh-Hans', 'en')
PLATE_SCALE = 8  # texture pixels per original unit; the board is 200 x 30 units
FONT = ROOT / 'build/fonts/HarmonyOS_Sans_SC_Regular.ttf'  # tools/content/prepare_fonts.py
for _model in MODELS:
    _model['mesh'] = ROOT / f"assets/models/{_model['key']}/mesh.json" if _model['key'] else None
# World-map travel trail (3D33): drawn by load_000A7EC0 801C4960 as one quad per step
# from the vertex buffer 801C97C0. The host checks this code is resident before it
# replaces the quads with one smooth ribbon (docs/native/native-ship-model.md).
TRAIL = {'code_vram': 0x801C4960, 'code_bytes': 0x40, 'vertex_buffer': 0x801C97C0, 'vertex_bytes': 0x3E800}
OVERLAY_ROM, OVERLAY_VRAM = 0xAAF30, 0x801C5670  # load_000A7EC0 anchor: table 801C5670
STRIDE = 28


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def triangle_commands(resource: bytes, display_list: int | None = None) -> list[int]:
    """Offsets of the TRI1/TRI2 commands in the replaced display list: the given
    descriptor, or the resource's only type-0 (model) list."""
    count = struct.unpack_from('>I', resource, 4)[0]
    descriptors = [struct.unpack_from('>III', resource, 12 + i * 12) for i in range(count)]
    if display_list is None:
        lists = [start for kind, _, start in descriptors if kind == 0]
        if len(lists) != 1:
            raise ValueError('expected exactly one model display list')
    else:
        kind, _, start = descriptors[display_list]
        if kind not in (0, 5):
            raise ValueError(f'descriptor {display_list} is not a display list')
        lists = [start]
    pc, found = lists[0], []
    while True:
        if pc + 8 > len(resource):
            raise ValueError('display list runs past the resource')
        op = resource[pc]
        if op == 0xDF:
            break
        if op in (0xDA, 0xD8, 0xDE):
            # A matrix change or a called list would put triangles outside the one host draw.
            raise ValueError(f'unsupported command {op:02X} at {pc:#x}')
        if op in (0x05, 0x06):
            found.append(pc)
        pc += 8
    if not found:
        raise ValueError('model has no triangle commands')
    return found


def plate_names(key: str) -> dict:
    """The plate text per locale: the data-area term tables first, then story terms."""
    names = {}
    for locale in ('zh-Hans', 'en'):
        table = json.loads((ROOT / f'content/locales/terms/{locale}.json').read_text())
        for section in table['sections'].values():
            if key in section:
                names[locale] = section[key]
    story = json.loads((ROOT / 'content/translation/story-terms.json').read_text())['terms']
    for locale, field in (('zh-Hans', 'zh'), ('en', 'en')):
        if locale not in names and key in story:
            names[locale] = story[key][field]
    missing = {'zh-Hans', 'en'} - set(names)
    if missing:
        raise ValueError(f'no term for plate {key}: {sorted(missing)}')
    return names


def render_plate(text: str):
    """The original plate's look at PLATE_SCALE: 2-unit green frame, black board, white
    oblique lettering with a dark rim, fitted to the board."""
    from PIL import Image, ImageDraw, ImageFont
    k = PLATE_SCALE
    width, height = 200 * k, 30 * k
    plate = Image.new('RGBA', (width, height), (0, 170, 0, 255))
    ImageDraw.Draw(plate).rectangle((2 * k, 2 * k, width - 2 * k - 1, height - 2 * k - 1), fill=(0, 0, 0, 255))
    size = 20 * k
    while True:
        font = ImageFont.truetype(str(FONT), size)
        left, top, right, bottom = font.getbbox(text, stroke_width=k // 3)
        if right - left <= 176 * k and bottom - top <= 21 * k:
            break
        size -= k // 2
    layer = Image.new('RGBA', (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)
    x = (width - (right - left)) // 2 - left
    y = (height - (bottom - top)) // 2 - top
    draw.text((x, y), text, font=font, fill=(255, 255, 255, 255), stroke_width=k // 3,
              stroke_fill=(255, 255, 255, 255))
    rim = layer.split()[3].point(lambda a: 255 if a else 0)
    shadow = Image.new('RGBA', (width, height), (70, 70, 80, 255))
    shadow.putalpha(rim)
    board = Image.new('RGBA', (width, height), (0, 0, 0, 0))
    board.alpha_composite(shadow, (k // 2, k // 2))
    board.alpha_composite(layer)
    shear = 0.22  # the original lettering leans right
    board = board.transform(board.size, Image.AFFINE, (1, shear, -shear * height / 2, 0, 1, 0), Image.BICUBIC)
    plate.alpha_composite(board)
    return plate


def rdram_image(resource: bytes) -> bytes:
    # RDRAM holds big-endian words byte-swapped on a little-endian host.
    return b''.join(resource[i:i + 4][::-1] for i in range(0, len(resource), 4))


def check_mesh(mesh: dict) -> None:
    positions, normals, colors, faces = mesh['positions'], mesh['normals'], mesh['colors'], mesh['faces']
    if not positions or not faces or not len(positions) == len(normals) == len(colors):
        raise ValueError('mesh arrays differ in length or are empty')
    for p, n, c in zip(positions, normals, colors):
        if len(p) != 3 or len(n) != 3 or len(c) != 4 or not all(math.isfinite(v) for v in p + n):
            raise ValueError('invalid vertex')
        if abs(sum(v * v for v in n) - 1) > 1e-3 or not all(type(v) is int and 0 <= v <= 255 for v in c):
            raise ValueError('invalid normal or colour')
    for f in faces:
        if len(f) != 3 or any(type(i) is not int or not 0 <= i < len(positions) for i in f):
            raise ValueError('invalid face')


def pack_vertices(mesh: dict, scale: float = 1.0) -> bytes:
    return b''.join(struct.pack('<6f4B', *(v * scale for v in p), *n, *c)
                    for p, n, c in zip(mesh['positions'], mesh['normals'], mesh['colors']))


def trail_code(rom: bytes) -> bytes:
    start = OVERLAY_ROM + TRAIL['code_vram'] - OVERLAY_VRAM
    return rom[start:start + TRAIL['code_bytes']]


def pack_indices(mesh: dict) -> bytes:
    return b''.join(struct.pack('<3I', *f) for f in mesh['faces'])


def build(output: Path) -> dict:
    rom = (ROOT / 'rom.z64').read_bytes()
    if digest(rom) != ROM_SHA256:
        raise ValueError('wrong ROM')
    table = ResourceTable(rom)
    output.mkdir(parents=True, exist_ok=True)
    entries = []
    skipped = []
    for model in MODELS:
        if model['mesh'] is None or not model['mesh'].exists():
            skipped.append(model['resource_id'])
            continue
        original, _ = table.extract(model['resource_id'])
        if digest(original) != model['original_sha256']:
            raise ValueError(f"resource {model['resource_id']} differs from the modelled original")
        mesh = json.loads(model['mesh'].read_text())
        check_mesh(mesh)
        commands = triangle_commands(original, model.get('display_list'))
        files = {'reference': f"{model['resource_id']}.reference.bin",
                 'vertices': f"{model['resource_id']}.vertices.bin",
                 'indices': f"{model['resource_id']}.indices.bin"}
        (output / files['reference']).write_bytes(rdram_image(original))
        scale = model.get('display_scale', 1.0)
        (output / files['vertices']).write_bytes(pack_vertices(mesh, scale))
        (output / files['indices']).write_bytes(pack_indices(mesh))
        plate = None
        if model['resource_id'] in PLATES:
            plate_list, text, key = PLATES[model['resource_id']]
            names = {'ja': text, **plate_names(key)}
            textures = {}
            for locale in PLATE_LOCALES:
                textures[locale] = f"{model['resource_id']}.plate.{locale}.png"
                render_plate(names[locale]).save(output / textures[locale])
            plate = {'display_list': plate_list, 'triangle_commands': triangle_commands(original, plate_list),
                     'text': names, 'textures': textures, 'size': [200 * PLATE_SCALE, 30 * PLATE_SCALE]}
            files.update({f'plate.{locale}': name for locale, name in textures.items()})
        entries.append({'resource_id': model['resource_id'], 'name': model['name'], 'plate': plate, **files,
                        'reference_bytes': len(original), 'original_sha256': digest(original),
                        'display_list': model.get('display_list'), 'triangle_commands': commands, 'vertex_stride': STRIDE,
                        'vertices_count': len(mesh['positions']), 'triangles': len(mesh['faces']),
                        'display_scale': scale, 'bounds': [[v * scale for v in b] for b in mesh['bounds']] if mesh.get('bounds') else None,
                        'mesh_sha256': digest(model['mesh'].read_bytes()),
                        'sha256': {name: digest((output / name).read_bytes()) for name in files.values()}})
    boards = []
    for resource, board in BOARDS.items():
        original, _ = table.extract(resource)
        if digest(original) != board['original_sha256']:
            raise ValueError(f'resource {resource} differs from the recorded original')
        reference = f'{resource}.reference.bin'
        (output / reference).write_bytes(rdram_image(original))
        for plate_list, text, key, suffix in board['plates']:
            names = {'ja': text, **{locale: name + suffix for locale, name in plate_names(key).items()}}
            textures = {locale: f'{resource}.{plate_list}.plate.{locale}.png' for locale in PLATE_LOCALES}
            for locale, name in textures.items():
                render_plate(names[locale]).save(output / name)
            boards.append({'resource_id': resource, 'name': f"{board['name']}: {names['en']}", 'reference': reference,
                           'reference_bytes': len(original), 'original_sha256': digest(original),
                           'display_list': plate_list, 'triangle_commands': triangle_commands(original, plate_list),
                           'text': names, 'textures': textures, 'size': [200 * PLATE_SCALE, 30 * PLATE_SCALE],
                           'sha256': {name: digest((output / name).read_bytes()) for name in (reference, *textures.values())}})
    code = trail_code(rom)
    trail = {'code_vram': TRAIL['code_vram'], 'code': rdram_image(code).hex(), 'code_sha256': digest(code),
             'vertex_buffer': TRAIL['vertex_buffer'], 'vertex_bytes': TRAIL['vertex_bytes']}
    manifest = {'schema': 'srw64.native-models.v1', 'rom_sha256': ROM_SHA256, 'models': entries, 'plates': boards, 'trail': trail,
                'without_mesh': skipped, 'not_handled': {str(k): v for k, v in NOT_HANDLED.items()}}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return validate(output)


def validate(directory: Path) -> dict:
    """Checked before a run; the host repeats the size and range checks."""
    directory = directory.resolve()
    manifest = json.loads((directory / 'manifest.json').read_text())
    if manifest.get('schema') != 'srw64.native-models.v1' or manifest.get('rom_sha256') != ROM_SHA256:
        raise ValueError('unsupported native model pack')
    known = {m['resource_id']: m for m in MODELS}
    for entry in manifest['models']:
        if entry['resource_id'] not in known:
            raise ValueError(f"unknown model resource {entry['resource_id']}")
        for name, expected in entry['sha256'].items():
            path = directory / name
            if path.parent != directory or digest(path.read_bytes()) != expected:
                raise ValueError(f'native model asset drift: {name}')
        reference = (directory / entry['reference']).read_bytes()
        original = rdram_image(reference)  # the swap is its own inverse
        if len(original) != entry['reference_bytes'] or digest(original) != known[entry['resource_id']]['original_sha256']:
            raise ValueError('reference differs from the recorded original resource')
        if triangle_commands(original, entry.get('display_list')) != entry['triangle_commands']:
            raise ValueError('triangle command offsets differ')
        plate = entry.get('plate')
        if plate is not None:
            if triangle_commands(original, plate['display_list']) != plate['triangle_commands']:
                raise ValueError('plate command offsets differ')
            if set(plate['textures']) != set(PLATE_LOCALES) or set(plate['textures'].values()) - set(entry['sha256']):
                raise ValueError('plate textures missing or unchecked')
        vertices = (directory / entry['vertices']).read_bytes()
        indices = (directory / entry['indices']).read_bytes()
        if len(vertices) != entry['vertices_count'] * STRIDE or len(indices) != entry['triangles'] * 12:
            raise ValueError('mesh sizes differ from the manifest')
        count = entry['vertices_count']
        if any(i >= count for (i,) in struct.iter_unpack('<I', indices)):
            raise ValueError('mesh index out of range')
    for entry in manifest.get('plates', []):
        board = BOARDS.get(entry['resource_id'])
        if board is None or (entry['display_list'], entry['text']['ja']) not in {p[:2] for p in board['plates']}:
            raise ValueError(f"unknown plate {entry['resource_id']}/{entry['display_list']}")
        for name, expected in entry['sha256'].items():
            path = directory / name
            if path.parent != directory or digest(path.read_bytes()) != expected:
                raise ValueError(f'native plate asset drift: {name}')
        original = rdram_image((directory / entry['reference']).read_bytes())
        if len(original) != entry['reference_bytes'] or digest(original) != board['original_sha256']:
            raise ValueError('plate reference differs from the recorded original resource')
        if triangle_commands(original, entry['display_list']) != entry['triangle_commands']:
            raise ValueError('plate command offsets differ')
        if set(entry['textures']) != set(PLATE_LOCALES) or {entry['reference'], *entry['textures'].values()} - set(entry['sha256']):
            raise ValueError('plate textures missing or unchecked')
    trail = manifest.get('trail')
    if trail is not None:
        code = rdram_image(bytes.fromhex(trail['code']))
        if trail['code_vram'] != TRAIL['code_vram'] or len(code) != TRAIL['code_bytes'] or digest(code) != trail['code_sha256']:
            raise ValueError('trail code identity differs')
        if (trail['vertex_buffer'], trail['vertex_bytes']) != (TRAIL['vertex_buffer'], TRAIL['vertex_bytes']):
            raise ValueError('trail vertex buffer differs')
    return {'path': str(directory), 'manifest_sha256': digest((directory / 'manifest.json').read_bytes()),
            'trail': trail is not None,
            'models': [{k: e[k] for k in ('resource_id', 'name', 'vertices_count', 'triangles')} for e in manifest['models']],
            'plates': [e['name'] for e in manifest.get('plates', [])]}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--output', type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument('--check', action='store_true', help='validate an existing pack only')
    args = parser.parse_args()
    print(json.dumps(validate(args.output) if args.check else build(args.output), ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
