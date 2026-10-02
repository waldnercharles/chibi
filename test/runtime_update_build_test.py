"""Exercise automatic runtime refresh wiring without building or installing."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RuntimeBuildHooks(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='chibi-build-hooks-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        shutil.copyfile(ROOT / 'Makefile', self.root / 'Makefile')
        (self.root / 'tools').mkdir()
        (self.root / 'tools/configure_native.sh').write_text('fixture-tool configure "$@"\n')
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
                    'FAIL_EDITOR', 'FAIL_PACKAGE'):
            env.pop(key, None)
        env.update(flags)
        env['PATH'] = str(self.bin) + os.pathsep + env['PATH']
        env['FIXTURE_LOG'] = str(self.log)
        result = subprocess.run(['make', target, f'BUILD_TARGET={build_target}', 'JOBS=2',
                                 'NATIVE_TOOLCHAIN_FLAGS=', 'PACKAGE_OUTPUT=dist/runtime'],
                                cwd=self.root, env=env, text=True, capture_output=True,
                                timeout=15)
        calls = [json.loads(line) for line in self.log.read_text().splitlines()]
        return result, calls

    def script_calls(self, calls, name):
        return [call for call in calls
                if call['command'] == 'python3' and call['args'][0] == f'tools/{name}.py']

    def test_registered_editor_build_packages_once_then_installs(self):
        result, calls = self.make()
        self.assertEqual(result.returncode, 0, result.stderr)
        packages = self.script_calls(calls, 'package_native')
        installs = self.script_calls(calls, 'install_game_runtime')
        self.assertEqual(len(packages), 1)
        self.assertEqual(len(installs), 1)
        self.assertLess(calls.index(packages[0]), calls.index(installs[0]))
        configures = [call for call in calls if call['command'] == 'configure']
        self.assertEqual(len(configures), 2)
        self.assertIn('-DCHIBI_DISTRIBUTION=ON', configures[1]['args'])
        self.assertIn('-DCHIBI_DEVELOPMENT_RELOAD=OFF', configures[1]['args'])
        self.assertIn('UPDATE_GAME_RUNTIME=0', configures[1]['makeflags'])
        self.assertEqual(installs[0]['args'], ['tools/install_game_runtime.py',
                                             '--package', 'dist/runtime'])

    def test_controller_build_does_not_refresh_game_runtime(self):
        result, calls = self.make(build_target='chibi_ctl')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.script_calls(calls, 'package_native'))
        self.assertFalse(self.script_calls(calls, 'install_game_runtime'))

    def test_profiling_and_development_builds_deliver_once_before_launch(self):
        for target in ('build-tracy', 'dev'):
            with self.subTest(target=target):
                result, calls = self.make(target=target)
                self.assertEqual(result.returncode, 0, result.stderr)
                packages = self.script_calls(calls, 'package_native')
                installs = self.script_calls(calls, 'install_game_runtime')
                self.assertEqual(len(packages), 1)
                self.assertEqual(len(installs), 1)
                self.assertLess(calls.index(packages[0]), calls.index(installs[0]))
                configures = [call for call in calls if call['command'] == 'configure']
                self.assertIn('-DCHIBI_DISTRIBUTION=ON', configures[1]['args'])
                self.assertIn('-DCHIBI_DEVELOPMENT_RELOAD=OFF', configures[1]['args'])
                if target == 'dev':
                    self.assertIn('-DCHIBI_DEVELOPMENT_RELOAD=ON', configures[0]['args'])
                    launch = next(call for call in calls if call['command'] == 'launch')
                    self.assertLess(calls.index(installs[0]), calls.index(launch))
                self.log.unlink()

    def test_unregistered_editor_build_does_not_package(self):
        result, calls = self.make(registered=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.script_calls(calls, 'package_native'))
        self.assertFalse(self.script_calls(calls, 'install_game_runtime'))

    def test_failed_editor_build_does_not_install(self):
        result, calls = self.make(FAIL_EDITOR='1')
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.script_calls(calls, 'package_native'))
        self.assertFalse(self.script_calls(calls, 'install_game_runtime'))

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
