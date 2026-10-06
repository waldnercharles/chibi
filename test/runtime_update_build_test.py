"""Exercise automatic runtime refresh wiring without building or installing."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


class RuntimeBuildHooks(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='chibi-build-hooks-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        shutil.copyfile(ROOT / 'Makefile', self.root / 'Makefile')
        (self.root / 'tools').mkdir()
        shutil.copyfile(ROOT / 'tools/quiet.py', self.root / 'tools/quiet.py')
        (self.root / 'tools/configure_native.sh').write_text('fixture-tool configure "$@"\n')
        (self.root / 'tools/configure_web.sh').write_text('fixture-tool configure-web "$@"\n')
        shutil.copyfile(ROOT / 'tools/web_env.sh', self.root / 'tools/web_env.sh')
        self.bin = self.root / 'fake-bin'
        self.bin.mkdir()
        self.log = self.root / 'calls.jsonl'
        fake = f'''#!{sys.executable}
import json, os, sys
from pathlib import Path
command = Path(sys.argv[0]).name
args = sys.argv[1:]
if command == 'fixture-tool':
    command, args = args[0], args[1:]
with open(os.environ['FIXTURE_LOG'], 'a') as log:
    log.write(json.dumps({{'command': command, 'args': args,
                          'makeflags': os.environ.get('MAKEFLAGS', '')}}) + '\\n')
if command == 'cmake' and os.environ.get('FAIL_EDITOR') == '1' and '--target' in args:
    if args[args.index('--target') + 1:] == ['chibi', '--parallel', '2']:
        sys.exit(11)
if command == 'cmake' and '--build' in args and 'chibi' in args:
    binary = Path(args[args.index('--build') + 1]) / 'chibi'
    binary.parent.mkdir(parents=True, exist_ok=True)
    binary.write_text('#!/bin/sh\\nfixture-tool launch "$@"\\n')
    binary.chmod(0o755)
if command == 'python3' and args[0] == 'tools/package_native.py' and os.environ.get('FAIL_PACKAGE') == '1':
    sys.exit(12)
'''
        for command in ('fixture-tool', 'cmake', 'python3'):
            path = self.bin / command
            path.write_text(fake)
            path.chmod(0o755)

    def make(self, target='build', build_target='chibi', registered=True, **flags):
        if registered:
            (self.root / '.chibi').mkdir(exist_ok=True)
            (self.root / '.chibi/game-runtime.json').write_text('{}\n')
        env = os.environ.copy()
        for key in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES', 'UPDATE_GAME_RUNTIME',
                    'FAIL_EDITOR', 'FAIL_PACKAGE', 'CHIBI_WORKSPACE_LOCK_FD', 'BUILD_DIR'):
            env.pop(key, None)
        env.update(flags)
        env['PATH'] = str(self.bin) + os.pathsep + env['PATH']
        env['FIXTURE_LOG'] = str(self.log)
        result = subprocess.run(['make', target, *([f'BUILD_TARGET={build_target}']
                                  if build_target is not None else []), 'JOBS=2',
                                 'NATIVE_TOOLCHAIN_FLAGS=', 'PACKAGE_OUTPUT=dist/runtime',
                                 f'QUIET={sys.executable} tools/quiet.py'],
                                cwd=self.root, env=env, text=True, capture_output=True,
                                timeout=15)
        calls = [json.loads(line) for line in self.log.read_text().splitlines()]
        return result, calls

    def script_calls(self, calls, name):
        return [call for call in calls
                if call['command'] == 'python3' and call['args'][0] == f'tools/{name}.py']

    def test_registered_editor_build_uses_only_requested_profile(self):
        result, calls = self.make()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.script_calls(calls, 'package_native'))
        self.assertFalse(self.script_calls(calls, 'install_game_runtime'))
        self.assertEqual(len([call for call in calls if call['command'] == 'configure']), 1)

    def test_launch_refreshes_registered_game_before_starting(self):
        for target in ('run', 'play', 'debug', 'debug-san', 'run-tracy', 'dev'):
            with self.subTest(target=target):
                result, calls = self.make(target=target)
                self.assertEqual(result.returncode, 0, result.stderr)
                packages = self.script_calls(calls, 'package_native')
                installs = self.script_calls(calls, 'install_game_runtime')
                launch = next(call for call in calls if call['command'] == 'launch')
                self.assertEqual(len(packages), 1)
                self.assertEqual(len(installs), 1)
                self.assertLess(calls.index(packages[0]), calls.index(installs[0]))
                self.assertLess(calls.index(installs[0]), calls.index(launch))
                configures = [call for call in calls if call['command'] == 'configure']
                self.assertIn('-DCHIBI_DISTRIBUTION=ON', configures[1]['args'])
                self.log.unlink()

    def test_dev_builds_then_launches_with_workspace_and_requested_arguments(self):
        result, calls = self.make(target='dev', registered=False,
                                  PROJECT='external game', SCENE='scenes/example.kdl',
                                  ARGS='--headless --frames 2')
        self.assertEqual(result.returncode, 0, result.stderr)
        build = next(call for call in calls if call['command'] == 'cmake')
        launch = next(call for call in calls if call['command'] == 'launch')
        self.assertEqual(build['args'], ['--build', 'build-dev', '--target',
                                         'chibi', '--parallel', '2'])
        self.assertLess(calls.index(build), calls.index(launch))
        self.assertEqual(launch['args'], ['--checkpoint', str(self.root.resolve() / '.chibi/workspace.json'),
                                         '--project-root', 'external game',
                                         '--scene', 'scenes/example.kdl',
                                         '--headless', '--frames', '2'])

    def test_dev_preserves_play_and_explicit_checkpoint_arguments(self):
        for arguments, expected in (('--play', ['--play']),
                                    ('--checkpoint "/custom workspace.json"',
                                     ['--checkpoint', '/custom workspace.json'])):
            with self.subTest(arguments=arguments):
                result, calls = self.make(target='dev', registered=False, ARGS=arguments)
                self.assertEqual(result.returncode, 0, result.stderr)
                launch = next(call for call in calls if call['command'] == 'launch')
                self.assertEqual(launch['args'], ['--project-root', 'projects/arena2d', *expected])
                self.log.unlink()

    def test_failed_dev_build_does_not_refresh_or_launch(self):
        result, calls = self.make(target='dev', FAIL_EDITOR='1')
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.script_calls(calls, 'package_native'))
        self.assertFalse(self.script_calls(calls, 'install_game_runtime'))
        self.assertFalse(any(call['command'] == 'launch' for call in calls))

    def test_default_build_limits_targets_to_app_and_controller(self):
        result, calls = self.make(build_target=None)
        self.assertEqual(result.returncode, 0, result.stderr)
        build = next(call for call in calls if call['command'] == 'cmake')
        self.assertEqual(build['args'], ['--build', 'build', '--target',
                                         'chibi', 'chibi_ctl', '--parallel', '2'])

    def test_build_directory_ignores_parent_but_keeps_explicit_fixture_override(self):
        with patch.dict(os.environ, {'BUILD_DIR': 'parent-build'}):
            for flags, expected in (({}, 'build'), ({'BUILD_DIR': 'fixture-build'}, 'fixture-build')):
                with self.subTest(flags=flags):
                    result, calls = self.make(**flags)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    build = next(call for call in calls if call['command'] == 'cmake')
                    self.assertEqual(build['args'][1], expected)
                    self.log.unlink()

    def test_web_reflection_reuses_native_configuration(self):
        result, calls = self.make(registered=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        native = next(call for call in calls if call['command'] == 'configure')['args']
        self.log.unlink()
        result, calls = self.make(target='web-configure', registered=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        reflect = next(call for call in calls if call['command'] == 'configure')['args']
        self.assertEqual(reflect, native)
        self.assertEqual(len([call for call in calls if call['command'] == 'configure-web']), 1)

    def test_controller_build_does_not_refresh_game_runtime(self):
        result, calls = self.make(build_target='chibi_ctl')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.script_calls(calls, 'package_native'))
        self.assertFalse(self.script_calls(calls, 'install_game_runtime'))

    def test_unregistered_editor_build_does_not_package(self):
        result, calls = self.make(registered=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.script_calls(calls, 'package_native'))
        self.assertFalse(self.script_calls(calls, 'install_game_runtime'))

    def test_failed_editor_build_does_not_install(self):
        result, calls = self.make(target='run', FAIL_EDITOR='1')
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.script_calls(calls, 'package_native'))
        self.assertFalse(self.script_calls(calls, 'install_game_runtime'))

    def test_launch_can_skip_runtime_refresh(self):
        result, calls = self.make(target='run', UPDATE_GAME_RUNTIME='0')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.script_calls(calls, 'package_native'))

    def test_explicit_package_installs_after_assembly(self):
        result, calls = self.make(target='package-native')
        self.assertEqual(result.returncode, 0, result.stderr)
        packages = self.script_calls(calls, 'package_native')
        installs = self.script_calls(calls, 'install_game_runtime')
        self.assertEqual(len(packages), 1)
        self.assertEqual(len(installs), 1)
        self.assertLess(calls.index(packages[0]), calls.index(installs[0]))

    def test_failed_assembly_does_not_install(self):
        result, calls = self.make(target='package-native', FAIL_PACKAGE='1')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(len(self.script_calls(calls, 'package_native')), 1)
        self.assertFalse(self.script_calls(calls, 'install_game_runtime'))


if __name__ == '__main__':
    unittest.main()
