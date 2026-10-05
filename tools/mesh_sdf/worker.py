"""Blender worker; use convert.py for enforced wall-time and memory budgets."""
import hashlib
import json
from pathlib import Path
import sys

import bpy
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

sys.path.insert(0, str(Path(__file__).parent))
from atlas import encode


def read_obj(path):
    vertices, faces = [], []
    for number, line in enumerate(path.read_text().splitlines(), 1):
        parts = line.split('#', 1)[0].split()
        if not parts:
            continue
        if parts[0] == 'v':
            if len(parts) != 4:
                raise ValueError(f'line {number}: vertices require exactly x y z')
            vertices.append([float(x) for x in parts[1:]])
        elif parts[0] == 'f':
            if len(parts) != 4:
                raise ValueError(f'line {number}: triangulate faces before export')
            ids = [int(x.split('/')[0]) for x in parts[1:]]
            if any(x == 0 or x > len(vertices) or x < -len(vertices) for x in ids):
                raise ValueError(f'line {number}: invalid vertex index')
            faces.append([x-1 if x > 0 else len(vertices)+x for x in ids])
        elif parts[0] not in ('vt', 'vn', 'o', 'g', 's', 'mtllib', 'usemtl'):
            raise ValueError(f'line {number}: unsupported OBJ directive {parts[0]!r}')
        if len(vertices) > 100_000 or len(faces) > 100_000:
            raise ValueError('mesh exceeds 100000 vertices/triangles')
    if not vertices or not faces:
        raise ValueError('mesh requires vertices and triangular faces')
    v, f = np.asarray(vertices, dtype=np.float64), np.asarray(faces, dtype=np.int32)
    if not np.isfinite(v).all():
        raise ValueError('vertices must be finite')
    return v, f


def topology(v, f):
    edges = {}
    for i, face in enumerate(f):
        for a, b in zip(face, np.roll(face, -1)):
            edges.setdefault(tuple(sorted((int(a), int(b)))), []).append((i, int(a), int(b)))
    boundary = sum(len(e) == 1 for e in edges.values())
    nonmanifold = sum(len(e) > 2 for e in edges.values())
    if boundary or nonmanifold:
        raise ValueError(f'not watertight: boundary_edges={boundary}, nonmanifold_edges={nonmanifold}; no repairs applied')
    neighbors = [[] for _ in f]
    for pair in edges.values():
        a, b = pair
        if a[1:] != b[1:][::-1]:
            raise ValueError('inconsistent component winding; reorient source faces explicitly')
        neighbors[a[0]].append(b[0]); neighbors[b[0]].append(a[0])
    incident = [[] for _ in v]
    for i, face in enumerate(f):
        for vertex in face:
            incident[vertex].append(i)
    for faces in incident:
        if not faces:
            raise ValueError('unused vertices; remove them explicitly before export')
        remaining, pending = set(faces), [faces[0]]
        while pending:
            i = pending.pop()
            if i not in remaining:
                continue
            remaining.remove(i)
            pending.extend(j for j in neighbors[i] if j in remaining)
        if remaining:
            raise ValueError('non-manifold vertex with disconnected face fans')
    seen, groups = set(), []
    for seed in range(len(f)):
        if seed in seen:
            continue
        pending, group = [seed], []
        while pending:
            i = pending.pop()
            if i in seen:
                continue
            seen.add(i); group.append(i); pending.extend(neighbors[i])
        groups.append(sorted(group))
    # Closed vertex fans must belong to a single edge-connected shell.
    owner = {}
    components = []
    for component, ids in enumerate(groups):
        ids = np.asarray(ids)
        used = np.unique(f[ids])
        for vertex in used:
            if int(vertex) in owner:
                raise ValueError('non-manifold vertex shared by disconnected shells')
            owner[int(vertex)] = component
        triangles = v[f[ids]]
        a, b, c = (triangles[:, i] for i in range(3))
        if np.any(np.linalg.norm(np.cross(b-a, c-a), axis=1) <= 1e-12):
            raise ValueError('degenerate triangle after transform')
        # Subtract local origin to avoid translation-induced cancellation.
        origin = a[0]
        volume = float(np.einsum('ij,ij->i', a-origin, np.cross(b-origin, c-origin)).sum()/6)
        if volume <= 1e-12:
            raise ValueError(f'component {component}: signed volume {volume}; require outward winding and nonzero volume')
        components.append({'triangles': len(ids), 'volume': volume, 'orientation': 'outward'})
    if len(owner) != len(v):
        raise ValueError('unused vertices; remove them explicitly before export')
    return groups, components


