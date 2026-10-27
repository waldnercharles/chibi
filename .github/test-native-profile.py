#!/usr/bin/env python3
"""Offline checks for the two CI profiles and their shared workflow wiring."""
import importlib.util
from pathlib import Path
import shlex
import os
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('native_profile', ROOT / '.github/native-profile.py')
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)


class ProfileTests(unittest.TestCase):
    def test_profiles_keep_debug_and_explicit_graph_optimization(self):
        for enabled in ('ON', 'OFF'):
            with self.subTest(sanitizers=enabled):
                env = profile.settings(enabled)
                self.assertEqual(env['BUILD_PROFILE'], 'sanitizer' if enabled == 'ON' else 'debug')
                self.assertEqual(env['BUILD_DEBUG_OPTIMIZATION'], '1')
                self.assertEqual(env['BUILD_DEBUG_FRAME_POINTERS'], 'on' if enabled == 'OFF' else '')
                flags = shlex.split(env['SANITIZER_CMAKE_FLAGS'])
                for flag in ('-DCMAKE_BUILD_TYPE=Debug', '-DCHIBI_ENABLE_WARNINGS=ON',
                             '-DCHIBI_ENABLE_CCACHE=ON', '-DCHIBI_SANITIZERS=address,undefined',
                             f'-DCHIBI_ENABLE_SANITIZERS={enabled}'):
                    self.assertIn(flag, flags)
                debug = '-g -O1' + (' -fno-omit-frame-pointer' if enabled == 'OFF' else '')
                for language in ('C', 'CXX', 'OBJC'):
                    self.assertIn(f'-DCMAKE_{language}_FLAGS_DEBUG={debug}', flags)
                self.assertNotIn('LOCAL_CMAKE_FLAGS', env)  # Release recursive make retains its own flags.

    def test_release_does_not_inherit_debug_optimization(self):
        result = subprocess.check_output(
            ['make', '-n', 'build-release', 'GRAPH_BUILD=1', 'BUILD_HOST=/nonexistent',
             'BUILD_TARGET=chibi-build-host'], cwd=ROOT, text=True,
            env=dict(os.environ, BUILD_DEBUG_OPTIMIZATION='1', BUILD_DEBUG_FRAME_POINTERS='on',
                     ASSET_GENERATOR_HOSTS='/tmp/borrowed-generator-hosts'))
        self.assertIn('BUILD_PROFILE=release BUILD_DEBUG_OPTIMIZATION=', result)
        self.assertIn('--profile "release"', result)
        self.assertNotIn('--debug-optimization "1"', result)
        self.assertNotIn('--debug-frame-pointers "on"', result)
        self.assertNotIn('--asset-generator-hosts', result)

    def test_invalid_profile_fails_closed(self):
        for value in ('', 'true', 'off', None):
            with self.assertRaises(ValueError):
                profile.settings(value)

    def test_concurrency_finishes_main_push_but_cancels_superseded_other_runs(self):
        text = (ROOT / '.github/workflows/ci.yml').read_text()
        block = text.split('\nconcurrency:\n', 1)[1].split('\nenv:', 1)[0]
        self.assertIn("group: ci-${{ github.workflow }}-${{ github.ref }}-${{ github.event_name }}-${{ (github.event_name == 'schedule' || inputs.sanitizers == true) && 'sanitizer' || 'fast' }}", block)
        self.assertNotIn('queue:', block)  # Default: one running and newest pending, not queue:max.
        line = next(line for line in block.splitlines() if 'cancel-in-progress:' in line)
        expression = line.split('${{', 1)[1].split('}}', 1)[0]
        tokens = shlex.split(expression)
        self.assertEqual(tokens, ['github.event_name', '!=', 'push', '||',
                                  'github.ref', '!=', 'refs/heads/main'])
        for event, ref, expected in (
                ('push', 'refs/heads/main', False),
                ('push', 'refs/heads/topic', True),
                ('pull_request', 'refs/pull/733/merge', True),
                ('workflow_dispatch', 'refs/heads/main', True),
                ('schedule', 'refs/heads/main', True)):
            with self.subTest(event=event, ref=ref):
                context = {'github.event_name': event, 'github.ref': ref}
                actual = any(context[field] != value
                             for field, _, value in (tokens[:3], tokens[4:]))
                self.assertEqual(actual, expected)

    def test_workflow_preserves_shared_jobs_and_groups(self):
        text = (ROOT / '.github/workflows/ci.yml').read_text()
        self.assertIn("cron: '0 8 * * *'", text)
        self.assertIn('type: boolean\n        default: false', text)
        self.assertIn("github.event_name == 'schedule' || inputs.sanitizers == true", text)
        concurrency = next(line for line in text.splitlines() if line.strip().startswith('group: ci-'))
        self.assertIn('${{ github.event_name }}', concurrency)
        self.assertIn("'sanitizer' || 'fast'", concurrency)
        self.assertEqual(text.count('run: python3 .github/native-profile.py'), 3)
        for job, name in (('native', 'Engine build and native tests'),
                          ('native-macos', 'Engine build and native tests (macOS)')):
            block = text.split(f'  {job}:\n', 1)[1]
            self.assertTrue(block.startswith(f'    name: {name}\n'))
            block = block.split('\n  windows:', 1)[0] if job == 'native-macos' else block.split('\n  native-macos:', 1)[0]
            self.assertIn('timeout-minutes: 45', block)
            self.assertIn('make test-tools', block)
            groups = ['python3 tools/test_policy.py group ' + name
                      for name in ('native', 'materials', 'projects', 'language')]
            positions = [block.index(command) for command in groups]
            self.assertEqual(positions, sorted(positions))
            self.assertTrue(all(block.count(command) == 1 for command in groups))
            selectors = block.split('- name: Native, material, project and language selectors', 1)[1].split('      - name:', 1)[0]
            self.assertIn('shell: bash', selectors)
            self.assertIn('set -e', selectors)
            self.assertNotIn('if:', selectors)  # Default success gate must not be weakened.
            if job == 'native':
                self.assertLess(block.index('make test-build'), positions[0])
            else:
                self.assertNotIn('make test-build', block)
            self.assertIn('run: python3 .github/verify-native-profile.py', block)
            release = block.index('run: make build-release')
            build = block.index('make build BUILD_TARGET=')
            verify = block.index('run: python3 .github/verify-native-profile.py')
            self.assertLess(release, build)
            self.assertLess(build, verify)
            self.assertLess(verify, positions[0])
            self.assertIn('chibi-compiler-host', block[release:build])
            self.assertEqual(block.count('ASSET_GENERATOR_HOSTS=build-release/sdk'), 2 if job == 'native' else 1)
        self.assertNotIn('continue-on-error:', text.split('  native:\n')[1].split('  windows:\n')[0])


if __name__ == '__main__':
    unittest.main()
