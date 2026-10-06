"""CPU-only regression checks for the temporary command-level quarantine."""
import os
import signal
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import test_policy as policy


class TestPolicy(unittest.TestCase):
    def test_admission_is_exact_and_does_not_split_suites(self):
        for arguments in policy.NATIVE:
            self.assertTrue(policy.admitted(arguments))
        for arguments in [(), ('--studio-documents',), ('--studio-panels',),
                          ('--studio-shell',), ('--studio-documents', 'tabs', 'extra'),
                          ('--graph-optimize', '--studio-documents'),
                          ('--bench-arena-scene-scale', '2048', '240', '1'),
                          ('--unmeasured',), ('--model-sampled-gpu',)]:
            self.assertFalse(policy.admitted(arguments))

    def test_exit_status_and_timeout_are_failures(self):
        self.assertEqual(policy.run_commands([[sys.executable, '-c', 'pass']]), 0)
        self.assertEqual(policy.run_commands([[sys.executable, '-c', 'raise SystemExit(7)']]), 7)
        self.assertEqual(policy.run_commands([[sys.executable, '-c', 'import time; time.sleep(.8)']], .1), 124)
        with self.assertRaises(ValueError):
            policy.run_commands([], 61)

    def test_aggregate_uses_one_deadline_and_stops_before_next_command(self):
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory) / 'ran'
            command = [sys.executable, '-c', 'import time; time.sleep(.18)']
            last = [sys.executable, '-c', f'from pathlib import Path; Path({str(marker)!r}).touch()']
            self.assertEqual(policy.run_commands([command, command, last], .3), 124)
            self.assertFalse(marker.exists())

    def test_timeout_kills_descendants(self):
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory) / 'escaped'
            descendant = f'import time; from pathlib import Path; time.sleep(.4); Path({str(marker)!r}).touch()'
            parent = f'import subprocess,sys,time; subprocess.Popen([sys.executable,"-c",{descendant!r}]); time.sleep(.8)'
            self.assertEqual(policy.run_commands([[sys.executable, '-c', parent]], .15), 124)
            time.sleep(.5)
            self.assertFalse(marker.exists())

    def test_nested_wrapper_and_setsid_child_share_deadline_and_are_reaped(self):
        with tempfile.TemporaryDirectory() as directory:
            started = Path(directory) / 'started'
            escaped = Path(directory) / 'escaped'
            grandchild = (f'import os,time; from pathlib import Path; '
                          f'Path({str(started)!r}).write_text(os.environ["CHIBI_TEST_DEADLINE"]); '
                          f'time.sleep(.5); Path({str(escaped)!r}).touch()')
            parent = (f'import subprocess,sys,time; '
                      f'subprocess.Popen([sys.executable,"-c",{grandchild!r}], start_new_session=True); '
                      'time.sleep(.8)')
            nested = (f'import sys; sys.path.insert(0,{str(ROOT / "tools")!r}); '
                      f'import test_policy; sys.exit(test_policy.run_commands('
                      f'[[sys.executable,"-c",{parent!r}]], 60))')
            before = time.monotonic()
            self.assertEqual(policy.run_commands([[sys.executable, '-c', nested]], .35), 124)
            self.assertTrue(started.exists(), 'nested setsid child must actually start')
            self.assertLessEqual(float(started.read_text()), before + .36)
            time.sleep(.3)
            self.assertFalse(escaped.exists())

    def test_interruption_reaps_setsid_grandchild(self):
        with tempfile.TemporaryDirectory() as directory:
            started = Path(directory) / 'started'
            escaped = Path(directory) / 'escaped'
            grandchild = (f'from pathlib import Path; import time; '
                          f'Path({str(started)!r}).touch(); time.sleep(.5); '
                          f'Path({str(escaped)!r}).touch()')
            parent = (f'import subprocess,sys,time; '
                      f'subprocess.Popen([sys.executable,"-c",{grandchild!r}], start_new_session=True); '
                      'time.sleep(.8)')
            wrapper = (f'import sys,signal; sys.path.insert(0,{str(ROOT / "tools")!r}); '
                       f'import test_policy; signal.signal(signal.SIGTERM,test_policy.terminate); '
                       f'test_policy.run_commands([[sys.executable,"-c",{parent!r}]], .8)')
            process = subprocess.Popen([sys.executable, '-c', wrapper], stdout=subprocess.DEVNULL,
                                       stderr=subprocess.DEVNULL)
            try:
                until = time.monotonic() + .35
                while not started.exists() and time.monotonic() < until:
                    time.sleep(.005)
                self.assertTrue(started.exists())
                process.send_signal(signal.SIGTERM)
                self.assertNotEqual(process.wait(timeout=.3), 0)
                time.sleep(.5)
                self.assertFalse(escaped.exists())
            finally:
                if process.poll() is None:
                    process.terminate()
                process.wait(timeout=.3)

    def test_expired_inherited_deadline_never_launches(self):
        with patch.dict(os.environ, {policy.DEADLINE: str(time.monotonic() - 1)}), \
                patch.object(policy.subprocess, 'Popen') as spawn:
            self.assertEqual(policy.run_commands([['unused']]), 124)
            spawn.assert_not_called()

    def test_setsid_orphan_is_reaped_after_parent_exits_normally(self):
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory) / 'escaped'
            descendant = f'import time; from pathlib import Path; time.sleep(.2); Path({str(marker)!r}).touch()'
            parent = f'import subprocess,sys; subprocess.Popen([sys.executable,"-c",{descendant!r}], start_new_session=True)'
            self.assertEqual(policy.run_commands([[sys.executable, '-c', parent]]), 0)
            time.sleep(.25)
            self.assertFalse(marker.exists())

    def test_unsupported_containment_fails_before_launch(self):
        with patch.object(policy.sys, 'platform', 'unsupported'), \
                patch.object(policy.subprocess, 'Popen') as spawn:
            with self.assertRaisesRegex(RuntimeError, 'subreaper'):
                policy.supervise([['unused']])
            spawn.assert_not_called()

    def test_disabled_cli_never_starts_binary_even_with_hardware_opt_in(self):
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory) / 'ran'
            binary = Path(directory) / 'fake-suite'
            binary.write_text(f'#!/bin/sh\ntouch "{marker}"\n')
            binary.chmod(0o755)
            result = subprocess.run([sys.executable, 'tools/test_policy.py', 'native',
                                     str(binary), '--studio-documents'], cwd=ROOT,
                                    env=dict(os.environ, CHIBI_EXECUTION_PROFILE='hardware', CHIBI_ALLOW_FULL_GPU='1'),
                                    capture_output=True, text=True, timeout=3)
            self.assertEqual(result.returncode, 2)
            self.assertIn('DISABLED', result.stdout + result.stderr)
            self.assertFalse(marker.exists())

    def test_make_quarantine_and_short_wiring(self):
        # Dry runs exercise the real Make parser without enrollment, builds or GPUs.
        import re
        source = (ROOT / 'Makefile').read_text()
        targets = ['test-full', 'test-slow', 'test-render', 'test-tools', 'test-package',
                   'test-compiler-parity', 'benchmark', 'profile-compare', 'agent-gpu-test']
        targets += [target for target in re.findall(r'^(test-tool-[\w-]+):', source, re.M)
                    if target.removeprefix('test-tool-') not in policy.TOOLS]
        for target in targets:
            recipe = source.split(target + ':', 1)[1].split('\n\n', 1)[0]
            self.assertEqual(recipe.splitlines()[0], '', target)
            self.assertIn('test_policy.py disabled', recipe.splitlines()[1], target)
        env = dict(os.environ, CHIBI_CLOUD='0')
        result = subprocess.run(['make', '-n', 'test', 'CHIBI_WORKSPACE_INNER=1'], cwd=ROOT,
                                env=env, capture_output=True, text=True, timeout=3)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('test_policy.py short', result.stdout)
        self.assertNotIn('cmake --build', result.stdout)
        result = subprocess.run(['make', '-n', 'test', 'test-fast'], cwd=ROOT,
                                env=env, capture_output=True, text=True, timeout=3)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('aggregate is unadmitted', result.stderr)

    def test_agent_exit_after_deadline_cannot_pass_between_polls(self):
        import agent
        child = Mock(pid=12345, returncode=0)
        child.poll.return_value = 0
        with patch.dict(os.environ, {}, clear=True), \
                patch.object(agent.subprocess, 'Popen', return_value=child), \
                patch.object(agent.os, 'killpg'), \
                patch.object(agent.time, 'monotonic', side_effect=[0, 61]):
            with self.assertRaisesRegex(RuntimeError, 'time_budget_exceeded'):
                agent.run(['unused'], 60)
        child.wait.assert_called_once()

    def test_make_and_agent_refuse_unmeasured_before_build_or_probe(self):
        env = dict(os.environ, CHIBI_CLOUD='0', CHIBI_EXECUTION_PROFILE='hardware', CHIBI_ALLOW_FULL_GPU='1')
        for command in [['make', 'test-fast', 'TEST_ARGS=--studio-documents'],
                        [sys.executable, 'tools/agent.py', 'test', '--', '--runtime-execution']]:
            result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=3)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('DISABLED', result.stdout + result.stderr)
            self.assertNotIn('configure-native', result.stdout)
            self.assertNotIn('cmake --build', result.stdout)

    def test_ci_does_not_schedule_quarantined_suites(self):
        ci = (ROOT / '.github/workflows/ci.yml').read_text()
        self.assertNotIn('run: make test\n', ci)
        self.assertNotIn('make test-render', ci)
        self.assertNotIn('--target check-behavior-backend', ci)
        self.assertIn('test_policy.py native build/test_suite', ci)
        self.assertIn('GITHUB_STEP_SUMMARY', ci)
        self.assertIn('name: Native partial coverage (${{ matrix.build_type }})', ci)
        self.assertNotIn('name: Native tests (', ci)


if __name__ == '__main__':
    unittest.main()
