"""Registered games receive independent, verified runtime copies."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
spec = importlib.util.spec_from_file_location('install_game_runtime', ROOT / 'tools/install_game_runtime.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class RuntimeUpdates(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='chibi-runtime-test-')
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name).resolve()
        self.game = self.root / 'Independent game'
        self.game.mkdir()
        (self.game / 'project.kdl').write_text('version 4\nname "Independent"\ncontent "content"\n')
        (self.game / '.codex').mkdir()
        self.original_config = 'model = "custom"\n\n[mcp_servers.other]\ncommand = "other"\n'
        (self.game / '.codex/config.toml').write_text(self.original_config)
        self.runtime = self.game / '.chibi/runtime'
        self.config = self.root / 'registration.json'
        self.config.write_text(json.dumps({'games': [str(self.game)]}))

    def package(self, name, version=4):
        package = self.root / name
        (package / 'bin').mkdir(parents=True)
        for binary in ('chibi', 'chibi_ctl', 'chibi-mcp'):
            path = package / 'bin' / binary
            path.write_text(f'#!/bin/sh\nprintf "%s\\n" "{name}:{binary}"\n')
            path.chmod(0o755)
        modules = package / 'share/chibi/mcp'
        modules.mkdir(parents=True)
        for name in ('setup-codex.mjs', 'engine-package.mjs'):
            shutil.copyfile(ROOT / 'tools/mcp' / name, modules / name)
        files = {path.relative_to(package).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                 for path in package.rglob('*') if path.is_file()}
        build = hashlib.sha256(json.dumps(files, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
        manifest = {'format': 1, 'build_id': build, 'project_version': version, 'files': files}
        (package / installer.MANIFEST).write_text(json.dumps(manifest))
        return package, manifest

    def active(self):
        return installer.validate_package(self.runtime)['build_id']

    def test_two_generations_update_binaries_and_pins_without_changing_local_interface(self):
        first, first_manifest = self.package('first')
        installer.install(first, self.config)
        initial = self.active()
        initial_binary = (self.runtime / 'bin/chibi').read_bytes()
        initial_config = (self.game / '.codex/config.toml').read_text()
        self.assertIn(self.original_config.strip(), initial_config)
        self.assertIn(str(self.runtime / 'bin/chibi-mcp'), initial_config)
        self.assertNotIn(str(first), initial_config)
        self.assertFalse(any(path.is_symlink() for path in self.runtime.rglob('*')))
        binary = self.runtime / 'bin/chibi'
        self.assertNotEqual(binary.stat().st_ino, (first / 'bin/chibi').stat().st_ino)
        self.assertEqual(binary.stat().st_nlink, 1)
        second, second_manifest = self.package('second')
        installer.install(second, self.config)
        self.assertNotEqual(initial, self.active())
        self.assertEqual(initial_binary, (first / 'bin/chibi').read_bytes())
        self.assertEqual(binary.read_bytes(), (second / 'bin/chibi').read_bytes())
        self.assertEqual(subprocess.check_output([str(binary)], text=True).strip(),
                         'second:chibi')
        self.assertEqual((self.game / '.codex/config.toml').read_text(), initial_config)
        self.assertEqual(json.loads((self.game / '.chibi-engine.json').read_text()), {
            'format': 1, 'build_id': second_manifest['build_id'], 'project_version': 4})
        installer.install(second, self.config)
        self.assertEqual(self.active(), second_manifest['build_id'])
        self.assertEqual(list((self.game / '.chibi').iterdir()), [self.runtime])
        self.assertNotEqual(first_manifest['build_id'], second_manifest['build_id'])
        shutil.rmtree(first)
        shutil.rmtree(second)
        self.assertEqual(subprocess.check_output([str(binary)], text=True).strip(),
                         'second:chibi')

    def test_corrupt_package_rejected_before_changing_active_game(self):
        first, _ = self.package('first')
        installer.install(first, self.config)
        active = self.active()
        pin = (self.game / '.chibi-engine.json').read_bytes()
        second, _ = self.package('second')
        (second / 'bin/chibi').write_text('corrupt')
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            installer.install(second, self.config)
        self.assertEqual(self.active(), active)
        self.assertEqual((self.game / '.chibi-engine.json').read_bytes(), pin)
        self.assertEqual(list((self.game / '.chibi').iterdir()), [self.runtime])

    def test_project_version_mismatch_rejected_before_changing_active_game(self):
        first, _ = self.package('first')
        installer.install(first, self.config)
        active = self.active()
        config = (self.game / '.codex/config.toml').read_bytes()
        pin = (self.game / '.chibi-engine.json').read_bytes()
        binary = (self.runtime / 'bin/chibi').read_bytes()
        second, _ = self.package('second', version=5)
        with self.assertRaisesRegex(ValueError, 'Project version mismatch'):
            installer.install(second, self.config)
        self.assertEqual(self.active(), active)
        self.assertEqual((self.game / '.codex/config.toml').read_bytes(), config)
        self.assertEqual((self.game / '.chibi-engine.json').read_bytes(), pin)
        self.assertEqual((self.runtime / 'bin/chibi').read_bytes(), binary)
        self.assertEqual(list((self.game / '.chibi').iterdir()), [self.runtime])

    def test_kdl_project_version_must_be_an_integer(self):
        package, _ = self.package('first')
        for document in ('version "4"\n', 'version #true\n', 'name "Independent"\n'):
            with self.subTest(document=document):
                (self.game / 'project.kdl').write_text(document)
                with self.assertRaisesRegex(ValueError, 'Project version mismatch'):
                    installer.install(package, self.config)
                self.assertFalse(self.runtime.exists())
                self.assertFalse((self.game / '.chibi-engine.json').exists())
                self.assertEqual((self.game / '.codex/config.toml').read_text(), self.original_config)

    def test_late_metadata_failure_rolls_back_all_runtime_copies(self):
        first, _ = self.package('first')
        installer.install(first, self.config)
        active = self.active()
        pin = (self.game / '.chibi-engine.json').read_bytes()
        config = (self.game / '.codex/config.toml').read_bytes()
        other = self.root / 'Other game'
        shutil.copytree(self.game, other)
        self.config.write_text(json.dumps({'games': [str(self.game), str(other)]}))
        second, _ = self.package('second')
        original_write = installer.atomic_write
        failed = False

        def fail_once(path, text):
            nonlocal failed
            if path == other / '.chibi-engine.json' and not failed:
                failed = True
                raise OSError('metadata write failed')
            return original_write(path, text)

        with patch.object(installer, 'atomic_write', side_effect=fail_once):
            with self.assertRaisesRegex(OSError, 'metadata write failed'):
                installer.install(second, self.config)
        self.assertEqual(self.active(), active)
        self.assertEqual((self.game / '.chibi-engine.json').read_bytes(), pin)
        self.assertEqual((self.game / '.codex/config.toml').read_bytes(), config)
        self.assertEqual(installer.validate_package(other / '.chibi/runtime')['build_id'], active)
        self.assertEqual((other / '.chibi-engine.json').read_bytes(), pin)
        self.assertEqual((other / '.codex/config.toml').read_bytes(), config)

    def test_unrelated_runtime_directory_is_preserved(self):
        self.runtime.mkdir(parents=True)
        marker = self.runtime / 'owned-tool'
        marker.write_text('keep')
        package, _ = self.package('first')
        with self.assertRaisesRegex(ValueError, 'not an engine package'):
            installer.install(package, self.config)
        self.assertEqual(marker.read_text(), 'keep')
        self.assertEqual(list(self.runtime.iterdir()), [marker])

    def test_runtime_link_to_an_external_package_is_rejected(self):
        package, _ = self.package('first')
        self.runtime.parent.mkdir()
        self.runtime.symlink_to(package, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'must stay inside the game'):
            installer.install(package, self.config)
        self.assertEqual(self.runtime.resolve(), package)
        self.assertFalse((self.game / '.chibi-engine.json').exists())

    def test_all_registered_games_are_validated_before_any_update(self):
        first, _ = self.package('first')
        installer.install(first, self.config)
        active = self.active()
        pin = (self.game / '.chibi-engine.json').read_bytes()
        incompatible = self.root / 'Other game'
        incompatible.mkdir()
        (incompatible / 'project.kdl').write_text('version 3\ncontent "content"\n')
        self.config.write_text(json.dumps({'games': [str(self.game), str(incompatible)]}))
        second, _ = self.package('second')
        with self.assertRaisesRegex(ValueError, 'Project version mismatch'):
            installer.install(second, self.config)
        self.assertEqual(self.active(), active)
        self.assertEqual((self.game / '.chibi-engine.json').read_bytes(), pin)
        self.assertFalse((incompatible / '.chibi/runtime').exists())

    def test_absent_registration_is_noop_even_without_a_package(self):
        self.assertIsNone(installer.install(self.root / 'missing package', self.root / 'missing registration'))
        self.assertFalse(self.runtime.exists())


if __name__ == '__main__':
    unittest.main()
