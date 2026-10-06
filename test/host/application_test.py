#!/usr/bin/env python3
"""Bounded native workspace recovery smoke; needs a display/GPU context."""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

APPLICATION = None


class ApplicationRecoveryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='chibi application recovery ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.project = self.root / 'project with spaces'
        scenes = self.project / 'content/scenes'
        scenes.mkdir(parents=True)
        info = subprocess.run([APPLICATION, '--engine-info'], check=True,
                              capture_output=True, text=True, timeout=30)
        version = json.loads(info.stdout)['project_version']
        (self.project / 'project.kdl').write_text(
            f'version {version}\nname "Recovery Test"\ncontent "content"\n'
            'start_scene "scenes/Start.kdl"\n')
        (scenes / 'Start.kdl').write_text(
            'version 1\nname "Start"\n(array)entities\n')
        self.sources = {path: path.read_bytes() for path in self.project.rglob('*')
                        if path.is_file()}
        self.checkpoint = self.root / 'workspace.json'
        self.window = self.root / 'workspace.json.window'
        self.window.write_text('31 37 640 360')

    def launch(self, project=None):
        result = subprocess.run(
            [APPLICATION, '--headless', '--frames', '1', '--project-root',
             str(project or self.project), '--checkpoint', str(self.checkpoint)],
            cwd=self.root, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for path, original in self.sources.items():
            self.assertEqual(path.read_bytes(), original, str(path))
        saved = json.loads(self.checkpoint.read_text())
        self.assertEqual(saved['version'], 1)
        self.assertEqual(Path(saved['project']).resolve(), self.project.resolve())
        self.assertIsInstance(json.loads(saved['scene']), dict)
        self.assertIsInstance(json.loads(saved['graph']), dict)
        bounds = [int(value) for value in self.window.read_text().split()]
        self.assertEqual(len(bounds), 4)
        self.assertEqual(bounds[2:], [640, 360])
        return result, saved

    def test_save_and_restart_reopen_the_saved_project(self):
        _, first = self.launch()
        result, restored = self.launch(self.root / 'missing launch project')
        self.assertEqual(json.loads(restored['scene']), json.loads(first['scene']))
        self.assertEqual(json.loads(restored['graph']), json.loads(first['graph']))
        self.assertNotIn('Preserved workspace at', result.stderr)
        self.assertEqual(list(self.root.glob('workspace.json.rejected-*')), [])

    def test_rejected_checkpoints_are_preserved_before_fresh_start(self):
        for original in (b'{unfinished workspace\n',
                         b'{"version":999,"scene":"{}","graph":"{}"}\n'):
            with self.subTest(checkpoint=original):
                before = set(self.root.glob('workspace.json.rejected-*'))
                self.checkpoint.write_bytes(original)
                result, _ = self.launch()
                archives = set(self.root.glob('workspace.json.rejected-*')) - before
                self.assertEqual(len(archives), 1)
                self.assertEqual(archives.pop().read_bytes(), original)
                self.assertIn('Retrying from project content', result.stderr)

    @unittest.skipUnless(sys.platform.startswith('linux'),
                         'signal readiness detection uses Linux /proc')
    def test_sigterm_preserves_the_workspace(self):
        # SDL otherwise installs its own SIGTERM handler during window setup.
        env = {**os.environ, 'SDL_NO_SIGNAL_HANDLERS': '1'}
        process = subprocess.Popen(
            [APPLICATION, '--headless', '--project-root', str(self.project),
             '--checkpoint', str(self.checkpoint)], cwd=self.root,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)
        try:
            deadline = time.monotonic() + 30
            ready = False
            while process.poll() is None and time.monotonic() < deadline:
                # The host installs SIGTERM only after application activation.
                status = Path(f'/proc/{process.pid}/status').read_text()
                caught = next(line.split()[1] for line in status.splitlines()
                              if line.startswith('SigCgt:'))
                if int(caught, 16) & (1 << (signal.SIGTERM - 1)):
                    ready = True
                    break
                time.sleep(0.02)
            self.assertTrue(ready, 'application did not install its shutdown handler')
            process.send_signal(signal.SIGTERM)
            output, error = process.communicate(timeout=30)
            self.assertEqual(process.returncode, 0, output + error)
            self.assertEqual(json.loads(self.checkpoint.read_text())['version'], 1)
            # A clean signal snapshot must also be accepted on restart.
            result, _ = self.launch()
            self.assertNotIn('Preserved workspace at', result.stderr)
        finally:
            if process.poll() is None:
                process.kill()
            process.communicate(timeout=30)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('application', type=Path)
    args, remaining = parser.parse_known_args()
    APPLICATION = str(args.application.resolve())
    unittest.main(argv=[__file__, *remaining])
