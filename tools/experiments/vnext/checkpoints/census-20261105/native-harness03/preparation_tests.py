#!/usr/bin/env python3
"""Authored data-only preparer regressions; NOT EXECUTED at this checkpoint.

Future explicit data-stage invocation: python -B preparation_tests.py
Synthetic source/archive bytes test preparation only, never compiler admission.
Mocks inject filesystem faults, not oracle results or successful preparation.
"""
import contextlib
import hashlib
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
import prepare_cases
import prepare_sources


class PreparationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.parent = Path(self.temporary.name).resolve()
        self.output = self.parent/'prepared'

    def stages(self):
        return list(self.parent.glob('.prepared.incomplete-*'))

    def source_args(self):
        root = self.parent/'root'
        root.mkdir()
        for name, _ in prepare_sources.SOURCE_MAP:
            path = root/name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'pub Version: u32 : 4;\n')
        vnext = root/prepare_sources.VNEXT
        for name, data in (
                ('source_census/Main.chibi', (prepare_sources.HERE/'Main.chibi').read_bytes()),
                ('slice_backend/foreign.h', b'/* synthetic test header */\n'),
                ('owned_process.py', b'# synthetic test controller, not executed\n')):
            path = vnext/name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        backend, adapter = self.parent/'backend.a', self.parent/'adapter.a'
        backend.write_bytes(b'synthetic backend, not loaded')
        adapter.write_bytes(b'synthetic adapter, not loaded')
        digest = hashlib.sha256(b'pub Version: u32 : 4;\n').hexdigest()
        return SimpleNamespace(root=root, output=self.output, backend=backend,
                               adapter=adapter, model_sha256=digest, pack_sha256=digest)

    def test_late_missing_frame_leaves_no_output_or_stage(self):
        valid = self.parent/'valid'
        valid.write_bytes(b'')
        with self.assertRaises(OSError):
            prepare_cases.prepare(self.output, [valid, self.parent/'missing'])
        self.assertFalse(self.output.exists())
        self.assertEqual(self.stages(), [])
        prepare_cases.prepare(self.output, [valid])
        self.assertTrue((self.output/'cases.json').is_file())

    def test_oversized_frame_leaves_no_output_or_stage(self):
        path = self.parent/'oversized'
        with path.open('wb') as stream:
            stream.truncate(536870913)  # Sparse file, never read or mapped.
        with self.assertRaises(ValueError):
            prepare_cases.prepare(self.output, [path])
        self.assertFalse(self.output.exists())
        self.assertEqual(self.stages(), [])

    def test_frame_symlink_and_directory_are_rejected(self):
        path = self.parent/'link'
        path.symlink_to(self.parent, target_is_directory=True)
        for invalid in (path, self.parent):
            with self.subTest(path=invalid), self.assertRaises(ValueError):
                prepare_cases.prepare(self.output, [invalid])
            self.assertEqual(self.stages(), [])
            self.assertFalse(self.output.exists())

    def test_missing_parent_is_not_created(self):
        with self.assertRaises(ValueError):
            prepare_cases.prepare(self.parent/'missing'/'out', [])
        self.assertFalse((self.parent/'missing').exists())

    def test_source_bad_pin_has_no_output_or_stage(self):
        args = self.source_args()
        args.model_sha256 = '0'*64
        with self.assertRaises(ValueError):
            prepare_sources.prepare(args)
        self.assertFalse(self.output.exists())
        self.assertEqual(self.stages(), [])

    def test_existing_directory_and_dangling_symlink_are_untouched(self):
        args = self.source_args()
        for kind in ('directory', 'symlink'):
            if kind == 'directory':
                self.output.mkdir()
            else:
                self.output.symlink_to(self.parent/'absent')
            for operation in (lambda: prepare_cases.prepare(self.output, []),
                              lambda: prepare_sources.prepare(args)):
                with self.assertRaises(ValueError):
                    operation()
                self.assertEqual(self.stages(), [])
            if kind == 'directory':
                self.assertEqual(list(self.output.iterdir()), [])
                self.output.rmdir()
            else:
                self.assertTrue(self.output.is_symlink())
                self.output.unlink()

    def test_transport_write_failure_retains_only_incomplete_stage(self):
        original = Path.write_bytes
        def fail(path, data):
            if path.name == 'trailing-bytes':
                raise OSError('injected transport write failure')
            return original(path, data)
        with patch.object(Path, 'write_bytes', fail), self.assertRaises(OSError):
            prepare_cases.prepare(self.output, [])
        self.assertFalse(self.output.exists())
        self.assertEqual(len(self.stages()), 1)
        self.assertFalse((self.stages()[0]/'cases.json').exists())
        prepare_cases.prepare(self.output, [])
        self.assertTrue((self.output/'cases.json').exists())
        self.assertEqual(len(self.stages()), 1)  # Old diagnostic tree untouched.

    def test_source_payload_open_failure_never_publishes(self):
        args = self.source_args()
        original = Path.open
        def fail(path, mode='r', *a, **kw):
            if mode == 'xb' and path.name == 'Main.chibi':
                raise OSError('injected source payload failure')
            return original(path, mode, *a, **kw)
        with patch.object(Path, 'open', fail), self.assertRaises(OSError):
            prepare_sources.prepare(args)
        self.assertFalse(self.output.exists())
        self.assertEqual(len(self.stages()), 1)
        self.assertFalse((self.stages()[0]/'source-manifest.json').exists())

    def test_manifest_close_failure_and_target_appearance(self):
        args = self.source_args()
        original = Path.open
        for name, operation in (
                ('cases.json', lambda: prepare_cases.prepare(self.output, [])),
                ('source-manifest.json', lambda: prepare_sources.prepare(args))):
            for failure in ('close', 'appeared'):
                with self.subTest(manifest=name, failure=failure):
                    @contextlib.contextmanager
                    def finish_manifest(path, mode, *a, **kw):
                        with original(path, mode, *a, **kw) as stream:
                            yield stream
                        if failure == 'close':
                            raise OSError('injected manifest close failure')
                        self.output.mkdir()
                        (self.output/'sentinel').write_bytes(b'untouched')
                    def intercept(path, mode='r', *a, **kw):
                        if mode == 'x' and path.name == name:
                            return finish_manifest(path, mode, *a, **kw)
                        return original(path, mode, *a, **kw)
                    prior = len(self.stages())
                    with patch.object(Path, 'open', intercept), self.assertRaises((OSError, ValueError)):
                        operation()
                    self.assertEqual(len(self.stages()), prior+1)
                    if failure == 'close':
                        self.assertFalse(self.output.exists())
                    else:
                        self.assertEqual((self.output/'sentinel').read_bytes(), b'untouched')
                        self.assertEqual(list(self.output.iterdir()), [self.output/'sentinel'])
                        (self.output/'sentinel').unlink()
                        self.output.rmdir()

    def test_rename_failure_retains_stage_without_final(self):
        args = self.source_args()
        for operation in (lambda: prepare_cases.prepare(self.output, []),
                          lambda: prepare_sources.prepare(args)):
            prior = len(self.stages())
            with patch.object(Path, 'rename', side_effect=OSError('injected rename failure')):
                with self.assertRaises(OSError):
                    operation()
            self.assertFalse(self.output.exists())
            self.assertEqual(len(self.stages()), prior+1)

    def test_success_has_all_controls_and_no_stage(self):
        prepare_cases.prepare(self.output, [])
        self.assertEqual(self.stages(), [])
        self.assertEqual(len(list((self.output/'invalid-transport').iterdir())), 9)
        manifest = json.loads((self.output/'cases.json').read_text())
        self.assertEqual(manifest['native_status'], 'not_run')
        self.assertEqual(manifest['oracle_sha256'], hashlib.sha256(
            (prepare_sources.HERE/'reference.py').read_bytes()).hexdigest())
        self.assertTrue((self.output/'cases.bin').is_file())

    def test_success_source_identity_and_final_embedded_path(self):
        args = self.source_args()
        prepare_sources.prepare(args)
        self.assertEqual(self.stages(), [])
        manifest = json.loads((self.output/'source-manifest.json').read_text())
        for name, expected in manifest['files'].items():
            self.assertEqual(prepare_sources.identity((self.output/name).read_bytes()), expected)
        self.assertIn(str(self.output/'run/normal/source-native.o'),
                      (self.output/'entry/Main.chibi').read_text())
        self.assertTrue((self.output/'run/normal').is_dir())
        self.assertEqual(set(manifest['stages'].values()), {'not_run'})


if __name__ == '__main__':
    unittest.main()
