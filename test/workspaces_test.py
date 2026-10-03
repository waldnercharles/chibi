#!/usr/bin/env python3
"""Exercise workspace ownership and process locks against real temporary repositories."""
import fcntl
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

SCRIPT = Path(__file__).resolve().parents[1] / 'tools' / 'workspaces.py'
spec = importlib.util.spec_from_file_location('workspaces', SCRIPT)
w = importlib.util.module_from_spec(spec)
spec.loader.exec_module(w)


class WorkspacesTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.repo = self.root / 'repo'
        self.repo.mkdir()
        self.env = mock.patch.dict(os.environ, {'CHIBI_WORKSPACE_HOME': str(self.root / 'state')})
        self.env.start()
        os.environ.pop('CHIBI_WORKSPACE_LOCK_FD', None)
        self.addCleanup(self.env.stop)
        os.environ.pop('CHIBI_WORKSPACE_LOCK_FD', None)
        for command in (['init', '-q'], ['config', 'user.email', 'test@example.invalid'],
                        ['config', 'user.name', 'Test']):
            subprocess.run(['git', '-C', str(self.repo), *command], check=True)
        (self.repo / 'file.txt').write_text('source\n')
        subprocess.run(['git', '-C', str(self.repo), 'add', 'file.txt'], check=True)
        subprocess.run(['git', '-C', str(self.repo), 'commit', '-qm', 'initial'], check=True)

    def acquire(self, task='task', app=None):
        return w.acquire(task, self.repo, app)

    def event(self, entry, session='one', event='SessionStart', app='codex'):
        return w.hook(app, {'session_id': session, 'cwd': entry['path'], 'hook_event_name': event})

    def cli(self, *args, cwd=None, payload=None, **kwargs):
        return subprocess.run([sys.executable, str(SCRIPT), *args], cwd=cwd,
                              input=json.dumps(payload) if payload else None,
                              text=True, capture_output=True, **kwargs)

    def native_event(self, path, session='native-one', event='SessionStart', app='codex'):
        w.hook(app, {'session_id': session, 'cwd': str(path), 'hook_event_name': event})

    def register_repo(self):
        with w.registry_lock() as registry:
            w.init_repository(registry, self.repo)

    def test_native_primary_and_multiple_sessions_preserve_source(self):
        self.register_repo()
        (self.repo / 'file.txt').write_text('dirty source\n')
        build = self.repo / 'build'
        build.mkdir()
        (build / 'CMakeCache.txt').write_text('existing output\n')
        before = w.git(self.repo, 'status', '--porcelain')
        branch = w.git(self.repo, 'branch', '--show-current')
        self.native_event(self.repo)
        self.native_event(self.repo, session='other', app='claude')
        with w.registry_lock() as registry:
            entry = w.workspace_for_path(registry, self.repo)
            self.assertEqual(entry['lifecycle'], 'app')
            self.assertEqual(len(entry['sessions']), 2)
            self.assertFalse(entry['build_integration'])
            self.assertFalse(registry.get('builds'))
        self.native_event(self.repo, event='Stop')
        with w.registry_lock() as registry:
            self.assertEqual(w.workspace_for_path(registry, self.repo)['status'], 'active')
        self.native_event(self.repo, session='other', event='Stop', app='claude')
        with w.registry_lock() as registry:
            self.assertEqual(w.workspace_for_path(registry, self.repo)['status'], 'idle')
        self.assertEqual(w.git(self.repo, 'status', '--porcelain'), before)
        self.assertEqual(w.git(self.repo, 'branch', '--show-current'), branch)
        self.assertEqual((build / 'CMakeCache.txt').read_text(), 'existing output\n')

    def test_detached_native_and_global_session_end_after_movement(self):
        self.register_repo()
        checkout = self.root / 'native'
        subprocess.run(['git', '-C', str(self.repo), 'worktree', 'add', '--detach', str(checkout)],
                       check=True, capture_output=True)
        self.native_event(self.repo)
        self.native_event(checkout, event='PreToolUse')
        with w.registry_lock() as registry:
            self.assertIsNone(w.workspace_for_path(registry, checkout)['branch'])
            self.assertEqual(len(registry['workspaces']), 2)
        self.native_event(checkout, event='SessionEnd')
        with w.registry_lock() as registry:
            self.assertTrue(all(not entry['sessions'] for entry in registry['workspaces'].values()))
        self.assertEqual(w.git(checkout, 'status', '--porcelain'), '')

    def test_native_recreation_has_new_identity_and_old_build_owner(self):
        self.register_repo()
        checkout = self.root / 'native'
        subprocess.run(['git', '-C', str(self.repo), 'worktree', 'add', '--detach', str(checkout)],
                       check=True, capture_output=True)
        self.native_event(checkout)
        with w.registry_lock() as registry:
            old = dict(w.workspace_for_path(registry, checkout))
            registry['builds'] = {'example': {'workspace_id': old['id']}}
        # Retain old inode to make path recreation deterministic on all filesystems.
        saved = self.root / 'old-checkout'
        subprocess.run(['git', '-C', str(self.repo), 'worktree', 'move', str(checkout), str(saved)],
                       check=True, capture_output=True)
        subprocess.run(['git', '-C', str(self.repo), 'worktree', 'add', '--detach', str(checkout)],
                       check=True, capture_output=True)
        self.native_event(checkout, event='SessionEnd')
        self.native_event(checkout, session='replacement')
        with w.registry_lock() as registry:
            new = w.workspace_for_path(registry, checkout)
            self.assertNotEqual(new['id'], old['id'])
            self.assertEqual(registry['workspaces'][old['id']]['status'], 'retired')
            self.assertEqual(registry['builds']['example']['workspace_id'], old['id'])
        result = self.cli('run', '--', sys.executable, str(SCRIPT), 'validate-lock', cwd=checkout, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        quiet = SCRIPT.with_name('quiet.py')
        result = self.cli('run', '--', sys.executable, str(quiet), 'identity-check', '--',
                          sys.executable, str(SCRIPT), 'validate-lock', cwd=checkout, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.native_event(checkout, event='Stop')
        with w.registry_lock() as registry:
            self.assertEqual(registry['workspaces'][old['id']]['status'], 'retired')

    def test_unknown_nested_repo_and_non_git_hook_are_ignored(self):
        self.register_repo()
        self.native_event(self.repo)
        nested = self.repo / 'other'
        nested.mkdir()
        subprocess.run(['git', '-C', str(nested), 'init', '-q'], check=True)
        self.native_event(nested, event='PreToolUse')
        self.native_event(self.root, event='SessionStart')
        with w.registry_lock() as registry:
            self.assertEqual(len(registry['workspaces']), 1)
        self.assertFalse((nested / w.MARKER).exists())
        with w.registry_lock() as registry:
            self.assertIsNone(w.workspace_for_path(registry, nested))

    def test_discovery_never_holds_registry_mutex(self):
        self.register_repo()
        original_git = w.git
        calls = []

        def checked_git(path, *args):
            with (w.state_home() / 'registry.lock').open('a+') as probe:
                fcntl.flock(probe, fcntl.LOCK_EX | fcntl.LOCK_NB)
            calls.append(args)
            return original_git(path, *args)

        previous = Path.cwd()
        try:
            os.chdir(self.repo)
            with mock.patch.object(w, 'git', side_effect=checked_git):
                self.native_event(self.repo)
                self.assertEqual(w.run_command([sys.executable, '-c', 'pass']), 0)
                with mock.patch.object(sys, 'argv', ['workspaces', 'enroll']):
                    self.assertEqual(w.main(), 0)
            self.assertTrue(calls)
            with mock.patch.object(w, 'git', side_effect=AssertionError('teardown must not invoke Git')):
                self.native_event(self.repo, event='Stop')
                self.native_event(self.repo, event='SessionEnd')
                with w.registry_lock() as registry:
                    self.assertIsNotNone(w.workspace_for_path(registry, self.repo))
        finally:
            os.chdir(previous)

    def test_quiet_enroll_before_session_and_uninstalled_noop(self):
        result = self.cli('enroll', cwd=self.repo, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, '')
        self.assertFalse(w.state_home().exists())
        self.assertFalse((self.repo / w.MARKER).exists())
        self.register_repo()
        result = self.cli('enroll', cwd=self.repo, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, '')
        with w.registry_lock() as registry:
            entry = w.workspace_for_path(registry, self.repo)
            self.assertEqual(entry['status'], 'idle')
            self.assertEqual(entry['sessions'], {})

    def test_native_run_enrolls_before_setup_and_accepts_parallel_sessions(self):
        self.register_repo()
        result = self.cli('run', '--', sys.executable, '-c', 'pass', cwd=self.repo, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        with w.registry_lock() as registry:
            entry = dict(w.workspace_for_path(registry, self.repo))
        with w.workspace_lock(entry):
            self.native_event(self.repo)
            self.native_event(self.repo, session='other')
        with w.registry_lock() as registry:
            self.assertEqual(len(registry['workspaces'][entry['id']]['sessions']), 2)

    def test_stable_paths_branches_and_capacity(self):
        first = self.acquire()
        self.assertEqual(first['path'], self.acquire()['path'])
        for task in ('two', 'three', 'four'):
            self.acquire(task)
        with self.assertRaisesRegex(ValueError, 'limit'):
            self.acquire('five')
        Path(first['path'], 'file.txt').write_text('unfinished\n')
        w.change_status(first['path'], 'finish')
        replacement = self.acquire('five')
        self.assertNotEqual(first['path'], replacement['path'])
        self.assertNotEqual(first['branch'], replacement['branch'])
        self.assertEqual(Path(first['path'], 'file.txt').read_text(), 'unfinished\n')
        with self.assertRaisesRegex(ValueError, 'reopen'):
            self.acquire()
        self.assertEqual(w.git(first['path'], 'status', '--porcelain'), 'M file.txt')

    def test_owner_conflicts_stop_and_revocation(self):
        entry = self.acquire(app='codex')
        self.event(entry)
        self.event(entry, event='Stop')
        with self.assertRaisesRegex(ValueError, 'another active'):
            self.event(entry, session='two')
        with self.assertRaisesRegex(ValueError, 'release'):
            w.change_status(entry['path'], 'finish')
        w.change_status(entry['path'], 'release')
        self.event(entry, session='two', app='claude')
        with self.assertRaisesRegex(ValueError, 'released'):
            self.event(entry)
        # A late ending session cannot release its successor.
        self.event(entry, event='SessionEnd')
        with w.registry_lock() as registry:
            self.assertEqual(registry['workspaces'][entry['id']]['owner']['session_id'], 'two')
        self.event(entry, session='two', event='SessionEnd', app='claude')
        with w.registry_lock() as registry:
            self.assertEqual(registry['workspaces'][entry['id']]['status'], 'idle')
        w.change_status(entry['path'], 'finish')
        with self.assertRaisesRegex(ValueError, 'finished'):
            self.event(entry, session='three')
        w.change_status(entry['path'], 'reopen')
        self.event(entry, session='three')

    def test_reserved_app_protects_launch(self):
        entry = self.acquire(app='claude')
        with self.assertRaisesRegex(ValueError, 'reserved'):
            self.event(entry)
        with self.assertRaisesRegex(ValueError, 'reservation'):
            w.change_status(entry['path'], 'finish')
        w.change_status(entry['path'], 'release')
        self.event(entry)

    def test_registry_rollback_on_failure(self):
        self.acquire()
        before = (w.state_home() / 'registry.json').read_bytes()
        with self.assertRaises(ValueError):
            with w.registry_lock() as registry:
                registry['workspaces'].clear()
                raise ValueError('abort')
        self.assertEqual(before, (w.state_home() / 'registry.json').read_bytes())

    def test_concurrent_acquire_resolves_one_task(self):
        commands = [[sys.executable, str(SCRIPT), 'acquire', 'race', '--repo', str(self.repo)]] * 4
        processes = [subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                     for command in commands]
        results = [process.communicate(timeout=15) for process in processes]
        self.assertTrue(all(process.returncode == 0 for process in processes), results)
        self.assertEqual(len({stdout.strip() for stdout, _ in results}), 1)
        with w.registry_lock() as registry:
            self.assertEqual(len(registry['workspaces']), 1)

    def test_build_lock_and_nested_command(self):
        entry = self.acquire()
        ready = self.root / 'ready'
        child = "import pathlib,time; pathlib.Path(%r).touch(); time.sleep(1)" % str(ready)
        process = subprocess.Popen([sys.executable, str(SCRIPT), 'run', '--', sys.executable, '-c', child],
                                   cwd=entry['path'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        import time
        deadline = time.time() + 5
        while not ready.exists() and process.poll() is None and time.time() < deadline:
            time.sleep(.02)
        self.assertTrue(ready.exists())
        with self.assertRaises(BlockingIOError):
            w.change_status(entry['path'], 'finish')
        with self.assertRaises(BlockingIOError):
            self.event(entry)
        stdout, stderr = process.communicate(timeout=10)
        self.assertEqual(process.returncode, 0, stderr)
        result = self.cli('run', '--', sys.executable, str(SCRIPT), 'run', '--', sys.executable,
                          '-c', 'import sys; sys.exit(7)', cwd=entry['path'], timeout=10)
        self.assertEqual(result.returncode, 7, result.stderr)

    def test_inherited_child_keeps_lock_after_wrapper_death(self):
        entry = self.acquire()
        ready = self.root / 'child-ready'
        done = self.root / 'child-done'
        child = ("import pathlib,time; ready=pathlib.Path(%r); done=pathlib.Path(%r); "
                 "ready.touch(); deadline=time.time()+5\n"
                 "while not done.exists() and time.time()<deadline: time.sleep(.02)" % (str(ready), str(done)))
        process = subprocess.Popen([sys.executable, str(SCRIPT), 'run', '--', sys.executable, '-c', child],
                                   cwd=entry['path'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        import time
        try:
            deadline = time.time() + 5
            while not ready.exists() and process.poll() is None and time.time() < deadline:
                time.sleep(.02)
            self.assertTrue(ready.exists())
            process.kill()
            process.wait(timeout=5)
            for action in ('finish', 'release'):
                with self.assertRaises(BlockingIOError):
                    w.change_status(entry['path'], action)
            with self.assertRaises(BlockingIOError):
                with w.workspace_lock(entry, blocking=False):
                    self.fail('retirement lock became available before inherited child exited')
        finally:
            done.touch()
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)
        deadline = time.time() + 5
        while True:
            try:
                with w.workspace_lock(entry, blocking=False):
                    break
            except BlockingIOError:
                if time.time() >= deadline:
                    self.fail('child failed to release inherited lock')
                time.sleep(.02)
        w.change_status(entry['path'], 'finish')

    def test_claude_hidden_desktop_flag_version_gate(self):
        entry = self.acquire(app='claude')
        fake = self.root / 'fake-claude'
        output = self.root / 'launch-path'
        fake.write_text('#!/usr/bin/env python3\nimport sys,pathlib\n'
                        'if sys.argv[1] == "--help": print("help")\n'
                        'elif sys.argv[1] == "--version": print("2.1.285 (Claude Code)")\n'
                        'elif sys.argv[1] == "--desktop": pathlib.Path(%r).write_text(str(pathlib.Path.cwd()))\n'
                        % str(output))
        fake.chmod(0o755)
        with mock.patch.object(w.shutil, 'which', return_value=str(fake)):
            w.open_workspace(entry, 'claude')
        self.assertEqual(output.read_text(), entry['path'])
        fake.write_text(fake.read_text().replace('2.1.285', '2.1.280'))
        with mock.patch.object(w.shutil, 'which', return_value=str(fake)):
            with self.assertRaisesRegex(ValueError, '2.1.285'):
                w.open_workspace(entry, 'claude')

    def test_jobserver_fds_survive_run(self):
        entry = self.acquire()
        read_fd, write_fd = os.pipe()
        try:
            env = dict(os.environ, MAKEFLAGS='--jobserver-auth=%d,%d -j' % (read_fd, write_fd))
            child = 'import os; os.fstat(%d); os.fstat(%d)' % (read_fd, write_fd)
            result = subprocess.run([sys.executable, str(SCRIPT), 'run', '--', sys.executable, '-c', child],
                                    cwd=entry['path'], env=env, pass_fds=(read_fd, write_fd),
                                    text=True, capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
        finally:
            os.close(read_fd)
            os.close(write_fd)

    def test_fake_lock_and_changed_marker_refused(self):
        entry = self.acquire()
        lock_path = w.state_home() / 'locks' / (entry['id'] + '.lock')
        lock_path.parent.mkdir(exist_ok=True)
        with lock_path.open('a+') as lock:
            with mock.patch.dict(os.environ, {'CHIBI_WORKSPACE_LOCK_FD': str(lock.fileno())}):
                with self.assertRaisesRegex(ValueError, 'invalid inherited'):
                    w.inherited_lock(entry)
                fcntl.flock(lock, fcntl.LOCK_EX)
                self.assertEqual(w.inherited_lock(entry), lock.fileno())
                with lock_path.open('a+') as unrelated:
                    with mock.patch.dict(os.environ, {'CHIBI_WORKSPACE_LOCK_FD': str(unrelated.fileno())}):
                        with self.assertRaisesRegex(ValueError, 'invalid inherited'):
                            w.inherited_lock(entry)
        marker = json.loads(w.marker_path(entry).read_text())
        marker['inode'] += 1
        w.atomic_json(w.marker_path(entry), marker)
        with self.assertRaisesRegex(ValueError, 'identity'):
            self.acquire()

    def test_pretool_denial_schema_and_quiet_success(self):
        entry = self.acquire()
        self.event(entry)
        payload = {'session_id': 'two', 'cwd': entry['path'], 'hook_event_name': 'PreToolUse'}
        denied = self.cli('hook', 'claude', payload=payload)
        self.assertEqual(denied.returncode, 0)
        self.assertEqual(json.loads(denied.stdout)['hookSpecificOutput']['permissionDecision'], 'deny')
        payload['session_id'] = 'one'
        allowed = self.cli('hook', 'codex', payload=payload)
        self.assertEqual(allowed.stdout.strip(), '{}')


if __name__ == '__main__':
    unittest.main()
