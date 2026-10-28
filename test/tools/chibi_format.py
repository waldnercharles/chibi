#!/usr/bin/env python3
"""Native formatter/config/filesystem and repository-enforcement boundaries."""
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('repository_format', ROOT / 'tools/chibi_format.py')
repo = importlib.util.module_from_spec(spec)
spec.loader.exec_module(repo)
BINARY = repo.formatter()
MESSY = b'package p\nF::()->int {return 1;}\n'
CANONICAL = b'package p\nF :: () -> int {\n    return 1;\n}\n'


class Fixture(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='chibi-format-test-')
        self.root = Path(self.temporary.name)
        (self.root / '.git').mkdir()
        self.source = self.write('src/Example.chibi', MESSY)

    def tearDown(self):
        self.temporary.cleanup()

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(text.encode() if isinstance(text, str) else text)
        return path

    def run_format(self, *arguments, source=None):
        return subprocess.run([str(BINARY), *map(str, arguments)], input=source, capture_output=True)


class Formatter(Fixture):
    def test_check_is_read_only_then_write_is_idempotent(self):
        self.assertEqual(self.run_format('--check', self.source).returncode, 1)
        self.assertEqual(self.source.read_bytes(), MESSY)
        self.assertEqual(self.run_format(self.source).returncode, 0)
        self.assertEqual(self.source.read_bytes(), CANONICAL)
        self.assertEqual(self.run_format('--check', self.source).returncode, 0)

    def test_nearest_config_replaces_parent_and_ignores_cwd(self):
        self.write('.chibi-format', 'indent_width = 2\nremove_optional_semicolons = true\n')
        self.write('src/.chibi-format', 'use_tabs = true\n')
        result = self.run_format('--stdin', self.source, source=MESSY)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b'\treturn 1;', result.stdout)  # no inheritance of removal
        self.assertEqual(self.source.read_bytes(), MESSY)

    def test_explicit_config_overrides_nearest(self):
        self.write('.chibi-format', 'unknown = true\n')
        config = self.write('settings', 'indent_width = 2\nremove_optional_semicolons = true\n')
        result = self.run_format('--stdin', self.source, '--config', config, source=MESSY)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b'\n  return 1\n', result.stdout)
        self.assertNotIn(b';', result.stdout)

    def test_repository_boundary_stops_discovery(self):
        self.write('.chibi-format', 'indent_width = 2\n')
        self.write('src/.git', 'gitdir: elsewhere\n')
        result = self.run_format('--stdin', self.source, source=MESSY)
        self.assertEqual(result.stdout, CANONICAL)

    def test_invalid_configs_fail_without_writes(self):
        for value in ('indent_width = 0', 'indent_width = 17', 'preferred_width = -1',
                      'preferred_width = 4097', 'use_tabs = yes', 'unknown = 1',
                      'indent_width = 2\nindent_width = 4', 'indent_width 2',
                      'remove_optional_semicolons = 1', 'indent_width = 4junk',
                      'indent_width = 9999999999999999999999999', 'x' * 4097):
            with self.subTest(value=value[:80]):
                self.write('.chibi-format', value)
                self.assertEqual(self.run_format(self.source).returncode, 1)
                self.assertEqual(self.source.read_bytes(), MESSY)

    def test_missing_explicit_config_fails(self):
        self.assertEqual(self.run_format('--config', self.root / 'absent', self.source).returncode, 1)

    def test_project_package_standalone_and_hidden_sources_share_policy(self):
        self.write('project.kdl', 'version 99\n')  # syntax formatter needs no project catalog
        self.write('content/behaviors/Move.chibi', 'Move::behavior {x:int=1;}\n')
        self.write('compiler/deep/Compile.chibi', MESSY)
        self.write('.hidden/Tests.chibi', MESSY)
        self.write('.chibi/cache/Invalid.chibi', 'not current source {')
        self.assertEqual(self.run_format(self.root).returncode, 0)
        self.assertEqual(self.run_format('--check', self.root).returncode, 0)
        self.assertIn(b'x: int = 1;', (self.root / 'content/behaviors/Move.chibi').read_bytes())
        self.assertEqual((self.root / '.hidden/Tests.chibi').read_bytes(), CANONICAL)

    def test_invalid_descendant_prevents_all_source_writes(self):
        invalid = self.write('z/Invalid.chibi', 'Broken :: behavior {')
        before = invalid.read_bytes()
        self.assertEqual(self.run_format(self.root).returncode, 1)
        self.assertEqual(self.source.read_bytes(), MESSY)
        self.assertEqual(invalid.read_bytes(), before)

    def test_symlink_files_and_directories_are_rejected(self):
        for directory in (False, True):
            link = self.root / 'link'
            link.symlink_to(self.source.parent if directory else self.source, target_is_directory=directory)
            self.assertEqual(self.run_format(self.root).returncode, 1)
            self.assertEqual(self.source.read_bytes(), MESSY)
            link.unlink()

    def test_config_symlink_is_rejected(self):
        actual = self.write('settings', 'indent_width = 2\n')
        (self.root / '.chibi-format').symlink_to(actual)
        self.assertEqual(self.run_format(self.source).returncode, 1)

    def test_missing_explicit_config_fails_for_empty_directory(self):
        empty = self.root / 'empty'
        empty.mkdir()
        self.assertEqual(self.run_format('--config', self.root / 'missing', empty).returncode, 1)

    def test_nul_input_and_config_rejected(self):
        self.source.write_bytes(MESSY + b'\0')
        self.assertEqual(self.run_format(self.source).returncode, 1)
        self.source.write_bytes(MESSY)
        self.write('.chibi-format', b'indent_width = 2\0\n')
        self.assertEqual(self.run_format(self.source).returncode, 1)

    def test_file_permissions_retained(self):
        self.source.chmod(0o640)
        self.assertEqual(self.run_format(self.source).returncode, 0)
        self.assertEqual(self.source.stat().st_mode & 0o777, 0o640)

    def test_stdin_preserves_embedded_comment_line_endings(self):
        source = b'package p\r\n/* first\r\nsecond */\r\nF::()->int {return 1;}\r\n'
        result = self.run_format('--stdin', self.source, source=source)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b'/* first\r\nsecond */', result.stdout)
        self.assertTrue(result.stdout.endswith(b'}\n'))
        self.assertNotIn(b'}\r\n', result.stdout)

    def test_new_buffer_filename_uses_parent_configuration(self):
        self.write('src/.chibi-format', 'indent_width = 2\n')
        result = self.run_format('--stdin', self.source.parent / 'New.chibi', source=MESSY)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b'\n  return 1;', result.stdout)


