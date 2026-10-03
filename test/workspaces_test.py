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
