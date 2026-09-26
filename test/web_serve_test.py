"""Local workspace saves must survive reload without escaping project content."""
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
        self.workspace = Path(self.directory.name).resolve()
        self.project = self.workspace / 'projects/bloom'
        self.content = self.project / 'content'
        self.content.mkdir(parents=True)
        (self.project / 'project.json').write_text(json.dumps({'version': 1, 'content': 'content'}))

    def save(self, path, text='name: Bloom\n'):
        server.save_asset(self.workspace, {'path': path, 'text': text})

    def test_save_and_reopen(self):
        self.save('/projects/bloom/content/scenes/Bloom.yaml')
        path = self.content / 'scenes/Bloom.yaml'
        self.assertEqual(path.read_text(), 'name: Bloom\n')
        self.save('projects/bloom/content/scenes/Bloom.yaml', 'name: Revised\n')
        self.assertEqual(path.read_text(), 'name: Revised\n')
        self.assertEqual(server.workspace_assets(self.workspace), {
            'projects/bloom/project.json': (self.project / 'project.json').read_text(),
            'projects/bloom/content/scenes/Bloom.yaml': 'name: Revised\n',
        })

    def test_invalid_document_preserves_previous_asset(self):
        self.save('projects/bloom/content/Bloom.yaml')
        with self.assertRaises(ValueError):
            self.save('projects/bloom/content/Bloom.yaml', '{')
        self.assertEqual((self.content / 'Bloom.yaml').read_text(), 'name: Bloom\n')

    def test_paths_stay_inside_content(self):
        (self.content / 'escape').symlink_to(self.project, target_is_directory=True)
        for path in ('projects/bloom/content/../outside.yaml', 'projects/bloom/outside.yaml',
                     '../outside.yaml', 'projects/bloom/content/escape/outside.yaml',
                     'projects/bloom/content/script.js'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                self.save(path)
        self.assertFalse((self.project / 'outside.yaml').exists())


if __name__ == '__main__':
    unittest.main()
