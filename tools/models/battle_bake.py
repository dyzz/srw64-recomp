"""Bake a Kit scene's lighting into one texture atlas (battle backgrounds, docs/design/battle-animation-rendering.md §11).

The battle camera always looks from +z at a low angle, so the parts become one object
with real materials (per colour name: base colour, roughness, glass, procedural detail),
lit by a sky and a sun, and Cycles bakes the combined light (sun, sky, shadows, bounce,
ambient occlusion, glossy sky reflection) into one atlas. Faces the camera never sees
(backs facing -z, undersides) are dropped before unwrapping. The host draws the atlas as
it is and adds a view-dependent sheen on glass (HdBakedPS.hlsl).

mesh.json (srw64.native-mesh.v2): positions, normals, uvs, colors (alpha 200 = glass,
255 = matte), faces, and texture: the atlas file next to it.
"""
from __future__ import annotations

import json
import math
from pathlib import Path

import bmesh
import bpy
from mathutils import Vector

GLASS = {'glass', 'glass2', 'window'}
GLOSSY = {'hull_white': 0.35, 'navy': 0.4, 'black': 0.45, 'red': 0.4, 'funnel_band': 0.4, 'orange': 0.5,
          'cont_o': 0.6, 'cont_b': 0.6, 'cont_g': 0.6, 'cont_r': 0.6, 'crane': 0.55, 'rail': 0.5, 'lamp': 0.5}
GRAIN = {'quay', 'quay_dark', 'coping', 'deck', 'asphalt', 'concrete', 'offwhite', 'beige', 'cream', 'white',
         'grey', 'darkgrey', 'roof', 'ship_deck', 'hatch'}


def srgb_to_linear(c):
    c = c / 255
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def material(name, rgba):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = nt.nodes['Principled BSDF']
    base = [srgb_to_linear(c) for c in rgba[:3]] + [1]
    coord = nt.nodes.new('ShaderNodeTexCoord')
    if name in GLASS:
        # every window pane a little different: cell noise on the object position, a pane
        # about 3 x 3.5 units (floors are 3.4-3.6 apart)
        mapping = nt.nodes.new('ShaderNodeMapping')
        mapping.inputs['Scale'].default_value = (1 / 3.0, 1 / 3.5, 1 / 3.0)
        nt.links.new(coord.outputs['Object'], mapping.inputs['Vector'])
        cells = nt.nodes.new('ShaderNodeTexVoronoi')
        cells.inputs['Randomness'].default_value = 0.0
        nt.links.new(mapping.outputs['Vector'], cells.inputs['Vector'])
        ramp = nt.nodes.new('ShaderNodeValToRGB')
        ramp.color_ramp.elements[0].color = [c * 0.55 for c in base[:3]] + [1]
        ramp.color_ramp.elements[1].color = [min(1, c * 1.35) for c in base[:3]] + [1]
        nt.links.new(cells.outputs['Color'], ramp.inputs['Fac'])
        nt.links.new(ramp.outputs['Color'], bsdf.inputs['Base Color'])
        bsdf.inputs['Roughness'].default_value = 0.06
        bsdf.inputs['Metallic'].default_value = 0.35
    else:
        if name in GRAIN:
            noise = nt.nodes.new('ShaderNodeTexNoise')
            noise.inputs['Scale'].default_value = 0.35
            noise.inputs['Detail'].default_value = 8
            nt.links.new(coord.outputs['Object'], noise.inputs['Vector'])
            mix = nt.nodes.new('ShaderNodeMix')
            mix.data_type = 'RGBA'
            mix.inputs['A'].default_value = [c * 0.86 for c in base[:3]] + [1]
            mix.inputs['B'].default_value = [min(1, c * 1.1) for c in base[:3]] + [1]
            nt.links.new(noise.outputs['Fac'], mix.inputs['Factor'])
            nt.links.new(mix.outputs['Result'], bsdf.inputs['Base Color'])
        else:
            bsdf.inputs['Base Color'].default_value = base
        bsdf.inputs['Roughness'].default_value = GLOSSY.get(name, 0.85)
    return mat


def world_and_sun():
    scene = bpy.context.scene
    world = bpy.data.worlds.new('sky')
    world.use_nodes = True
    nt = world.node_tree
    sky = nt.nodes.new('ShaderNodeTexSky')
    sky.sky_type = 'NISHITA'
    sky.sun_elevation = math.radians(38)
    sky.sun_rotation = math.radians(200)
    sky.sun_disc = False
    sky.air_density, sky.dust_density = 1.0, 1.5
    nt.links.new(sky.outputs['Color'], nt.nodes['Background'].inputs['Color'])
    nt.nodes['Background'].inputs['Strength'].default_value = 0.18
    scene.world = world
    sun = bpy.data.objects.new('sun', bpy.data.lights.new('sun', 'SUN'))
    sun.data.energy = 3.2
    sun.data.angle = math.radians(1.5)
    sun.data.color = (1.0, 0.96, 0.9)
    # from the upper left and in front of the buildings (the camera side, +z = Blender -y),
    # as the host's key light falls on the ships
    direction = Vector((-0.45, -0.55, 0.7)).normalized()   # towards the sun, Blender axes
    sun.rotation_euler = (-direction).to_track_quat('-Z', 'Y').to_euler()
    scene.collection.objects.link(sun)


