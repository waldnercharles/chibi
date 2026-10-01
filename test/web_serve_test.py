"""Local workspace saves must survive reload without escaping project content."""
import contextlib
import errno
import io
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

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
        (self.project / 'project.yaml').write_text('version: 1\ncontent: content\n')

    def save(self, path, text='name: Bloom\n'):
        server.save_asset(self.workspace, {'path': path, 'text': text})

    def test_save_and_reopen(self):
        self.save('/projects/bloom/content/scenes/Bloom.yaml')
        path = self.content / 'scenes/Bloom.yaml'
        self.assertEqual(path.read_text(), 'name: Bloom\n')
        self.save('projects/bloom/content/scenes/Bloom.yaml', 'name: Revised\n')
        self.assertEqual(path.read_text(), 'name: Revised\n')
        self.assertEqual(server.workspace_assets(self.workspace), {
            'projects/bloom/project.yaml': (self.project / 'project.yaml').read_text(),
            'projects/bloom/content/scenes/Bloom.yaml': 'name: Revised\n',
        })

    def test_watch_follows_edits_and_removals(self):
        asset = self.content / 'A.yaml'
        asset.write_text('a: 1\n')
        watch = server.WorkspaceWatch(self.workspace)
        self.assertEqual(watch.changes(0, 0), (0, {}))
        asset.write_text('a: 22\n')
        watch.poll()
        self.assertEqual(watch.changes(0, 0), (1, {'projects/bloom/content/A.yaml': 'a: 22\n'}))
        asset.unlink()
        watch.poll()
        self.assertEqual(watch.changes(1, 0), (2, {'projects/bloom/content/A.yaml': None}))

    def test_save_without_text_deletes(self):
        self.save('projects/bloom/content/Bloom.yaml')
        self.save('projects/bloom/content/Bloom.yaml', None)
        self.assertFalse((self.content / 'Bloom.yaml').exists())
        with self.assertRaises(ValueError):
            self.save('projects/bloom/project.yaml', None)

    def test_manifest_saves(self):
        self.save('projects/bloom/project.yaml', 'version: 1\ncontent: content\nui: Shell\n')
        self.assertEqual((self.project / 'project.yaml').read_text(), 'version: 1\ncontent: content\nui: Shell\n')
        with self.assertRaises(ValueError):
            self.save('projects/bloom/project.yaml', '[]')

    def test_invalid_document_preserves_previous_asset(self):
        self.save('projects/bloom/content/Bloom.yaml')
        with self.assertRaises(ValueError):
            self.save('projects/bloom/content/Bloom.yaml', '{')
        self.assertEqual((self.content / 'Bloom.yaml').read_text(), 'name: Bloom\n')

    def test_theme_saves_as_written(self):
        self.save('projects/bloom/content/ui/theme/tokens.rcss', ':root { --primary: #c2453b; }\n')
        self.assertEqual((self.content / 'ui/theme/tokens.rcss').read_text(), ':root { --primary: #c2453b; }\n')

    def test_markup_saves_as_written(self):
        self.save('projects/bloom/content/ui/Hud.rml', '<div class="hud">{{ save.score }}</div>\n')
        self.assertEqual((self.content / 'ui/Hud.rml').read_text(), '<div class="hud">{{ save.score }}</div>\n')

    def test_paths_stay_inside_content(self):
        (self.content / 'escape').symlink_to(self.project, target_is_directory=True)
        for path in ('projects/bloom/content/../outside.yaml', 'projects/bloom/outside.yaml',
                     '../outside.yaml', 'projects/bloom/content/escape/outside.yaml',
                     'projects/bloom/content/script.js'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                self.save(path)
        self.assertFalse((self.project / 'outside.yaml').exists())


class WebServeLaunch(unittest.TestCase):
    def test_port_conflict_reports_alternative_before_ready_url(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            workspace = root / 'workspace'
            project = workspace / 'projects/bloom'
            (project / 'content').mkdir(parents=True)
            (project / 'project.yaml').write_text('content: content\n')
            build = root / 'build'
            build.mkdir()
            for suffix in ('html', 'js', 'wasm', 'data'):
                (build / f'chibi.{suffix}').write_text('')

            stdout, stderr = io.StringIO(), io.StringIO()
            argv = ['web_serve.py', '--workspace', str(workspace), '--project',
                    str(project), '--build', str(build), '--port', '8099']
            with patch.object(sys, 'argv', argv), \
                    patch.object(server, 'ThreadingHTTPServer',
                                 side_effect=OSError(errno.EADDRINUSE, 'Address already in use')), \
                    contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                with self.assertRaises(SystemExit) as error:
                    server.main()

            self.assertEqual(error.exception.code, 2)
            self.assertEqual(stdout.getvalue(), '')
            self.assertIn('port 8099 is already in use', stderr.getvalue())
            self.assertIn('make web-serve WEB_PORT=8100', stderr.getvalue())
            self.assertNotIn('Traceback', stderr.getvalue())


if __name__ == '__main__':
    unittest.main()
