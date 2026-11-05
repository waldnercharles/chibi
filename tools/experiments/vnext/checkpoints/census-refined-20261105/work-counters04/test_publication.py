"""AUTHORED, NOT EXECUTED. Data-only publication boundary tests.

Run only after independent review and explicit execution authorization.
No prepare(), source transforms, Chibi/compiler/native code, or count oracle is
called. IO fault injection models environment failures, never compiler success.
"""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from instrument_prepared import digest, publish_prepared


class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.parent = Path(self.temporary.name).resolve()
        self.output = self.parent / 'published'
        self.data = {'a.bin': b'first\x00', 'nested/b.bin': b'second\xff'}
        self.receipt = {'files': [
            {'path': name, 'bytes': len(value), 'sha256': digest(value)}
            for name, value in sorted(self.data.items())]}

    def stages(self):
        return list(self.parent.glob('.incomplete-published-*'))

    def publish(self):
        publish_prepared(self.data, self.receipt, self.output)

    def assert_complete(self, root):
        self.assertEqual(
            {str(p.relative_to(root)) for p in root.rglob('*') if p.is_file()},
            set(self.data) | {'source-manifest.json'})
        for name, value in self.data.items():
            self.assertEqual((root / name).read_bytes(), value)
        self.assertEqual(json.loads((root / 'source-manifest.json').read_bytes()), self.receipt)

    def test_success_publishes_complete_tree_once_with_manifest_last(self):
        opened = []
        open_path, rename_path = Path.open, Path.rename
        def observe_open(path, *args, **kwargs):
            if args and args[0] == 'xb':
                self.assertFalse(self.output.exists())
                opened.append(path.name)
            return open_path(path, *args, **kwargs)
        def observe_rename(path, target):
            self.assertEqual(path.parent, self.output.parent)
            self.assertEqual(target, self.output)
            self.assert_complete(path)
            return rename_path(path, target)
        with patch.object(Path, 'open', observe_open), patch.object(Path, 'rename', observe_rename):
            self.publish()
        self.assertEqual(opened, ['a.bin', 'b.bin', 'source-manifest.json'])
        self.assert_complete(self.output)
        self.assertEqual(self.stages(), [])

    def test_existing_targets_are_untouched(self):
        for kind in ('file', 'directory', 'dangling_symlink'):
            with self.subTest(kind=kind):
                self.output = self.parent / kind
                if kind == 'file':
                    self.output.write_bytes(b'keep')
                elif kind == 'directory':
                    self.output.mkdir()
                else:
                    self.output.symlink_to(self.parent / 'absent')
                with self.assertRaises(ValueError):
                    self.publish()
                if kind == 'file':
                    self.assertEqual(self.output.read_bytes(), b'keep')
                elif kind == 'directory':
                    self.assertEqual(list(self.output.iterdir()), [])
                else:
                    self.assertTrue(self.output.is_symlink())
        self.assertEqual(list(self.parent.glob('.incomplete-*')), [])

    def test_missing_parent_is_not_created(self):
        self.output = self.parent / 'absent' / 'published'
        with self.assertRaises(ValueError):
            self.publish()
        self.assertFalse(self.output.parent.exists())

    def test_stage_allocation_failure_leaves_final_absent(self):
        with patch('instrument_prepared.tempfile.mkdtemp', side_effect=OSError('injected allocation')):
            with self.assertRaises(OSError):
                self.publish()
        self.assertFalse(self.output.exists())
        self.assertEqual(self.stages(), [])

    def test_payload_and_manifest_io_failures_retain_stage_only(self):
        open_path, read_path = Path.open, Path.read_bytes
        for name in ('b.bin', 'source-manifest.json'):
            for fault in ('open', 'write', 'short_write', 'close', 'read', 'mismatch'):
                with self.subTest(name=name, fault=fault):
                    previous = set(self.stages())
                    class FaultStream:
                        def __init__(self, stream):
                            self.stream = stream
                        def __enter__(self):
                            self.stream.__enter__()
                            return self
                        def write(self, value):
                            if fault == 'write':
                                self.stream.write(value[:1])
                                raise OSError('injected write')
                            if fault == 'short_write':
                                return self.stream.write(value[:1])
                            return self.stream.write(value)
                        def __exit__(self, *args):
                            result = self.stream.__exit__(*args)
                            if fault == 'close':
                                raise OSError('injected close')
                            return result
                    def fail_open(path, *args, **kwargs):
                        if path.name == name and args and args[0] == 'xb':
                            if fault == 'open':
                                raise OSError('injected open')
                            return FaultStream(open_path(path, *args, **kwargs))
                        return open_path(path, *args, **kwargs)
                    def fail_read(path):
                        if path.name == name:
                            if fault == 'read':
                                raise OSError('injected read')
                            if fault == 'mismatch':
                                return b'changed'
                        return read_path(path)
                    with patch.object(Path, 'open', fail_open), patch.object(Path, 'read_bytes', fail_read):
                        with self.assertRaises(RuntimeError) as caught:
                            self.publish()
                    self.assertIsInstance(caught.exception.__cause__, OSError)
                    self.assertIn('before publication', str(caught.exception))
                    self.assertFalse(self.output.exists())
                    added = set(self.stages()) - previous
                    self.assertEqual(len(added), 1)
                    stage = added.pop()
                    self.assertIn(str(stage), str(caught.exception))
                    self.assertEqual((stage / 'a.bin').read_bytes(), self.data['a.bin'])
                    if name == 'b.bin':
                        self.assertFalse((stage / 'source-manifest.json').exists())

    def test_target_appearing_before_rename_is_not_replaced(self):
        read_path = Path.read_bytes
        def introduce_target(path):
            value = read_path(path)
            if path.name == 'source-manifest.json':
                self.output.mkdir()
            return value
        with patch.object(Path, 'read_bytes', introduce_target):
            with self.assertRaises(RuntimeError) as caught:
                self.publish()
        self.assertIsInstance(caught.exception.__cause__, FileExistsError)
        self.assertEqual(list(self.output.iterdir()), [])
        self.assertEqual(len(self.stages()), 1)
        self.assert_complete(self.stages()[0])

    def test_rename_error_requires_reconciliation_even_after_effect(self):
        rename_path = Path.rename
        for after_effect in (False, True):
            with self.subTest(after_effect=after_effect):
                previous = set(self.stages())
                def fail_rename(path, target):
                    if after_effect:
                        rename_path(path, target)
                    raise OSError('injected uncertain rename')
                with patch.object(Path, 'rename', fail_rename):
                    with self.assertRaises(RuntimeError) as caught:
                        self.publish()
                self.assertIn('rename outcome uncertain', str(caught.exception))
                self.assertIn(str(self.output), str(caught.exception))
                self.assertIsInstance(caught.exception.__cause__, OSError)
                self.assertEqual(self.output.exists(), after_effect)
                added = set(self.stages()) - previous
                self.assertEqual(len(added), 0 if after_effect else 1)
                self.assert_complete(self.output if after_effect else added.pop())


if __name__ == '__main__':
    unittest.main()
