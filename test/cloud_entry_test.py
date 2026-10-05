"""Repository cloud bootstrap wiring; real compiler smoke is make cloud-smoke."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
from unittest import mock
import unittest

import workspace_make_test as workspace

ROOT = Path(__file__).resolve().parents[1]


class CloudEntry(workspace.WorkspaceMake):
    """Reuse the lock/jobserver/recursive Make tests with cloud bootstrap enabled."""

    def setUp(self):
        super().setUp()
        shutil.copytree(ROOT / 'tools/cloud', self.root / 'tools/cloud')
        self.prefix = self.root / 'cloud'
        versions = {'cmake': 'cmake version 4.2.3', 'ninja': '1.12.1',
                    'emcc': 'emcc 3.1.69', 'ccache': 'ccache 4.11.2',
                    'clang-format': 'clang-format version 22.1.8',
                    'clang-19': 'Debian clang version 19.1.7',
                    'clang++-19': 'Debian clang version 19.1.7'}
        for name, version in versions.items():
            directory = 'python/bin' if name in ('cmake', 'clang-format') else 'root/usr/bin'
            self.executable(self.prefix / directory / name, f'echo "{version}"\n')
        for name in ('llvm-ar', 'llvm-nm', 'wasm-ld'):
            self.executable(self.prefix / 'root/usr/lib/llvm-19/bin' / name, ':\n')
        self.executable(self.prefix / 'root/usr/bin/wasm-opt', ':\n')
        for name in ('emscripten-config',
                     'root/usr/lib/llvm-19/lib/clang/19/include/stddef.h',
                     'root/usr/lib/llvm-19/lib/clang/19/lib/linux/libclang_rt.asan-x86_64.a',
                     'root/usr/share/emscripten/node_modules/acorn/package.json'):
            path = self.prefix / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        # Make detection portable in tests, without a production OS override knob.
        release = self.root / 'os-release'
        release.write_text('ID=debian\nVERSION_ID=13\n')
        entry = self.root / 'tools/cloud/entry.sh'
        entry.write_text(entry.read_text().replace('/etc/os-release', str(release)))
        self.executable(self.bin / 'uname', 'echo x86_64\n')
        # Existing PATH selections stay first and must pass the version check.
        cmake = self.bin / 'cmake'
        original = cmake.read_text()
        cmake.write_text(original.replace('args = sys.argv[1:]',
                        'args = sys.argv[1:]\nif args == ["--version"]:\n'
                        '    print("cmake version 4.2.3")\n    sys.exit(0)'))
        tool = self.bin / 'fixture-tool'
        tool.write_text(tool.read_text().replace("'makeflags': os.environ.get('MAKEFLAGS')",
                        "'makeflags': os.environ.get('MAKEFLAGS'), 'cc': os.environ.get('CC'), "
                        "'cxx': os.environ.get('CXX'), 'flags': os.environ.get('NATIVE_TOOLCHAIN_FLAGS'), "
                        "'cflags': os.environ.get('CFLAGS'), 'toolchain': os.environ.get('CMAKE_TOOLCHAIN_FILE'), "
                        "'prefix': os.environ.get('CHIBI_CLOUD_ACTIVE'), 'path': os.environ.get('PATH')"))
        self.extra_env = {}

    @staticmethod
    def executable(path, body):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#!/bin/sh\n' + body)
        path.chmod(0o755)

    def make(self, *args, **kwargs):
        env = os.environ.copy()
        for name in ('CC', 'CXX', 'CFLAGS', 'NATIVE_TOOLCHAIN_FLAGS', 'CMAKE_TOOLCHAIN_FILE',
                     'CHIBI_CLOUD_ACTIVE', 'CHIBI_CLOUD_KEEP_PATH', 'CHIBI_CLOUD',
                     'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH', 'LIBRARY_PATH'):
            env.pop(name, None)
        env.update(CHIBI_CLOUD_TOOLS=str(self.prefix), **self.extra_env)
        with mock.patch.dict(os.environ, env, clear=True):
            return super().make(*args, **kwargs)

    def test_plain_make_defaults_before_locked_recipes(self):
        result = self.make(['build'])
        self.assertEqual(result.returncode, 0, result.stderr)
        call = next(c for c in self.calls() if c['command'] == 'fixture-tool')
        self.assertEqual(call['cc'], str(self.prefix / 'root/usr/bin/clang-19'))
        self.assertEqual(call['prefix'], str(self.prefix))
        self.assertTrue(call['locked'])

    def test_environment_compilers_and_empty_flags_are_preserved(self):
        self.extra_env = {'CC': '/usr/bin/cc', 'CXX': '', 'CFLAGS': ''}
        result = self.make(['build'])
        self.assertEqual(result.returncode, 0, result.stderr)
        call = next(c for c in self.calls() if c['command'] == 'fixture-tool')
        self.assertEqual((call['cc'], call['cxx'], call['cflags'], call['flags']),
                         ('/usr/bin/cc', '', '', ''))

    def test_make_compilers_and_toolchain_are_preserved(self):
        result = self.make(['build'], variables=('CC=', 'CXX=/usr/bin/c++',
                           'CMAKE_TOOLCHAIN_FILE=/chosen/toolchain.cmake'))
        self.assertEqual(result.returncode, 0, result.stderr)
        call = next(c for c in self.calls() if c['command'] == 'fixture-tool')
        self.assertEqual((call['cc'], call['cxx'], call['toolchain']),
                         ('', '/usr/bin/c++', '/chosen/toolchain.cmake'))

    def test_missing_prefix_passes_through(self):
        shutil.rmtree(self.prefix)
        result = self.make(['build'])
        self.assertEqual(result.returncode, 0, result.stderr)
        call = next(c for c in self.calls() if c['command'] == 'fixture-tool')
        self.assertIsNone(call['prefix'])
        self.assertIsNone(call['cc'])

    def test_fedora_passes_through_with_prefix_present(self):
        (self.root / 'os-release').write_text('ID=fedora\nVERSION_ID=43\n')
        result = self.make(['build'])
        self.assertEqual(result.returncode, 0, result.stderr)
        call = next(c for c in self.calls() if c['command'] == 'fixture-tool')
        self.assertIsNone(call['prefix'])
        self.assertIsNone(call['cc'])

    def test_opt_out_preserves_old_entry_behavior(self):
        result = self.make(['build'], variables=('CHIBI_CLOUD=0',))
        self.assertEqual(result.returncode, 0, result.stderr)
        call = next(c for c in self.calls() if c['command'] == 'fixture-tool')
        self.assertIsNone(call['prefix'])

    def test_partial_prefix_fails_before_enrollment_or_configure(self):
        (self.prefix / 'root/usr/bin/clang-19').unlink()
        result = self.make(['build'])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Cloud toolchain missing', result.stderr)
        self.assertFalse((self.root / 'outer-runs').exists())
        self.assertFalse((self.root / 'calls.jsonl').exists())

    def test_incompatible_path_tool_is_not_silently_replaced(self):
        self.executable(self.bin / 'cmake', 'echo "cmake version 3.25.1"\n')
        result = self.make(['build'])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('does not satisfy cmake >= 4.2', result.stderr)
        self.assertIn('CHIBI_CLOUD=0', result.stderr)
        self.assertFalse((self.root / 'outer-runs').exists())

    def test_help_dry_run_and_query_skip_partial_prefix(self):
        (self.prefix / 'root/usr/bin/clang-19').unlink()
        for goals, options, expected in ((['help'], (), 0), (['build'], ('-n',), 0),
                                         (['build'], ('-q',), 1), (['build'], ('-t',), 0)):
            with self.subTest(options=options):
                result = self.make(goals, options=options, managed=False)
                self.assertEqual(result.returncode, expected, result.stderr)
                self.assertNotIn('Cloud toolchain missing', result.stderr)
        self.assertFalse((self.root / 'calls.jsonl').exists())

    def test_explicit_formatter_wins_over_incompatible_path_formatter(self):
        self.executable(self.bin / 'clang-format', 'echo "clang-format version 14.0.0"\n')
        chosen = self.prefix / 'python/bin/clang-format'
        self.extra_env = {'CLANG_FORMAT': str(chosen)}
        result = self.make(['build'])
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_long_make_options_still_bootstrap(self):
        result = self.make(['build'], options=('--no-print-directory',))
        self.assertEqual(result.returncode, 0, result.stderr)
        call = next(c for c in self.calls() if c['command'] == 'fixture-tool')
        self.assertEqual(call['prefix'], str(self.prefix))

    def test_compatible_command_line_path_stays_exact(self):
        chosen = ':'.join(map(str, (self.bin, self.root / 'tools/cloud/bin',
                         self.prefix / 'python/bin', self.prefix / 'root/usr/bin')))
        chosen += ':' + os.environ['PATH']
        result = self.make(['build'], variables=(f'PATH={chosen}',))
        self.assertEqual(result.returncode, 0, result.stderr)
        call = next(c for c in self.calls() if c['command'] == 'fixture-tool')
        self.assertEqual(call['path'], chosen)
        self.assertEqual(call['prefix'], str(self.prefix))

    def test_command_line_path_is_not_extended_behind_makes_back(self):
        # Python/make remain available, but this explicit PATH lacks the cloud
        # tools. The default must not be validated then discarded by child Make.
        path = self.root / 'explicit-path'
        path.mkdir()
        for name in ('bash', 'dirname', 'make'):
            (path / name).symlink_to(shutil.which(name))
        (path / 'python3').symlink_to(sys.executable)
        shutil.copyfile(self.bin / 'uname', path / 'uname')
        (path / 'uname').chmod(0o755)
        result = self.make(['build'], variables=(f'PATH={path}',))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('run bash', result.stderr)
        self.assertFalse((self.root / 'outer-runs').exists())

    def test_direct_mcp_entry_bootstraps_without_stdout_noise(self):
        directory = self.root / 'tools/mcp'
        directory.mkdir()
        shutil.copyfile(ROOT / 'tools/mcp/launch.sh', directory / 'launch.sh')
        controller = self.root / 'controller'
        self.executable(controller, ':\n')
        node = self.bin / 'node'
        self.executable(node, 'if [ "$1" = --version ]; then echo v24.19.0; exit; fi\n'
                        'if [ "$1" = -e ]; then exit; fi\n'
                        'printf "%s" "$CHIBI_CLOUD_ACTIVE" > "$MCP_ENV_LOG"\n')
        env = os.environ.copy()
        env.pop('CHIBI_CLOUD_ACTIVE', None)
        env.update(CHIBI_CLOUD='auto', CHIBI_CLOUD_TOOLS=str(self.prefix), CHIBI_CTL=str(controller),
                   PATH=str(self.bin) + ':' + env['PATH'], MCP_ENV_LOG=str(self.root / 'mcp-env'))
        result = subprocess.run(['sh', 'tools/mcp/launch.sh'], cwd=self.root,
                                env=env, capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, '')
        self.assertEqual((self.root / 'mcp-env').read_text(), str(self.prefix))

    def test_modeling_checks_selected_blender_not_only_local_prefix(self):
        # Isolate dispatch from provisioning: selected PATH Blender must also run
        # the fixture, and its failure must stop the requested command.
        (self.root / 'tools/cloud/modeling_setup.sh').write_text('exit 0\n')
        self.executable(self.bin / 'blender', 'exit 7\n')
        env = os.environ.copy()
        env.update(CHIBI_CLOUD_TOOLS=str(self.prefix),
                   PATH=str(self.bin) + ':' + env['PATH'])
        result = subprocess.run(['bash', 'tools/cloud/bin/start_skill', '--modeling',
                                 '--', 'touch', str(self.root / 'model-command-ran')],
                                cwd=self.root, env=env, capture_output=True, text=True, timeout=20)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Selected Blender', result.stderr)
        self.assertFalse((self.root / 'model-command-ran').exists())


if __name__ == '__main__':
    unittest.main()
