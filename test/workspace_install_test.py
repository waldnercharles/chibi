#!/usr/bin/env python3
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    'installer', Path(__file__).resolve().parents[1] / 'tools/install_workspace_tools.py')
installer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(installer)


class InstallTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.home = Path(self.temp.name)
        self.state = self.home / 'state'
        self.source = self.home / 'source'
        self.source.mkdir()
        for name in ('workspaces.py', 'workspace_retirement.py'):
            (self.source / name).write_text('# test script\n')
        self.codex = self.home / '.codex/hooks.json'
        self.claude = self.home / '.claude/settings.json'
        self.codex.parent.mkdir()
        self.claude.parent.mkdir()
        self.original = {'hooks': {'Stop': [{'hooks': [{'type': 'command', 'command': 'existing-hook'}]}]},
                         'unrelated': {'enabled': True}}
        self.codex.write_text(json.dumps(self.original))
        self.claude.write_text(json.dumps({'permissions': {'allow': ['Read']}}))

    def install(self, apply=True):
        return installer.install(self.home, self.state, self.source, apply=apply)

    def test_preserves_settings_and_is_idempotent(self):
        result = self.install()
        merged = json.loads(self.codex.read_text())
        self.assertEqual(merged['unrelated'], self.original['unrelated'])
        self.assertEqual(merged['hooks']['Stop'][0], self.original['hooks']['Stop'][0])
        self.assertEqual(len(merged['hooks']['Stop']), 2)
        self.assertEqual(json.loads(self.claude.read_text())['permissions'], {'allow': ['Read']})
        backup = Path(result['backup']) / str(self.codex).lstrip('/')
        self.assertEqual(json.loads(backup.read_text()), self.original)
        self.assertEqual(self.install()['changed'], 0)

    def test_bad_second_config_leaves_everything_untouched(self):
        before = self.codex.read_bytes()
        self.claude.write_text('{bad json')
        with self.assertRaises(ValueError):
            self.install()
        self.assertEqual(self.codex.read_bytes(), before)
        self.assertFalse(self.state.exists())
        self.assertFalse((self.home / '.local/bin/chibi-workspace').exists())

    def test_dry_run_writes_nothing(self):
        before = self.codex.read_bytes()
        self.assertEqual(self.install(False)['mode'], 'dry-run')
        self.assertEqual(self.codex.read_bytes(), before)
        self.assertFalse(self.state.exists())

    def test_failed_write_restores_prior_settings(self):
        original = installer.atomic_write
        before = {path: path.read_bytes() for path in (self.codex, self.claude)}
        calls = 0

        def fail_once(path, content, mode):
            nonlocal calls
            calls += 1
            if calls == 5:
                raise OSError('simulated full disk')
            return original(path, content, mode)

        with patch.object(installer, 'atomic_write', fail_once):
            with self.assertRaises(OSError):
                self.install()
        for path, content in before.items():
            self.assertEqual(path.read_bytes(), content)
        self.assertFalse((self.home / '.local/bin/chibi-workspace').exists())

    def test_symlink_config_is_not_replaced(self):
        real = self.home / 'actual-settings'
        self.claude.rename(real)
        self.claude.symlink_to(real)
        with self.assertRaisesRegex(ValueError, 'symlink'):
            self.install()
        self.assertTrue(self.claude.is_symlink())
        self.assertEqual(json.loads(self.codex.read_text()), self.original)


if __name__ == '__main__':
    unittest.main()
