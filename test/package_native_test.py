"""Standalone release checks. Run integration with --package /path/to/package."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = None


def run(command, cwd, env=None, input=None):
    return subprocess.run([str(arg) for arg in command], cwd=cwd, env=env,
                          input=input, text=True, capture_output=True, timeout=90)


class PackagingFailureTests(unittest.TestCase):
    def test_incomplete_build_does_not_replace_existing_output(self):
        with tempfile.TemporaryDirectory(prefix='chibi-package-test-') as directory:
            root = Path(directory)
            build, output = root / 'incomplete build', root / 'previous release'
            build.mkdir()
            output.mkdir()
            marker = output / 'keep.txt'
            marker.write_text('previous usable release\n')
            result = run([sys.executable, ROOT / 'tools/package_native.py',
                          '--build', build, '--output', output], root)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(marker.read_text(), 'previous usable release\n')
            self.assertEqual(list(output.iterdir()), [marker])


@unittest.skipUnless(PACKAGE, 'pass --package to check a built native release')
class StandalonePackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='chibi standalone ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.package = self.root / 'relocated engine with spaces'
        shutil.copytree(PACKAGE, self.package)
        self.cwd = self.root / 'unrelated working directory'
        self.cwd.mkdir()
        self.project = self.root / 'external game'
        self.project.mkdir()
        self.metadata = json.loads((self.package / 'share/chibi/engine.json').read_text())
        self.env = {key: value for key, value in os.environ.items()
                    if not key.startswith('CHIBI_')}
        blockers = self.root / 'blocked compilers'
        blockers.mkdir()
        self.rebuild_marker = self.root / 'attempted rebuild'
        for name in ('make', 'cmake', 'ninja', 'cc', 'clang', 'gcc', 'git'):
            executable = blockers / name
            executable.write_text('#!/bin/sh\nprintf "%s\\n" "$0" >> "$CHIBI_TEST_REBUILD_MARKER"\nexit 99\n')
            executable.chmod(0o755)
        self.env['PATH'] = str(blockers) + os.pathsep + self.env.get('PATH', '')
        self.env['CHIBI_TEST_REBUILD_MARKER'] = str(self.rebuild_marker)
        self.project.joinpath('project.yaml').write_text(
            f'version: {self.metadata["project_version"]}\nname: Standalone Test\ncontent: content\n')
        config = self.project / '.codex/config.toml'
        config.parent.mkdir()
        retained = '[agents.default]\nmodel = "existing-model"\n\n[mcp_servers.other]\ncommand = "existing-command"\n'
        config.write_text(retained)
        setup = run([self.package / 'bin/chibi-mcp', '--setup', self.project], self.cwd, self.env)
        self.assertEqual(setup.returncode, 0, setup.stderr)
        self.assertIn(retained, config.read_text())
        pin = json.loads((self.project / '.chibi-engine.json').read_text())
        self.assertEqual(pin['build_id'], self.metadata['build_id'])
        self.assertEqual(pin['project_version'], self.metadata['project_version'])
        self.assertFalse(self.rebuild_marker.exists())
        prototypes = self.project / 'content/prototypes'
        prototypes.mkdir(parents=True)
        prototypes.joinpath('PackageProbe.yaml').write_text(
            'name: PackageProbe\ncomponents:\n  GAME.GameTransform: {x: 12.0, y: -8.0}\n')

    def mcp(self, name, arguments):
        request = {'jsonrpc': '2.0', 'id': 1, 'method': 'tools/call',
                   'params': {'name': name, 'arguments': arguments}}
        process = run([self.package / 'bin/chibi-mcp', self.project], self.cwd,
                      self.env, json.dumps(request) + '\n')
        self.assertEqual(process.returncode, 0, process.stderr)
        responses = [json.loads(line) for line in process.stdout.splitlines() if line.strip()]
        self.assertEqual(len(responses), 1, process.stdout)
        response = responses[0]
        self.assertNotIn('error', response, response)
        self.assertFalse(response['result'].get('isError'), response)
        payload = response['result']['structuredContent']
        self.assertTrue(payload.get('ok'), payload)
        self.assertFalse(self.rebuild_marker.exists(), 'packaged authoring attempted a build')
        return payload['result']

    def test_manifest_covers_every_payload_file(self):
        checksums = self.metadata['files']
        actual = {path.relative_to(self.package).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                  for path in self.package.rglob('*')
                  if path.is_file() and path != self.package / 'share/chibi/engine.json'}
        self.assertEqual(actual, checksums)
        self.assertEqual(self.metadata['format'], 1)
        self.assertTrue(self.metadata['platform'])
        self.assertTrue(self.metadata['arch'])

    def test_release_contains_resources_without_engine_sources(self):
        resources = self.package / 'share/chibi'
        for name in ('shaders', 'ui', 'studio', 'mcp'):
            self.assertTrue((resources / name).is_dir(), name)
            self.assertTrue(any(path.is_file() for path in (resources / name).rglob('*')), name)
        for path in self.package.rglob('*'):
            self.assertNotIn(path.name, ('CMakeLists.txt', 'Makefile', '.git', 'CMakeCache.txt'))
            self.assertNotIn(path.suffix, ('.c', '.cpp', '.h', '.o', '.a'), str(path))

    def test_relocation_resource_paths_and_system_dependencies(self):
        resource_root = self.package / 'share/chibi'
        for name in ('chibi', 'chibi_ctl'):
            binary = self.package / 'bin' / name
            result = run([binary, '--engine-info'], self.cwd, self.env)
            self.assertEqual(result.returncode, 0, result.stderr)
            info = json.loads(result.stdout)
            self.assertTrue(info['distribution'])
            self.assertEqual(Path(info['resource_root']).resolve(), resource_root.resolve())
            self.assertEqual(info['build_id'], self.metadata['build_id'])
            self.assertEqual(info['project_version'], self.metadata['project_version'])
            for key in ('shaders', 'ui', 'studio'):
                self.assertEqual(Path(info[key]).resolve(), (resource_root / key).resolve())
                self.assertTrue(Path(info[key]).is_dir())
            if sys.platform == 'darwin':
                dependencies = run(['/usr/bin/otool', '-L', binary], self.cwd)
                self.assertEqual(dependencies.returncode, 0, dependencies.stderr)
                for line in dependencies.stdout.splitlines()[1:]:
                    library = line.strip().split(' (', 1)[0]
                    self.assertTrue(library.startswith(('/usr/lib/', '/System/Library/')),
                                    f'{name} requires unpackaged dependency: {library}')
        self.assertFalse(self.rebuild_marker.exists())

    def test_external_project_authoring_and_deterministic_scenario(self):
        selector = {'kind': 'prototype', 'name': 'PackageProbe'}
        found = self.mcp('searchAssets', {'query': 'PackageProbe'})
        self.assertIn('PackageProbe', json.dumps(found))
        self.assertTrue(self.mcp('validateAsset', selector)['valid'])
        before = self.mcp('readAsset', selector)
        first = self.mcp('runScenario', {**selector, 'frames': 2, 'fixed_dt': 1 / 60})
        self.mcp('buildPrototype', {**selector, 'expected_hash': before['hash'],
                 'operations': [{'op': 'set_field', 'component': 'GameTransform',
                                 'field': 'x', 'value': 37}]})
        after = self.mcp('readAsset', selector)
        self.assertNotEqual(before['hash'], after['hash'])
        self.assertTrue(self.mcp('validateAsset', selector)['valid'])
        args = {**selector, 'frames': 2, 'fixed_dt': 1 / 60}
        changed, repeated = self.mcp('runScenario', args), self.mcp('runScenario', args)
        self.assertNotEqual(first['world_hash'], changed['world_hash'])
        self.assertEqual(changed['world_hash'], repeated['world_hash'])
        self.assertGreaterEqual(changed['live_entities'], 1)
        snapshot = self.mcp('captureRuntimeSnapshot', args)
        transforms = [component for entity in snapshot['entities']
                      for component in entity['components'] if component['name'] == 'GameTransform']
        self.assertTrue(transforms, snapshot)
        self.assertTrue(any(field['name'] == 'x' and field['value'] == 37
                            for component in transforms for field in component['fields']), snapshot)
        self.assertFalse((self.project / 'build').exists())


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--package', type=Path)
    args, remaining = parser.parse_known_args()
    PACKAGE = args.package.resolve() if args.package else None
    # unittest's decorator runs before argument parsing, so enable the opt-in suite here.
    StandalonePackageTests.__unittest_skip__ = PACKAGE is None
    unittest.main(argv=[sys.argv[0], *remaining])
