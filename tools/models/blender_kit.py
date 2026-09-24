"""Shared Blender helpers for the HD world-map models (docs/native/native-ship-model.md).

A model script builds parts in the original resource's local axes and hands them
to export(). Import it from a script run by Blender:

    import sys; sys.path.insert(0, str(Path(__file__).resolve().parent))
    from blender_kit import Kit

Axes: the original model's local frame, x across, y up, z forward (bow); units are
the original mesh's. Blender views use (x, -z, y). Colours are sRGB bytes; alpha
below 128 marks an emissive surface for the host shader.
"""
from __future__ import annotations
import json
import math
import os
from pathlib import Path

import bmesh
import bpy
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[2]
VIEWER_MODELS = ROOT / 'build/model-viewer/models'
VIEWER = ROOT / 'build/model-viewer'

PALETTE = {
    'hull': (224, 225, 220, 255), 'panel': (196, 199, 200, 255), 'grey': (150, 154, 160, 255),
    'dark': (74, 78, 88, 255), 'navy': (38, 48, 84, 255), 'red': (196, 40, 34, 255),
    'orange': (226, 150, 70, 255), 'deckline': (150, 92, 44, 255), 'yellow': (226, 196, 60, 255),
    'nozzle': (40, 26, 30, 255), 'glow': (140, 200, 255, 40), 'window': (30, 52, 80, 255),
}


def P(x, up, fwd):
    """Original-model axes (x, y=up, z=forward) -> Blender (x, -z, y)."""
    return Vector((x, -fwd, up))


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    return bpy.context.scene


