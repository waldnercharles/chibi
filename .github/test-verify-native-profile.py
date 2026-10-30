#!/usr/bin/env python3
"""Offline profile-verifier fixtures; real build evidence is checked in CI."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('native_profile', Path(__file__).with_name('verify-native-profile.py'))
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)


def flags(sanitized):
    return ['-g', '-O1', '-fno-omit-frame-pointer', '-UNDEBUG'] + (
        ['-fsanitize=address,undefined'] if sanitized else [])


class ProfileTests(unittest.TestCase):
    def test_profiles_reject_drift(self):
        for sanitized in (False, True):
            good = flags(sanitized)
            profile.compile_flags(good, sanitized, True)
            for bad in ('-O0', '-DNDEBUG', '-DNDEBUG=1', '-g0', '-fomit-frame-pointer',
                        '-fno-sanitize=undefined', '-fsanitize=thread'):
                with self.subTest(sanitized=sanitized, bad=bad), self.assertRaises(ValueError):
                    profile.compile_flags(good + [bad], sanitized, True)
            for required in good:
                with self.subTest(missing=required), self.assertRaises(ValueError):
                    profile.compile_flags([arg for arg in good if arg != required], sanitized, True)
        with self.assertRaises(ValueError):
            profile.compile_flags(flags(True), False)
        with self.assertRaises(ValueError):
            profile.compile_flags(flags(False), True)

    def test_observed_argv_not_just_flags(self):
        command = ['clang', '-g', '-c', 'source.c', '-o', 'object.o']
        self.assertTrue(profile.observed(command, [['ccache', *command, '-MD', '-MF', 'object.d']]))
        self.assertFalse(profile.observed(command, [['clang', '-g', '-c', 'different.c', '-o', 'object.o']]))

    def fixture(self, root, sanitized, graph):
        database = []
        commands = []
        links = []
        for name, (target, source) in profile.REPRESENTATIVES.items():
            output = root / ('objects' if graph else 'CMakeFiles') / (target if graph else target + '.dir') / 'file.o'
            output.parent.mkdir(parents=True, exist_ok=True)
            output.touch()
            arguments = ['clang', *flags(sanitized), '-o', str(output), '-c', '/source/' + source]
            database.append(dict(file='/source/' + source, output=str(output), arguments=arguments))
            commands.append(arguments)
            Path(str(output) + '.chibi-record').write_text('\n'.join(arguments) + '\n\ninputs\nstamp\n')
        for target in ('test_tools', 'test_language'):
            directory = root / 'test' / target
            directory.mkdir(parents=True)
            binary = directory / target
            binary.touch()
            output = str(binary) if graph else target
            arguments = ['clang', *flags(sanitized), 'file.o', '-o', output]
            links.append(dict(output=str(binary), arguments=arguments))
            commands.append(arguments)
            Path(str(binary) + '.chibi-record').write_text('\n'.join(arguments) + '\n\ninputs\nstamp\n')
            recipe = directory / 'CMakeFiles' / (target + '.dir') / 'link.txt'
            recipe.parent.mkdir(parents=True)
            recipe.write_text(' '.join(arguments))
        (root / 'compile_commands.json').write_text(json.dumps(database))
        if graph:
            (root / 'link_commands.json').write_text(json.dumps(links))
        header = root / 'test/tools/testing/include/export_compiler.h'
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text('\n'.join(json.dumps(arg) + ',' for arg in ['clang', *flags(sanitized)]))
        (root / 'CMakeCache.txt').write_text('CMAKE_BUILD_TYPE:STRING=Debug\nCHIBI_ENABLE_SANITIZERS:BOOL=' + ('ON' if sanitized else 'OFF'))
        log = root / 'build.log'
        log.write_text('\n'.join(' '.join(command) for command in commands))
        return log

    def test_full_receipts_and_missing_execution_evidence(self):
        for sanitized in (False, True):
            for graph in (False, True):
                with self.subTest(sanitized=sanitized, graph=graph), tempfile.TemporaryDirectory() as temp:
                    root = Path(temp)
                    log = self.fixture(root, sanitized, graph)
                    symbols = '__asan_init\n__ubsan_handle_add_overflow' if sanitized else 'main'
                    with patch.object(profile.subprocess, 'check_output', return_value=symbols):
                        receipt = profile.verify(root, log, sanitized)
                        self.assertEqual(len(receipt['compile']), 4)
                        self.assertEqual(len(receipt['links']), 2)
                        if graph:
                            next(root.glob('objects/**/*.chibi-record')).write_text('different command\n\n')
                        else:
                            log.write_text('no compiler commands')
                        with self.assertRaises(ValueError):
                            profile.verify(root, log, sanitized)

    def test_ninja_verbose_commands(self):
        for sanitized in (False, True):
            with self.subTest(sanitized=sanitized), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                log = self.fixture(root, sanitized, False)
                text = log.read_text()
                for target in ('test_tools', 'test_language'):
                    recipe = next(root.rglob(f'CMakeFiles/{target}.dir/link.txt'))
                    old = recipe.read_text()
                    new = old.replace('-o ' + target, '-o test/' + target + '/' + target)
                    text = text.replace(old, '[12/12] : && ' + new + ' && :')
                    recipe.unlink()
                log.write_text(text)
                (root / 'build.ninja').touch()
                symbols = '__asan_init\n__ubsan_handle_add_overflow' if sanitized else 'main'
                with patch.object(profile.subprocess, 'check_output', return_value=symbols):
                    receipt = profile.verify(root, log, sanitized)
                    self.assertEqual(len(receipt['links']), 2)

    def test_symbol_presence_is_checked(self):
        for sanitized, symbols in ((False, '__asan_init'), (False, '__ubsan_handle_add'),
                                   (True, 'main'), (True, '__asan_init')):
            with patch.object(profile.subprocess, 'check_output', return_value=symbols), self.assertRaises(ValueError):
                profile.symbols(Path('binary'), sanitized)


if __name__ == '__main__':
    unittest.main()
