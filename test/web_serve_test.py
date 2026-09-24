"""Local workspace saves must survive reload without escaping the project."""
import importlib.util
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

    def save(self, path, text='name: Bloom\n'):
        server.save_asset(self.project, self.content, {'path': path, 'text': text})

    def test_save_and_reopen(self):
        self.save('/content/scenes/Bloom.yaml')
        path = self.content / 'scenes/Bloom.yaml'
        self.assertEqual(path.read_text(), 'name: Bloom\n')
        self.save('content/scenes/Bloom.yaml', 'name: Revised\n')
        self.assertEqual(path.read_text(), 'name: Revised\n')

    def test_invalid_document_preserves_previous_asset(self):
        self.save('content/Bloom.yaml')
        with self.assertRaises(ValueError):
            self.save('content/Bloom.yaml', '{')
        self.assertEqual((self.content / 'Bloom.yaml').read_text(), 'name: Bloom\n')

    def test_paths_stay_inside_content(self):
        (self.content / 'escape').symlink_to(self.project, target_is_directory=True)
        for path in ('content/../outside.yaml', '../outside.yaml',
                     'content/escape/outside.yaml', 'content/script.js'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                self.save(path)
        self.assertFalse((self.project / 'outside.yaml').exists())


if __name__ == '__main__':
    unittest.main()
