"""Exercise Pages publishing with local Git repositories and mocked build/HTTP."""
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('publish_web', ROOT / 'tools/publish_web.py')
publisher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publisher)


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args],
                                   stderr=subprocess.DEVNULL).decode().strip()


class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.environment = patch.dict(os.environ, {
            'GIT_AUTHOR_NAME': 'Publish test', 'GIT_AUTHOR_EMAIL': 'test@example.com',
            'GIT_COMMITTER_NAME': 'Publish test', 'GIT_COMMITTER_EMAIL': 'test@example.com',
            'GIT_CONFIG_COUNT': '1', 'GIT_CONFIG_KEY_0': 'commit.gpgSign',
            'GIT_CONFIG_VALUE_0': 'false', 'MAKEFLAGS': 'inherited',
            'MFLAGS': 'inherited', 'MAKEOVERRIDES': 'inherited',
        })
        self.environment.start()
        self.addCleanup(self.environment.stop)
        self.temporary = tempfile.TemporaryDirectory(prefix='chibi-publish-test-')
        self.addCleanup(self.temporary.cleanup)
        base = Path(self.temporary.name)
        self.remote = base / 'origin.git'
        self.root = base / 'repo'
        subprocess.run(['git', 'init', '--bare', str(self.remote)], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.root.mkdir()
        git(self.root, 'init', '-b', 'main')
        git(self.root, 'remote', 'add', 'origin', str(self.remote))
        (self.root / 'input').write_text('remote main')
        (self.root / 'tools').mkdir()
        shutil.copyfile(ROOT / 'tools/package_web.py', self.root / 'tools/package_web.py')
        git(self.root, 'add', '.')
        git(self.root, 'commit', '-m', 'main')
        git(self.root, 'push', 'origin', 'main')
        self.source_commit = git(self.root, 'rev-parse', 'HEAD')
        git(self.root, 'checkout', '--orphan', 'gh-pages')
        git(self.root, 'rm', '-rf', '.')
        (self.root / 'DEPLOYMENT_COMMIT').write_text('old\n')
        (self.root / 'CNAME').write_text('example.test\n')
        git(self.root, 'add', '.')
        git(self.root, 'commit', '-m', 'site')
        git(self.root, 'push', 'origin', 'gh-pages')
        self.original_site = git(self.remote, 'rev-parse', 'gh-pages')
        git(self.root, 'checkout', 'main')
        (self.root / 'input').write_text('local commit')
        git(self.root, 'add', '.')
        git(self.root, 'commit', '-m', 'unpushed')
        (self.root / 'input').write_text('dirty local work')
        self.original_local = git(self.root, 'rev-parse', 'HEAD')
        self.calls = []
        self.requests = []
        self.external_project = None
        self.runtime_installs = []

    def add_project_link(self, target='../../neon-swarm'):
        checkout = Path(self.temporary.name) / 'engine-main'
        subprocess.run(['git', 'clone', '-b', 'main', str(self.remote), str(checkout)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        (checkout / 'projects').mkdir()
        (checkout / 'projects/arena2d').symlink_to(target, target_is_directory=True)
        git(checkout, 'add', 'projects/arena2d')
        git(checkout, 'commit', '-m', 'Link game repository')
        git(checkout, 'push', 'origin', 'main')
        self.source_commit = git(checkout, 'rev-parse', 'HEAD')

    def publish(self, mode='ok', paired=True):
        real_run = publisher.run

        def run(*args, **kwargs):
            self.calls.append(args)
            if args[0] == 'python3' and args[1].endswith('/install_game_runtime.py'):
                self.assertIn('DEPLOYMENT_COMMIT', self.requests)
                self.assertIn('BUILD_SOURCES.json', self.requests)
                self.runtime_installs.append(args)
                return None
            if args[0] == 'gh':
                if args[1] == 'repo':
                    return 'test/repo'
                if '--method' in args:
                    return '{}'
                return json.dumps({'build_type': 'legacy',
                    'source': {'branch': 'gh-pages', 'path': '/'},
                    'html_url': 'https://example.test/'})
            if args[0] != 'make':
                return real_run(*args, **kwargs)
            source = kwargs['cwd']
            self.assertEqual((source / 'input').read_text(), 'remote main')
            self.assertEqual(git(source, 'rev-parse', 'HEAD'), self.source_commit)
            if args[1] == 'package-native':
                self.assertIn('UPDATE_GAME_RUNTIME=0', args)
                return None
            if self.external_project:
                project = source / 'projects/arena2d'
                self.assertFalse(project.is_symlink())
                self.assertEqual((project / 'content/Game.yaml').read_text(), 'name: Committed game\n')
                self.assertFalse((project / '.git').exists())
                self.assertFalse((project / '.codex').exists())
                self.assertFalse((project / 'content/Untracked.yaml').exists())
            for key in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES'):
                self.assertNotIn(key, kwargs['env'])
            if mode == 'fail':
                raise subprocess.CalledProcessError(1, args)
            for profile in (('build-web', 'build-web-threads') if paired else ('build-web',)):
                build = source / profile
                build.mkdir()
                assets = ['chibi.html', 'chibi.js', 'chibi.wasm', 'chibi.data', 'chibi-default.js']
                if profile.endswith('threads'):
                    assets.extend(('chibi-isolation.js', 'chibi.worker.js'))
                for name in assets:
                    (build / name).write_text(f'compiled {profile} {name}')
            if mode == 'advance':
                git(source, 'checkout', '-b', 'advance')
                (source / 'input').write_text('advanced remote main')
                git(source, 'add', 'input')
                git(source, 'commit', '-m', 'advanced')
                git(source, 'push', 'origin', 'HEAD:main')

        def urlopen(url, timeout):
            parsed = urlsplit(url)
            self.assertEqual(parsed.netloc, 'example.test')
            name = parsed.path.lstrip('/')
            self.requests.append(name)
            content = subprocess.check_output(
                ['git', '-C', str(self.remote), 'show', f'gh-pages:{name}'],
                stderr=subprocess.DEVNULL)
            return io.BytesIO(content)

        with patch.object(publisher, 'run', run), patch.object(publisher, 'urlopen', urlopen):
            publisher.publish(self.root, 2, '')

    def assert_local_unchanged(self):
        self.assertEqual((self.root / 'input').read_text(), 'dirty local work')
        self.assertEqual(git(self.root, 'rev-parse', 'HEAD'), self.original_local)

    def test_remote_main_publishes_paired_assets_and_verifies_live_hashes(self):
        self.publish()
        self.assertEqual(git(self.remote, 'show', 'gh-pages:DEPLOYMENT_COMMIT'), self.source_commit)
        self.assertEqual(git(self.remote, 'show', 'gh-pages:BUILD_COMMIT'), self.source_commit)
        self.assertEqual(git(self.remote, 'show', 'gh-pages:index.html'), 'compiled build-web chibi.html')
        self.assertEqual(git(self.remote, 'show', 'gh-pages:threads/index.html'),
                         'compiled build-web-threads chibi.html')
        self.assertEqual(git(self.remote, 'show', 'gh-pages:CNAME'), 'example.test')
        self.assertEqual(git(self.remote, 'rev-parse', 'main'), self.source_commit)
        assets = git(self.remote, 'ls-tree', '-r', '--name-only', 'gh-pages').splitlines()
        self.assertEqual(set(self.requests), set(assets) - {'CNAME', 'BUILD_COMMIT', '.nojekyll'})
        self.assertIn('threads/chibi.worker.js', self.requests)
        self.assertTrue(any('--method' in args for args in self.calls))
        self.assert_local_unchanged()

    def test_sibling_project_publishes_committed_assets_and_source_marker(self):
        game = Path(self.temporary.name) / 'neon-swarm'
        (game / 'content').mkdir(parents=True)
        git(game, 'init', '-b', 'main')
        (game / 'project.yaml').write_text('name: Neon Swarm\ncontent: content\n')
        (game / 'content/Game.yaml').write_text('name: Committed game\n')
        (game / '.gitignore').write_text('.codex/\n')
        git(game, 'add', '.')
        git(game, 'commit', '-m', 'Game content')
        game_commit = git(game, 'rev-parse', 'HEAD')
        (game / 'content/Game.yaml').write_text('name: Dirty game\n')
        (game / 'content/Untracked.yaml').write_text('name: Untracked\n')
        (game / '.codex').mkdir()
        (game / '.codex/config.toml').write_text('local = true\n')
        self.external_project = game
        self.add_project_link()
        self.publish()
        marker = json.loads(git(self.remote, 'show', 'gh-pages:BUILD_SOURCES.json'))
        self.assertEqual(marker, {'engine': self.source_commit,
                                 'projects': {'projects/arena2d': game_commit}})
        self.assertIn('BUILD_SOURCES.json', self.requests)
        self.assertEqual((game / 'content/Game.yaml').read_text(), 'name: Dirty game\n')
        self.assertEqual(git(game, 'rev-parse', 'HEAD'), game_commit)
        self.assert_local_unchanged()

    def test_missing_project_stops_before_build_or_publication(self):
        self.add_project_link()
        with self.assertRaisesRegex(RuntimeError, 'Missing external project'):
            self.publish()
        self.assertFalse(any(args[0] == 'make' for args in self.calls))
        self.assertEqual(git(self.remote, 'rev-parse', 'gh-pages'), self.original_site)
        self.assertFalse(any('--method' in args for args in self.calls))
        self.assert_local_unchanged()

    def test_non_git_project_stops_before_build_or_publication(self):
        (Path(self.temporary.name) / 'neon-swarm').mkdir()
        self.add_project_link()
        with self.assertRaisesRegex(RuntimeError, 'committed Git HEAD'):
            self.publish()
        self.assertFalse(any(args[0] == 'make' for args in self.calls))
        self.assertEqual(git(self.remote, 'rev-parse', 'gh-pages'), self.original_site)
        self.assertFalse(any('--method' in args for args in self.calls))
        self.assert_local_unchanged()

    def test_build_failure_leaves_site_and_local_work_unchanged(self):
        with self.assertRaises(subprocess.CalledProcessError):
            self.publish(mode='fail')
        self.assertEqual(git(self.remote, 'rev-parse', 'gh-pages'), self.original_site)
        self.assertFalse(any('--method' in args for args in self.calls))
        self.assertFalse(self.requests)
        self.assert_local_unchanged()

    def test_registered_runtime_updates_from_published_source_after_live_verification(self):
        (self.root / '.chibi').mkdir()
        config = self.root / '.chibi/game-runtime.json'
        config.write_text('{}')
        self.publish()
        builds = [args for args in self.calls if args[:2] == ('make', 'package-native')]
        self.assertEqual(len(builds), 1)
        self.assertEqual(len(self.runtime_installs), 1)
        install = self.runtime_installs[0]
        self.assertEqual(install[-2:], ('--config', str(config)))
        self.assertTrue(install[install.index('--package') + 1].endswith('/source/dist/game-runtime'))

    def test_failed_build_does_not_update_registered_runtime(self):
        (self.root / '.chibi').mkdir()
        (self.root / '.chibi/game-runtime.json').write_text('{}')
        with self.assertRaises(subprocess.CalledProcessError):
            self.publish(mode='fail')
        self.assertEqual(self.runtime_installs, [])

    def test_failed_live_verification_does_not_update_registered_runtime(self):
        (self.root / '.chibi').mkdir()
        (self.root / '.chibi/game-runtime.json').write_text('{}')
        with patch.object(publisher, 'wait_for_deployment',
                          side_effect=RuntimeError('deployment did not verify')):
            with self.assertRaisesRegex(RuntimeError, 'deployment did not verify'):
                self.publish()
        self.assertEqual(self.runtime_installs, [])
        self.assert_local_unchanged()

    def test_remote_advance_rejects_publication(self):
        with self.assertRaisesRegex(RuntimeError, 'origin/main advanced'):
            self.publish(mode='advance')
        self.assertNotEqual(git(self.remote, 'rev-parse', 'main'), self.source_commit)
        self.assertEqual(git(self.remote, 'rev-parse', 'gh-pages'), self.original_site)
        self.assertFalse(any('--method' in args for args in self.calls))
        self.assertFalse(self.requests)
        self.assert_local_unchanged()


if __name__ == '__main__':
    unittest.main()
