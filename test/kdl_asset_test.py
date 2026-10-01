"""Tooling reads the same KDL asset shapes and values as the engine."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import kdl_asset


class KdlAssets(unittest.TestCase):
    def test_asset_shapes(self):
        text = '''name "Player"
components {
    GAME.GameHealth current=20 maximum=20
    (object)GAME.GameInput
}
(array)behaviors "Movement"
(array)nodes {
    - id=2 type="Branch"
    - {
        id 5
        inputs { Condition #true; }
    }
}
(array)empty
'''
        self.assertEqual(kdl_asset.loads(text), {
            'name': 'Player',
            'components': {'GAME.GameHealth': {'current': 20, 'maximum': 20}, 'GAME.GameInput': {}},
            'behaviors': ['Movement'],
            'nodes': [{'id': 2, 'type': 'Branch'}, {'id': 5, 'inputs': {'Condition': True}}],
            'empty': [],
        })

    def test_literals_and_comments(self):
        text = r'''// A comment before a node.
unsigned 18446744073709551615
signed -9223372036854775808
color 0xff_ff
ratio 1.25e-2
missing #null
label "line\nquote\"slash\\snowman\u{2603}"
raw #"C:\folder\file"#
/* nested /* comment */ */ flag #false
/- ignored 123
object keep=1 /- omit=2 {
    nested "yes"
}
'''
        doc = kdl_asset.loads(text)
        self.assertEqual(doc, {
            'unsigned': (1 << 64) - 1, 'signed': -(1 << 63), 'color': 65535,
            'ratio': 0.0125, 'missing': None, 'label': 'line\nquote"slash\\snowman☃',
            'raw': r'C:\folder\file', 'flag': False, 'object': {'keep': 1, 'nested': 'yes'},
        })
        self.assertEqual(kdl_asset.loads(kdl_asset.dumps(doc)), doc)

    def test_multiline_strings(self):
        text = 'label """\n    first line\n    second line\n    """\nraw #"""\n    C:\\folder\n    """#\n'
        self.assertEqual(kdl_asset.loads(text), {'label': 'first line\nsecond line', 'raw': r'C:\folder'})
        with self.assertRaises(kdl_asset.Error):
            kdl_asset.loads('label """\n  short\n    """')

    def test_ambiguous_values_are_explicit(self):
        doc = {'true': 'false', 'one': [1], 'none': [], 'object': {}, 'literal': '1', 'nested': [[True], {}]}
        self.assertEqual(kdl_asset.loads(kdl_asset.dumps(doc)), doc)
        self.assertIn('(array)one 1', kdl_asset.dumps(doc))
        self.assertIn('(object)object', kdl_asset.dumps(doc))

    def test_invalid_assets(self):
        for text in ['name: YAML', '{}', '[]', 'a 1\na 2', 'a x=1 x=2',
                     'a x=1 { x 2; }', '(array)a x=1', '(array)a { item 1; }',
                     'a 18446744073709551616', 'a -9223372036854775809',
                     'a #nan', 'a #true { b 1; }', 'a "bad\\u{d800}"',
                     'a {', 'a "unterminated', '/* unterminated']:
            with self.subTest(text=text), self.assertRaises(kdl_asset.Error):
                kdl_asset.loads(text)

    def test_reflected_payloads_are_kdl(self):
        cave = kdl_asset.loads((ROOT / 'projects/arena2d/content/prototypes/Cave.kdl').read_text())
        parameters = cave['components']['GAME.MaterialTerrain']['parameters']
        self.assertEqual(kdl_asset.loads(parameters), {
            'Radius': 225.0, 'Roughness': 100.0, 'ChamberSpacing': 1300.0, 'OriginX': 960.0,
        })
        floor = kdl_asset.loads((ROOT / 'content/prototypes/ArenaFloor.kdl').read_text())
        parts = kdl_asset.loads(floor['components']['GAME.GameMeshParts']['parts'])['-']
        self.assertEqual(len(parts), 3)
        self.assertEqual(parts[0]['primitive'], 0)
        self.assertEqual(parts[0]['size'], [461.88021535170066, 6, 352])
        self.assertEqual(parts[2]['position'], [-281.74693136453743, 0, 0])

    def test_checked_in_asset_corpus(self):
        paths = [ROOT / 'project.kdl']
        for directory in ['content', 'projects', 'test/fixtures']:
            paths.extend((ROOT / directory).rglob('*.kdl'))
        self.assertGreater(len(paths), 200)
        for path in paths:
            with self.subTest(path=path.relative_to(ROOT)):
                document = kdl_asset.loads(path.read_text())
                if path.name == 'project.kdl':
                    self.assertEqual(document['version'], 4)
                    self.assertTrue(document['start_scene'].endswith('.kdl'))
                    self.assertTrue((path.parent / document['content'] / document['start_scene']).is_file())
                self.assertEqual(kdl_asset.loads(kdl_asset.dumps(document)), document)


if __name__ == '__main__':
    unittest.main()
