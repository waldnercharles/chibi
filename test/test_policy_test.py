"""CPU-only regression checks for the temporary command-level quarantine."""
import os
import json
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

# Allow nested interpreters to start before testing cleanup. Descendants and
# siblings stay alive beyond the timeout; observation outlasts an escaped write.
CLEANUP_TIMEOUT = 2
ESCAPE_DELAY = 3
CLEANUP_OBSERVATION = ESCAPE_DELAY + .1


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
            started = Path(directory) / 'started'
            marker = Path(directory) / 'escaped'
            descendant = (f'import time; from pathlib import Path; Path({str(started)!r}).touch(); '
                          f'time.sleep({ESCAPE_DELAY}); Path({str(marker)!r}).touch()')
            parent = f'import subprocess,sys,time; subprocess.Popen([sys.executable,"-c",{descendant!r}]); time.sleep(6)'
            self.assertEqual(policy.run_commands([[sys.executable, '-c', parent]], CLEANUP_TIMEOUT), 124)
            self.assertTrue(started.exists(), 'descendant must actually start')
            time.sleep(CLEANUP_OBSERVATION)
            self.assertFalse(marker.exists())

    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux subreaper-specific escaped-session guarantee")
    def test_nested_wrapper_and_setsid_child_share_deadline_and_are_reaped(self):
        with tempfile.TemporaryDirectory() as directory:
            started = Path(directory) / 'started'
            escaped = Path(directory) / 'escaped'
            grandchild = (f'import os,time; from pathlib import Path; '
                          f'Path({str(started)!r}).write_text(os.environ["CHIBI_TEST_DEADLINE"]); '
                          f'time.sleep({ESCAPE_DELAY}); Path({str(escaped)!r}).touch()')
            parent = (f'import subprocess,sys,time; '
                      f'subprocess.Popen([sys.executable,"-c",{grandchild!r}], start_new_session=True); '
                      'time.sleep(6)')
            nested = (f'import sys; sys.path.insert(0,{str(ROOT / "tools")!r}); '
                      f'import test_policy; sys.exit(test_policy.run_commands('
                      f'[[sys.executable,"-c",{parent!r}]], 60))')
            before = time.monotonic()
            self.assertEqual(policy.run_commands([[sys.executable, '-c', nested]], CLEANUP_TIMEOUT), 124)
            self.assertTrue(started.exists(), 'nested setsid child must actually start')
            self.assertLessEqual(float(started.read_text()), before + CLEANUP_TIMEOUT + .01)
            time.sleep(CLEANUP_OBSERVATION)
            self.assertFalse(escaped.exists())

    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux subreaper-specific escaped-session guarantee")
    def test_interruption_reaps_setsid_grandchild(self):
        with tempfile.TemporaryDirectory() as directory:
            started = Path(directory) / 'started'
            escaped = Path(directory) / 'escaped'
            grandchild = (f'from pathlib import Path; import time; '
                          f'Path({str(started)!r}).touch(); time.sleep({ESCAPE_DELAY}); '
                          f'Path({str(escaped)!r}).touch()')
            parent = (f'import subprocess,sys,time; '
                      f'subprocess.Popen([sys.executable,"-c",{grandchild!r}], start_new_session=True); '
                      'time.sleep(6)')
            wrapper = (f'import sys,signal; sys.path.insert(0,{str(ROOT / "tools")!r}); '
                       f'import test_policy; signal.signal(signal.SIGTERM,test_policy.terminate); '
                       f'test_policy.run_commands([[sys.executable,"-c",{parent!r}]], 6)')
            process = subprocess.Popen([sys.executable, '-c', wrapper], stdout=subprocess.DEVNULL,
                                       stderr=subprocess.DEVNULL)
            try:
                until = time.monotonic() + 3
                while not started.exists() and time.monotonic() < until:
                    time.sleep(.005)
                self.assertTrue(started.exists())
                process.send_signal(signal.SIGTERM)
                self.assertNotEqual(process.wait(timeout=2), 0)
                time.sleep(CLEANUP_OBSERVATION)
                self.assertFalse(escaped.exists())
            finally:
                if process.poll() is None:
                    process.terminate()
                process.wait(timeout=2)

    def test_expired_inherited_deadline_never_launches(self):
        with patch.dict(os.environ, {policy.DEADLINE: str(time.monotonic() - 1)}), \
                patch.object(policy.subprocess, 'Popen') as spawn:
            self.assertEqual(policy.run_commands([['unused']]), 124)
            spawn.assert_not_called()

    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux subreaper-specific escaped-session guarantee")
    def test_setsid_orphan_is_reaped_after_parent_exits_normally(self):
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory) / 'escaped'
            descendant = f'import time; from pathlib import Path; time.sleep(.2); Path({str(marker)!r}).touch()'
            parent = f'import subprocess,sys; subprocess.Popen([sys.executable,"-c",{descendant!r}], start_new_session=True)'
            self.assertEqual(policy.run_commands([[sys.executable, '-c', parent]]), 0)
            time.sleep(.25)
            self.assertFalse(marker.exists())

    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux subreaper capability regression")
    def test_missing_proc_children_uses_ps_and_still_reaps_descendants(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            marker, started = root / 'escaped', root / 'started'
            driver = root / 'missing_children_supervisor.py'
            driver.write_text(
                f'import sys,signal; sys.path.insert(0,{str(ROOT / "tools")!r})\n'
                'import test_policy\n'
                'read_text=test_policy.Path.read_text\n'
                'def missing_children(path,*args,**kwargs):\n'
                '    if str(path).startswith("/proc/") and path.name=="children":\n'
                '        raise FileNotFoundError("kernel has no proc children file")\n'
                '    return read_text(path,*args,**kwargs)\n'
                'test_policy.Path.read_text=missing_children\n'
                'signal.signal(signal.SIGTERM,test_policy.terminate)\n'
                'signal.signal(signal.SIGINT,test_policy.terminate)\n'
                'try: sys.exit(test_policy.main())\n'
                'except (test_policy.TerminationRequested,KeyboardInterrupt): sys.exit(130)\n')
            descendant = (f'from pathlib import Path; import time; '
                          f'Path({str(started)!r}).touch(); time.sleep({ESCAPE_DELAY}); '
                          f'Path({str(marker)!r}).touch()')
            parent = (f'import subprocess,sys,time; '
                      f'subprocess.Popen([sys.executable,"-c",{descendant!r}], start_new_session=True); '
                      'time.sleep(6)')
            with patch.object(policy, '__file__', str(driver)):
                self.assertEqual(policy.run_commands([[sys.executable, '-c', 'pass']]), 0)
                self.assertEqual(policy.run_commands([[sys.executable, '-c', parent]], CLEANUP_TIMEOUT), 124)
            self.assertTrue(started.exists(), 'escaped-session descendant must actually start')
            time.sleep(CLEANUP_OBSERVATION)
            self.assertFalse(marker.exists(), 'fallback must retain subreaper cleanup')

    def test_no_cleanup_reader_fails_before_test_work(self):
        with patch.object(policy, 'enable_subreaper', return_value=True), \
                patch.object(policy, 'proc_children', side_effect=FileNotFoundError()), \
                patch.object(policy, 'process_children', side_effect=OSError('ps unavailable')), \
                patch.object(policy.subprocess, 'Popen') as spawn:
            with self.assertRaisesRegex(RuntimeError, 'no test started'):
                policy.supervise([['must-not-run']])
            spawn.assert_not_called()

    def test_macos_selection_keeps_short_commands_without_linux_subreaper(self):
        with patch.object(policy.sys, 'platform', 'darwin'), \
                patch.object(policy.ctypes, 'CDLL') as libc, \
                patch.dict(os.environ):
            self.assertFalse(policy.enable_subreaper())
            self.assertEqual(policy.supervise([[sys.executable, '-c', 'pass']]), 0)
            libc.assert_not_called()

    def test_macos_nested_timeout_reaps_subtree_without_touching_sibling(self):
        # Simulate the actual supervisor platform branch in a fresh process.
        # The caller remains inside the outer aggregate's live process group.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            marker, started, sibling_marker = (root / name for name in ('escaped', 'started', 'sibling'))
            driver = root / 'portable_supervisor.py'
            driver.write_text(
                f'import sys,signal; sys.path.insert(0,{str(ROOT / "tools")!r})\n'
                'import test_policy\n'
                'test_policy.sys.platform="darwin"\n'
                'signal.signal(signal.SIGTERM,test_policy.terminate)\n'
                'signal.signal(signal.SIGINT,test_policy.terminate)\n'
                'try: sys.exit(test_policy.main())\n'
                'except (test_policy.TerminationRequested, KeyboardInterrupt): sys.exit(130)\n')
            descendant = (f'from pathlib import Path; import time; '
                          f'Path({str(started)!r}).touch(); time.sleep({ESCAPE_DELAY}); '
                          f'Path({str(marker)!r}).touch()')
            parent = (f'import subprocess,sys,time; '
                      f'subprocess.Popen([sys.executable,"-c",{descendant!r}]); time.sleep(6)')
            sibling = subprocess.Popen([sys.executable, '-c',
                f'from pathlib import Path; import time; time.sleep({ESCAPE_DELAY}); Path({str(sibling_marker)!r}).touch()'])
            try:
                with patch.object(policy, '__file__', str(driver)), \
                        patch.dict(os.environ, {policy.PROCESS_GROUP: str(os.getpgrp())}):
                    self.assertEqual(policy.run_commands([[sys.executable, '-c', parent]], CLEANUP_TIMEOUT), 124)
                self.assertTrue(started.exists(), 'grandchild must have started before timeout')
                time.sleep(CLEANUP_OBSERVATION)
                self.assertFalse(marker.exists(), 'nested cleanup must not wait for the outer aggregate')
                self.assertEqual(sibling.wait(timeout=.1), 0)
                self.assertTrue(sibling_marker.exists(), 'cleanup must preserve sibling processes')
            finally:
                if sibling.poll() is None:
                    sibling.kill()
                sibling.wait()

    def test_nested_wrappers_reuse_the_owned_process_group(self):
        with tempfile.TemporaryDirectory() as directory:
            outer = Path(directory) / 'outer'
            inner = Path(directory) / 'inner'
            command = f'import os; from pathlib import Path; Path({str(inner)!r}).write_text(str(os.getpgrp()))'
            nested = (f'import os,sys; from pathlib import Path; sys.path.insert(0,{str(ROOT / "tools")!r}); '
                      f'import test_policy; Path({str(outer)!r}).write_text(str(os.getpgrp())); '
                      f'sys.exit(test_policy.run_commands([[sys.executable,"-c",{command!r}]]))')
            self.assertEqual(policy.run_commands([[sys.executable, '-c', nested]]), 0)
            self.assertEqual(outer.read_text(), inner.read_text())
        self.assertFalse(policy.inherited_group({}))
        self.assertTrue(policy.inherited_group({policy.PROCESS_GROUP: str(os.getpgrp())}))

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

    def test_make_selector_metacharacters_never_execute_extra_commands(self):
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory) / 'injected'
            env = dict(os.environ, CHIBI_CLOUD='0')
            for target, variable in [('test-fast', 'TEST_ARGS'), ('agent-test', 'AGENT_SELECTOR')]:
                for suffix in [f'; touch {marker}', f' && touch {marker}',
                               f' `touch {marker}`', f' $$(touch {marker})']:
                    with self.subTest(variable=variable, suffix=suffix):
                        # Even a regression cannot proceed into a real build.
                        result = subprocess.run(['make', target, 'QUIET=false',
                                                 f'{variable}=--graph-optimize{suffix}'],
                                                cwd=ROOT, env=env, capture_output=True,
                                                text=True, timeout=3)
                        self.assertNotEqual(result.returncode, 0)
                        self.assertIn('DISABLED: unadmitted native invocation', result.stdout + result.stderr)
                        self.assertFalse(marker.exists(), 'selector data was executed by a shell')

    def test_environment_transport_preserves_multi_token_selectors(self):
        with tempfile.TemporaryDirectory() as directory:
            binary, record = Path(directory) / 'test_suite', Path(directory) / 'argv.json'
            binary.write_text(f'#!{sys.executable}\nimport sys,json\nfrom pathlib import Path\n'
                              f'Path({str(record)!r}).write_text(json.dumps(sys.argv[1:]))\n')
            binary.chmod(0o755)
            for value in ['--studio-documents tabs', '--studio-documents "tabs"']:
                with self.subTest(value=value):
                    result = subprocess.run([sys.executable, 'tools/test_policy.py',
                                             'native-env', 'TEST_ARGS', str(binary)],
                                            cwd=ROOT, env=dict(os.environ, TEST_ARGS=value),
                                            capture_output=True, text=True, timeout=3)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(json.loads(record.read_text()), ['--studio-documents', 'tabs'])
            # The agent transport also builds argv directly and keeps the deadline wrapper.
            with patch.dict(os.environ, AGENT_SELECTOR='--graph-optimize'), \
                    patch.object(sys, 'argv', ['test_policy.py', 'agent-env', directory, 'test']), \
                    patch.object(policy, 'run_commands', return_value=0) as run:
                self.assertEqual(policy.main(), 0)
                self.assertEqual(run.call_args.args[0], [[sys.executable, str(ROOT / 'tools/agent.py'),
                                                        '--build-dir', directory, 'test', '--', '--graph-optimize']])

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
