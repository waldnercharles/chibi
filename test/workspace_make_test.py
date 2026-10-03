"""Exercise managed Make's lock across recursive goals and launched runtimes."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

HELPER = '''import contextlib, fcntl, os, re, subprocess, sys
from pathlib import Path
root = Path.cwd()
lock_path = root / '.fixture-workspace.lock'
def validate():
    fd = int(os.environ['CHIBI_WORKSPACE_LOCK_FD'])
    a, b = os.fstat(fd), lock_path.stat()
    assert (a.st_dev, a.st_ino) == (b.st_dev, b.st_ino)
    with lock_path.open('a+') as probe:
        try:
            fcntl.flock(probe, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return fd
        raise ValueError('unheld lock')
@contextlib.contextmanager
def registry_lock():
    yield {}
def workspace_for_path(registry, path):
    return {'path': str(root)}
def validate_marker(entry):
    assert (root / '.chibi-workspace.json').is_file()
def inherited_lock(entry):
    return validate()
def inherited_fds(*extra):
    return extra
if __name__ == '__main__':
    if sys.argv[1] == 'validate-lock':
        validate()
    elif sys.argv[1] == 'run':
        with (root / 'outer-runs').open('a') as log:
            log.write('run\\n')
        with lock_path.open('a+') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            fd = lock.fileno()
            os.set_inheritable(fd, True)
            os.environ['CHIBI_WORKSPACE_LOCK_FD'] = str(fd)
            # Preserve make's inherited jobserver descriptors as well as this lock.
            sys.exit(subprocess.call(sys.argv[3:], close_fds=False))
'''

TOOL = '''#!PYTHON
import fcntl, json, os, sys
from pathlib import Path
args = sys.argv[1:]
locked = False
if os.environ.get('CHIBI_WORKSPACE_LOCK_FD'):
    fd = int(os.environ['CHIBI_WORKSPACE_LOCK_FD'])
    os.fstat(fd)
    with Path('.fixture-workspace.lock').open('a+') as probe:
        try:
            fcntl.flock(probe, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            locked = True
with Path('calls.jsonl').open('a') as log:
    log.write(json.dumps({'command': Path(sys.argv[0]).name, 'args': args,
                          'locked': locked, 'makeflags': os.environ.get('MAKEFLAGS')}) + '\\n')
if Path(sys.argv[0]).name == 'cmake' and '--build' in args and 'chibi' in args:
    binary = Path(args[args.index('--build') + 1]) / 'chibi'
    binary.parent.mkdir(parents=True, exist_ok=True)
    binary.write_text('#!/bin/sh\\nfixture-tool launch "$@"\\n')
    binary.chmod(0o755)
'''


class WorkspaceMake(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        shutil.copyfile(ROOT / 'Makefile', self.root / 'Makefile')
        tools = self.root / 'tools'
        tools.mkdir()
        shutil.copyfile(ROOT / 'tools/quiet.py', tools / 'quiet.py')
        (tools / 'workspaces.py').write_text(HELPER)
        (tools / 'configure_native.sh').write_text('fixture-tool configure "$@"\n')
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        for command in ('cmake', 'fixture-tool'):
            path = self.bin / command
            path.write_text(TOOL.replace('PYTHON', sys.executable))
            path.chmod(0o755)

    def make(self, goals, managed=True, fd=None, variables=(), options=()):
        if managed:
            (self.root / '.chibi-workspace.json').write_text('{}')
        env = os.environ.copy()
        for name in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES', 'CHIBI_WORKSPACE_LOCK_FD'):
            env.pop(name, None)
        if fd is not None:
            env['CHIBI_WORKSPACE_LOCK_FD'] = fd
        env['PATH'] = str(self.bin) + os.pathsep + env['PATH']
        return subprocess.run(['make', '-j2', *options, *goals, 'NATIVE_TOOLCHAIN_FLAGS=',
                               'LOCAL_CMAKE_FLAGS=-DFIXTURE=value with spaces',
                               'BUILD_TARGET=chibi', 'JOBS=3', *variables,
                               f'QUIET={sys.executable} tools/quiet.py'],
                              cwd=self.root, env=env, capture_output=True, text=True,
                              timeout=60)

    def calls(self):
        return [json.loads(line) for line in (self.root / 'calls.jsonl').read_text().splitlines()]

    def test_multiple_goals_delegate_once_and_hold_lock_through_runtime(self):
        result = self.make(['build', 'run'], variables=('BUILD_DIR=build with spaces',
                                                       'RELEASE_BUILD_DIR=release with spaces'))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / 'outer-runs').read_text(), 'run\n')
        calls = self.calls()
        self.assertTrue(all(call['locked'] for call in calls))
        self.assertTrue(any(call['args'][0] == 'launch' for call in calls))
        config = next(call for call in calls
                      if call['args'][:2] == ['configure', 'build with spaces'])
        self.assertEqual(config['args'][-3:], ['-DFIXTURE=value', 'with', 'spaces'])
        build = next(call for call in calls
                     if call['command'] == 'cmake'
                     and call['args'][:2] == ['--build', 'build with spaces'])
        self.assertEqual(build['args'][-2:], ['--parallel', '3'])
        self.assertEqual(build['args'][1], 'build with spaces')
        self.assertTrue(any(call['args'][:2] == ['--build', 'release with spaces']
                            for call in calls))
        self.assertNotIn('jobserver unavailable', result.stderr)
        self.assertNotIn('forcing -j', result.stderr)

    def test_verbose_commands_keep_workspace_lock(self):
        result = self.make(['build'], variables=('VERBOSE=1',))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(all(call['locked'] for call in self.calls()))

    def test_default_goal_is_wrapped_once(self):
        result = self.make([])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / 'outer-runs').read_text(), 'run\n')
        self.assertTrue(all(call['locked'] for call in self.calls()))

    def test_unmanaged_checkout_has_no_wrapper_process(self):
        result = self.make(['build-release'], managed=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.root / 'outer-runs').exists())
        self.assertTrue(all(not call['locked'] for call in self.calls()))

    def test_dry_run_delegates_once_without_building(self):
        result = self.make(['build-release'], options=('-n',))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / 'outer-runs').read_text(), 'run\n')
        self.assertFalse((self.root / 'calls.jsonl').exists())
        self.assertFalse((self.root / '.chibi/logs').exists())
        self.assertIn('cmake --build', result.stdout)

    def test_query_reports_phony_goal_pending_without_running(self):
        result = self.make(['build'], options=('-q',))
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertFalse((self.root / 'outer-runs').exists())
        self.assertFalse((self.root / 'calls.jsonl').exists())

    def test_forged_fd_fails_before_build(self):
        result = self.make(['build'], fd='99')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Invalid inherited workspace lock', result.stderr)
        self.assertFalse((self.root / 'calls.jsonl').exists())
        self.assertFalse((self.root / 'outer-runs').exists())

    def test_registration_precedes_build_directory_creation(self):
        tools = self.root / 'tools'
        shutil.copyfile(ROOT / 'tools/configure.sh', tools / 'configure.sh')
        (self.root / '.chibi-workspace.json').write_text('{}')
        (tools / 'workspace_retirement.py').write_text(
            'import pathlib, sys\n'
            'assert sys.argv[1:] == ["register", "fresh-build"]\n'
            'assert not pathlib.Path("fresh-build").exists()\n'
            'pathlib.Path("registered").touch()\n'
            'sys.exit(19)\n')
        result = subprocess.run(['sh', 'tools/configure.sh', 'native', 'fresh-build'],
                                cwd=self.root, capture_output=True, text=True)
        self.assertEqual(result.returncode, 19)
        self.assertTrue((self.root / 'registered').exists())
        self.assertFalse((self.root / 'fresh-build').exists())


class NativeWorkspaceMake(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.repo = self.root / 'repo'
        self.repo.mkdir()
        tools = self.repo / 'tools'
        tools.mkdir()
        shutil.copyfile(ROOT / 'Makefile', self.repo / 'Makefile')
        for name in ('workspaces.py', 'workspace_retirement.py', 'quiet.py'):
            shutil.copyfile(ROOT / 'tools' / name, tools / name)
        (tools / 'configure.sh').write_text('# workspace_retirement.py registration fixture\n')
        (tools / 'configure_native.sh').write_text('fixture-tool configure "$@"\n')
        self.bin = self.repo / 'bin'
        self.bin.mkdir()
        tool = TOOL.replace('PYTHON', sys.executable).replace(
            "Path('.fixture-workspace.lock')",
            "(Path(json.loads(Path('.chibi-workspace.json').read_text())['state_home']) / "
            "'locks' / (json.loads(Path('.chibi-workspace.json').read_text())['id'] + '.lock'))")
        for name in ('cmake', 'fixture-tool'):
            path = self.bin / name
            path.write_text(tool)
            path.chmod(0o755)
        self.env = os.environ.copy()
        for name in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES', 'CHIBI_WORKSPACE_LOCK_FD'):
            self.env.pop(name, None)
        self.state = self.root / 'state'
        self.env.update(CHIBI_WORKSPACE_HOME=str(self.state),
                        PATH=str(self.bin) + os.pathsep + self.env['PATH'])
        self.git('init', '-q')
        self.git('config', 'user.email', 'test@example.invalid')
        self.git('config', 'user.name', 'Test')
        self.git('add', '.')
        self.git('commit', '-qm', 'fixture')
        self.checkout = self.root / 'native'
        self.git('worktree', 'add', '-qb', 'native', str(self.checkout))

    def git(self, *args):
        return subprocess.run(['git', '-C', str(self.repo), *args], check=True,
                              capture_output=True, text=True, env=self.env)

    def make(self, *args):
        return subprocess.run(['make', *args, 'NATIVE_TOOLCHAIN_FLAGS=',
                               'LOCAL_CMAKE_FLAGS=', 'BUILD_TARGET=chibi',
                               f'QUIET={sys.executable} tools/quiet.py'],
                              cwd=self.checkout, env=self.env, capture_output=True,
                              text=True, timeout=60)

    def register_repo(self):
        result = subprocess.run([sys.executable, str(self.repo / 'tools/workspaces.py'),
                                 'init', '--repo', str(self.repo)], env=self.env,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_native_setup_build_enrolls_and_locks_before_any_session_hook(self):
        self.register_repo()
        self.assertFalse((self.checkout / '.chibi-workspace.json').exists())
        result = self.make('build')
        self.assertEqual(result.returncode, 0, result.stderr)
        marker = json.loads((self.checkout / '.chibi-workspace.json').read_text())
        registry = json.loads((self.state / 'registry.json').read_text())
        entry = registry['workspaces'][marker['id']]
        self.assertEqual(entry['lifecycle'], 'app')
        self.assertEqual(entry['status'], 'idle')
        self.assertEqual(entry['sessions'], {})
        self.assertTrue(entry['build_integration'])
        calls = [json.loads(line) for line in
                 (self.checkout / 'calls.jsonl').read_text().splitlines()]
        self.assertTrue(calls)
        self.assertTrue(all(call['locked'] for call in calls))
        self.assertNotIn('workspaces:', result.stdout)

    def test_uninstalled_native_checkout_creates_no_registry_or_marker(self):
        result = self.make('help')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.state.exists())
        self.assertFalse((self.checkout / '.chibi-workspace.json').exists())

    def test_enrollment_error_stops_before_make_commands(self):
        self.register_repo()
        registry = json.loads((self.state / 'registry.json').read_text())
        registry['version'] = -1
        (self.state / 'registry.json').write_text(json.dumps(registry))
        result = self.make('build')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Workspace enrollment failed', result.stderr)
        self.assertFalse((self.checkout / 'calls.jsonl').exists())
        self.assertFalse((self.checkout / 'build').exists())

    def test_known_registry_with_unknown_repo_does_not_enroll(self):
        other = self.root / 'other'
        other.mkdir()
        subprocess.run(['git', '-C', str(other), 'init', '-q'], check=True)
        result = subprocess.run([sys.executable, str(self.repo / 'tools/workspaces.py'),
                                 'init', '--repo', str(other)], env=self.env,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = self.make('help')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.checkout / '.chibi-workspace.json').exists())
        registry = json.loads((self.state / 'registry.json').read_text())
        self.assertEqual(registry['workspaces'], {})



if __name__ == '__main__':
    unittest.main()
