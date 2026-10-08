#!/usr/bin/env python3
"""Offline Blender import of the user-supplied SNES Arwing OBJ/palette.

Run: blender --background --python-exit-code 1 --python tools/import_arwing_cockpit.py -- SOURCE_DIR
The normal build consumes the resulting JSON and does not need Blender.
"""
import hashlib
import json
from pathlib import Path
import sys
import bpy
from mathutils import Vector

source = Path(sys.argv[sys.argv.index('--') + 1])
root = Path(__file__).resolve().parents[1]
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.ops.wm.obj_import(filepath=str(source / 'Snes_Arwing.obj'))
image = bpy.data.images.get('Arwing.png')
if image is None:
    raise RuntimeError('The supplied Arwing.png palette is required')
pixels = list(image.pixels)
width, height = image.size
result = {'format': 1,
          'source_obj_sha256': hashlib.sha256((source / 'Snes_Arwing.obj').read_bytes()).hexdigest(),
          'source_texture_sha256': hashlib.sha256((source / 'Arwing.png').read_bytes()).hexdigest(),
          'modules': []}
for obj in sorted((o for o in bpy.context.scene.objects if o.type == 'MESH'), key=lambda o: o.name):
    mesh = obj.data
    mesh.calc_loop_triangles()  # Handles concave canopy-frame ngons; never use a triangle fan.
    uv = mesh.uv_layers.active.data
    colors = {}
    for polygon in mesh.polygons:
        u, v = [sum(uv[i].uv[a] for i in polygon.loop_indices) / len(polygon.loop_indices) for a in range(2)]
        x, y = max(0, min(width - 1, int(u * width))), max(0, min(height - 1, int(v * height)))
        offset = 4 * (y * width + x)
        colors[polygon.index] = [round(c * 255) for c in pixels[offset:offset + 4]]
    panes = [i for i, color in colors.items() if color[3] < 255]
    if panes and panes != [4, 12, 16, 17]:
        raise RuntimeError('Unexpected translucent faces; review the supplied asset before import')
    vertices = []
    for vertex in mesh.vertices:
        p = obj.matrix_world @ vertex.co
        vertices.append([round(p.x, 6), round(p.z, 6), round(-p.y, 6)])
    triangles, degenerate = [], 0
    for triangle in mesh.loop_triangles:
        if triangle.polygon_index in panes:
            continue
        # Test the rounded source coordinates actually written to JSON. The OBJ
        # contains coincident/near-collinear corners at six-decimal precision.
        a, b, c = [Vector(vertices[i]) for i in triangle.vertices]
        if (b - a).cross(c - a).length_squared < 1e-12:
            degenerate += 1
            continue
        triangles.append({'indices': list(triangle.vertices), 'face': triangle.polygon_index,
                          'rgb': colors[triangle.polygon_index][:3]})
    edges = sorted({tuple(sorted((p.vertices[i], p.vertices[(i + 1) % len(p.vertices)])))
                    for p in mesh.polygons if p.index not in panes for i in range(len(p.vertices))})
    result['modules'].append({'name': 'canopy' if panes else 'hull_interior',
                              'vertices_obj': vertices, 'triangles': triangles, 'edges': edges,
                              'removed_pane_faces': panes, 'degenerate_triangles_removed': degenerate})
if len(result['modules']) != 2 or sum(len(m['triangles']) for m in result['modules']) != 497:
    raise RuntimeError('Unexpected model topology: ' + repr([(m['name'], len(m['triangles']), m['degenerate_triangles_removed']) for m in result['modules']]))
(root / 'assets/vr/arwing-snes/geometry.json').write_text(json.dumps(result, indent=2) + '\n')