class Kit:
    """Collects coloured parts; every primitive takes original-model coordinates."""

    def __init__(self, colors: dict | None = None):
        self.scene = reset_scene()
        self.colors = dict(PALETTE)
        self.colors.update(colors or {})
        self.parts = []  # (object, colour name)

    # -- primitives -----------------------------------------------------------------

    def add_object(self, bm, color, bevel=0.0, segments=2, angle=40, name='part'):
        if color not in self.colors:
            raise KeyError(f'unknown colour {color!r}')
        mesh = bpy.data.meshes.new(name)
        bm.normal_update()
        bm.to_mesh(mesh)
        bm.free()
        obj = bpy.data.objects.new(name, mesh)
        self.scene.collection.objects.link(obj)
        if bevel > 0:
            mod = obj.modifiers.new('bevel', 'BEVEL')
            mod.width = bevel
            mod.segments = segments
            mod.limit_method = 'ANGLE'
            mod.angle_limit = math.radians(angle)
            mod.harden_normals = False
        self.parts.append((obj, color))
        return obj

    def loft(self, sections, color, bevel=0.0, cap=True, name='loft', segments=2):
        """sections: [(fwd, [(x, up), ...])] with equal point counts around each ring."""
        bm = bmesh.new()
        rings = [[bm.verts.new(P(x, up, fwd)) for x, up in pts] for fwd, pts in sections]
        n = len(rings[0])
        for a, b in zip(rings, rings[1:]):
            for i in range(n):
                j = (i + 1) % n
                bm.faces.new((a[i], a[j], b[j], b[i]))
        if cap:
            bm.faces.new(list(reversed(rings[0])))
            bm.faces.new(rings[-1])
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        return self.add_object(bm, color, bevel, segments=segments, name=name)

    def box(self, center, size, color, bevel=0.0, taper=None, name='box', segments=2):
        """Axis-aligned box. taper=(sx, sup) scales the bow (+z) end face."""
        cx, cu, cf = center
        sx, su, sf = (s / 2 for s in size)
        tx, tu = taper or (1, 1)
        back = [(-sx, -su), (sx, -su), (sx, su), (-sx, su)]
        front = [(x * tx, u * tu) for x, u in back]
        return self.loft([(cf - sf, [(cx + x, cu + u) for x, u in back]),
                          (cf + sf, [(cx + x, cu + u) for x, u in front])],
                         color, bevel, name=name, segments=segments)

    def cylinder(self, a, b, radius, color, segments=16, radius_b=None, bevel=0.0, cap=True, name='cyl'):
        """Cylinder or cone frustum between two points."""
        a, b = P(*a), P(*b)
        axis = b - a
        bm = bmesh.new()
        bmesh.ops.create_cone(bm, cap_ends=cap, cap_tris=False, segments=segments, radius1=radius,
                              radius2=radius if radius_b is None else radius_b, depth=axis.length)
        rot = axis.normalized().to_track_quat('Z', 'Y').to_matrix().to_4x4()
        bmesh.ops.transform(bm, matrix=Matrix.Translation((a + b) / 2) @ rot, verts=bm.verts)
        return self.add_object(bm, color, bevel, name=name)

    def sphere(self, center, radius, color, scale=(1, 1, 1), segments=16, name='sph'):
        """UV sphere; scale=(x, up, fwd)."""
        bm = bmesh.new()
        bmesh.ops.create_uvsphere(bm, u_segments=segments, v_segments=max(4, segments // 2), radius=radius)
        sx, su, sf = scale
        bmesh.ops.transform(bm, matrix=Matrix.Translation(P(*center)) @ Matrix.Diagonal((sx, sf, su, 1)), verts=bm.verts)
        return self.add_object(bm, color, name=name)

    def plate(self, points, thickness, color, bevel=0.0, name='plate'):
        """Flat polygon (original coordinates, any plane) extruded along its normal."""
        bm = bmesh.new()
        base = [bm.verts.new(P(*p)) for p in points]
        normal = (base[1].co - base[0].co).cross(base[2].co - base[0].co).normalized()
        top = [bm.verts.new(v.co + normal * thickness) for v in base]
        bm.faces.new(list(reversed(base)))
        bm.faces.new(top)
        n = len(base)
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((base[i], base[j], top[j], top[i]))
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        return self.add_object(bm, color, bevel, name=name)

    @staticmethod
    def mirrored(fn):
        for side in (-1, 1):
            fn(side)

    # -- export ------------------------------------------------------------------------

    def triangles(self, sharp_angle=38):
        depsgraph = bpy.context.evaluated_depsgraph_get()
        tris = []
        for obj, color in self.parts:
            mesh = bpy.data.meshes.new_from_object(obj.evaluated_get(depsgraph))
            mesh.transform(obj.matrix_world)
            mesh.set_sharp_from_angle(angle=math.radians(sharp_angle))
            mesh.calc_loop_triangles()
            normals = mesh.corner_normals
            for tri in mesh.loop_triangles:
                corners = []
                for loop in tri.loops:
                    v = mesh.vertices[mesh.loops[loop].vertex_index].co
                    n = normals[loop].vector
                    corners.append(((v.x, v.z, -v.y), (n.x, n.z, -n.y)))
                tris.append((corners, color))
            bpy.data.meshes.remove(mesh)
        return tris

    def export(self, out, name, previews=False, reference=None, reference_parts=None, sharp_angle=38):
        """Write mesh.json and model.glb into out; previews render standard views, and a
        reference resource id adds the same views of the original (optionally only some
        of its parts, e.g. without a name plate) side by side in compare.png."""
        out = Path(out)
        out.mkdir(parents=True, exist_ok=True)
        index, positions, normals, colors, faces = {}, [], [], [], []
        for corners, color in self.triangles(sharp_angle):
            face = []
            for p, n in corners:
                key = (tuple(round(c, 5) for c in p), tuple(round(c, 4) for c in n), color)
                if key not in index:
                    index[key] = len(positions)
                    length = math.sqrt(sum(c * c for c in n)) or 1.0
                    positions.append([round(c, 5) for c in p])
                    normals.append([round(c / length, 6) for c in n])
                    colors.append(list(self.colors[color]))
                face.append(index[key])
            if len(set(face)) == 3:
                faces.append(face)
        lo = [min(p[k] for p in positions) for k in range(3)]
        hi = [max(p[k] for p in positions) for k in range(3)]
        mesh = {'schema': 'srw64.native-mesh.v1', 'name': name,
                'frame': 'original resource local axes: +y up, +z bow',
                'bounds': [lo, hi], 'positions': positions, 'normals': normals, 'colors': colors, 'faces': faces}
        (out / 'mesh.json').write_text(json.dumps(mesh, separators=(',', ':')))
        print(json.dumps({'name': name, 'vertices': len(positions), 'triangles': len(faces), 'bounds': [lo, hi]}))
        for obj, _ in self.parts:
            bpy.data.objects.remove(obj, do_unlink=True)
        self.parts = []
        obj = mesh_object(mesh, 'HD')
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.export_scene.gltf(filepath=str(out / 'model.glb'), export_format='GLB', use_selection=True)
        if previews:
            views = standard_views(lo, hi)
            render_views(obj, out, 'preview', views)
            if reference is not None:
                obj.hide_render = True
                ref = original_object(reference, reference_parts)
                render_views(ref, out, 'reference', views, textured=True)
                compare_sheet(out, views)
        return mesh


def mesh_object(mesh, name):
    data = bpy.data.meshes.new(name)
    data.from_pydata([P(*p) for p in mesh['positions']], [], mesh['faces'])
    data.update()
    attr = data.color_attributes.new('Col', 'BYTE_COLOR', 'CORNER')
    for poly in data.polygons:
        for loop in poly.loop_indices:
            c = mesh['colors'][data.loops[loop].vertex_index]
            attr.data[loop].color_srgb = [c[0] / 255, c[1] / 255, c[2] / 255, 1.0]
    data.normals_split_custom_set([P(*mesh['normals'][l.vertex_index]) for l in data.loops])
    obj = bpy.data.objects.new(name, data)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def original_object(resource_id: int, parts=None):
    """The original model from the local model viewer build (tools/model_viewer/build.py),
    with its prim colours and source textures, in the same Blender axes."""
    path = VIEWER_MODELS / f'{resource_id}.json'
    if not path.exists():
        raise FileNotFoundError(f'{path} missing: run .venv/bin/python tools/model_viewer/build.py first')
    model = json.loads(path.read_text())
    verts, faces, uvs, mats = [], [], [], []
    materials = {}
    data = bpy.data.meshes.new(f'original-{resource_id}')
    for batch in model['batches']:
        if parts is not None and batch['part'] not in parts:
            continue
        pos, uv = batch['positions'], batch['uvs']
        key = (batch.get('texture'), tuple(batch.get('color') or (200, 200, 200, 255)))
        if key not in materials:
            materials[key] = len(materials)
            data.materials.append(original_material(*key))
        for t in range(0, len(pos) // 9):
            base = len(verts)
            for k in range(3):
                x, y, z = pos[t * 9 + k * 3: t * 9 + k * 3 + 3]
                verts.append(P(x, y, z))
                uvs.append((uv[t * 6 + k * 2], 1 - uv[t * 6 + k * 2 + 1]))
            faces.append((base, base + 1, base + 2))
            mats.append(materials[key])
    data.from_pydata(verts, [], faces)
    data.update()
    layer = data.uv_layers.new(name='UV')
    for poly, m in zip(data.polygons, mats):
        poly.material_index = m
        for loop in poly.loop_indices:
            layer.data[loop].uv = uvs[data.loops[loop].vertex_index]
    obj = bpy.data.objects.new(f'original-{resource_id}', data)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def original_material(texture, color):
    mat = bpy.data.materials.new('orig')
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes['Principled BSDF']
    rgba = [c / 255 for c in color]
    bsdf.inputs['Base Color'].default_value = [pow(c, 2.2) for c in rgba[:3]] + [1]
    mat.diffuse_color = [pow(c, 2.2) for c in rgba[:3]] + [1]
    if texture and (VIEWER / texture).exists():
        tex = nodes.new('ShaderNodeTexImage')
        tex.image = bpy.data.images.load(str(VIEWER / texture))
        tex.interpolation = 'Closest'
        links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
        links.new(tex.outputs['Alpha'], bsdf.inputs['Alpha'])
    return mat


def standard_views(lo, hi):
    """Orthographic side/top/front/rear plus two obliques and the world-map angle."""
    size = [h - l for l, h in zip(lo, hi)]
    length, width, height = size[2], size[0], size[1]
    cz = (lo[1] + hi[1]) / 2
    span = max(length, width) * 1.15
    far = max(size) * 3 + 60
    near = max(size) * 1.8 + 10  # perspective views (60 mm lens)
    return [
        ('side', (far, 0, cz), (0, 0, cz), span, None),
        ('top', (0, 0, far), (0, 0, 0), span, (0, 0, 0)),
        ('front', (0, -far, cz), (0, 0, cz), max(width, height) * 1.3, None),
        ('rear', (0, far, cz), (0, 0, cz), max(width, height) * 1.3, None),
        ('bow34', (-near * 0.55, -near * 0.65, near * 0.4), (0, 0, cz * 0.5), None, None),
        ('stern34', (near * 0.5, near * 0.6, near * 0.35), (0, 0, cz * 0.5), None, None),
        ('worldmap', (near * 0.3, near * 0.5, near * 0.95), (0, 0, 0), None, None),
    ]


def render_views(obj, out, prefix, views, textured=False):
    scene = bpy.context.scene
    for other in scene.objects:
        if other.type == 'MESH':
            other.hide_render = other is not obj
    scene.render.engine = 'BLENDER_WORKBENCH'
    shading = scene.display.shading
    shading.light = 'STUDIO'
    shading.color_type = 'TEXTURE' if textured else 'VERTEX'
    shading.show_cavity = not textured
    shading.cavity_type = 'BOTH'
    shading.show_backface_culling = False
    scene.render.resolution_x, scene.render.resolution_y = 1200, 800
    scene.world = scene.world or bpy.data.worlds.new('w')
    scene.world.color = (0.04, 0.06, 0.1)
    scene.view_settings.view_transform = 'Standard'
    cam = scene.camera
    if cam is None:
        cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
        scene.collection.objects.link(cam)
        scene.camera = cam
    for name, loc, target, ortho, euler in views:
        cam.location = Vector(loc)
        cam.rotation_euler = euler or (Vector(target) - cam.location).to_track_quat('-Z', 'Y').to_euler()
        cam.data.type = 'ORTHO' if ortho else 'PERSP'
        if ortho:
            cam.data.ortho_scale = ortho
        else:
            cam.data.lens = 60
        scene.render.filepath = str(Path(out) / f'{prefix}-{name}.png')
        bpy.ops.render.render(write_still=True)


def compare_sheet(out, views):
    """reference (left) against HD (right) for every view, half size, as compare.png."""
    import numpy as np
    rows = []
    for name, *_ in views:
        pair = []
        for prefix in ('reference', 'preview'):
            image = bpy.data.images.load(str(Path(out) / f'{prefix}-{name}.png'))
            w, h = image.size
            pair.append(np.array(image.pixels[:], dtype=np.float32).reshape(h, w, 4)[::2, ::2])
        rows.append(np.concatenate(pair, axis=1))
    grid = np.concatenate(rows[::-1], axis=0)  # Blender images start at the bottom row
    sheet = bpy.data.images.new('compare', grid.shape[1], grid.shape[0])
    sheet.pixels = grid.ravel()
    sheet.filepath_raw = str(Path(out) / 'compare.png')
    sheet.file_format = 'PNG'
    sheet.save()


def args():
    """OUTPUT [--previews] after Blender's '--'."""
    import sys
    rest = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    return (rest[0] if rest else os.environ.get('TMPDIR', '/tmp') + '/hd-model'), '--previews' in rest
