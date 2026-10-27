#!/usr/bin/env python3
"""Offline CI wiring/identity checks; does not claim real compiler cache hits."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('native_ccache', Path(__file__).with_name('native-ccache.py'))
cache = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cache)


class CacheTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in ('Makefile', 'CMakeLists.txt', 'third_party/archives.tsv',
                     '.github/build-tools.txt', '.github/native-ccache.py',
                     'cmake/test.cmake', 'tools/toolchain/Graph.chibi'):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(name)
        self.env = dict(RUNNER_OS='Linux', RUNNER_ARCH='X64', CC='clang', CXX='clang++',
                        ImageOS='ubuntu24', ImageVersion='1')

    def key(self, env=None, run=None):
        return cache.identity(self.root, env or self.env, run or (lambda *args: ' '.join(args)))

    def test_stable_without_source_revision(self):
        before = self.key()
        (self.root / 'source.c').write_text('changed source')
        self.assertEqual(before, self.key(dict(self.env, GITHUB_SHA='new revision')))

    def test_platform_and_compiler_invalidation(self):
        before = self.key()
        for key in ('RUNNER_OS', 'RUNNER_ARCH', 'ImageOS', 'ImageVersion', 'CC', 'CXX'):
            with self.subTest(key=key):
                self.assertNotEqual(before, self.key(dict(self.env, **{key: 'different'})))
        self.assertNotEqual(before, self.key(run=lambda *args: 'new version'))

    def test_recipe_and_dependency_invalidation(self):
        for path in sorted(self.root.rglob('*')):
            if path.is_file():
                with self.subTest(path=path):
                    before = self.key()
                    path.write_text(path.read_text() + 'changed')
                    self.assertNotEqual(before, self.key())

    def test_macos_sdk_invalidation(self):
        env = dict(self.env, RUNNER_OS='macOS')
        before = self.key(env)
        self.assertNotEqual(before, self.key(env, lambda *args: 'new sdk' if args[0] == 'xcrun' else ' '.join(args)))

    def test_job_local_configuration(self):
        env = dict(self.env, RUNNER_TEMP=str(self.root), GITHUB_WORKSPACE='/workspace/source',
                   GITHUB_ENV=str(self.root / 'env'))
        cache.configure(env)
        self.assertEqual((self.root / 'chibi-ccache.conf').read_text(),
                         'max_size = 500M\ncompiler_check = content\n')
        self.assertEqual((self.root / 'env').read_text(),
                         f'CCACHE_DIR={self.root}/chibi-ccache\n'
                         f'CCACHE_CONFIGPATH={self.root}/chibi-ccache.conf\n'
                         'CCACHE_BASEDIR=/workspace/source\n')
        self.assertFalse((self.root / 'chibi-ccache/ccache.conf').exists())

    def test_workflow_cache_scope(self):
        workflow = (cache.ROOT / '.github/workflows/ci.yml').read_text()
        self.assertEqual(workflow.count('uses: actions/cache/restore@v4'), 2)
        self.assertEqual(workflow.count('uses: actions/cache/save@v4'), 2)
        guard = "if: success() && github.event_name == 'push' && github.ref == 'refs/heads/main' && steps.ccache.outputs.cache-hit != 'true'"
        self.assertEqual(workflow.count(guard), 2)
        self.assertEqual(workflow.count('path: ${{ env.CCACHE_DIR }}'), 4)
        self.assertEqual(workflow.count('key: ${{ steps.ccache-config.outputs.prefix }}-${{ github.sha }}'), 4)
        self.assertEqual(workflow.count('restore-keys: ${{ steps.ccache-config.outputs.prefix }}-'), 2)


if __name__ == '__main__':
    unittest.main()
