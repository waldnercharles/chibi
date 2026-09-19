"""Local workspace saves must survive reload without escaping the project."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    'web_serve', Path(__file__).resolve().parents[1] / 'tools/web_serve.py')
server = importlib.util.module_from_spec(spec)
spec.loader.exec_module(server)


class WorkspaceSaves(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.project = Path(self.directory.name).resolve()
        self.content = self.project / 'content'
        self.content.mkdir()

    def save(self, path, text='{"name":"Bloom"}'):
        server.save_asset(self.project, self.content, {'path': path, 'text': text})

    def test_save_and_reopen(self):
        self.save('/content/scenes/Bloom.json')
        path = self.content / 'scenes/Bloom.json'
        self.assertEqual(json.loads(path.read_text()), {'name': 'Bloom'})
        self.save('content/scenes/Bloom.json', '{"name":"Revised"}')
        self.assertEqual(json.loads(path.read_text()), {'name': 'Revised'})

    def test_invalid_json_preserves_previous_asset(self):
        self.save('content/Bloom.json')
        with self.assertRaises(ValueError):
            self.save('content/Bloom.json', '{')
        self.assertEqual(json.loads((self.content / 'Bloom.json').read_text()), {'name': 'Bloom'})

    def test_paths_stay_inside_content(self):
        (self.content / 'escape').symlink_to(self.project, target_is_directory=True)
        for path in ('content/../outside.json', '../outside.json',
                     'content/escape/outside.json', 'content/script.js'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                self.save(path)
        self.assertFalse((self.project / 'outside.json').exists())


if __name__ == '__main__':
    unittest.main()
