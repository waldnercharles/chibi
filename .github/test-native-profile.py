#!/usr/bin/env python3
"""Offline checks for the two CI profiles and their shared workflow wiring."""
import importlib.util
from pathlib import Path
import shlex
import runpy
import sys
import os
import subprocess
import unittest
from unittest.mock import patch

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
                self.assertNotIn('LOCAL_CMAKE_FLAGS', env)  # The Release build retains its own flags.

    def test_linux_graph_proof_is_bounded_and_explicit(self):
        text = (ROOT / '.github/workflows/linux-graph.yml').read_text()
        triggers = text.split('\non:\n', 1)[1].split('\npermissions:', 1)[0]
        self.assertEqual([line for line in triggers.splitlines()
                          if line.startswith('  ') and not line.startswith('   ')],
                         ['  pull_request:', '  workflow_dispatch:'])
        self.assertIn('types: [labeled]', triggers)
        self.assertIn('type: boolean\n        default: false', triggers)
        self.assertIn('permissions:\n  contents: read\n', text)
        self.assertNotIn('write', text.split('\npermissions:', 1)[1].split('\njobs:', 1)[0])
        self.assertIn('timeout-minutes: 45', text)
        self.assertIn("JOBS: '2'", text)
        self.assertIn('persist-credentials: false', text)
        self.assertIn('GH_TOKEN: ${{ github.token }}', text)
        self.assertIn('group: linux-graph-${{ github.event.pull_request.number || github.ref }}', text)
        exact = '${{ github.event.pull_request.head.sha || github.sha }}'
        self.assertIn('EXPECTED_SHA: ' + exact, text)
        self.assertIn('ref: ' + exact, text)
        self.assertEqual(text.count('test "$(git rev-parse HEAD)" = "$EXPECTED_SHA"'), 2)
        gate = ' '.join(text.split('    if: >-\n', 1)[1].split('    concurrency:', 1)[0].split())
        selection = next(line for line in text.splitlines() if 'CHIBI_CI_SANITIZERS:' in line)
        selection = selection.split('${{', 1)[1].split('}}', 1)[0].strip()
        expected_gate = (
            "github.event_name == 'workflow_dispatch' || "
            "((github.event.label.name == 'ci-linux-graph' || "
            "github.event.label.name == 'ci-linux-graph-sanitizers') && "
            "github.event.pull_request.head.repo.full_name == github.repository)")
        expected_selection = (
            "(inputs.sanitizers == true || github.event.label.name == "
            "'ci-linux-graph-sanitizers') && 'ON' || 'OFF'")
        self.assertEqual(gate, expected_gate)
        self.assertEqual(selection, expected_selection)

        def evaluate(expression, context):
            for field in sorted(context, key=len, reverse=True):
                expression = expression.replace(field, repr(context[field]))
            expression = expression.replace('true', 'True').replace('&&', 'and').replace('||', 'or')
            return eval(expression, {'__builtins__': {}}, {})

        for event, label, same_repo, sanitizers, allowed, profile in (
                ('pull_request', 'ci-linux-graph', True, False, True, 'OFF'),
                ('pull_request', 'ci-linux-graph-sanitizers', True, False, True, 'ON'),
                ('workflow_dispatch', '', True, False, True, 'OFF'),
                ('workflow_dispatch', '', True, True, True, 'ON'),
                ('pull_request', 'unrelated', True, False, False, 'OFF'),
                ('pull_request', 'ci-linux-graph', False, False, False, 'OFF'),
                ('pull_request', 'ci-linux-graph-sanitizers', False, False, False, 'ON')):
            with self.subTest(event=event, label=label, same_repo=same_repo, sanitizers=sanitizers):
                context = {'github.event_name': event, 'github.event.label.name': label,
                           'github.event.pull_request.head.repo.full_name': 'owner/repo' if same_repo else 'fork/repo',
                           'github.repository': 'owner/repo', 'inputs.sanitizers': sanitizers}
                self.assertEqual(evaluate(gate, context), allowed)
                self.assertEqual(evaluate(selection, context), profile)

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
            block = block.split('\n  native-targets:', 1)[0] if job == 'native-macos' else block.split('\n  native-macos:', 1)[0]
            self.assertIn('timeout-minutes: 45', block)
            self.assertIn('sh tools/build.sh test --build-host build-release/sdk/chibi-build-host short', block)
            groups = ['sh tools/build.sh test --build-host build-release/sdk/chibi-build-host native materials',
                      'sh tools/build.sh test --build-host build-release/sdk/chibi-build-host projects language']
            positions = [block.index(command) for command in groups]
            self.assertEqual(positions, sorted(positions))
            self.assertTrue(all(block.count(command) == 1 for command in groups))
            selectors = block.split('- name: Native, material, project and language selectors', 1)[1].split('      - name:', 1)[0]
            self.assertIn('shell: bash', selectors)
            self.assertIn('set -e', selectors)
            self.assertNotIn('if:', selectors)  # Default success gate must not be weakened.
            self.assertNotIn('make test-build', block)
            self.assertIn('run: python3 .github/verify-native-profile.py', block)
            release = block.index('run: sh tools/build.sh build --release')
            build = block.index('sh tools/build.sh build --profile')
            verify = block.index('run: python3 .github/verify-native-profile.py')
            self.assertLess(release, build)
            self.assertLess(build, verify)
            self.assertLess(verify, positions[0])
            self.assertIn('chibi-compiler-host', block[release:build])
            self.assertEqual(block.count('--asset-generator-hosts build-release/sdk'), 1)
            self.assertIn('--host "$PWD/build-release/sdk/chibi-build-host"', block[build:verify])
            if job == 'native':
                parity = block.index('build-host "$CHIBI_BUILD_HOST" build-graph-debug-optimization')
                self.assertLess(verify, parity)
        diagnostics = ['Capture macOS facade identities', 'Preserve macOS facade identities']
        optional = []
        native_jobs = text.split('  native:\n', 1)[1].split('  format:\n', 1)[0]
        for step in native_jobs.split('\n      - '):
            if 'continue-on-error:' not in step:
                continue
            name = step.splitlines()[0].removeprefix('name: ')
            self.assertIn(name, diagnostics)
            self.assertEqual(step.count('continue-on-error:'), 1)
            self.assertIn('\n        continue-on-error: true\n', step)
            optional.append(name)
        self.assertEqual(optional, diagnostics)


if __name__ == '__main__':
    unittest.main()
