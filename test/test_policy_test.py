"""CPU-only regression checks for the temporary command-level quarantine."""
import os
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
        self.assertEqual(policy.run_commands([[sys.executable, '-c', 'import time; time.sleep(3)']], .1), 124)
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
            parent = f'import subprocess,sys,time; subprocess.Popen([sys.executable,"-c",{descendant!r}]); time.sleep(3)'
            self.assertEqual(policy.run_commands([[sys.executable, '-c', parent]], .15), 124)
            time.sleep(.5)
            self.assertFalse(marker.exists())

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
        targets += re.findall(r'^(test-tool-[\w-]+):', source, re.M)
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
        with patch.object(agent.subprocess, 'Popen', return_value=child), \
                patch.object(agent.os, 'killpg'), \
                patch.object(agent.time, 'monotonic', side_effect=[0, 61]):
            with self.assertRaisesRegex(RuntimeError, 'time_budget_exceeded'):
                agent.run(['unused'], 60)
        child.wait.assert_called_once()

    def test_make_and_agent_refuse_unmeasured_before_build_or_probe(self):
        env = dict(os.environ, CHIBI_CLOUD='0', CHIBI_EXECUTION_PROFILE='hardware', CHIBI_ALLOW_FULL_GPU='1')
        for command in [['make', 'test-fast', 'TEST_ARGS=--studio-documents'],
                        [sys.executable, 'tools/agent.py', 'test', '--', '--compiled-behaviors']]:
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


if __name__ == '__main__':
    unittest.main()
