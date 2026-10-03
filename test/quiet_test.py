"""Check output suppression, failure visibility, and bounded log retention."""
import fcntl
import os
import signal
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import quiet


class QuietOutput(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.logs = Path(self.temporary.name)

    def run_phase(self, script, verbose=False):
        env = dict(os.environ, VERBOSE='1' if verbose else '0')
        env.pop('CHIBI_WORKSPACE_LOCK_FD', None)
        return subprocess.run([sys.executable, str(ROOT / 'tools/quiet.py'),
                               '--log-dir', str(self.logs), 'fixture', '--',
                               sys.executable, '-c', script],
                              capture_output=True, text=True, env=env)

    def test_success_saves_both_streams_without_protocol_stdout(self):
        result = self.run_phase('import sys; print("details"); print("warnings", file=sys.stderr)')
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, '')
        self.assertNotIn('details', result.stderr)
        log = next(self.logs.glob('*.log')).read_text()
        self.assertIn('details', log)
        self.assertIn('warnings', log)
        self.assertIn('ok', result.stderr)

    def test_failure_preserves_status_and_diagnostics_with_noisy_tail(self):
        result = self.run_phase('import sys; print("error: broken compilation"); '
                                'print("noise" * 10000); sys.exit(19)')
        self.assertEqual(result.returncode, 19)
        self.assertIn('error: broken compilation', result.stderr)
        self.assertLess(len(result.stderr), 9000)
        self.assertGreater(next(self.logs.glob('*.log')).stat().st_size, 49000)

    def test_signal_exit_is_preserved(self):
        result = self.run_phase('import os, signal; os.kill(os.getpid(), signal.SIGTERM)')
        self.assertEqual(result.returncode, -signal.SIGTERM)
        self.assertIn('FAILED', result.stderr)

    def test_retention_enforces_total_size(self):
        for number in range(3):
            path = self.logs / f'{number}.log'
            with path.open('wb') as log:
                log.truncate(quiet.MAX_LOG_BYTES // 2)
            os.utime(path, (number, number))
        current = self.logs / 'current.log'
        current.write_text('new output')
        quiet.prune(self.logs, current)
        self.assertFalse((self.logs / '0.log').exists())
        self.assertTrue((self.logs / '2.log').exists())

    def test_verbose_streams_and_saves_output(self):
        result = self.run_phase('print("full details")', verbose=True)
        self.assertIn('full details', result.stderr)
        self.assertIn('full details', next(self.logs.glob('*.log')).read_text())

    def test_killed_wrapper_does_not_protect_stale_log_forever(self):
        result = self.run_phase('import os, signal; '
                                'os.kill(os.getppid(), signal.SIGKILL)')
        self.assertEqual(result.returncode, -signal.SIGKILL)
        stale = next(self.logs.glob('*.log'))
        self.assertTrue(stale.with_suffix('.running').exists())
        os.utime(stale, (0, 0))
        for number in range(quiet.MAX_LOGS):
            path = self.logs / f'completed-{number}.log'
            path.write_text('completed output')
            os.utime(path, (number + 1, number + 1))
        current = self.logs / 'current.log'
        current.write_text('current output')
        quiet.prune(self.logs, current)
        self.assertFalse(stale.with_suffix('.running').exists())
        self.assertFalse(stale.exists())
        self.assertLessEqual(len(list(self.logs.glob('*.log'))), quiet.MAX_LOGS)

    def test_retention_preserves_active_logs_and_bounds_completed_logs(self):
        for number in range(30):
            path = self.logs / f'{number}.log'
            path.write_text('old output')
            os.utime(path, (number, number))
        active = self.logs / '0.log'
        lock = active.with_suffix('.running').open('wb')
        self.addCleanup(lock.close)
        fcntl.flock(lock, fcntl.LOCK_EX)
        current = self.logs / 'current.log'
        current.write_text('new output')
        quiet.prune(self.logs, current)
        self.assertTrue(active.exists())
        self.assertTrue(current.exists())
        self.assertLessEqual(len(list(self.logs.glob('*.log'))), quiet.MAX_LOGS + 1)
        self.assertFalse((self.logs / '1.log').exists())


if __name__ == '__main__':
    unittest.main()
