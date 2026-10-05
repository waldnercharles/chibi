"""CPU integration checks: python3 -m unittest discover -s test -p test_mesh_sdf.py.
Set CHIBI_CTL for the optional native MCP author/load/bake check.
"""
import hashlib
import contextlib
import io
import importlib.util
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock
import zlib

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT/'tools/mesh_sdf/convert.py'
BOX = ROOT/'test/fixtures/mesh_sdf/box.obj'


def pixels(path):
    png = path.read_bytes()
    width, height = struct.unpack('>II', png[16:24])
    chunks, offset = [], 8
    while offset < len(png):
        size = struct.unpack('>I', png[offset:offset+4])[0]
        if png[offset+4:offset+8] == b'IDAT':
            chunks.append(png[offset+8:offset+8+size])
        offset += size+12
    raw = zlib.decompress(b''.join(chunks))
    assert all(raw[i*(width*4+1)] == 0 for i in range(height))
    return width, b''.join(raw[i*(width*4+1)+1:(i+1)*(width*4+1)] for i in range(height))


def box_mesh(center, half):
    vertices, faces = [], []
    for line in BOX.read_text().splitlines():
        parts = line.split()
        if parts[0] == 'v':
            vertices.append([float(v)/b*h+c for v,b,h,c in zip(parts[1:], (1,2,3), half, center)])
        if parts[0] == 'f':
            faces.append([int(v)-1 for v in parts[1:]])
    return vertices, faces


def write_mesh(path, meshes):
    lines, offset = [], 0
    for vertices, faces in meshes:
        lines.extend('v '+' '.join(map(str, v)) for v in vertices)
        lines.extend('f '+' '.join(str(i+1+offset) for i in f) for f in faces)
        offset += len(vertices)
    path.write_text('\n'.join(lines)+'\n')


def box_distance(point, center, half):
    q = [abs(p-c)-h for p,c,h in zip(point, center, half)]
    return math.sqrt(sum(max(a,0)**2 for a in q))+min(max(q),0)


