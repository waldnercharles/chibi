"""Policy checks compile only the JSON validator; no graphics initialization."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import signal
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from gpu_capabilities import classify_devices

spec = importlib.util.spec_from_file_location('agent', ROOT / 'tools/agent.py')
agent = importlib.util.module_from_spec(spec)
spec.loader.exec_module(agent)


class PolicyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.temp.name) / 'policy'
        source = Path(cls.temp.name) / 'policy.c'
        source.write_text('''
#include "automation/execution_policy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--video-driver")) {
        const char *driver = chibi_preview_video_driver(atoi(argv[2]));
        puts(driver ? driver : "unchanged");
        return 0;
    }
    if (argc == 3) {
        puts(chibi_preview_hidden(atoi(argv[2])) ? "hidden" : "visible");
        return 0;
    }
    yyjson_doc *doc = yyjson_read(argv[1], strlen(argv[1]), 0);
    const char *error = chibi_render_policy(yyjson_doc_get_root(doc));
    puts(error ? error : "allowed");
    yyjson_doc_free(doc);
    return 0;
}
''')
        subprocess.run([os.environ.get('CC', 'cc'), '-I.', '-Isrc', str(source),
                        'src/automation/execution_policy.c', 'third_party/yyjson/yyjson.c',
                        '-o', str(cls.binary)], cwd=ROOT, check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def policy(self, request, profile='iteration', opt_in='0'):
        env = dict(os.environ, CHIBI_EXECUTION_PROFILE=profile, CHIBI_ALLOW_FULL_GPU=opt_in)
        return subprocess.check_output([str(self.binary), json.dumps(request)], env=env, text=True).strip()

    def test_preview_and_capture_are_hidden_without_changing_hardware_benchmarks(self):
        for profile, opt_in, frames, expected in [
                ('iteration', '0', 1, 'hidden'), ('mp4', '0', 180, 'hidden'),
                ('hardware', '1', 2, 'hidden'), ('hardware', '1', 180, 'visible')]:
            env = dict(os.environ, CHIBI_EXECUTION_PROFILE=profile, CHIBI_ALLOW_FULL_GPU=opt_in)
            result = subprocess.check_output([str(self.binary), '--window-mode', str(frames)],
                                             env=env, text=True).strip()
            self.assertEqual(result, expected)

    def test_offscreen_selection_is_scoped_and_respects_explicit_choices(self):
        base = dict(os.environ, CHIBI_EXECUTION_PROFILE='iteration')
        for key in ['SDL_VIDEO_DRIVER', 'SDL_VIDEODRIVER', 'CHIBI_PREVIEW_DISPLAY_CONTEXT']:
            base.pop(key, None)
        cases = [
            ({}, '1', 'offscreen'),
            ({'CHIBI_EXECUTION_PROFILE': 'mp4'}, '1', 'offscreen'),
            ({'WAYLAND_DISPLAY': 'wayland-0', 'DISPLAY': ':0'}, '1', 'offscreen'),
            ({'CHIBI_PREVIEW_DISPLAY_CONTEXT': 'xvfb'}, '1', 'x11'),
            ({'SDL_VIDEO_DRIVER': 'wayland'}, '1', 'unchanged'),
            ({'SDL_VIDEO_DRIVER': 'x11'}, '1', 'unchanged'),
            ({'SDL_VIDEO_DRIVER': ''}, '1', 'unchanged'),
            ({'SDL_VIDEODRIVER': 'x11'}, '1', 'unchanged'),
            ({'CHIBI_EXECUTION_PROFILE': 'hardware', 'CHIBI_ALLOW_FULL_GPU': '1'}, '1', 'unchanged'),
            ({'CHIBI_EXECUTION_PROFILE': 'gpu-check'}, '1', 'unchanged'),
            ({'CHIBI_EXECUTION_PROFILE': 'unknown'}, '1', 'unchanged'),
            ({}, '0', 'unchanged'),
        ]
        for override, linux, expected in cases:
            with self.subTest(override=override, linux=linux):
                result = subprocess.check_output([str(self.binary), '--video-driver', linux],
                                                 env=dict(base, **override), text=True).strip()
                self.assertEqual(result, expected)

    def test_failed_health_probe_does_not_prevent_CPU_planning(self):
        with patch.object(agent.subprocess, 'run', side_effect=subprocess.TimeoutExpired('probe', 5)):
            health = agent.startup_health()
        self.assertEqual(health['status'], 'unavailable')
        self.assertEqual(health['discovery_timeout_seconds'], 5)
        self.assertFalse(agent.test_plan('logic', health)['gpu_startup'])
        self.assertTrue(agent.test_plan('render', health)['gpu_startup'])
        self.assertIn('inconclusive', agent.test_plan('render', health)['discovery_note'])

    def test_non_vulkan_discovery_does_not_block_rendering(self):
        for backend in ['Metal', 'GLES']:
            health = dict(status='unavailable', backend='Vulkan', devices=[], acceleration='unknown')
            with self.subTest(actual_possible_backend=backend):
                plan = agent.test_plan('render', health)
                self.assertTrue(plan['commands'])
                self.assertIn(backend, plan['discovery_note'])
                self.assertFalse(plan['full_verification'])
                temporal = agent.test_plan('temporal', health)
                self.assertTrue(temporal['requires_explicit_mp4_request'])
                self.assertEqual(temporal['render_seconds'], 120)

    def test_capability_type_not_device_name_drives_classification(self):
        self.assertEqual(classify_devices([dict(type=4, name='RTX-looking CPU label')]), 'software')
        self.assertEqual(classify_devices([dict(type=2, name='llvmpipe-looking GPU label')]), 'hardware')
        self.assertEqual(classify_devices([dict(type=1)]), 'hardware')
        self.assertEqual(classify_devices([dict(type=3)]), 'unknown')
        self.assertEqual(classify_devices([]), 'unknown')

    def test_purpose_and_capabilities_choose_bounded_work(self):
        unavailable = dict(status='unavailable', devices=[], acceleration='unknown')
        self.assertFalse(agent.test_plan('logic', unavailable)['gpu_startup'])
        self.assertTrue(agent.test_plan('render', unavailable)['gpu_startup'])
        self.assertFalse(agent.test_plan('render', unavailable)['full_verification'])
        for acceleration, size in [('hardware', [320, 180]), ('software', [96, 64])]:
            health = dict(status='enumerated', devices=[dict(type=2 if acceleration == 'hardware' else 4)],
                          acceleration=acceleration)
            plan = agent.test_plan('render', health)
            self.assertTrue(plan['gpu_startup'])
            self.assertEqual(plan['render_seconds'], 60)
            self.assertEqual(plan['reference_resolution'], 'unchanged')
            self.assertEqual(plan['preview_size'], size)
            self.assertFalse(plan['full_verification'])
            self.assertEqual(plan['pending'][0]['status'], 'pending')
            temporal = agent.test_plan('temporal', health)
            self.assertTrue(temporal['requires_explicit_mp4_request'])
            self.assertEqual(temporal['output_frames'], 60)

    def test_preview_default_and_warmup(self):
        self.assertEqual(self.policy(dict(operation='preview_scene')), 'allowed')
        self.assertEqual(self.policy(dict(operation='preview_scene', gpu_warmup_frames=32)), 'allowed')
        self.assertNotEqual(self.policy(dict(operation='preview_scene', gpu_warmup_frames=33)), 'allowed')

    def test_no_multi_frame_preview_or_suite_disguised_as_preview(self):
        for request in [dict(operation='preview_scene', render_frames=2),
                        dict(operation='test_suite'), dict(operation='preview_shader', capture_frames='/tmp/x')]:
            self.assertNotEqual(self.policy(request), 'allowed')

    def test_resolution_and_types_fail_closed(self):
        for values in [dict(width=641), dict(width=640, height=640), dict(render_frames=True),
                       dict(frames=61), dict(width=0), dict(gpu_warmup_frames=-1)]:
            self.assertNotEqual(self.policy(dict(operation='preview_scene', **values)), 'allowed')

    def test_mp4_explicit_bounded_capture(self):
        request = dict(operation='preview_scene', render_frames=180, capture_frames='/tmp/frames')
        self.assertEqual(self.policy(request, 'mp4'), 'allowed')
        self.assertNotEqual(self.policy(request), 'allowed')
        self.assertNotEqual(self.policy(dict(request, render_frames=181), 'mp4'), 'allowed')
        self.assertNotEqual(self.policy(dict(request, capture_frames='relative'), 'mp4'), 'allowed')
        self.assertNotEqual(self.policy(dict(request, metal_capture='/tmp/gpu'), 'mp4'), 'allowed')

    def test_hardware_needs_both_settings(self):
        request = dict(operation='preview_scene', render_frames=1000, width=2048)
        self.assertNotEqual(self.policy(request, 'hardware'), 'allowed')
        self.assertEqual(self.policy(request, 'hardware', '1'), 'allowed')
        self.assertNotEqual(self.policy(request, 'typo', '1'), 'allowed')

    def test_selector_classification(self):
        self.assertIn('--model-sampled', agent.CPU)
        for selector in ['--model-sampled-gpu', '--studio-documents', '--radiance', '--ui-bench', '--typo']:
            self.assertNotIn(selector, agent.CPU)
        self.assertNotIn('--studio-documents', agent.GPU)
        for selector in ['--radiance-diagnostics-vulkan', '--radiance-diagnostics-gles']:
            self.assertNotIn(selector, agent.CPU)
            self.assertNotIn(selector, agent.GPU)

    def test_runner_failure_and_timeout_never_pass(self):
        with self.assertRaisesRegex(RuntimeError, 'child_exit_7'):
            agent.run(['python3', '-c', 'raise SystemExit(7)'], 1)
        with self.assertRaisesRegex(RuntimeError, 'time_budget_exceeded'):
            agent.run(['python3', '-c', 'import time; time.sleep(30)'], .05)

    @unittest.skipUnless(Path('/proc/self/status').exists(), 'process liveness uses Linux proc')
    def test_runner_cleans_children_on_interrupt_monitor_error_and_parent_exit(self):
        real_sleep = time.sleep
        for failure in [KeyboardInterrupt(), OSError('monitor failed'), None]:
            with self.subTest(failure=type(failure).__name__), tempfile.TemporaryDirectory() as directory:
                pids_file = Path(directory) / 'pids.json'
                program = (
                    'import os,sys,json,subprocess,time; '
                    'child=subprocess.Popen([sys.executable,"-c","import time;time.sleep(30)"]); '
                    f'open({str(pids_file)!r},"w").write(json.dumps([os.getpid(),child.pid])); '
                    + ('time.sleep(30)' if failure else 'pass'))
                def interrupt(_):
                    deadline = time.monotonic() + 3
                    while not pids_file.exists() and time.monotonic() < deadline:
                        real_sleep(.01)
                    if isinstance(failure, KeyboardInterrupt):
                        os.kill(os.getpid(), signal.SIGINT)
                    if failure:
                        raise failure
                    real_sleep(.01)
                try:
                    with patch.object(agent.time, 'sleep', side_effect=interrupt):
                        if failure:
                            with self.assertRaises(type(failure)):
                                agent.run([sys.executable, '-c', program], 5)
                        else:
                            agent.run([sys.executable, '-c', program], 5)
                    pids = json.loads(pids_file.read_text())
                    deadline = time.monotonic() + 3
                    for pid in pids:
                        while time.monotonic() < deadline:
                            try:
                                state = Path(f'/proc/{pid}/stat').read_text().split(') ')[1][0]
                            except FileNotFoundError:
                                break
                            if state == 'Z':  # Cannot execute; waiting for the system reaper.
                                break
                            real_sleep(.01)
                        else:
                            self.fail(f'child {pid} remained alive after cleanup')
                    with self.assertRaises(ChildProcessError):
                        os.waitpid(pids[0], os.WNOHANG)
                finally:
                    if pids_file.exists():
                        for pid in json.loads(pids_file.read_text()):
                            try:
                                os.kill(pid, signal.SIGKILL)
                            except ProcessLookupError:
                                pass

    @unittest.skipUnless(Path('/proc/self/status').exists(), 'process liveness uses Linux proc')
    def test_cli_SIGTERM_cleans_detached_child_and_descendant(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pids_file = root / 'pids.json'
            executable = root / 'test_suite'
            executable.write_text(f'#!{sys.executable}\n' +
                                  'import os,sys,json,subprocess,time\n'
                                  'child=subprocess.Popen([sys.executable,"-c","import time; time.sleep(30)"])\n'
                                  f'open({str(pids_file)!r},"w").write(json.dumps([os.getpid(),child.pid]))\n'
                                  'time.sleep(30)\n')
            executable.chmod(0o700)
            wrapper = subprocess.Popen([sys.executable, 'tools/agent.py', '--build-dir', directory,
                                        'test', '--', '--graph-optimize'], cwd=ROOT,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            pids = []
            try:
                deadline = time.monotonic() + 8
                while not pids_file.exists() and time.monotonic() < deadline:
                    time.sleep(.01)
                self.assertTrue(pids_file.exists(), 'fixture child never started')
                pids = json.loads(pids_file.read_text())
                wrapper.send_signal(signal.SIGTERM)
                stdout, _ = wrapper.communicate(timeout=5)
                self.assertEqual(wrapper.returncode, 143)
                self.assertEqual(json.loads(stdout)['reason'], 'terminated_by_SIGTERM')
                for pid in pids:
                    deadline = time.monotonic() + 3
                    while time.monotonic() < deadline:
                        try:
                            state = Path(f'/proc/{pid}/stat').read_text().split(') ')[1][0]
                        except FileNotFoundError:
                            break
                        if state == 'Z':
                            break
                        time.sleep(.01)
                    else:
                        self.fail(f'child {pid} remained alive after wrapper SIGTERM')
            finally:
                if wrapper.poll() is None:
                    wrapper.kill()
                wrapper.communicate()
                for pid in pids:
                    try:
                        os.kill(pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass

    @unittest.skipUnless(Path('/proc/self/status').exists(), 'RSS monitoring uses Linux proc')
    def test_resident_memory_budget(self):
        with self.assertRaisesRegex(RuntimeError, 'resident_memory_budget_exceeded'):
            agent.run([sys.executable, '-c', 'import time; time.sleep(30)'], 2, rss_mib=1)

    def test_incomplete_capture_is_not_published(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            controller = root / 'chibi_ctl'
            controller.write_text(f'#!{sys.executable}\n' +
                                  'import json,sys,pathlib\n'
                                  'request=json.load(sys.stdin)\n'
                                  'pathlib.Path(request["capture_frames"]).write_bytes(b"partial")\n'
                                  'print(json.dumps({"ok":True}))\n')
            controller.chmod(0o700)
            encoder = root / 'ffmpeg'
            encoder.write_text('#!/bin/sh\nexit 99\n')
            encoder.chmod(0o700)
            request = root / 'request.json'
            request.write_text(json.dumps(dict(operation='preview_scene', width=64, height=64)))
            output = root / 'capture.mp4'
            command = [sys.executable, 'tools/agent.py', '--build-dir', directory, 'mp4',
                       str(request), '--frames', '3', '--output', str(output)]
            env = dict(os.environ, PATH=directory + os.pathsep + os.environ['PATH'])
            result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(json.loads(result.stdout)['reason'], 'incomplete_frame_capture')
            self.assertFalse(output.exists())

    def test_wrapper_blocks_before_launch(self):
        command = ['python3', 'tools/agent.py', '--build-dir', '/nonexistent', 'test', '--', '--studio-documents']
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        response = json.loads(result.stdout)
        self.assertEqual(response['status'], 'blocked')
        self.assertFalse(response['full_verification'])
        self.assertTrue(response['required_checks_outstanding'])


if __name__ == '__main__':
    unittest.main()
