#!/usr/bin/env python3
"""Exercise retirement against real temporary Git and Ninja build fixtures."""
import fcntl
import json
import os
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import workspace_retirement as retirement
import workspaces


class RetirementTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name).resolve() / 'repo'
        self.root.mkdir()
        self.state = Path(self.temp.name).resolve() / 'state'
        self.environment = patch.dict(os.environ, {'CHIBI_WORKSPACE_HOME': str(self.state)})
        self.environment.start()
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        self.entry = {'id': 'test', 'repo': 'repo', 'path': str(self.root),
                      'branch': 'cwaldner/test', 'status': 'idle', 'owner': None,
                      'last_used': 0, 'created': 0}
        info = self.root.stat()
        marker = {'version': 1, 'id': 'test', 'state_home': str(self.state),
                  'path': str(self.root), 'device': info.st_dev, 'inode': info.st_ino}
        (self.root / workspaces.MARKER).write_text(json.dumps(marker))
        with workspaces.registry_lock() as registry:
            registry['workspaces']['test'] = self.entry
        (self.state / 'locks').mkdir()
        self.lock = (self.state / 'locks/test.lock').open('a+')
        fcntl.flock(self.lock, fcntl.LOCK_EX)
        self.fd_env = patch.dict(os.environ, {'CHIBI_WORKSPACE_LOCK_FD': str(self.lock.fileno())})
        self.fd_env.start()
        self.build = self.root / 'build'

    def tearDown(self):
        self.fd_env.stop()
        self.lock.close()
        self.environment.stop()
        self.temp.cleanup()

    def fixture(self):
        self.assertTrue(retirement.register(self.build))
        (self.build / 'CMakeCache.txt').write_text(f'CMAKE_HOME_DIRECTORY:INTERNAL={self.root}\n')
        (self.build / 'build.ninja').write_text(
            'rule generate\n  command = printf generated > $out\n'
            'build engine.o: generate\n'
            'build libengine.a: generate\n'
            'build program.js: generate\n'
            'build all: phony engine.o libengine.a program.js\n')
        subprocess.run(['ninja', '-C', str(self.build)], check=True, capture_output=True)
        self.assertEqual(retirement.capture_workspace(self.root, self.lock.fileno()), 3)
        with workspaces.registry_lock() as registry:
            registry['builds'][str(self.build)]['last_used'] = time.time() - 10 * 86400

    def sweep(self, apply=True):
        fcntl.flock(self.lock, fcntl.LOCK_UN)
        with patch.object(retirement, 'process_idle', return_value=True):
            return retirement.sweep(apply=apply, keep=0, idle_days=7)

    def test_requires_lease_and_never_adopts_existing_build(self):
        fcntl.flock(self.lock, fcntl.LOCK_UN)
        self.assertFalse(retirement.register(self.build))
        self.assertFalse(self.build.exists())
        fcntl.flock(self.lock, fcntl.LOCK_EX)
        self.build.mkdir()
        self.assertFalse(retirement.register(self.build))
        self.assertFalse((self.build / retirement.MARKER).exists())

    def test_unknown_ignored_and_untracked_files_survive(self):
        self.fixture()
        (self.root / '.gitignore').write_text('build/\n')
        (self.build / 'authored.mesh').write_text('precious')
        (self.build / 'unknown.o').write_text('precious')
        result = self.sweep()
        self.assertEqual(result[0]['files'], 3)
        self.assertEqual((self.build / 'authored.mesh').read_text(), 'precious')
        self.assertEqual((self.build / 'unknown.o').read_text(), 'precious')
        self.assertTrue((self.build / 'build.ninja').exists())
        self.assertTrue((self.build / 'CMakeCache.txt').exists())

    def test_dry_run_does_not_remove_outputs(self):
        self.fixture()
        self.assertEqual(self.sweep(False)[0]['files'], 3)
        self.assertTrue((self.build / 'engine.o').exists())

    def test_busy_lease_owner_and_reservation_protect_build(self):
        self.fixture()
        with patch.object(retirement, 'process_idle', return_value=True):
            self.assertEqual(retirement.sweep(True, 0, 7), [])
        fcntl.flock(self.lock, fcntl.LOCK_UN)
        for key, value in [('owner', {'session_id': 'live'}), ('reserved_app', 'codex')]:
            with workspaces.registry_lock() as registry:
                registry['workspaces']['test'][key] = value
            with patch.object(retirement, 'process_idle', return_value=True):
                self.assertEqual(retirement.sweep(True, 0, 7), [])
            with workspaces.registry_lock() as registry:
                registry['workspaces']['test'].pop(key)
        self.assertTrue((self.build / 'engine.o').exists())

    def test_modified_replaced_and_symlink_outputs_survive(self):
        self.fixture()
        (self.build / 'engine.o').write_text('edited')
        archive = self.build / 'libengine.a'
        archive.unlink()
        archive.write_text('generated')
        script = self.build / 'program.js'
        script.unlink()
        secret = self.root / 'secret.js'
        secret.write_text('precious')
        script.symlink_to(secret)
        self.assertEqual(self.sweep(), [])
        self.assertEqual(secret.read_text(), 'precious')
        self.assertTrue(script.is_symlink())

    def test_tracked_output_and_nested_repository_are_protected(self):
        self.fixture()
        subprocess.run(['git', '-C', str(self.root), 'add', 'build/engine.o'], check=True)
        self.assertEqual(self.sweep()[0]['files'], 2)
        self.assertTrue((self.build / 'engine.o').exists())
        nested = self.build / 'nested'
        subprocess.run(['git', 'init', '-q', str(nested)], check=True)
        self.assertIn('repository', self.sweep()[0]['skipped'])

    def test_changed_build_identity_and_foreign_cache_are_protected(self):
        self.fixture()
        (self.build / 'CMakeCache.txt').write_text('CMAKE_HOME_DIRECTORY:INTERNAL=/tmp\n')
        self.assertIn('foreign', self.sweep()[0]['skipped'])
        original = self.root / 'original-build'
        self.build.rename(original)
        self.build.mkdir()
        self.assertIn('ownership', self.sweep()[0]['skipped'])
        self.assertTrue((original / 'engine.o').exists())

    def test_activity_scan_failure_and_recent_retention_protect_outputs(self):
        self.fixture()
        fcntl.flock(self.lock, fcntl.LOCK_UN)
        with patch.object(retirement, 'process_idle', return_value=False):
            self.assertEqual(retirement.sweep(True, 0, 7), [])
        with patch.object(retirement, 'process_idle', return_value=True):
            self.assertEqual(retirement.sweep(True, 1, 7), [])
        with patch.object(retirement.shutil, 'which', return_value=None):
            self.assertFalse(retirement.process_idle(self.root))

    def test_registration_rejects_symlinked_build_parent(self):
        outside = Path(self.temp.name) / 'outside'
        outside.mkdir()
        (self.root / 'linked').symlink_to(outside, target_is_directory=True)
        self.assertFalse(retirement.register(self.root / 'linked/build'))
        self.assertFalse((outside / 'build').exists())

    def test_unrelated_descriptor_cannot_borrow_somebody_elses_lease(self):
        with (self.state / 'locks/test.lock').open('a+') as unrelated:
            self.assertFalse(retirement.lease_inherited(self.entry, unrelated.fileno()))
        self.assertTrue(retirement.lease_inherited(self.entry, self.lock.fileno()))

    def test_cache_outputs_and_hardlinks_are_preserved(self):
        self.fixture()
        with patch.dict(os.environ, {'CHIBI_DEPENDENCY_PACKAGE_CACHE_DIR': str(self.build)}):
            self.assertEqual(self.sweep(), [])
        alias = self.root / 'object-alias'
        os.link(self.build / 'engine.o', alias)
        self.assertEqual(self.sweep()[0]['files'], 2)
        self.assertEqual(alias.read_text(), 'generated')
        self.assertTrue((self.build / 'engine.o').exists())

    def test_paused_active_workspace_retirement_respects_recent_app_activity(self):
        self.fixture()
        with workspaces.registry_lock() as registry:
            registry['workspaces']['test']['status'] = 'active'
            registry['workspaces']['test']['last_used'] = time.time()
        self.assertEqual(self.sweep(), [])
        with workspaces.registry_lock() as registry:
            registry['workspaces']['test']['last_used'] = 0
        self.assertEqual(self.sweep()[0]['files'], 3)

    def test_expensive_scans_do_not_hold_registry_mutex_and_owner_is_rechecked(self):
        self.fixture()
        fcntl.flock(self.lock, fcntl.LOCK_UN)
        def claimed_during_inspection(path):
            with (self.state / 'registry.lock').open('a+') as probe:
                fcntl.flock(probe, fcntl.LOCK_EX | fcntl.LOCK_NB)
            with workspaces.registry_lock() as registry:
                registry['workspaces']['test']['owner'] = {'session_id': 'new'}
            return True
        with patch.object(retirement, 'process_idle', side_effect=claimed_during_inspection):
            self.assertEqual(retirement.sweep(True, 0, 7), [])
        self.assertTrue((self.build / 'engine.o').exists())

    def test_foreign_checkout_referencing_outputs_prevents_retirement(self):
        self.fixture()
        (self.root / 'tracked').write_text('initial')
        subprocess.run(['git', '-C', str(self.root), 'add', 'tracked'], check=True)
        subprocess.run(['git', '-C', str(self.root), '-c', 'user.name=Test',
                        '-c', 'user.email=test@example.invalid', 'commit', '-qm', 'fixture'], check=True)
        other = self.root.parent / 'other'
        subprocess.run(['git', '-C', str(self.root), 'worktree', 'add', '--detach',
                        str(other)], check=True, capture_output=True)
        cache = other / 'custom-cache'
        cache.mkdir()
        (cache / 'CMakeCache.txt').write_text(f'EXTERNAL_CONTROLLER:FILEPATH={self.build}/engine.o\n')
        self.assertIn('referenced', self.sweep()[0]['skipped'])
        self.assertTrue((self.build / 'engine.o').exists())
        shutil.rmtree(other)
        # Keep its stale Git registration; absent paths need no destructive
        # pruning and must not block retirement everywhere else.
        self.assertEqual(self.sweep()[0]['files'], 3)


if __name__ == '__main__':
    unittest.main()