class Enforcement(Fixture):
    def setUp(self):
        super().setUp()
        shutil.rmtree(self.root / '.git')
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        self.write('tools/format/exceptions.json', json.dumps({'version': 1, 'exact_bytes': {}, 'generated': {}}))
        self.source.write_bytes(CANONICAL)
        self.add()

    def add(self):
        subprocess.run(['git', '-C', str(self.root), 'add', '.'], check=True)

    def check(self, **kwargs):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return repo.check_tree(self.root, BINARY, repo.inventory(self.root), **kwargs)

    def manifest(self, exact=None, generated=None):
        self.write('tools/format/exceptions.json', json.dumps({'version': 1, 'exact_bytes': exact or {}, 'generated': generated or {}}))

    def test_all_tracked_directories_included(self):
        for directory in ('tools/compiler/nested', 'test', 'src', '.hidden'):
            self.write(directory + '/New.chibi', MESSY)
        self.add()
        self.assertEqual(self.check(), 1)
        self.assertEqual(self.check(write=True), 0)
        self.assertEqual(self.check(), 0)

    def test_exact_fixture_hash_and_expected_failure_are_both_required(self):
        fixture = self.write('test/Invalid.chibi', 'Old historical syntax {')
        self.write('test/owner.c', '// owns the migration oracle\n')
        entry = {'sha256': hashlib.sha256(fixture.read_bytes()).hexdigest(), 'reason': 'frozen legacy migration input', 'test': 'test/owner.c'}
        self.manifest(exact={'test/Invalid.chibi': entry})
        self.add()
        self.assertEqual(self.check(), 0)
        fixture.write_bytes(b'Changed historical syntax {')
        self.assertEqual(self.check(write=True), 1)
        self.assertEqual(fixture.read_bytes(), b'Changed historical syntax {')
        fixture.write_bytes(CANONICAL)
        entry['sha256'] = hashlib.sha256(CANONICAL).hexdigest()
        self.manifest(exact={'test/Invalid.chibi': entry})
        self.assertEqual(self.check(), 1)  # cannot exempt valid source

    def test_stale_and_broad_exceptions_rejected(self):
        for path in ('test/*.chibi', 'missing.chibi', '../outside.chibi'):
            self.manifest(exact={path: {'sha256': '0' * 64, 'reason': 'invalid', 'test': 'owner.c'}})
            with self.assertRaises(ValueError):
                self.check()

    def test_generated_sources_use_generator_consistency_not_rewrite(self):
        generated = self.write('generated/Output.chibi', CANONICAL)
        check = self.write('generator.py', 'from pathlib import Path\nassert Path("generated/Output.chibi").read_bytes() == ' + repr(CANONICAL) + '\n')
        self.manifest(generated={'generated/Output.chibi': {'reason': 'fixture generator owns exact output', 'check': [sys.executable, 'generator.py']}})
        self.add()
        self.assertEqual(self.check(write=True), 0)
        self.assertEqual(generated.read_bytes(), CANONICAL)
        generated.write_bytes(MESSY)
        self.assertEqual(self.check(), 1)
        # Even a consistent generator must itself emit canonical Chibi.
        check.write_text('from pathlib import Path\nassert Path("generated/Output.chibi").read_bytes() == ' + repr(MESSY) + '\n')
        self.assertEqual(self.check(write=True), 1)
        self.assertEqual(generated.read_bytes(), MESSY)

    def test_mutating_generator_check_cannot_change_live_files(self):
        generated = self.write('generated/Output.chibi', MESSY)
        self.write('generator.py', 'from pathlib import Path\nPath("generated/Output.chibi").write_text("mutated")\n')
        self.manifest(generated={'generated/Output.chibi': {'reason': 'negative mutation control', 'check': [sys.executable, 'generator.py']}})
        self.add()
        for write in (False, True):
            self.assertEqual(self.check(write=write), 1)
            self.assertEqual(generated.read_bytes(), MESSY)

    def test_make_check_and_configure_never_write_or_install_hooks(self):
        makefile = (ROOT / 'Makefile').read_text()
        check = makefile.split('check-format:\n', 1)[1].split('\n\n', 1)[0]
        self.assertIn('chibi_format.py --check', check)
        self.assertNotIn('--write', check)
        self.assertNotIn('config core.hooksPath', (ROOT / 'tools/configure.sh').read_text())

    def test_partially_staged_source_checks_index_not_worktree(self):
        old_root, old_inputs = repo.ROOT, repo.FORMAT_INPUTS
        repo.ROOT, repo.FORMAT_INPUTS = self.root, ()
        try:
            self.source.write_bytes(MESSY)  # unstaged, index is canonical
            before = self.source.read_bytes()
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(repo.staged(BINARY, repo.inventory(self.root)), 0)
            self.assertEqual(self.source.read_bytes(), before)
            self.add()  # index is messy, working tree becomes canonical
            self.source.write_bytes(CANONICAL)
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(repo.staged(BINARY, repo.inventory(self.root)), 1)
            self.assertEqual(self.source.read_bytes(), CANONICAL)
        finally:
            repo.ROOT, repo.FORMAT_INPUTS = old_root, old_inputs

    def test_in_repository_runtime_cache_is_not_a_staged_source_input(self):
        old_root, old_inputs = repo.ROOT, repo.FORMAT_INPUTS
        repo.ROOT, repo.FORMAT_INPUTS = self.root, ()
        try:
            binary = self.root / '.chibi/format/checker'
            binary.parent.mkdir(parents=True)
            shutil.copy2(BINARY, binary)
            runtime = self.root / '.chibi/tree-sitter'
            binary.with_suffix('.json').write_text(json.dumps({
                'runtime_root': str(runtime), 'inputs': {str(runtime / 'lib/src/lib.c'): 'pinned'}}))
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(repo.staged(binary, repo.inventory(self.root)), 0)
        finally:
            repo.ROOT, repo.FORMAT_INPUTS = old_root, old_inputs

    def test_staged_config_not_unstaged_config_controls_check(self):
        old_root, old_inputs = repo.ROOT, repo.FORMAT_INPUTS
        repo.ROOT, repo.FORMAT_INPUTS = self.root, ()
        try:
            self.write('.chibi-format', 'indent_width = 4\n')
            self.add()
            self.write('.chibi-format', 'indent_width = 2\n')
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(repo.staged(BINARY, repo.inventory(self.root)), 0)
        finally:
            repo.ROOT, repo.FORMAT_INPUTS = old_root, old_inputs

    def staged_write(self):
        with mock.patch.object(repo, 'ROOT', self.root), mock.patch.object(repo, 'FORMAT_INPUTS', ()), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return repo.staged(BINARY, repo.inventory(self.root), write=True)

    def index_bytes(self):
        return (self.root / '.git/index').read_bytes()

    def test_staged_write_formats_full_index_without_staging_unrelated_edits(self):
        sources = [self.source, self.write('compiler/deep/Spaced name.chibi', MESSY),
                   self.write('tools/.hidden/Line\nbreak.chibi', MESSY)]
        self.source.write_bytes(MESSY)
        self.source.chmod(0o755)
        unrelated = self.write('notes.txt', 'staged notes\n')
        self.add()
        unrelated.write_text('unstaged notes\n')
        (self.root / '.git/index').chmod(0o600)
        self.assertEqual(self.staged_write(), 0)
        for source in sources:
            self.assertEqual(source.read_bytes(), CANONICAL)
            self.assertEqual(repo.git(self.root, 'show', ':' + source.relative_to(self.root).as_posix()), CANONICAL)
        self.assertEqual(self.source.stat().st_mode & 0o777, 0o755)
        self.assertEqual(repo.inventory(self.root)['src/Example.chibi'], '100755')
        self.assertEqual((self.root / '.git/index').stat().st_mode & 0o777, 0o600)
        self.assertEqual(unrelated.read_text(), 'unstaged notes\n')
        self.assertEqual(repo.git(self.root, 'show', ':notes.txt'), b'staged notes\n')
        index = self.index_bytes()
        self.assertEqual(self.staged_write(), 0)
        self.assertEqual(self.index_bytes(), index)
        self.assertFalse((self.root / '.git/index.lock').exists())

    def test_staged_write_leaves_canonical_partial_file_and_config_alone(self):
        self.write('.chibi-format', 'indent_width = 4\n')
        self.add()
        self.source.write_bytes(MESSY)
        self.write('.chibi-format', 'indent_width = 2\n')
        index = self.index_bytes()
        self.assertEqual(self.staged_write(), 0)
        self.assertEqual(self.source.read_bytes(), MESSY)
        self.assertEqual(self.index_bytes(), index)
        self.assertEqual((self.root / '.chibi-format').read_text(), 'indent_width = 2\n')

    def test_staged_write_rejects_partial_rewrite_before_any_changes(self):
        self.source.write_bytes(MESSY)
        other = self.write('z/Partial.chibi', MESSY)
        self.add()
        partial = MESSY + b'// unstaged comment\n'
        other.write_bytes(partial)
        index = self.index_bytes()
        with self.assertRaisesRegex(ValueError, 'unstaged changes'):
            self.staged_write()
        self.assertEqual(self.source.read_bytes(), MESSY)
        self.assertEqual(other.read_bytes(), partial)
        self.assertEqual(self.index_bytes(), index)

    def test_staged_write_uses_staged_config_and_rejects_conflicting_discovery(self):
        self.source.write_bytes(MESSY)
        config = self.write('.chibi-format', 'indent_width = 2\nremove_optional_semicolons = true\n')
        self.add()
        self.assertEqual(self.staged_write(), 0)
        self.assertIn(b'\n  return 1\n', self.source.read_bytes())
        self.source.write_bytes(MESSY)
        self.add()
        for name in ('.chibi-format', 'src/.chibi-format'):
            with self.subTest(name=name):
                changed = self.write(name, 'use_tabs = true\n')
                index = self.index_bytes()
                with self.assertRaisesRegex(ValueError, 'configuration changes'):
                    self.staged_write()
                self.assertEqual(self.source.read_bytes(), MESSY)
                self.assertEqual(self.index_bytes(), index)
                changed.unlink()
                config.write_text('indent_width = 2\nremove_optional_semicolons = true\n')

    def test_staged_write_invalid_source_prevents_every_rewrite(self):
        self.source.write_bytes(MESSY)
        invalid = self.write('test/Invalid.chibi', 'Broken :: behavior {')
        self.add()
        index, before = self.index_bytes(), invalid.read_bytes()
        self.assertEqual(self.staged_write(), 1)
        self.assertEqual(self.source.read_bytes(), MESSY)
        self.assertEqual(invalid.read_bytes(), before)
        self.assertEqual(self.index_bytes(), index)

    def test_staged_write_preserves_exact_and_generated_sources(self):
        self.source.write_bytes(MESSY)
        fixture = self.write('test/Invalid.chibi', 'Old historical syntax {')
        self.write('test/owner.c', '// migration oracle\n')
        generated = self.write('generated/Output.chibi', CANONICAL)
        self.write('generator.py', 'from pathlib import Path\nassert Path("generated/Output.chibi").read_bytes() == ' + repr(CANONICAL) + '\n')
        self.manifest(exact={'test/Invalid.chibi': {
            'sha256': hashlib.sha256(fixture.read_bytes()).hexdigest(),
            'reason': 'frozen migration oracle', 'test': 'test/owner.c'}},
            generated={'generated/Output.chibi': {'reason': 'generator-owned output', 'check': [sys.executable, 'generator.py']}})
        self.add()
        self.assertEqual(self.staged_write(), 0)
        self.assertEqual(fixture.read_bytes(), b'Old historical syntax {')
        self.assertEqual(generated.read_bytes(), CANONICAL)
        self.source.write_bytes(MESSY)
        generated.write_bytes(MESSY)
        self.add()
        index = self.index_bytes()
        self.assertEqual(self.staged_write(), 1)
        self.assertEqual(generated.read_bytes(), MESSY)
        self.assertEqual(self.source.read_bytes(), MESSY)
        self.assertEqual(self.index_bytes(), index)
        generated.write_bytes(CANONICAL)
        fixture.write_text('Changed historical syntax {')
        self.add()
        index = self.index_bytes()
        self.assertEqual(self.staged_write(), 1)
        self.assertEqual(fixture.read_bytes(), b'Changed historical syntax {')
        self.assertEqual(self.source.read_bytes(), MESSY)
        self.assertEqual(self.index_bytes(), index)

    def test_staged_write_refuses_existing_index_lock(self):
        self.source.write_bytes(MESSY)
        self.add()
        lock = self.write('.git/index.lock', 'another Git operation\n')
        index = self.index_bytes()
        with self.assertRaises(FileExistsError):
            self.staged_write()
        self.assertEqual(lock.read_text(), 'another Git operation\n')
        self.assertEqual(self.source.read_bytes(), MESSY)
        self.assertEqual(self.index_bytes(), index)

    def test_staged_write_detects_index_race_before_worktree_writes(self):
        self.source.write_bytes(MESSY)
        self.add()
        original_check = repo.check_tree
        index = self.index_bytes()
        def race(*args, **kwargs):
            result = original_check(*args, **kwargs)
            # Simulate a writer ignoring Git's lock without corrupting the index.
            path = self.root / '.git/index'
            timestamp = path.stat().st_mtime_ns + 1_000_000
            os.utime(path, ns=(timestamp, timestamp))
            return result
        with mock.patch.object(repo, 'check_tree', side_effect=race):
            with self.assertRaisesRegex(ValueError, 'index changed'):
                self.staged_write()
        self.assertEqual(self.source.read_bytes(), MESSY)
        self.assertEqual(self.index_bytes(), index)
        self.assertFalse((self.root / '.git/index.lock').exists())

    def test_staged_write_detects_source_and_config_races_before_writes(self):
        self.source.write_bytes(MESSY)
        self.write('.chibi-format', 'indent_width = 4\n')
        self.add()
        original_run = subprocess.run
        for name in ('src/Example.chibi', '.chibi-format'):
            with self.subTest(name=name):
                path = self.root / name
                before, index = path.read_bytes(), self.index_bytes()
                def race(command, *args, **kwargs):
                    result = original_run(command, *args, **kwargs)
                    if 'update-index' in command:
                        path.write_bytes(before + b'// concurrent edit\n')
                    return result
                with mock.patch.object(repo.subprocess, 'run', side_effect=race):
                    with self.assertRaisesRegex(ValueError, 'changed during formatting'):
                        self.staged_write()
                self.assertEqual(path.read_bytes(), before + b'// concurrent edit\n')
                self.assertEqual(self.index_bytes(), index)
                path.write_bytes(before)

    def test_staged_write_rolls_back_own_replacements_on_late_failure(self):
        self.source.write_bytes(MESSY)
        other = self.write('z/Other.chibi', MESSY)
        self.add()
        index = self.index_bytes()
        original_replace = Path.replace
        def fail(path, target):
            if target == other:
                raise OSError('simulated replacement failure')
            return original_replace(path, target)
        with mock.patch.object(Path, 'replace', fail):
            with self.assertRaisesRegex(OSError, 'simulated replacement failure'):
                self.staged_write()
        self.assertEqual(self.source.read_bytes(), MESSY)
        self.assertEqual(other.read_bytes(), MESSY)
        self.assertEqual(self.index_bytes(), index)
        self.assertFalse(list(self.root.rglob('.chibi-format-*')))
        self.assertFalse((self.root / '.git/index.lock').exists())

    def test_staged_write_preserves_edit_racing_with_replacement(self):
        self.source.write_bytes(MESSY)
        other = self.write('z/Other.chibi', MESSY)
        self.add()
        index = self.index_bytes()
        original_replace = Path.replace
        concurrent = MESSY + b'// concurrent edit\n'
        def race(path, target):
            result = original_replace(path, target)
            if target == self.source:
                self.source.write_bytes(concurrent)
            return result
        with mock.patch.object(Path, 'replace', race):
            with self.assertRaisesRegex(ValueError, 'source changed during formatting'):
                self.staged_write()
        self.assertEqual(self.source.read_bytes(), concurrent)
        self.assertEqual(other.read_bytes(), MESSY)
        self.assertEqual(self.index_bytes(), index)

    def test_staged_write_rejects_symlinked_worktree_source_parent(self):
        self.source.write_bytes(MESSY)
        self.add()
        index = self.index_bytes()
        moved = self.root / 'external'
        self.source.parent.rename(moved)
        self.source.parent.symlink_to(moved, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'regular parent directories'):
            self.staged_write()
        self.assertEqual((moved / self.source.name).read_bytes(), MESSY)
        self.assertEqual(self.index_bytes(), index)

    def test_staged_write_respects_alternate_index(self):
        self.source.write_bytes(MESSY)
        self.add()
        index = self.index_bytes()
        alternate = self.write('.git/alternate-index', index)
        with mock.patch.dict(os.environ, {'GIT_INDEX_FILE': str(alternate)}):
            self.assertEqual(self.staged_write(), 0)
            self.assertEqual(repo.git(self.root, 'show', ':src/Example.chibi'), CANONICAL)
        self.assertEqual(self.source.read_bytes(), CANONICAL)
        self.assertEqual(self.index_bytes(), index)
        self.assertFalse((self.root / '.git/alternate-index.lock').exists())

    def test_real_precommit_formats_normal_all_and_only_commits(self):
        # The isolated repository reuses the real native formatter; no toolchain
        # install or user hook configuration is needed by this process test.
        driver = self.write('.git/format-driver.py',
            'import importlib.util\nfrom pathlib import Path\n'
            f'spec = importlib.util.spec_from_file_location("formatter", {str(ROOT / "tools/chibi_format.py")!r})\n'
            'module = importlib.util.module_from_spec(spec)\nspec.loader.exec_module(module)\n'
            f'module.ROOT = Path({str(self.root)!r})\nmodule.FORMAT_INPUTS = ()\n'
            f'module.formatter = lambda: Path({str(BINARY)!r})\nraise SystemExit(module.main())\n')
        self.write('tools/chibi_format.py', driver.read_bytes())
        hook = self.write('.git/hooks/pre-commit', (ROOT / '.githooks/pre-commit').read_bytes())
        hook.chmod(0o755)
        self.source.write_bytes(MESSY)
        self.add()
        def commit(*arguments):
            result = subprocess.run(['git', '-C', str(self.root), '-c', 'core.hooksPath=.git/hooks',
                                     '-c', 'user.name=Formatter Test', '-c', 'user.email=formatter@example.invalid',
                                     '-c', 'commit.gpgsign=false',
                                     'commit', '-q', '-m', 'Format fixture', *arguments], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
        commit()
        self.assertEqual(repo.git(self.root, 'show', 'HEAD:src/Example.chibi'), CANONICAL)
        self.source.write_bytes(MESSY.replace(b'return 1', b'return 2'))
        commit('-a')
        self.assertEqual(repo.git(self.root, 'show', 'HEAD:src/Example.chibi'), CANONICAL.replace(b'return 1', b'return 2'))
        self.write('notes.txt', 'unrelated staged content\n')
        self.add()
        self.source.write_bytes(MESSY.replace(b'return 1', b'return 3'))
        commit('--only', '--', 'src/Example.chibi')
        self.assertEqual(repo.git(self.root, 'show', 'HEAD:src/Example.chibi'), CANONICAL.replace(b'return 1', b'return 3'))
        self.assertEqual(repo.git(self.root, 'show', ':notes.txt'), b'unrelated staged content\n')
        self.assertNotIn(b'notes.txt', repo.git(self.root, 'ls-tree', '--name-only', 'HEAD'))
        self.assertFalse(list((self.root / '.git').glob('*.lock')))

    def test_opt_in_hook_selects_autoformat_not_read_only_check(self):
        hook = (ROOT / '.githooks/pre-commit').read_text()
        self.assertIn('chibi_format.py --staged-write', hook)

    def test_hook_install_is_opt_in_and_never_overwrites_custom_hook(self):
        script = ROOT / 'tools/install-hooks.sh'
        def install():
            return subprocess.run(['sh', str(script)], cwd=self.root, capture_output=True)
        hook = self.write('.git/hooks/pre-commit', '#!/bin/sh\nexit 0\n')
        self.assertEqual(install().returncode, 1)
        self.assertEqual(hook.read_text(), '#!/bin/sh\nexit 0\n')
        hook.unlink()
        subprocess.run(['git', '-C', str(self.root), 'config', '--local', 'core.hooksPath', 'custom'], check=True)
        self.assertEqual(install().returncode, 1)
        subprocess.run(['git', '-C', str(self.root), 'config', '--local', '--unset', 'core.hooksPath'], check=True)
        self.assertEqual(install().returncode, 0)
        self.assertEqual(install().returncode, 0)
        self.assertEqual(repo.git(self.root, 'config', '--local', '--get', 'core.hooksPath').strip(), b'.githooks')


if __name__ == '__main__':
    unittest.main()
