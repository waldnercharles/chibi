"""Policy checks compile only the JSON validator; no graphics initialization."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
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
#include <string.h>
int main(int argc, char **argv) {
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

    def test_runner_failure_and_timeout_never_pass(self):
        with self.assertRaisesRegex(RuntimeError, 'child_exit_7'):
            agent.run(['python3', '-c', 'raise SystemExit(7)'], 1)
        with self.assertRaisesRegex(RuntimeError, 'time_budget_exceeded'):
            agent.run(['python3', '-c', 'import time; time.sleep(30)'], .05)

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