def join(parts, colors):
    """One object, a material per colour name, faces the camera cannot see removed."""
    depsgraph = bpy.context.evaluated_depsgraph_get()
    bm = bmesh.new()
    names = []
    for obj, color in parts:
        mesh = bpy.data.meshes.new_from_object(obj.evaluated_get(depsgraph))
        mesh.transform(obj.matrix_world)
        if color not in names:
            names.append(color)
        index = names.index(color)
        for poly in mesh.polygons:
            poly.material_index = index
        tmp = bmesh.new()
        tmp.from_mesh(mesh)
        mapping = {v: bm.verts.new(v.co) for v in tmp.verts}
        for f in tmp.faces:
            new = bm.faces.new([mapping[v] for v in f.verts])
            new.material_index = index
            new.smooth = f.smooth
        tmp.free()
        bpy.data.meshes.remove(mesh)
    bm.normal_update()
    # Blender -y is the camera side (+z original): drop faces turned clearly away, and
    # undersides
    hidden = [f for f in bm.faces if f.normal.y > 0.55 or f.normal.z < -0.6]
    bmesh.ops.delete(bm, geom=hidden, context='FACES')
    bmesh.ops.triangulate(bm, faces=bm.faces)
    data = bpy.data.meshes.new('baked')
    bm.to_mesh(data)
    bm.free()
    obj = bpy.data.objects.new('baked', data)
    bpy.context.scene.collection.objects.link(obj)
    for name in names:
        data.materials.append(material(name, colors[name]))
    for other, _ in parts:
        bpy.data.objects.remove(other, do_unlink=True)
    return obj, names


def unwrap(obj, size):
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=4 / size, area_weight=0.0,
                             correct_aspect=True, scale_to_bounds=False)
    bpy.ops.uv.pack_islands(margin=4 / size, rotate=True)
    bpy.ops.object.mode_set(mode='OBJECT')


def bake(kit, out, size=4096, samples=128):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    scene = bpy.context.scene
    obj, names = join(kit.parts, kit.colors)
    kit.parts = []
    world_and_sun()
    unwrap(obj, size)
    image = bpy.data.images.new('atlas', size, size, alpha=False)
    for mat in obj.data.materials:
        node = mat.node_tree.nodes.new('ShaderNodeTexImage')
        node.image = image
        mat.node_tree.nodes.active = node
    scene.render.engine = 'CYCLES'
    prefs = bpy.context.preferences.addons['cycles'].preferences
    try:
        prefs.compute_device_type = 'METAL'
        prefs.get_devices()
        for device in prefs.devices:
            device.use = True
        scene.cycles.device = 'GPU'
    except Exception:
        scene.cycles.device = 'CPU'
    scene.cycles.samples = samples
    scene.render.bake.margin = 6
    scene.view_settings.view_transform = 'Standard'
    scene.view_settings.look = 'None'
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.bake(type='COMBINED')
    image.filepath_raw = str(out / 'texture.png')
    image.file_format = 'PNG'
    # save through the view transform (Standard: linear -> sRGB display values)
    image.save_render(str(out / 'texture.png'), scene=scene)
    export(obj, names, out)


def export(obj, names, out):
    mesh = obj.data
    mesh.calc_loop_triangles()
    uv = mesh.uv_layers.active.data
    normals = mesh.corner_normals
    index, positions, normals_out, uvs, colors, faces = {}, [], [], [], [], []
    for tri in mesh.loop_triangles:
        code = 200 if names[tri.material_index] in GLASS else 255
        face = []
        for loop in tri.loops:
            v = mesh.vertices[mesh.loops[loop].vertex_index].co
            n = normals[loop].vector
            p = (round(v.x, 4), round(v.z, 4), round(-v.y, 4))
            nn = (n.x, n.z, -n.y)
            length = math.sqrt(sum(c * c for c in nn)) or 1.0
            nn = tuple(round(c / length, 5) for c in nn)
            t = (round(uv[loop].uv.x, 6), round(1 - uv[loop].uv.y, 6))
            key = (p, nn, t, code)
            if key not in index:
                index[key] = len(positions)
                positions.append(list(p)); normals_out.append(list(nn)); uvs.append(list(t)); colors.append([255, 255, 255, code])
            face.append(index[key])
        if len(set(face)) == 3:
            faces.append(face)
    lo = [min(p[k] for p in positions) for k in range(3)]
    hi = [max(p[k] for p in positions) for k in range(3)]
    data = {'schema': 'srw64.native-mesh.v2', 'name': 'baked', 'frame': 'original resource local axes: +y up',
            'bounds': [lo, hi], 'positions': positions, 'normals': normals_out, 'uvs': uvs, 'colors': colors,
            'faces': faces, 'texture': 'texture.png'}
    (out / 'mesh.json').write_text(json.dumps(data, separators=(',', ':')))
    print(json.dumps({'vertices': len(positions), 'triangles': len(faces), 'bounds': [lo, hi]}))
