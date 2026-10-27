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
            env=dict(os.environ, BUILD_DEBUG_OPTIMIZATION='1'))
        self.assertIn('BUILD_PROFILE=release BUILD_DEBUG_OPTIMIZATION=', result)
        self.assertIn('--profile "release"', result)
        self.assertNotIn('--debug-optimization "1"', result)

    def test_invalid_profile_fails_closed(self):
        for value in ('', 'true', 'off', None):
            with self.assertRaises(ValueError):
                profile.settings(value)

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
            self.assertIn('make -j1 test-native test-materials test-projects test-language-all', block)
            self.assertIn('run: python3 .github/verify-native-profile.py', block)
        self.assertNotIn('continue-on-error:', text.split('  native:\n')[1].split('  windows:\n')[0])


if __name__ == '__main__':
    unittest.main()