def bake(v, f, groups, half, n):
    axes = [np.linspace(-h, h, n) for h in half]
    field = np.full((n, n, n), np.inf)
    for ids in groups:
        triangles = v[f[ids]]
        tree = BVHTree.FromPolygons([Vector(x) for x in v], f[ids].tolist(), all_triangles=True)
        for iz, z in enumerate(axes[2]):
            for iy, y in enumerate(axes[1]):
                points = np.array([(x, y, z) for x in axes[0]])
                # Solid angle avoids ray/edge parity ambiguities. Components form
                # a union, not nested cavities. Work bounded by voxel*triangle cap.
                winding = np.zeros(n)
                for triangle in triangles:
                    a, b, c = (triangle[i] - points for i in range(3))
                    la, lb, lc = (np.linalg.norm(x, axis=1) for x in (a, b, c))
                    numerator = np.einsum('ij,ij->i', a, np.cross(b, c))
                    denominator = la*lb*lc + np.einsum('ij,ij->i', a, b)*lc + np.einsum('ij,ij->i', b, c)*la + np.einsum('ij,ij->i', c, a)*lb
                    winding += 2*np.arctan2(numerator, denominator)
                inside = np.abs(winding) > 2*np.pi
                distance = np.array([tree.find_nearest(Vector(p))[3] for p in points])
                field[iz, iy] = np.minimum(field[iz, iy], np.where(inside, -distance, distance))
    return field


def main():
    stage = Path(sys.argv[sys.argv.index('--')+1])
    s = json.loads((stage/'settings.json').read_text())
    if bpy.app.version[:3] != (4, 3, 2):
        raise ValueError(f'requires Blender 4.3.2, got {bpy.app.version_string}')
    v, f = read_obj(stage/'source.obj')
    if len(f)*s['resolution']**3 > s['max_work']:
        raise ValueError(f"work budget exceeded: {len(f)*s['resolution']**3} > {s['max_work']}")
    if s['axes'] == 'blender':
        v = v[:, [0, 2, 1]] * [1, 1, -1]
    v = v*s['scale'] + s['translate']
    if not np.isfinite(v).all() or np.max(np.abs(v)) > 1e6:
        raise ValueError('transformed coordinates must be finite and within +/-1e6 engine units')
    # BVH stores float32. Validate and sample that exact effective geometry,
    # including degeneracies caused by large offsets or very small details.
    effective = v.astype(np.float32).astype(np.float64)
    rounding = float(np.max(np.abs(effective-v)))
    v = effective
    groups, components = topology(v, f)
    half = np.max(np.abs(v), axis=0) + s['padding']
    field = bake(v, f, groups, half, s['resolution'])
    if not np.isfinite(field).all() or not (field < 0).any():
        raise ValueError('grid resolves no interior; increase resolution or adjust transform/padding')
    result = stage/'result'
    (result/'content/sdf').mkdir(parents=True)
    grid, decoded = encode(field, half, result/'content/sdf')
    if not (decoded < 0).any():
        raise ValueError('RG16 quantization resolves no interior; reduce bounds or enlarge mesh')
    with open(bpy.app.binary_path, 'rb') as executable:
        blender_sha256 = hashlib.file_digest(executable, 'sha256').hexdigest()
    meta = {'schema': 1, 'settings': s, 'blender': bpy.app.version_string,
            'blender_build_hash': bpy.app.build_hash.decode(), 'blender_binary_sha256': blender_sha256, 'numpy': np.__version__,
            'zlib': __import__('zlib').ZLIB_RUNTIME_VERSION,
            'vertex_storage': 'float32 (Blender BVH)', 'vertex_rounding_max': rounding,
            'python': sys.version.split()[0], 'vertices': len(v), 'triangles': len(f), 'components': components,
            'boundary_edges': 0, 'nonmanifold_edges': 0, 'repairs': [],
            'sign': 'negative inside union of closed outward shells; solid angle winding; no cavities',
            'assumptions': ['non-self-intersecting input shells', 'OBJ positions already have object transforms baked'],
            'transform': 'engine = scale * (x,z,-y) + translate' if s['axes'] == 'blender' else 'engine = scale * (x,y,z) + translate',
            'bounds': [v.min(axis=0).tolist(), v.max(axis=0).tolist()], 'half_size': half.tolist(),
            'geometry_sha256': hashlib.sha256(v.astype('<f8').tobytes()+f.astype('<i4').tobytes()).hexdigest(),
            'work': {'samples': s['resolution']**3, 'sign_triangle_evaluations': len(f)*s['resolution']**3},
            'grids': [grid]}
    (result/'provenance.json').write_text(json.dumps(meta, indent=2, sort_keys=True)+'\n')


if __name__ == '__main__':
    main()
