"""Local workspace saves must survive reload without escaping project content."""
import base64
import contextlib
import errno
import io
import importlib.util
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))

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
        (self.project / 'project.kdl').write_text('version 1\ncontent "content"\n')

    def save(self, path, text='name "Bloom"\n'):
        server.save_asset(self.workspace, {'path': path, 'text': text})

    def test_save_and_reopen(self):
        self.save('/projects/bloom/content/scenes/Bloom.kdl')
        path = self.content / 'scenes/Bloom.kdl'
        self.assertEqual(path.read_text(), 'name "Bloom"\n')
        self.save('projects/bloom/content/scenes/Bloom.kdl', 'name "Revised"\n')
        self.assertEqual(path.read_text(), 'name "Revised"\n')
        self.assertEqual(server.workspace_assets(self.workspace), {
            'projects/bloom/project.kdl': (self.project / 'project.kdl').read_text(),
            'projects/bloom/content/scenes/Bloom.kdl': 'name "Revised"\n',
        })

    def test_watch_follows_edits_and_removals(self):
        asset = self.content / 'A.kdl'
        asset.write_text('a 1\n')
        watch = server.WorkspaceWatch(self.workspace)
        self.assertEqual(watch.changes(0, 0), (0, {}))
        asset.write_text('a 22\n')
        watch.poll()
        self.assertEqual(watch.changes(0, 0), (1, {'projects/bloom/content/A.kdl': 'a 22\n'}))
        asset.unlink()
        watch.poll()
        self.assertEqual(watch.changes(1, 0), (2, {'projects/bloom/content/A.kdl': None}))

    def test_watch_rechecks_paths_before_reading_and_keeps_manifest_updates(self):
        asset = self.content / 'A.kdl'
        asset.write_text('a 1\n')
        manifest = self.project / 'project.kdl'
        watch = server.WorkspaceWatch(self.workspace)
        manifest.write_text('version 2\ncontent "content"\n')
        watch.poll()
        self.assertEqual(watch.changes(0, 0), (1, {
            'projects/bloom/project.kdl': 'version 2\ncontent "content"\n',
        }))

        outside = Path(self.directory.name).parent / f'{Path(self.directory.name).name}-private.kdl'
        outside.write_text('private #true\n')
        self.addCleanup(outside.unlink, missing_ok=True)
        asset.unlink()
        asset.symlink_to(outside)
        self.assertNotIn('projects/bloom/content/A.kdl', server.workspace_assets(self.workspace))
        watch.poll()
        self.assertEqual(watch.changes(1, 0), (2, {'projects/bloom/content/A.kdl': None}))

    def test_save_without_text_deletes(self):
        self.save('projects/bloom/content/Bloom.kdl')
        self.save('projects/bloom/content/Bloom.kdl', None)
        self.assertFalse((self.content / 'Bloom.kdl').exists())
        image = self.content / 'art/portrait.png'
        image.parent.mkdir(parents=True)
        image.write_bytes(b'png data')
        self.save('projects/bloom/content/art/portrait.png', None)
        self.assertFalse(image.exists())
        with self.assertRaises(ValueError):
            self.save('projects/bloom/project.kdl', None)

    def test_image_assets_are_encoded_and_watched_for_edits_and_removal(self):
        art = self.content / 'art'
        art.mkdir()
        png = art / 'Portrait.PNG'
        jpeg = art / 'Backdrop.jpeg'
        jpg = art / 'Badge.jpg'
        png.write_bytes(b'\x89PNG\r\n\x1a\nfirst')
        jpeg.write_bytes(b'jpeg bytes')
        jpg.write_bytes(b'jpg bytes')

        assets = server.workspace_assets(self.workspace)
        self.assertEqual(assets['projects/bloom/content/art/Portrait.PNG'], {
            'encoding': 'base64', 'data': base64.b64encode(png.read_bytes()).decode('ascii')
        })
        self.assertEqual(assets['projects/bloom/content/art/Backdrop.jpeg']['encoding'], 'base64')
        self.assertEqual(assets['projects/bloom/content/art/Badge.jpg']['encoding'], 'base64')

        watch = server.WorkspaceWatch(self.workspace)
        png.write_bytes(b'\x89PNG\r\n\x1a\nupdated image bytes')
        watch.poll()
        self.assertEqual(watch.changes(0, 0), (1, {
            'projects/bloom/content/art/Portrait.PNG': {
                'encoding': 'base64', 'data': base64.b64encode(png.read_bytes()).decode('ascii')
            }
        }))
        png.unlink()
        watch.poll()
        self.assertEqual(watch.changes(1, 0), (2, {'projects/bloom/content/art/Portrait.PNG': None}))

    def test_manifest_saves(self):
        self.save('projects/bloom/project.kdl', 'version 1\ncontent "content"\nui "Shell"\n')
        self.assertEqual((self.project / 'project.kdl').read_text(), 'version 1\ncontent "content"\nui "Shell"\n')
        with self.assertRaises(ValueError):
            self.save('projects/bloom/project.kdl', '[]')

    def test_invalid_document_preserves_previous_asset(self):
        self.save('projects/bloom/content/Bloom.kdl')
        with self.assertRaises(ValueError):
            self.save('projects/bloom/content/Bloom.kdl', '{')
        self.assertEqual((self.content / 'Bloom.kdl').read_text(), 'name "Bloom"\n')

    def test_theme_saves_as_written(self):
        self.save('projects/bloom/content/ui/theme/tokens.rcss', ':root { --primary: #c2453b; }\n')
        self.assertEqual((self.content / 'ui/theme/tokens.rcss').read_text(), ':root { --primary: #c2453b; }\n')

    def test_create_project_writes_manifest_and_start_scene(self):
        result = server.create_project(self.workspace, {'name': 'Sample Project', 'version': 5})
        project = self.workspace / 'projects/sample-project'
        manifest = server.kdl_asset.loads((project / 'project.kdl').read_text())
        scene_path = project / 'content/scenes/Start.kdl'
        scene = server.kdl_asset.loads(scene_path.read_text())
        self.assertEqual(result['root'], '/projects/sample-project/')
        self.assertEqual(manifest, {
            'version': 5, 'name': 'Sample Project', 'content': 'content',
            'start_scene': 'scenes/Start.kdl',
        })
        self.assertEqual(scene, {'version': 1, 'name': 'Start', 'entities': []})
        self.assertEqual(server.workspace_assets(self.workspace)[
            'projects/sample-project/content/scenes/Start.kdl'], scene_path.read_text())

    def test_create_project_uses_a_unique_folder(self):
        server.create_project(self.workspace, {'name': 'Sample Project', 'version': 5})
        result = server.create_project(self.workspace, {'name': 'Sample Project', 'version': 5})
        self.assertEqual(result['root'], '/projects/sample-project-2/')

    def test_create_project_removes_partial_files_after_a_write_error(self):
        write_text = Path.write_text

        def fail_scene(path, *args, **kwargs):
            if path.name == 'Start.kdl':
                raise OSError('disk full')
            return write_text(path, *args, **kwargs)

        with patch.object(Path, 'write_text', fail_scene), self.assertRaises(OSError):
            server.create_project(self.workspace, {'name': 'Sample Project', 'version': 5})
        self.assertFalse((self.workspace / 'projects/sample-project').exists())

    def test_create_project_rejects_invalid_names_and_versions(self):
        for request in (
            {'name': '  ', 'version': 5},
            {'name': 'x' * 129, 'version': 5},
            {'name': 'Name', 'version': 0},
        ):
            with self.subTest(request=request), self.assertRaises(ValueError):
                server.create_project(self.workspace, request)
        self.assertFalse((self.workspace / 'projects/name').exists())

    def test_markup_saves_as_written(self):
        self.save('projects/bloom/content/ui/Hud.rml', '<div class="hud">{{ save.score }}</div>\n')
        self.assertEqual((self.content / 'ui/Hud.rml').read_text(), '<div class="hud">{{ save.score }}</div>\n')

    def test_paths_stay_inside_content(self):
        (self.content / 'escape').symlink_to(self.project, target_is_directory=True)
        for path in ('projects/bloom/content/../outside.kdl', 'projects/bloom/outside.kdl',
                     '../outside.kdl', 'projects/bloom/content/escape/outside.kdl',
                     'projects/bloom/content/script.js'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                self.save(path)
        self.assertFalse((self.project / 'outside.kdl').exists())


class WebServeLaunch(unittest.TestCase):
    def test_bridge_precedes_quoted_and_minified_loaders(self):
        for entry in (
            '<script src="chibi-default.js"></script>',
            "<script src='chibi-default.js'></script>",
            '<script src=chibi-default.js></script>',
            '<script defer src=chibi-isolation.js></script>',
            '<script async src="chibi.js"></script>',
            '<script src=chibi.js async></script>',
        ):
            with self.subTest(entry=entry):
                prefix = '<html>\n<script>var Module={};</script>\n'
                text = prefix + entry + '</html>'
                result = server.inject_workspace_bridge(text, 'Module.workspace=true;')
                self.assertEqual(result, prefix + '<script>Module.workspace=true;</script>' + entry + '</html>')

    def test_bridge_skips_template_comments_and_inline_script_text(self):
        prefix = '''<script>var Module={};const example="<script src=chibi.js>";</script>
<!-- <script src=chibi.js></script> -->
<template><template></template><script src=chibi.js async></script></template>'''
        entry = '<script src=chibi-default.js></script>'
        result = server.inject_workspace_bridge(prefix + entry, 'bridge();')
        self.assertEqual(result, prefix + '<script>bridge();</script>' + entry)

    def test_missing_executable_entry_is_an_error(self):
        for text in ('', '<script src=other.js></script>',
                     '<template><script src=chibi.js></script></template>'):
            with self.subTest(text=text), self.assertRaisesRegex(ValueError, 'cannot connect the workspace'):
                server.inject_workspace_bridge(text, 'bridge();')

    def test_missing_entry_stops_server_before_reporting_ready(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            workspace = root / 'workspace'
            project = workspace / 'projects/bloom'
            (project / 'content').mkdir(parents=True)
            (project / 'project.kdl').write_text('content "content"\n')
            build = root / 'build'
            build.mkdir()
            for suffix in ('html', 'js', 'wasm', 'data'):
                (build / f'chibi.{suffix}').write_text('')
            stdout, stderr = io.StringIO(), io.StringIO()
            argv = ['web_serve.py', '--workspace', str(workspace), '--project',
                    str(project), '--build', str(build)]
            with patch.object(sys, 'argv', argv), \
                    patch.object(server, 'ThreadingHTTPServer') as http, \
                    contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                with self.assertRaises(SystemExit) as error:
                    server.main()
            self.assertEqual(error.exception.code, 2)
            http.assert_not_called()
            self.assertEqual(stdout.getvalue(), '')
            self.assertIn('chibi.html: No executable Chibi launch script found', stderr.getvalue())
            self.assertNotIn('Traceback', stderr.getvalue())

    def test_port_conflict_reports_alternative_before_ready_url(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            workspace = root / 'workspace'
            project = workspace / 'projects/bloom'
            (project / 'content').mkdir(parents=True)
            (project / 'project.kdl').write_text('content "content"\n')
            build = root / 'build'
            build.mkdir()
            for suffix in ('html', 'js', 'wasm', 'data'):
                (build / f'chibi.{suffix}').write_text('')
            (build / 'chibi.html').write_text('<script async src=chibi.js></script>')

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

    def test_bridge_precedes_quoted_and_minified_bootstraps(self):
        for bootstrap in ('<script src="chibi-default.js"></script>',
                          '<script src=chibi-default.js></script>',
                          "<script src='chibi-isolation.js'></script>",
                          '<script src=chibi-isolation.js></script>',
                          '<script async src="chibi.js"></script>'):
            with self.subTest(bootstrap=bootstrap):
                shell = '<script>var Module = {};</script>' + bootstrap
                bridged = server.inject_workspace_bridge(shell, 'installWorkspace(Module)')
                self.assertEqual(bridged, '<script>var Module = {};</script>'
                                 '<script>installWorkspace(Module)</script>' + bootstrap)


class LinkedWorkspaceSaves(WorkspaceSaves):
    def setUp(self):
        super().setUp()
        # The registered workspace path stays stable while the game owns its files.
        self.sibling = self.workspace.parent / (self.workspace.name + '-game')
        self.project.rename(self.sibling)
        self.addCleanup(shutil.rmtree, self.sibling)
        self.project.symlink_to(self.sibling, target_is_directory=True)
        self.content = self.sibling / 'content'

    def test_external_editor_edits_keep_workspace_paths(self):
        asset = self.content / 'A.kdl'
        asset.write_text('a 1\n')
        watch = server.WorkspaceWatch(self.workspace)
        self.save('/projects/bloom/content/A.kdl', 'a 22\n')
        self.assertEqual(asset.read_text(), 'a 22\n')
        watch.poll()
        self.assertEqual(watch.changes(0, 0), (1, {'projects/bloom/content/A.kdl': 'a 22\n'}))

    def test_repo_files_are_excluded(self):
        (self.sibling / 'private.kdl').write_text('secret "local"\n')
        self.assertEqual(list(server.workspace_assets(self.workspace)), ['projects/bloom/project.kdl'])
        with self.assertRaises(ValueError):
            self.save('projects/bloom/private.kdl')

    def test_launch_uses_the_workspace_alias(self):
        build = self.workspace / 'build'
        build.mkdir()
        for suffix in ('html', 'js', 'wasm', 'data'):
            (build / f'chibi.{suffix}').write_text('<script async></script>')
        snapshot = self.workspace / 'snapshot'
        snapshot.mkdir()

        def bind(address, handler):
            # Even an absolute sibling --project must start at the browser alias.
            html = (snapshot / 'chibi.html').read_text()
            self.assertIn('"/projects/bloom"', html)
            raise OSError(errno.EADDRINUSE, 'Address already in use')

        argv = ['web_serve.py', '--workspace', str(self.workspace), '--project',
                str(self.sibling), '--build', str(build)]
        with patch.object(sys, 'argv', argv), \
                patch.object(server, 'ThreadingHTTPServer', side_effect=bind), \
                patch.object(server.tempfile, 'TemporaryDirectory',
                             return_value=contextlib.nullcontext(snapshot)), \
                contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as error:
                server.main()
        self.assertEqual(error.exception.code, 2)

    def test_unregistered_project_is_rejected(self):
        self.project.unlink()
        argv = ['web_serve.py', '--workspace', str(self.workspace), '--project', str(self.sibling)]
        stderr = io.StringIO()
        with patch.object(sys, 'argv', argv), contextlib.redirect_stderr(stderr):
            with self.assertRaises(SystemExit) as error:
                server.main()
        self.assertEqual(error.exception.code, 2)
        self.assertIn('linked projects need their target checkout', stderr.getvalue())


if __name__ == '__main__':
    unittest.main()
