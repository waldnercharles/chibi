#!/usr/bin/env python3
"""Offline CI wiring/identity checks; does not claim real compiler cache hits."""
import importlib.util
from pathlib import Path
import tempfile
import subprocess
from unittest.mock import patch
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
        return cache.identity(self.root, env or self.env, run or (lambda *args: ' '.join(args)), fingerprint=lambda command: command)

    def test_stable_without_source_revision(self):
        before = self.key()
        (self.root / 'source.c').write_text('changed source')
        self.assertEqual(before, self.key(dict(self.env, GITHUB_SHA='new revision')))

    def test_platform_and_compiler_invalidation(self):
        before = self.key()
        for key in ('RUNNER_OS', 'RUNNER_ARCH', 'ImageOS', 'ImageVersion', 'CC', 'CXX'):
            with self.subTest(key=key):
                after = self.key(dict(self.env, **{key: 'different'}))
                self.assertNotEqual(before[0], after[0])
                self.assertNotEqual(before[1], after[1])
        for tool in ('clang', 'clang++', 'ccache', 'dpkg-query'):
            with self.subTest(tool=tool):
                after = self.key(run=lambda *args: 'new version' if args[0] == tool else ' '.join(args))
                self.assertNotEqual(before[0], after[0])
                self.assertNotEqual(before[1], after[1])

    def test_recipe_and_dependency_invalidation(self):
        for path in sorted(self.root.rglob('*')):
            if path.is_file():
                with self.subTest(path=path):
                    before = self.key()
                    path.write_text(path.read_text() + 'changed')
                    self.assertNotEqual(before[0], self.key()[0])
                    self.assertEqual(before[1], self.key()[1])

    def test_compiler_content_invalidation(self):
        def key(content):
            return cache.identity(self.root, self.env, lambda *args: 'same version',
                                  fingerprint=lambda command: content)
        before = key('old compiler bytes')
        after = key('new compiler bytes')
        self.assertNotEqual(before[0], after[0])
        self.assertNotEqual(before[1], after[1])

    def test_compiler_missing_fails_closed(self):
        with self.assertRaises(FileNotFoundError):
            cache.compiler_digest('/nonexistent/chibi-compiler')

    def test_compiler_hash_reads_contents(self):
        compiler = self.root / 'compiler'
        compiler.write_text('old')
        compiler.chmod(0o700)
        before = cache.compiler_digest(str(compiler))
        compiler.write_text('new')
        self.assertNotEqual(before, cache.compiler_digest(str(compiler)))

    def test_primary_is_nested_under_compatibility(self):
        primary, compatible = self.key()
        self.assertTrue(primary.startswith(compatible + '-'))
        self.assertTrue(primary.startswith('ccache-v2-Linux-X64-'))

    def test_macos_sdk_invalidation(self):
        env = dict(self.env, RUNNER_OS='macOS')
        before = self.key(env)
        for field in ('--show-sdk-path', '--show-sdk-version', '--show-sdk-build-version'):
            with self.subTest(field=field):
                after = self.key(env, lambda *args: 'new sdk' if field in args else ' '.join(args))
                self.assertNotEqual(before[0], after[0])
                self.assertNotEqual(before[1], after[1])

    def test_macos_companion_package_invalidation(self):
        env = dict(self.env, RUNNER_OS='macOS')
        before = self.key(env)
        for command in ('deps', 'list'):
            with self.subTest(command=command):
                after = self.key(env, lambda *args: 'changed llvm package' if
                                 args[:2] == ('brew', command) else ' '.join(args))
                self.assertNotEqual(before[0], after[0])
                self.assertNotEqual(before[1], after[1])

    def test_identity_query_failure_fails_closed(self):
        for platform, tool in (('Linux', 'dpkg-query'), ('macOS', 'brew'), ('macOS', 'xcrun')):
            with self.subTest(platform=platform, tool=tool):
                def run(*args):
                    if args[0] == tool:
                        raise subprocess.CalledProcessError(1, args)
                    return ' '.join(args)
                with self.assertRaises(subprocess.CalledProcessError):
                    self.key(dict(self.env, RUNNER_OS=platform), run)

    def test_output_provenance(self):
        env = dict(self.env, RUNNER_TEMP=str(self.root), GITHUB_WORKSPACE=str(self.root),
                   GITHUB_ENV=str(self.root / 'env'), GITHUB_OUTPUT=str(self.root / 'output'))
        primary, compatible = self.key()
        with patch.object(cache.os, 'environ', env), \
             patch.object(cache, 'identity', return_value=(primary, compatible)):
            cache.main()
        self.assertEqual((self.root / 'output').read_text(),
                         f'prefix={primary}\ncompatibility-prefix={compatible}\n')

    def test_job_local_configuration(self):
        env = dict(self.env, RUNNER_TEMP=str(self.root), GITHUB_WORKSPACE='/workspace/source',
                   GITHUB_ENV=str(self.root / 'env'))
        cache.configure(env)
        self.assertEqual((self.root / 'chibi-ccache.conf').read_text(),
                         'max_size = 500M\ncompiler_check = content\nhash_dir = true\nsloppiness =\n')
        self.assertEqual((self.root / 'env').read_text(),
                         f'CCACHE_DIR={self.root}/chibi-ccache\n'
                         f'CCACHE_CONFIGPATH={self.root}/chibi-ccache.conf\n'
                         'CCACHE_BASEDIR=/workspace/source\n')
        self.assertFalse((self.root / 'chibi-ccache/ccache.conf').exists())

    def test_workflow_cache_scope(self):
        workflow = (cache.ROOT / '.github/workflows/ci.yml').read_text()
        # Two compiler-object caches and three compiled-asset-compiler caches.
        self.assertEqual(workflow.count('uses: actions/cache/restore@v4'), 5)
        self.assertEqual(workflow.count('uses: actions/cache/save@v4'), 4)
        asset_guard = "if: success() && github.event_name == 'push' && github.ref == 'refs/heads/main' && steps.asset-cache.outputs.cache-hit != 'true'"
        self.assertEqual(workflow.count(asset_guard), 2)
        self.assertEqual(workflow.count('path: .chibi/asset-compiler-cache'), 5)
        self.assertNotIn('key: ${{ steps.ccache-config.outputs.compatibility-prefix }}', workflow)
        self.assertNotIn('pull_request_target:', workflow)
        guard = "if: success() && github.event_name == 'push' && github.ref == 'refs/heads/main' && steps.ccache.outputs.cache-hit != 'true'"
        self.assertEqual(workflow.count(guard), 2)
        self.assertEqual(workflow.count('path: ${{ env.CCACHE_DIR }}'), 4)
        self.assertEqual(workflow.count('key: ${{ steps.ccache-config.outputs.prefix }}-${{ github.sha }}'), 4)
        self.assertEqual(workflow.count('restore-keys: |\n            ${{ steps.ccache-config.outputs.prefix }}-\n            ${{ steps.ccache-config.outputs.compatibility-prefix }}-'), 2)


if __name__ == '__main__':
    unittest.main()
