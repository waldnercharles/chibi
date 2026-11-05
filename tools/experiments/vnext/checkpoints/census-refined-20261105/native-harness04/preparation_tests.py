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
import re
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
import prepare_cases
import prepare_sources


# Independently frozen bridge/prepare_native.py closure contract (2026-11-05),
# expanded through typed/prepare.py and frontend/prepare.py by source inspection.
# Do not derive this fixture from prepare_sources.SOURCE_MAP: omissions and source
# swaps in that implementation are precisely what the success test must detect.
EXPECTED_CLOSURE = (
    ('tools/compiler/source_parser/Declarations.chibi', 'project/source_parser/Declarations.chibi'),
    ('tools/compiler/source_parser/Expressions.chibi', 'project/source_parser/Expressions.chibi'),
    ('tools/compiler/source_parser/Parser.chibi', 'project/source_parser/Parser.chibi'),
    ('tools/compiler/source_parser/Statements.chibi', 'project/source_parser/Statements.chibi'),
    ('tools/compiler/source_parser/Tree.chibi', 'project/source_parser/Tree.chibi'),
    ('tools/compiler/source_parser/Types.chibi', 'project/source_parser/Types.chibi'),
    ('tools/compiler/source_tokenizer/Tokenizer.chibi', 'project/source_tokenizer/Tokenizer.chibi'),
    ('tools/compiler/syntax_diagnostics/Diagnostics.chibi', 'project/syntax_diagnostics/Diagnostics.chibi'),
    ('tools/compiler/json_string/JsonString.chibi', 'project/json_string/JsonString.chibi'),
    ('tools/compiler/lexical_binding/Binding.chibi', 'project/lexical_binding/Binding.chibi'),
    ('tools/compiler/named_binding/Binding.chibi', 'project/named_binding/Binding.chibi'),
    ('tools/compiler/signature_reader/Reader.chibi', 'project/signature_reader/Reader.chibi'),
    ('tools/compiler/type_constructors/Constructors.chibi', 'project/type_constructors/Constructors.chibi'),
    ('tools/compiler/unit_algebra/Units.chibi', 'project/unit_algebra/Units.chibi'),
    ('tools/compiler/package_graph/Graph.chibi', 'project/package_graph/Graph.chibi'),
    ('packages/core/strings/Strings.chibi', 'core/strings/Strings.chibi'),
    ('packages/core/os/Os.chibi', 'core/os/Os.chibi'),
    ('packages/core/bytes/Bytes.chibi', 'core/bytes/Bytes.chibi'),
    ('packages/core/bytes/Tests.chibi', 'core/bytes/Tests.chibi'),
    ('packages/core/hash/sha256/Sha256.chibi', 'core/hash/sha256/Sha256.chibi'),
    ('packages/core/hash/sha256/Tests.chibi', 'core/hash/sha256/Tests.chibi'),
    ('packages/core/sys/posix/Posix.chibi', 'core/sys/posix/Posix.chibi'),
    ('packages/core/target/Target.chibi', 'core/target/Target.chibi'),
    ('packages/core/sys/windows/Decimal.chibi', 'core/sys/windows/Decimal.chibi'),
    ('packages/core/sys/windows/Windows.chibi', 'core/sys/windows/Windows.chibi'),
    ('tools/experiments/vnext/frontend/typed/Model.chibi', 'project/vnext_typed/Model.chibi'),
    ('tools/experiments/vnext/frontend/typed/Produce.chibi', 'project/vnext_typed/Produce.chibi'),
    ('tools/experiments/vnext/frontend/typed/Verify.chibi', 'project/vnext_typed/Verify.chibi'),
    ('tools/experiments/vnext/frontend/bridge/generated/Pack.chibi', 'project/vnext_bridge/Pack.chibi'),
)


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
        self.source_bytes = {}
        for name, target in EXPECTED_CLOSURE:
            path = root/name
            path.parent.mkdir(parents=True, exist_ok=True)
            data = ('// synthetic source: '+name+'\n').encode()
            if target == 'project/vnext_bridge/Pack.chibi':
                data += b'pub Version: u32 : 4;\n'
            path.write_bytes(data)
            self.source_bytes[target] = data
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
        model_digest = hashlib.sha256(self.source_bytes['project/vnext_typed/Model.chibi']).hexdigest()
        pack_digest = hashlib.sha256(self.source_bytes['project/vnext_bridge/Pack.chibi']).hexdigest()
        return SimpleNamespace(root=root, output=self.output, backend=backend,
                               adapter=adapter, model_sha256=model_digest, pack_sha256=pack_digest)

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
        # Literal CCN1 fixtures and tuples, not serialized or evaluated by the
        # implementation under test. The second external input is deliberately
        # named earlier lexically, so sorting the inputs cannot hide an order bug.
        empty_frame = bytes.fromhex(
            '43434e31 01000000 01000000 00000000 01000000 01000000 00000000')
        empty_tuple = (0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 28)
        replacement_frame = bytes.fromhex(
            '43434e31 01000000 01000000 01000000 '
            '02000000 01000000 03000000 610a20 '
            '03000000 01000000 04000000 620a0a63')
        replacement_tuple = (0, 0, 1, 1, 3, 2, 1, 1, 3, 2, 1, 1, 4, 3, 2, 47)
        extra = [(self.parent/'z-replacement', replacement_frame, replacement_tuple),
                 (self.parent/'a-short', b'BAD!', (1, 0, 0, 0, 0, 0, 0, 0,
                                                 0, 0, 0, 0, 0, 0, 0, 4))]
        for path, data, _ in extra:
            path.write_bytes(data)
        prepare_cases.prepare(self.output, [path for path, _, _ in extra])
        self.assertEqual(self.stages(), [])
        manifest = json.loads((self.output/'cases.json').read_text())
        self.assertEqual(manifest['native_status'], 'not_run')
        self.assertEqual(manifest['full_domain_status'], 'not_claimed')
        self.assertEqual(manifest['oracle_sha256'], hashlib.sha256(
            (prepare_sources.HERE/'reference.py').read_bytes()).hexdigest())
        transport = (self.output/'cases.bin').read_bytes()
        self.assertEqual(transport[:8], b'CCTEST1\n')
        self.assertGreaterEqual(len(transport), 16)
        count = int.from_bytes(transport[8:16], 'little')
        self.assertEqual(count, 115)  # 113 retained corpus cases + two external.
        self.assertEqual(len(manifest['cases']), count)
        at = 16
        decoded = []
        for row in manifest['cases']:
            self.assertGreaterEqual(len(transport)-at, 136)
            extent = int.from_bytes(transport[at:at+8], 'little')
            expected = tuple(int.from_bytes(transport[pos:pos+8], 'little', signed=True)
                             for pos in range(at+8, at+136, 8))
            at += 136
            self.assertLessEqual(extent, len(transport)-at)
            data = transport[at:at+extent]
            at += extent
            self.assertEqual(row['bytes'], extent)
            self.assertEqual(row['expected'], list(expected))
            self.assertEqual(row['sha256'], hashlib.sha256(data).hexdigest())
            decoded.append((row['name'], data, expected, row['source']))
        self.assertEqual(at, len(transport))  # No trailing or omitted frame bytes.
        self.assertEqual(decoded[0], ('payload-0', empty_frame, empty_tuple, None))
        self.assertTrue(all(source is None for _, _, _, source in decoded[:-2]))
        self.assertEqual(decoded[-2:], [(path.name, data, expected, str(path))
                                      for path, data, expected in extra])

        # Independently spell the nine malformed envelopes; no production
        # frame/record/struct helper or oracle contributes expected control bytes.
        one = b'CCTEST1\n'+b'\x01'+b'\0'*7
        valid = (one+b'\x1c'+b'\0'*7+
                 b''.join(value.to_bytes(8, 'little', signed=True) for value in empty_tuple)+
                 empty_frame)
        controls = {
            'empty': b'',
            'magic': b'BADTEST\n'+b'\x01'+b'\0'*7,
            'zero-count': b'CCTEST1\n'+b'\0'*8,
            'excess-count': b'CCTEST1\n'+bytes.fromhex('a186010000000000'),
            'missing-size': one,
            'excess-size': one+bytes.fromhex('0100002000000000'),
            'missing-tuple': one+b'\0'*8,
            'truncated-payload': valid[:-1],
            'trailing-bytes': valid+b'x',
        }
        directory = self.output/'invalid-transport'
        self.assertEqual({path.name for path in directory.iterdir()}, set(controls))
        for name, expected in controls.items():
            self.assertEqual((directory/name).read_bytes(), expected, name)

    def test_success_source_identity_and_final_embedded_path(self):
        args = self.source_args()
        # Preserve original source bytes before preparation rather than trusting
        # the output manifest to define either membership or expected contents.
        expected = dict(self.source_bytes)
        fixtures = [('source_census/Main.chibi', (args.root/
                     'tools/experiments/vnext/source_census/Main.chibi').read_bytes()),
                    ('source_census/Capabilities.chibi',
                     (prepare_sources.HERE/'Capabilities.chibi').read_bytes())]
        for name, data in fixtures:
            expected['inputs/'+name] = data
        for name in ('native_adapter.c', 'native_adapter.h', 'aot_main.c',
                     'host-contract.h', 'CensusMain.chibi.in', 'Capabilities.chibi',
                     'reference.py', 'prepare_cases.py', 'prepare_sources.py',
                     'preparation_tests.py', 'README.md'):
            expected['inputs/'+name] = (prepare_sources.HERE/name).read_bytes()
        expected['inputs/foreign.h'] = (args.root/
            'tools/experiments/vnext/slice_backend/foreign.h').read_bytes()
        expected['controller/owned_process.py'] = (args.root/
            'tools/experiments/vnext/owned_process.py').read_bytes()
        template = (prepare_sources.HERE/'CensusMain.chibi.in').read_text()
        # Split the original template, then interleave independently specified
        # JSON literals. Unlike production's regex substitution, inserted source
        # text is never searched for another marker.
        slots = re.split(r'("LIBRARY_PATH"|"ADAPTER_PATH"|FIXTURE_SOURCES|OUTPUT_PATH)', template)
        rows = ['{path: '+json.dumps(name)+', text: '+
                json.dumps(data.decode('utf-8'), ensure_ascii=False)+
                ', document: '+str(document)+'}'
                for document, (name, data) in enumerate(fixtures, 19)]
        substitutions = {'"LIBRARY_PATH"': json.dumps(str(args.backend)),
                         '"ADAPTER_PATH"': json.dumps(str(args.adapter)),
                         'FIXTURE_SOURCES': '[\n'+',\n'.join(rows)+'\n]',
                         'OUTPUT_PATH': json.dumps(str(self.output/'run/normal/source-native.o'))}
        self.assertCountEqual(slots[1::2], substitutions)
        expected['entry/Main.chibi'] = ''.join(
            substitutions[part] if index % 2 else part
            for index, part in enumerate(slots)).encode('utf-8')
        prepare_sources.prepare(args)
        self.assertEqual(self.stages(), [])
        manifest = json.loads((self.output/'source-manifest.json').read_text())
        self.assertEqual(set(manifest['files']), set(expected))
        self.assertEqual({str(path.relative_to(self.output)) for path in self.output.rglob('*')
                          if path.is_file()}, set(expected) | {'source-manifest.json'})
        for name, data in expected.items():
            self.assertEqual((self.output/name).read_bytes(), data, name)
        # Retain the separate write/manifest integrity check.
        for name, identity in manifest['files'].items():
            data = (self.output/name).read_bytes()
            self.assertEqual({'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}, identity)
        self.assertIn(str(self.output/'run/normal/source-native.o'),
                      (self.output/'entry/Main.chibi').read_text())
        self.assertTrue((self.output/'run/normal').is_dir())
        self.assertEqual(set(manifest['stages'].values()), {'not_run'})


if __name__ == '__main__':
    unittest.main()
