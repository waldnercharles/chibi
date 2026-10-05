"""CPU integration checks: python3 -m unittest discover -s test -p test_mesh_sdf.py.
Set CHIBI_CTL for the optional native MCP author/load/bake check.
"""
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
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
        self.assertIn('timed out', self.convert(extra=['--max-seconds', '.001'], success=False))
        self.assertIn('resolution must', self.convert(extra=['--resolution', '129'], success=False))
        self.assertIn('must be finite', self.convert(extra=['--scale', 'nan'], success=False))
        self.assertIn('supported input', self.convert(source=self.root/'mesh.stl', success=False))

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