def decoded_samples(dest, meta):
    grid = meta['grids'][0]; n = grid['resolution']; columns = math.ceil(math.sqrt(n))
    width, data = pixels(dest/grid['image'])
    for z in range(n):
        for y in range(n):
            for x in range(n):
                at = 4*((z//columns*n+y)*width + z%columns*n+x)
                point = [(i/(n-1)*2-1)*h for i,h in zip((x,y,z), meta['half_size'])]
                value = ((data[at]*256+data[at+1])/65535*2-1)*grid['range']
                yield point, value


class MeshSdf(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def convert(self, name='result', source=BOX, extra=(), success=True):
        dest = self.root/name
        run = subprocess.run([sys.executable, str(CLI), str(source), str(dest), '--axes', 'blender',
                              '--scale', '2', '--padding', '1', '--resolution', '9', *extra],
                             text=True, capture_output=True, timeout=30)
        if success:
            self.assertEqual(run.returncode, 0, run.stderr)
            return dest, json.loads((dest/'provenance.json').read_text())
        self.assertNotEqual(run.returncode, 0)
        self.assertFalse(dest.exists())
        self.assertFalse(list(self.root.glob('.mesh-sdf-*')))
        return run.stderr

    def test_determinism_and_analytic_samples(self):
        dest, meta = self.convert()
        again, other = self.convert('again')
        self.assertEqual(meta, other)
        self.assertEqual(meta['bounds'], [[-2, -6, -4], [2, 6, 4]])
        self.assertEqual(meta['half_size'], [3, 7, 5])
        self.assertEqual(meta['repairs'], [])
        self.assertEqual(meta['settings']['source_sha256'], hashlib.sha256(BOX.read_bytes()).hexdigest())
        grid = meta['grids'][0]
        self.assertEqual((dest/grid['image']).read_bytes(), (again/grid['image']).read_bytes())
        width, data = pixels(dest/grid['image'])
        for z in range(9):
            for y in range(9):
                for x in range(9):
                    q = [abs((i/4-1)*h)-b for i,h,b in zip((x,y,z), (3,7,5), (2,6,4))]
                    expected = math.sqrt(sum(max(a,0)**2 for a in q))+min(max(q),0)
                    at = 4*((z//3*9+y)*width + z%3*9+x)
                    decoded = ((data[at]*256+data[at+1])/65535*2-1)*grid['range']
                    self.assertAlmostEqual(decoded, expected, delta=grid['range']/65535+2e-6)

    def test_translation_and_engine_axes(self):
        _, meta = self.convert(extra=['--axes', 'engine', '--scale', '.5', '--translate', '1', '2', '3'])
        self.assertEqual(meta['bounds'], [[.5, 1, 1.5], [1.5, 3, 4.5]])
        self.assertEqual(meta['half_size'], [2.5, 4, 5.5])

    def test_blender_handedness(self):
        shifted = self.root/'shifted.obj'
        lines = []
        for line in BOX.read_text().splitlines():
            p = line.split()
            if p[0] == 'v':
                line = 'v ' + ' '.join(str(float(x)+offset) for x, offset in zip(p[1:], (1,2,3)))
            lines.append(line)
        shifted.write_text('\n'.join(lines))
        _, meta = self.convert(source=shifted)
        self.assertEqual(meta['bounds'], [[0, 0, -8], [4, 12, 0]])

    def test_invalid_topology_and_budget(self):
        open_mesh = self.root/'open.obj'
        open_mesh.write_text(BOX.read_text().rsplit('f ', 1)[0])
        self.assertIn('boundary_edges=3', self.convert(source=open_mesh, success=False))
        flipped = self.root/'flipped.obj'
        flipped.write_text(BOX.read_text().replace('f 1 3 2', 'f 1 2 3'))
        self.assertIn('inconsistent component winding', self.convert(source=flipped, success=False))
        nonmanifold = self.root/'nonmanifold.obj'
        nonmanifold.write_text(BOX.read_text()+'f 1 3 2\n')
        self.assertIn('nonmanifold_edges=3', self.convert(source=nonmanifold, success=False))
        inward = self.root/'inward.obj'
        inward.write_text('\n'.join(' '.join([p[0], *p[:0:-1]]) if p[0] == 'f' else line
                                    for line in BOX.read_text().splitlines() if (p := line.split())))
        self.assertIn('require outward winding', self.convert(source=inward, success=False))
        self.assertIn('resource budget', self.convert(extra=['--max-memory-mib', '1'], success=False))
        self.assertIn('work budget exceeded', self.convert(extra=['--max-work', '1'], success=False))
        self.assertRegex(self.convert(extra=['--max-seconds', '.001'], success=False), 'timed out|wall-time budget exhausted')
        self.assertIn('resolution must', self.convert(extra=['--resolution', '129'], success=False))
        self.assertIn('must be finite', self.convert(extra=['--scale', 'nan'], success=False))
        self.assertIn('supported input', self.convert(source=self.root/'mesh.stl', success=False))

    def test_component_union_semantics(self):
        cases = {
            'disconnected': [((-2,0,0), (1,1,1)), ((2,0,0), (1,1,1))],
            'overlapping': [((-.5,0,0), (1,1,1)), ((.5,0,0), (1,1,1))],
            'nested': [((0,0,0), (2,2,2)), ((0,0,0), (.5,.5,.5))],
        }
        for name, boxes in cases.items():
            with self.subTest(name=name):
                source = self.root/(name+'.obj')
                write_mesh(source, [box_mesh(*box) for box in boxes])
                dest, meta = self.convert(name=name, source=source,
                                          extra=['--axes', 'engine', '--scale', '1', '--padding', '.5'])
                self.assertEqual(len(meta['components']), 2)
                for point, value in decoded_samples(dest, meta):
                    expected = min(box_distance(point, *box) for box in boxes)
                    self.assertAlmostEqual(value, expected, delta=meta['grids'][0]['range']/65535+2e-6)

    def test_concave_shell_sign(self):
        polygon = [(-2,-2), (2,-2), (2,0), (0,0), (0,2), (-2,2)]
        vertices = [(x,y,z) for z in (-1,1) for x,y in polygon]
        faces = []
        for i in range(1,5):
            faces += [(0,i+1,i), (6,6+i,7+i)]
        for i in range(6):
            j = (i+1)%6
            faces += [(i,j,j+6), (i,j+6,i+6)]
        source = self.root/'concave.obj'
        write_mesh(source, [(vertices, faces)])
        dest, meta = self.convert(source=source, extra=['--axes', 'engine', '--scale', '1'])
        self.assertEqual(len(meta['components']), 1)
        self.assertAlmostEqual(meta['components'][0]['volume'], 24)
        tested = 0
        for point, value in decoded_samples(dest, meta):
            # L prism = union of two rectangular prisms. Test sign, excluding
            # zero distances to their internal partition or outer surfaces.
            oracle = min(box_distance(point, (0,-1,0), (2,1,1)),
                         box_distance(point, (-1,1,0), (1,1,1)))
            if abs(oracle) > .01:
                self.assertEqual(value < 0, oracle < 0, point)
                tested += 1
            if point == [-.75,0,0]:
                self.assertLess(value, -.7)  # Interior across the partition.
        self.assertGreater(tested, 500)

    def test_bounded_regular_input(self):
        fifo = self.root/'fifo.obj'
        os.mkfifo(fifo)
        self.assertIn('regular local file', self.convert(source=fifo, extra=['--max-seconds', '1'], success=False))
        device = self.root/'device.obj'
        device.symlink_to('/dev/zero')
        self.assertIn('regular local file', self.convert(source=device, success=False))
        large = self.root/'large.obj'
        with large.open('wb') as stream:
            stream.truncate(8*1024*1024+1)
        self.assertIn('8 MiB', self.convert(source=large, success=False))
        spec = importlib.util.spec_from_file_location('mesh_convert', CLI)
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        # Grow the same regular file after fstat. Reading must still be bounded
        # and reject excess bytes even though the observed size was initially 0.
        growing = self.root/'growing.obj'; growing.touch()
        real_stat = os.fstat
        def grow(descriptor):
            before = real_stat(descriptor)
            with growing.open('r+b') as stream:
                stream.truncate(32*1024*1024)
            return before
        with mock.patch.object(module.os, 'fstat', side_effect=grow):
            with self.assertRaisesRegex(ValueError, '8 MiB'):
                module.read_source(growing)
        # A stall in input preparation is covered by the same deadline as Blender.
        argv = [str(CLI), str(growing), str(self.root/'deadline'), '--axes', 'engine',
                '--scale', '1', '--max-seconds', '.02']
        error = io.StringIO(); started = time.monotonic()
        with mock.patch.object(sys, 'argv', argv), contextlib.redirect_stderr(error):
            with mock.patch.object(module, 'read_source', side_effect=lambda path: time.sleep(2)):
                self.assertEqual(module.main(), 1)
        self.assertLess(time.monotonic()-started, 1)
        self.assertIn('wall-time budget exhausted', error.getvalue())
        self.assertFalse((self.root/'deadline').exists())


    def test_native_metadata_limits(self):
        self.assertIn('Half Size', self.convert(extra=['--axes', 'engine', '--scale', '1',
                                                      '--padding', '1000000', '--resolution', '3'], success=False))
        # Bounds fit, but diagonal distances need a Range larger than native permits.
        self.assertIn('Range', self.convert(extra=['--axes', 'engine', '--scale', '1',
                                                  '--padding', '600000', '--resolution', '3'], success=False))
        run = subprocess.run([os.environ.get('BLENDER', 'blender'), '-b', '-t', '1',
                              '--python-exit-code', '1', '--python', str(ROOT/'test/mesh_sdf_contract.py')],
                             capture_output=True, text=True, timeout=30)
        self.assertEqual(run.returncode, 0, run.stdout+run.stderr)

    @unittest.skipUnless(os.environ.get('CHIBI_CTL'), 'set CHIBI_CTL for native controller round trip')
    def test_controller_round_trip(self):
        dest, meta = self.convert()
        (dest/'project.kdl').write_text('version 5\nname "Local mesh test"\ncontent "content"\n')
        spec = importlib.util.spec_from_file_location('mesh_author', ROOT/'tools/mesh_sdf/author.py')
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        controller = Path(os.environ['CHIBI_CTL']).resolve()
        inspection = module.author(dest, controller, 'Box')
        self.assertIn(meta['grids'][0]['image'], json.dumps(inspection))
        result = module.call(dest, controller, 'bakeModel', model='Box', destination=str(self.root/'baked'),
                             options={'cell_size': 1, 'max_axis_cells': 32, 'max_triangles': 20000,
                                      'max_samples': 1000000, 'max_work': 100000000})
        self.assertGreater(result['structuredContent']['result']['triangles'], 0)
        vertices = [list(map(float, line.split()[1:])) for line in
                    (self.root/'baked/model.obj').read_text().splitlines() if line.startswith('v ')]
        for axis, extent in enumerate((2, 6, 4)):
            self.assertAlmostEqual(min(v[axis] for v in vertices), -extent, delta=.002)
            self.assertAlmostEqual(max(v[axis] for v in vertices), extent, delta=.002)
        subprocess.run([str(controller), '--project-root', str(dest), '--format'], check=True, capture_output=True)

    def test_suzanne_encoder_compatibility(self):
        # Execute the original encoder extracted from the pinned recipe, with a
        # synthetic tiny field. No Suzanne regeneration or GPU required.
        run = subprocess.run([os.environ.get('BLENDER', 'blender'), '-b', '-t', '1', '--python-exit-code', '1', '--python',
                              str(ROOT/'test/mesh_sdf_compat.py')], capture_output=True, text=True, timeout=30)
        self.assertEqual(run.returncode, 0, run.stdout+run.stderr)


if __name__ == '__main__':
    unittest.main()
