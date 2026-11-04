#!/usr/bin/env python3
"""Python-only preparation regressions. Never launch Chibi, LLVM or test binaries."""

import hashlib
import json
import re
from pathlib import Path
import shutil
import sys
import tempfile
import types
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
AUDIT_ACTIVE = False
LAUNCH_EVENTS = []


def prohibit_launch(event, arguments):
    if AUDIT_ACTIVE and (event in ("subprocess.Popen", "os.system", "ctypes.dlopen")
                         or event.startswith(("os.exec", "os.spawn", "os.posix_spawn", "os.fork"))):
        LAUNCH_EVENTS.append(event)
        raise AssertionError(f"data-only preparation attempted forbidden operation: {event}")


sys.addaudithook(prohibit_launch)


def load(path):
    module = types.ModuleType("typed_preparation_test_subject")
    module.__file__ = str(path)
    exec(compile(path.read_bytes(), str(path), "exec"), module.__dict__)
    return module


class PreparationTests(unittest.TestCase):
    def setUp(self):
        global AUDIT_ACTIVE
        LAUNCH_EVENTS.clear()
        AUDIT_ACTIVE = True
        self.addCleanup(self.check_no_launches)
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

    def check_no_launches(self):
        global AUDIT_ACTIVE
        AUDIT_ACTIVE = False
        self.assertEqual(LAUNCH_EVENTS, [], "preparation must not launch tools or load native libraries")

    def expected_main(self):
        paths = {
            b"FIXTURE_SOURCE": self.typed / "fixtures/EchoJoin.chibi",
            b"PACKAGE_MAIN_SOURCE": self.root / "tools/experiments/vnext/slice_cases/modules/Main.chibi",
            b"PACKAGE_MATH_SOURCE": self.root / "tools/experiments/vnext/slice_cases/modules/math/Math.chibi",
            b"FOREIGN_SOURCE": self.root / "tools/experiments/vnext/slice_cases/foreign/Main.chibi",
            b"MISSING_SYMBOL_SOURCE": self.root / "tools/experiments/vnext/slice_cases/rejected/missing_symbol.chibi",
            b"PRIVATE_IMPORT_SOURCE": self.root / "tools/experiments/vnext/slice_cases/rejected/private_import.chibi",
        }
        literals = {marker: json.dumps(path.read_bytes().decode("utf-8"), ensure_ascii=False).encode()
                    for marker, path in paths.items()}
        template = (self.typed / "tests/Main.chibi.in").read_bytes()
        return re.sub(b"|".join(literals), lambda match: literals[match[0]], template)

    def reject(self, exception=ValueError):
        with self.assertRaises(exception):
            self.subject.prepare(self.output)
        self.assertFalse(self.output.exists())

    def test_exact_sources_literal_and_relative_manifest(self):
        result = self.subject.prepare(self.output)
        self.assertEqual(len(self.subject.SOURCE_MAP), 19)
        self.assertEqual(len(result["files"]), 27)
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
        self.assertEqual((self.output / "Main.chibi").read_bytes(), self.expected_main())
        self.assertEqual(result["imports"]["project:package_graph"], ["core:strings"])
        self.assertIn("project:package_graph", result["imports"]["project:vnext_typed"])
        generated = next(row for row in result["files"] if row["path"] == "Main.chibi")
        self.assertEqual(generated["generated_from"], ["inputs/Main.chibi.in", "inputs/EchoJoin.chibi",
                         "inputs/modules/Main.chibi", "inputs/modules/math/Math.chibi",
                         "inputs/foreign/Main.chibi", "inputs/missing_symbol.chibi", "inputs/private_import.chibi"])
        serialized = (self.output / "source-manifest.json").read_text()
        self.assertNotIn(str(self.base), serialized)
        self.assertEqual(json.loads(serialized), result)
        self.assertEqual(sorted(str(path.relative_to(self.output)) for path in self.output.rglob("*") if path.is_file()),
                         sorted([row["path"] for row in result["files"]] + ["source-manifest.json"]))

    def test_utf8_quotes_backslashes_and_crlf_roundtrip(self):
        fixture = 'package café\r\n// "quoted" \\ slash\tend\r\n'.encode("utf-8")
        (self.typed / "fixtures/EchoJoin.chibi").write_bytes(fixture)
        self.subject.prepare(self.output)
        generated = (self.output / "Main.chibi").read_bytes()
        self.assertEqual(generated, self.expected_main())
        self.assertIn(json.dumps(fixture.decode("utf-8"), ensure_ascii=False).encode(), generated)
        self.assertEqual((self.output / "inputs/EchoJoin.chibi").read_bytes(), fixture)

    def test_missing_or_duplicate_marker(self):
        path = self.typed / "tests/Main.chibi.in"
        original = path.read_bytes()
        for marker in (b"FIXTURE_SOURCE", b"PACKAGE_MAIN_SOURCE", b"PACKAGE_MATH_SOURCE", b"FOREIGN_SOURCE", b"MISSING_SYMBOL_SOURCE", b"PRIVATE_IMPORT_SOURCE"):
            for value in (b'"absent"', marker + b" " + marker):
                with self.subTest(marker=marker, value=value):
                    path.write_bytes(original.replace(marker, value))
                    self.reject()

    def test_source_markers_are_not_reinterpreted_inside_inserted_bytes(self):
        source = self.root / "tools/experiments/vnext/slice_cases/modules/Main.chibi"
        source.write_bytes(source.read_bytes() + b"// PACKAGE_MATH_SOURCE FIXTURE_SOURCE PRIVATE_IMPORT_SOURCE\n")
        self.subject.prepare(self.output)
        self.assertEqual((self.output / "Main.chibi").read_bytes(), self.expected_main())
        self.assertEqual((self.output / "inputs/modules/Main.chibi").read_bytes(), source.read_bytes())

    def test_package_fixtures_are_exact_independent_inputs(self):
        original = {
            "inputs/modules/Main.chibi": "slice_cases/modules/Main.chibi",
            "inputs/modules/math/Math.chibi": "slice_cases/modules/math/Math.chibi",
            "inputs/private_import.chibi": "slice_cases/rejected/private_import.chibi",
        }
        self.subject.prepare(self.output)
        for copied, path in original.items():
            self.assertEqual((self.output / copied).read_bytes(),
                             (self.root / "tools/experiments/vnext" / path).read_bytes())
        self.assertEqual(len((self.output / "inputs/modules/Main.chibi").read_bytes()) +
                         len((self.output / "inputs/modules/math/Math.chibi").read_bytes()), 298)

    def test_invalid_utf8_and_unsupported_control_byte(self):
        for value in (b"\xff", b"\x00"):
            with self.subTest(value=value):
                (self.typed / "fixtures/EchoJoin.chibi").write_bytes(value)
                self.reject((UnicodeDecodeError, ValueError))

    def test_extra_source_in_each_package_root(self):
        for package in ("tools/compiler/signature_reader", "tools/compiler/package_graph", "packages/core/strings", "tools/experiments/vnext/frontend/typed"):
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

    def test_bounded_nested_package_identifiers(self):
        for package in ("core:hash/sha256", "core:sys/windows", "project2:package_3/child4",
                        "a:" + "/".join(["b"] * 8), "a:" + "b" * 64):
            with self.subTest(package=package):
                source = f'    import name "{package}"\n'.encode()
                self.assertEqual(self.subject.imports(source, "fixture"), [package])
        for package in ("core:/sys", "core:sys//posix", "core:../sys", "core:sys/..",
                        "core:sys/./posix", "core:sys/", "core:0sys", "core:\\sys",
                        "a:" + "/".join(["b"] * 9), "a:" + "b" * 65):
            with self.subTest(package=package), self.assertRaises(ValueError):
                self.subject.imports(f'import name "{package}"\n'.encode(), "fixture")

    def test_source_map_rejects_traversal_and_duplicate_destinations(self):
        for name in ("/absolute.chibi", "../outside.chibi", "core//file.chibi", "core/./file.chibi"):
            for source_map in ([(name, "project/file.chibi")], [("file.chibi", name)]):
                with self.subTest(source_map=source_map), self.assertRaises(ValueError):
                    self.subject.read_sources(source_map, [])
        with self.assertRaises(ValueError):
            self.subject.read_sources(self.subject.SOURCE_MAP, [self.subject.SOURCE_MAP[0]])
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
