#!/usr/bin/env python3
"""Python-only preparation regressions. Never launch Chibi, LLVM or test binaries."""

import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
import types
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]


def load(path):
    module = types.ModuleType("typed_preparation_test_subject")
    module.__file__ = str(path)
    exec(compile(path.read_bytes(), str(path), "exec"), module.__dict__)
    return module


class PreparationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name).resolve()
        self.root = self.base / "checkout"
        original = load(HERE / "prepare.py")
        sources = [path for path, _ in original.SOURCE_MAP + original.INPUT_MAP]
        sources += ["tools/experiments/vnext/frontend/prepare.py",
                    "tools/experiments/vnext/frontend/typed/prepare.py"]
        for name in sources:
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, target)
        self.typed = self.root / "tools/experiments/vnext/frontend/typed"
        self.subject = load(self.typed / "prepare.py")
        self.output = self.base / "prepared"

    def reject(self, exception=ValueError):
        with self.assertRaises(exception):
            self.subject.prepare(self.output)
        self.assertFalse(self.output.exists())

    def test_exact_sources_literal_and_relative_manifest(self):
        result = self.subject.prepare(self.output)
        self.assertEqual(len(self.subject.SOURCE_MAP), 18)
        self.assertEqual(len(result["files"]), 21)
        self.assertEqual(result["compile"], "not_run")
        self.assertEqual(result["typed_tests"], "not_run")
        for row in result["files"]:
            data = (self.output / row["path"]).read_bytes()
            self.assertFalse(Path(row["path"]).is_absolute())
            self.assertEqual(row["bytes"], len(data))
            self.assertEqual(row["sha256"], hashlib.sha256(data).hexdigest())
            if "source" in row:
                self.assertFalse(Path(row["source"]).is_absolute())
                self.assertEqual(data, (self.root / row["source"]).read_bytes())
        template = (self.typed / "tests/Main.chibi.in").read_bytes()
        fixture = (self.typed / "fixtures/EchoJoin.chibi").read_bytes()
        expected = template.replace(b"FIXTURE_SOURCE", json.dumps(fixture.decode("utf-8"), ensure_ascii=False).encode("utf-8"))
        self.assertEqual((self.output / "Main.chibi").read_bytes(), expected)
        serialized = (self.output / "source-manifest.json").read_text()
        self.assertNotIn(str(self.base), serialized)
        self.assertEqual(json.loads(serialized), result)
        self.assertEqual(sorted(str(path.relative_to(self.output)) for path in self.output.rglob("*") if path.is_file()),
                         sorted([row["path"] for row in result["files"]] + ["source-manifest.json"]))

    def test_utf8_quotes_backslashes_and_crlf_roundtrip(self):
        fixture = 'package café\r\n// "quoted" \\ slash\tend\r\n'.encode("utf-8")
        (self.typed / "fixtures/EchoJoin.chibi").write_bytes(fixture)
        self.subject.prepare(self.output)
        template = (self.typed / "tests/Main.chibi.in").read_bytes()
        prefix, suffix = template.split(b"FIXTURE_SOURCE")
        generated = (self.output / "Main.chibi").read_bytes()
        literal = generated[len(prefix):len(generated) - len(suffix)]
        self.assertEqual(json.loads(literal).encode("utf-8"), fixture)
        self.assertEqual((self.output / "inputs/EchoJoin.chibi").read_bytes(), fixture)

    def test_missing_or_duplicate_marker(self):
        path = self.typed / "tests/Main.chibi.in"
        original = path.read_bytes()
        for value in (b'"absent"', b"FIXTURE_SOURCE FIXTURE_SOURCE"):
            with self.subTest(value=value):
                path.write_bytes(original.replace(b"FIXTURE_SOURCE", value))
                self.reject()

    def test_invalid_utf8_and_unsupported_control_byte(self):
        for value in (b"\xff", b"\x00"):
            with self.subTest(value=value):
                (self.typed / "fixtures/EchoJoin.chibi").write_bytes(value)
                self.reject((UnicodeDecodeError, ValueError))

    def test_extra_source_in_each_package_root(self):
        for package in ("tools/compiler/signature_reader", "packages/core/strings", "tools/experiments/vnext/frontend/typed"):
            with self.subTest(package=package):
                extra = self.root / package / "Unexpected.chibi"
                extra.write_text("package unexpected\n")
                self.reject()
                extra.unlink()

    def test_unknown_import_and_wrong_package_declaration(self):
        source = self.typed / "Produce.chibi"
        original = source.read_bytes()
        for value in (original + b'\nimport extra "project:unknown"\n',
                      original.replace(b"package vnext_typed", b"package wrong", 1)):
            with self.subTest(value=value[:32]):
                source.write_bytes(value)
                self.reject()

    def test_existing_output_is_untouched(self):
        self.output.mkdir()
        sentinel = self.output / "keep.txt"
        sentinel.write_bytes(b"unchanged")
        with self.assertRaises(FileExistsError):
            self.subject.prepare(self.output)
        self.assertEqual(list(self.output.iterdir()), [sentinel])
        self.assertEqual(sentinel.read_bytes(), b"unchanged")

    def test_source_and_output_symlinks_are_rejected(self):
        source = self.typed / "Model.chibi"
        moved = source.with_suffix(".saved")
        source.rename(moved)
        source.symlink_to(moved.name)
        self.reject()
        source.unlink()
        moved.rename(source)
        self.output.symlink_to(self.base / "missing")
        with self.assertRaises(FileExistsError):
            self.subject.prepare(self.output)
        self.assertTrue(self.output.is_symlink())
        self.assertFalse((self.base / "missing").exists())

    def test_changed_working_bytes_are_copied_not_mislabeled_as_git(self):
        source = self.typed / "Model.chibi"
        source.write_bytes(source.read_bytes() + b"\n// Working-tree change.\n")
        result = self.subject.prepare(self.output)
        row = next(row for row in result["files"] if row["path"] == "project/vnext_typed/Model.chibi")
        self.assertEqual(row["sha256"], hashlib.sha256(source.read_bytes()).hexdigest())
        self.assertIn("working-tree", result["purpose"])
        self.assertNotIn("source_tree", result)
        self.assertNotIn("source_head", result)

    def test_helper_loading_does_not_use_sibling_modules_or_caches(self):
        package = self.typed.parent / "prepare"
        package.mkdir()
        (package / "__init__.py").write_text("raise AssertionError('wrong helper')\n")
        (self.typed.parent / "prepare.pyc").write_bytes(b"invalid bytecode")
        before = list(sys.path)
        subject = load(self.typed / "prepare.py")
        self.assertEqual(sys.path, before)
        self.assertEqual(subject.helpers.literal.__code__.co_filename, str(self.typed.parent / "prepare.py"))
        subject.prepare(self.output)


if __name__ == "__main__":
    unittest.main()
