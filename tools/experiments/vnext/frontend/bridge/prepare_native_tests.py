#!/usr/bin/env python3
"""Data-only native preparation regressions; no compiler, linker or native runs."""

import ast
import copy
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys
import tempfile
import types
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
BRIDGE = "tools/experiments/vnext/frontend/bridge"
AUDIT_ACTIVE = False
LAUNCH_EVENTS = []


def prohibit_launch(event, arguments):
    # Real audit events, not replacements for process or loader functions.
    # Python's in-process compile/exec remains allowed for loading the subject.
    if AUDIT_ACTIVE and (event in ("subprocess.Popen", "os.system", "ctypes.dlopen")
                         or event.startswith(("os.exec", "os.spawn", "os.posix_spawn", "os.fork"))):
        LAUNCH_EVENTS.append(event)
        raise AssertionError(f"data-only preparation attempted forbidden operation: {event}")


sys.addaudithook(prohibit_launch)


def load(path):
    module = types.ModuleType("native_preparation_test_subject")
    module.__file__ = str(path)
    exec(compile(path.read_bytes(), str(path), "exec"), module.__dict__)
    return module


class NativePreparationTests(unittest.TestCase):
    def setUp(self):
        global AUDIT_ACTIVE
        LAUNCH_EVENTS.clear()
        AUDIT_ACTIVE = True
        self.addCleanup(self.check_no_launches)
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.base = Path(temporary.name).resolve()
        self.root = self.base / "checkout"
        original = load(HERE / "prepare_native.py")
        sources = [path for path, _ in original.SOURCE_MAP + original.INPUT_MAP]
        sources += [BRIDGE + "/prepare_native.py", BRIDGE + "/generate.py",
                    BRIDGE + "/generated/arena.rs", BRIDGE + "/run_native.py",
                    "tools/experiments/vnext/owned_process.py",
                    "tools/experiments/vnext/frontend/prepare.py",
                    "tools/experiments/vnext/frontend/typed/prepare.py"]
        sources += [BRIDGE + "/registry_callers.py", "tools/experiments/vnext/slice_cases.json"]
        registry = json.loads((ROOT / "tools/experiments/vnext/slice_cases.json").read_bytes())
        sources += ["tools/experiments/vnext/" + name for row in registry["cases"] for name in row["files"]]
        for name in set(sources):
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, target)
        self.bridge = self.root / BRIDGE
        self.subject = load(self.bridge / "prepare_native.py")
        self.output = self.base / "prepared"
        # Identity-only data, deliberately not a loadable native library.
        self.library = self.base / "backend.so"
        self.library.write_bytes(b"fixture backend identity only\n")
        self.template = self.bridge / "tests/NativeMain.chibi.in"
        self.fixture = self.bridge / "tests/NativeCases.chibi"

    def check_no_launches(self):
        global AUDIT_ACTIVE
        AUDIT_ACTIVE = False
        self.assertEqual(LAUNCH_EVENTS, [], "preparation must not launch tools or load native libraries")

    def prepare(self, case="nativecases"):
        return self.subject.prepare(self.output, self.library, case)

    def reject(self, exception=ValueError):
        with self.assertRaises(exception):
            self.prepare()
        self.assertFalse(self.output.exists())

    def expected_entry(self):
        source = json.dumps(self.fixture.read_bytes().decode("utf-8"), ensure_ascii=False)
        sources = ('[\n        {path: "NativeCases.chibi", text: ' + source +
                   ', document: 19}\n    ]').encode()
        replacements = {
            b'"LIBRARY_PATH"': json.dumps(str(self.library), ensure_ascii=False).encode(),
            b"FIXTURE_SOURCES": sources,
            b"OUTPUT_PATH": json.dumps(str(self.output / "run/normal/source-native.o"), ensure_ascii=False).encode(),
        }
        return re.sub(b'"LIBRARY_PATH"|FIXTURE_SOURCES|OUTPUT_PATH',
                      lambda match: replacements[match[0]], self.template.read_bytes())

    def source_records(self, entry, binding="sources"):
        body = entry.split(f"    {binding}: typed.Source[] = [\n", 1)[1].split("    ];", 1)[0]
        string = r'("(?:[^"\\]|\\.)*")'
        rows = re.findall(r'^        \{path: ' + string + ', text: ' + string +
                          r', document: ([0-9]+)\},?$', body, re.M)
        self.assertEqual(len(body.splitlines()), len(rows))
        return [(json.loads(path), json.loads(source).encode("utf-8"), int(document))
                for path, source, document in rows]

    def test_exact_closure_bytes_manifest_and_no_execution(self):
        # Independent old closure plus exactly Graph, not the preparer's own map
        # compared back to itself. Tests/fixtures are not discovered dependencies.
        compiler = ("source_parser/Declarations.chibi", "source_parser/Expressions.chibi",
                    "source_parser/Parser.chibi", "source_parser/Statements.chibi",
                    "source_parser/Tree.chibi", "source_parser/Types.chibi",
                    "source_tokenizer/Tokenizer.chibi", "syntax_diagnostics/Diagnostics.chibi",
                    "json_string/JsonString.chibi", "lexical_binding/Binding.chibi",
                    "named_binding/Binding.chibi", "signature_reader/Reader.chibi",
                    "type_constructors/Constructors.chibi", "unit_algebra/Units.chibi",
                    "package_graph/Graph.chibi")
        expected_sources = {("tools/compiler/" + name, "project/" + name) for name in compiler}
        expected_sources.add(("packages/core/strings/Strings.chibi", "core/strings/Strings.chibi"))
        for name in ("Model.chibi", "Produce.chibi", "Verify.chibi"):
            expected_sources.add(("tools/experiments/vnext/frontend/typed/" + name, "project/vnext_typed/" + name))
        expected_sources.add((BRIDGE + "/generated/Pack.chibi", "project/vnext_bridge/Pack.chibi"))
        for name in ("os/Os.chibi", "target/Target.chibi", "sys/windows/Decimal.chibi",
                     "sys/windows/Windows.chibi", "sys/posix/Posix.chibi",
                     "hash/sha256/Sha256.chibi", "hash/sha256/Tests.chibi",
                     "bytes/Bytes.chibi", "bytes/Tests.chibi"):
            expected_sources.add(("packages/core/" + name, "core/" + name))
        self.assertEqual(set(self.subject.SOURCE_MAP), expected_sources)
        self.assertEqual(len(self.subject.SOURCE_MAP), 29)
        self.assertEqual(set(self.subject.INPUT_MAP), {
            (BRIDGE + "/tests/NativeCases.chibi", "inputs/NativeCases.chibi"),
            (BRIDGE + "/tests/NativeMain.chibi.in", "inputs/NativeMain.chibi.in"),
            (BRIDGE + "/tests/native_aot_caller.c", "inputs/native_aot_caller.c"),
        })
        result = self.prepare()
        self.assertEqual(len(result["files"]), 33)
        rows = {row["path"]: row for row in result["files"]}
        self.assertEqual(len(rows), 33)
        for name, row in rows.items():
            self.assertFalse(Path(name).is_absolute())
            data = (self.output / name).read_bytes()
            self.assertEqual(row["bytes"], len(data))
            self.assertEqual(row["sha256"], hashlib.sha256(data).hexdigest())
            if "source" in row:
                self.assertFalse(Path(row["source"]).is_absolute())
                self.assertEqual(data, (self.root / row["source"]).read_bytes())
            else:
                self.assertEqual(name, "entry/Main.chibi")
                self.assertIn("inputs/NativeMain.chibi.in", row["generated_from"])
                self.assertIn("inputs/NativeCases.chibi", row["generated_from"])
        self.assertEqual((self.output / "entry/Main.chibi").read_bytes(), self.expected_entry())
        self.assertEqual(result["imports"]["project:package_graph"], ["core:strings"])
        self.assertIn("project:package_graph", result["imports"]["project:vnext_typed"])
        self.assertEqual(json.loads((self.output / "source-manifest.json").read_bytes()), result)
        self.assertEqual(sorted(str(path.relative_to(self.output)) for path in self.output.rglob("*") if path.is_file()),
                         sorted([*rows, *[row["path"] for row in result["implementation"]], "source-manifest.json"]))
        self.assertEqual(result["stages"], {name: "not_run" for name in
                         ("chibi_compile", "jit_normal", "perf_failure", "c_link", "aot_run")})
        self.assertEqual(result["backend"], {
            "path": str(self.library), "resolved": str(self.library),
            "bytes": self.library.stat().st_size,
            "sha256": hashlib.sha256(self.library.read_bytes()).hexdigest(),
        })
        controllers = {"controller/run_native.py": self.bridge / "run_native.py",
                       "controller/owned_process.py": self.root / "tools/experiments/vnext/owned_process.py"}
        self.assertEqual({row["path"] for row in result["implementation"]}, set(controllers))
        for row in result["implementation"]:
            data = (self.output / row["path"]).read_bytes()
            self.assertEqual(data, controllers[row["path"]].read_bytes())
            self.assertEqual(row["bytes"], len(data))
            self.assertEqual(row["sha256"], hashlib.sha256(data).hexdigest())
        for row in result["preparation"]:
            data = (self.root / row["source"]).read_bytes()
            self.assertEqual(row["bytes"], len(data))
            self.assertEqual(row["sha256"], hashlib.sha256(data).hexdigest())
        self.assertFalse((self.output / "run/normal/source-native.o").exists())
        self.assertIn("working-tree", result["purpose"])
        self.assertIn("not", result["purpose"])

    def test_literal_utf8_crlf_quotes_and_backslash_paths(self):
        self.fixture.write_bytes('package café\r\n// "quote" \\ slash\tend\r\n'.encode())
        self.library = self.base / 'lib "café" \\ identity.so'
        self.library.write_bytes(b"fixture")
        self.output = self.base / 'prepared "café" \\ output'
        self.prepare()
        self.assertEqual((self.output / "entry/Main.chibi").read_bytes(), self.expected_entry())
        self.assertEqual((self.output / "inputs/NativeCases.chibi").read_bytes(), self.fixture.read_bytes())

    def test_inserted_marker_text_is_not_replaced_again(self):
        self.fixture.write_bytes(b'package example\n// FIXTURE_SOURCES OUTPUT_PATH "LIBRARY_PATH"\n')
        self.library = self.base / 'LIBRARY_PATH FIXTURE_SOURCES OUTPUT_PATH.so'
        self.library.write_bytes(b"fixture")
        self.output = self.base / "OUTPUT_PATH FIXTURE_SOURCES LIBRARY_PATH"
        self.prepare()
        self.assertEqual((self.output / "entry/Main.chibi").read_bytes(), self.expected_entry())

    def test_missing_duplicate_and_invalid_marker_contexts(self):
        original = self.template.read_bytes()
        changes = []
        for marker in (b"LIBRARY_PATH", b"FIXTURE_SOURCES", b"OUTPUT_PATH"):
            changes.extend([(marker, b"missing"), (marker, marker + b" " + marker)])
        changes.extend([(b'"LIBRARY_PATH"', b"LIBRARY_PATH"),
                        (b'"LIBRARY_PATH"', b'"prefix LIBRARY_PATH"'),
                        (b"FIXTURE_SOURCES", b'"FIXTURE_SOURCES"'),
                        (b"OUTPUT_PATH", b'"OUTPUT_PATH"'),
                        (b"FIXTURE_SOURCES", b"prefixFIXTURE_SOURCES"),
                        (b"OUTPUT_PATH", b"OUTPUT_PATHsuffix")])
        for marker, replacement in changes:
            with self.subTest(marker=marker, replacement=replacement):
                self.template.write_bytes(original.replace(marker, replacement))
                self.reject()

    def test_stale_generated_files_are_rejected_without_rewriting(self):
        for name in ("Pack.chibi", "arena.rs"):
            path = self.bridge / "generated" / name
            original = path.read_bytes()
            stale = original + b"\n// stale fixture\n"
            with self.subTest(name=name):
                path.write_bytes(stale)
                self.reject()
                self.assertEqual(path.read_bytes(), stale)
                path.write_bytes(original)

    def test_malformed_imports_reject_and_nested_imports_are_supported(self):
        self.assertEqual(self.subject.typed.imports(b'import sys "core:sys/posix"\n', "fixture"),
                         ["core:sys/posix"])
        original = self.template.read_bytes()
        for name in ("core:", "core:/os", "core:../os", "core:sys/../os",
                     "core:sys//posix", "core:sys/", "/core:os", "core:./os"):
            with self.subTest(name=name):
                self.template.write_bytes(original + f'\nimport extra "{name}"\n'.encode())
                self.reject()

    def test_unknown_import_wrong_package_and_extra_package_file(self):
        path = self.root / "packages/core/os/Os.chibi"
        original = path.read_bytes()
        for data in (original + b'\nimport extra "core:unknown"\n',
                     original.replace(b"package os", b"package incorrect", 1)):
            with self.subTest(data=data[:32]):
                path.write_bytes(data)
                self.reject()
        path.write_bytes(original)
        for package in ("packages/core/os", "packages/core/sys/posix", BRIDGE + "/generated"):
            with self.subTest(package=package):
                extra = self.root / package / "Unexpected.chibi"
                extra.write_bytes(b"package unexpected\n")
                self.reject()
                extra.unlink()

    def test_invalid_fixture_bytes(self):
        for data in (b"\xff", b"\x00"):
            with self.subTest(data=data):
                self.fixture.write_bytes(data)
                self.reject((ValueError, UnicodeDecodeError))

    def test_source_and_output_symlinks_rejected(self):
        moved = self.fixture.with_suffix(".saved")
        self.fixture.rename(moved)
        self.fixture.symlink_to(moved.name)
        self.reject()
        self.fixture.unlink()
        moved.rename(self.fixture)
        self.output.symlink_to(self.base / "absent")
        with self.assertRaises(FileExistsError):
            self.prepare()
        self.assertTrue(self.output.is_symlink())
        self.assertFalse((self.base / "absent").exists())

    def test_symlinked_parent_directories_rejected(self):
        directory = self.bridge / "tests"
        moved = self.bridge / "saved-tests"
        directory.rename(moved)
        directory.symlink_to(moved.name, target_is_directory=True)
        self.reject()
        directory.unlink()
        moved.rename(directory)
        real = self.base / "real-output-parent"
        real.mkdir()
        link = self.base / "linked-output-parent"
        link.symlink_to(real, target_is_directory=True)
        self.output = link / "prepared"
        self.reject()
        self.assertEqual(list(real.iterdir()), [])

    def test_working_source_bytes_are_copied(self):
        source = self.root / "tools/experiments/vnext/frontend/typed/Produce.chibi"
        source.write_bytes(source.read_bytes() + b"\n// Local working-tree change.\n")
        result = self.prepare()
        row = next(row for row in result["files"] if row["path"] == "project/vnext_typed/Produce.chibi")
        self.assertEqual((self.output / row["path"]).read_bytes(), source.read_bytes())
        self.assertEqual(row["sha256"], hashlib.sha256(source.read_bytes()).hexdigest())
        self.assertNotIn("source_head", result)
        self.assertNotIn("source_tree", result)

    def test_existing_directory_untouched(self):
        self.output.mkdir()
        sentinel = self.output / "keep.txt"
        sentinel.write_bytes(b"unchanged")
        with self.assertRaises(FileExistsError):
            self.prepare()
        self.assertEqual(list(self.output.iterdir()), [sentinel])
        self.assertEqual(sentinel.read_bytes(), b"unchanged")

    def test_backend_empty_directory_and_symlink_rejected(self):
        self.library.write_bytes(b"")
        self.reject()
        self.library.unlink()
        self.library.mkdir()
        self.reject()
        self.library.rmdir()
        target = self.base / "actual.so"
        target.write_bytes(b"fixture")
        self.library.symlink_to(target)
        self.reject()


    def registry(self):
        path = self.root / "tools/experiments/vnext/slice_cases.json"
        return path, json.loads(path.read_bytes())

    def test_registry_callers_exact_values_sources_and_membership(self):
        for case, count, files, rejections in (("scalars", 15, 44, 9), ("direct_calls", 23, 35, 0)):
            self.output = self.base / case
            result = self.prepare(case)
            rows = {row["path"]: row for row in result["files"]}
            self.assertEqual(len(rows), files)
            self.assertEqual(result["schema"], 2)
            self.assertEqual(result["expectations"]["case"], case)
            self.assertEqual(result["expectations"]["aot_checks"], count)
            self.assertEqual(len(result["expectations"]["rejections"]), rejections)
            record = json.loads(result["expectations"]["aot_success"])
            self.assertEqual(record["aot_c_caller_checks"], count)
            self.assertNotIn("inputs/NativeCases.chibi", rows)
            self.assertIn("inputs/registry_callers.py", rows)
            self.assertIn(BRIDGE + "/registry_callers.py", [r["source"] for r in result["preparation"]])
            path, registry = self.registry()
            self.assertEqual((self.output / "inputs/slice_cases.json").read_bytes(), path.read_bytes())
            selected = next(row for row in registry["cases"] if row["id"] == case)
            entry = (self.output / "entry/Main.chibi").read_text()
            source = (self.root / "tools/experiments/vnext" / selected["files"][0]).read_bytes()
            self.assertEqual(self.source_records(entry), [(selected["files"][0], source, 19)])
            self.assertEqual((self.output / "inputs" / selected["files"][0]).read_bytes(), source)
            self.assertEqual(len(re.findall(r"^    result[0-9]+ :=", entry, re.M)), count)
            self.assertLess(entry.rfind(" := ExpectedEntry("), entry.index(": fn("))
            c = (self.output / "inputs/native_aot_caller.c").read_text()
            self.assertEqual(c.count("    ++checks;"), count)
            self.assertIn("INT64_MIN", c)
            self.assertNotIn("INT64_C(9223372036854775808)", c)
            self.assertIn(": i64 = -9223372036854775807 - 1;", entry)
            self.assertIn(": i64 = 9223372036854775807;", entry)
            self.assertNotIn("i64(9223372036854775807)", entry)
            self.assertIn(" -> bool = address", entry)
            self.assertIn("extern bool ", c)
            if case == "direct_calls":
                self.assertIn("pub TrueLiteral :: () -> bool { return true; }", source.decode())
                self.assertIn("pub FalseLiteral :: () -> bool { return false; }", source.decode())
                self.assertIn(["TrueLiteral", [], True], selected["calls"])
                self.assertIn(["FalseLiteral", [], False], selected["calls"])
                self.assertIn(["SignedGreater", [-1, 0], False], selected["calls"])
                self.assertIn(["SignedGreater", [0, -1], True], selected["calls"])
                self.assertIn(["SignedGreater", [0, 0], False], selected["calls"])
                self.assertIn("extern bool TrueLiteral(void);", c)
                self.assertIn("extern bool FalseLiteral(void);", c)
            rejection_body = entry.split("CheckSourceRejections :: () {", 1)[1].split("PerfBuildIdFailure ::", 1)[0]
            self.assertEqual(rejection_body.count("switch typed.ProduceSources("), rejections)
            self.assertNotIn("bridge.Pack(", rejection_body)
            self.assertNotIn("chibi_vnext_compile_both(", rejection_body)
            for index, name in enumerate(result["expectations"]["rejections"]):
                row = next(row for row in registry["cases"] if row["id"] == name)
                expected = (self.root / "tools/experiments/vnext" / row["files"][0]).read_bytes()
                self.assertEqual(self.source_records(rejection_body, f"sources{index}"),
                                 [(row["files"][0], expected, 100 + index)])
                self.assertIn(f"diagnostic.document == {100 + index}", rejection_body)
                self.assertIn(f"registry {name}: {row['reject'][0]}", rejection_body)
            for row in rows.values():
                self.assertEqual(hashlib.sha256((self.output / row["path"]).read_bytes()).hexdigest(), row["sha256"])

    def test_executed_generator_changes_are_rejected_before_output(self):
        for filename, case in (("registry_callers.py", "scalars"),
                               ("generate.py", "nativecases")):
            with self.subTest(filename=filename):
                path = self.bridge / filename
                self.output = self.base / ("race-" + filename)
                original = path.read_bytes()
                changed = []
                def trace(frame, event, argument):
                    # Real source change after the generator executes, before its
                    # caller resumes. Both routes cover the pre-fix runpy loader.
                    candidate = frame.f_locals.get("path", frame.f_locals.get("path_name"))
                    if (not changed and event == "return"
                            and frame.f_code.co_name in ("load_source", "run_path")
                            and candidate is not None and str(candidate) == str(path)):
                        path.write_bytes(original + b"\n# Changed after execution.\n")
                        changed.append(True)
                    return trace
                previous = sys.gettrace()
                try:
                    sys.settrace(trace)
                    with self.assertRaisesRegex(ValueError, "changed"):
                        self.prepare(case)
                finally:
                    sys.settrace(previous)
                    path.write_bytes(original)
                self.assertEqual(changed, [True])
                self.assertFalse(self.output.exists())

    def test_original_modules_and_private_import_keep_separate_closures(self):
        result = self.prepare("modules")
        rows = {row["path"]: row for row in result["files"]}
        self.assertEqual(len(result["files"]), 37)
        self.assertEqual(len(rows), 37)
        main = "slice_cases/modules/Main.chibi"
        math = "slice_cases/modules/math/Math.chibi"
        private = "slice_cases/rejected/private_import.chibi"
        expected_inputs = {"inputs/NativeMain.chibi.in", "inputs/slice_cases.json",
                           "inputs/registry_callers.py", "inputs/native_aot_caller.c",
                           "inputs/" + main, "inputs/" + math, "inputs/" + private}
        self.assertEqual({name for name in rows if name.startswith("inputs/")}, expected_inputs)
        self.assertEqual(sum(row["path"] == "inputs/" + math for row in result["files"]), 1)
        raw = {name: (self.root / "tools/experiments/vnext" / name).read_bytes()
               for name in (main, math, private)}
        self.assertEqual([len(raw[name]) for name in (main, math, private)], [181, 117, 109])
        self.assertEqual([hashlib.sha256(raw[name]).hexdigest() for name in (main, math, private)], [
            "86ded16117e844c3c57a7aca9191617ab148c9aa9c237a8502f397e75377e4cf",
            "6fe411ca68648829984aa48f7b8c08da049d99c0f7792b379b9bcb70269c39be",
            "450d919e432a1913473410fbf8eea6fa1f8d9d249ced4cba18e322d35c2b2e4c",
        ])
        for name, value in raw.items():
            self.assertEqual((self.output / "inputs" / name).read_bytes(), value)
            self.assertIn("inputs/" + name, rows["entry/Main.chibi"]["generated_from"])
        entry = (self.output / "entry/Main.chibi").read_text()
        self.assertEqual(self.source_records(entry), [(main, raw[main], 19), (math, raw[math], 20)])
        self.assertEqual(self.source_records(entry, "sources0"),
                         [(private, raw[private], 100), (math, raw[math], 101)])
        self.assertEqual(entry.count("switch typed.ProduceSources(sources)"), 2)
        self.assertIn("return PerfBuildIdFailure(sources);", entry)
        self.assertEqual(entry.count("chibi_vnext_compile_both(words,"), 1)
        self.assertEqual(len(re.findall(r"^    result[0-9]+ :=", entry, re.M)), 2)
        self.assertIn("for (iteration in 0..<8)", entry)
        self.assertIn('FindFunction(module, ".vnext.2") == 2', entry)
        self.assertIn('module.functions[1].exported == 0', entry)
        self.assertIn('if (origin.document == 20) { source = sources[1].text; }', entry)
        self.assertIn('for (name in ["Double", "Hidden", ".vnext.2", ".vnext.3"])', entry)
        rejection = entry.split("CheckSourceRejections :: () {", 1)[1].split("PerfBuildIdFailure ::", 1)[0]
        self.assertNotIn("bridge.Pack(", rejection)
        self.assertNotIn("chibi_vnext_compile_both(", rejection)
        self.assertIn('diagnostic.message == "private member"', rejection)
        self.assertIn("diagnostic.document == 100", rejection)
        start = raw[private].index(b"math.Hidden")
        end = start + len(b"math.Hidden")
        line = raw[private][:start].count(b"\n") + 1
        column = start - raw[private].rfind(b"\n", 0, start)
        self.assertIn(f"diagnostic.start == {start} && diagnostic.end == {end} && diagnostic.line == {line} && diagnostic.column == {column}", rejection)
        c = (self.output / "inputs/native_aot_caller.c").read_text()
        self.assertIn("extern int64_t Apply(int64_t, bool);", c)
        self.assertIn("Apply(INT64_C(21), true) != INT64_C(42)", c)
        self.assertIn("Apply(INT64_C(21), false) != INT64_C(21)", c)
        self.assertEqual(c.count("    ++checks;"), 2)
        self.assertNotIn("extern int64_t Double", c)
        self.assertEqual(result["expectations"], {
            "case": "modules", "rejections": ["private_import"], "aot_checks": 2,
            "aot_success": '{"aot_c_caller_checks":2,"status":"passed","input":"source-produced-copied-object"}\n',
        })

    def test_package_registry_requires_exact_ordered_paths_and_rejections(self):
        path, original = self.registry()
        for name in ("modules", "private_import"):
            for change in ("omit", "duplicate", "reverse", "wrong", "traversal", "absolute", "extra", "empty", "not_list"):
                registry = copy.deepcopy(original)
                row = next(row for row in registry["cases"] if row["id"] == name)
                if change == "omit": row["files"].pop()
                elif change == "duplicate": row["files"][1] = row["files"][0]
                elif change == "reverse": row["files"].reverse()
                elif change == "wrong": row["files"][1] = "slice_cases/scalars/Main.chibi"
                elif change == "traversal": row["files"][1] = "slice_cases/modules/../math/Math.chibi"
                elif change == "absolute": row["files"][1] = "/slice_cases/modules/math/Math.chibi"
                elif change == "extra": row["files"].append("slice_cases/direct_calls/Main.chibi")
                elif change == "empty": row["files"] = []
                else: row["files"] = "slice_cases/modules/Main.chibi"
                path.write_text(json.dumps(registry))
                with self.subTest(name=name, change=change), self.assertRaises(ValueError):
                    self.prepare("modules")
                self.assertFalse(self.output.exists())
        for change in ("category", "anchor", "foreign"):
            registry = copy.deepcopy(original)
            row = next(row for row in registry["cases"] if row["id"] == "private_import")
            if change == "category": row["reject"][0] = "link"
            elif change == "anchor": row["reject"][2] = "absent anchor"
            else: row["foreign_fixture"] = "slice_probe.c"
            path.write_text(json.dumps(registry))
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.prepare("modules")
            self.assertFalse(self.output.exists())

    def test_package_dependency_bytes_transport_and_file_guards(self):
        math = self.root / "tools/experiments/vnext/slice_cases/modules/math/Math.chibi"
        original = math.read_bytes()
        changed = original.replace(b"\n", b"\r\n") + '// café "LIBRARY_PATH" FIXTURE_SOURCES OUTPUT_PATH\n'.encode()
        math.write_bytes(changed)
        self.prepare("modules")
        entry = (self.output / "entry/Main.chibi").read_text()
        self.assertEqual(self.source_records(entry)[1][1], changed)
        self.assertEqual(self.source_records(entry, "sources0")[1][1], changed)
        self.output = self.base / "invalid"
        for source in (b"\xff", b"\x00", b"", b"x" * 65536):
            math.write_bytes(source)
            with self.subTest(source=source[:10]), self.assertRaises((ValueError, UnicodeDecodeError)):
                self.prepare("modules")
            self.assertFalse(self.output.exists())
        math.unlink()
        with self.assertRaises(FileNotFoundError):
            self.prepare("modules")
        self.assertFalse(self.output.exists())
        saved = math.with_suffix(".saved")
        saved.write_bytes(original)
        math.symlink_to(saved.name)
        with self.assertRaises(ValueError):
            self.prepare("modules")
        self.assertFalse(self.output.exists())

    def test_preparer_runner_admission_policies_stay_in_step_without_execution(self):
        callers = load(self.bridge / "registry_callers.py")
        # Inspect data constants without importing/executing any runner code.
        tree = ast.parse((self.bridge / "run_native.py").read_bytes())
        policy = {node.targets[0].id: ast.literal_eval(node.value)
                  for node in tree.body if isinstance(node, ast.Assign)
                  and isinstance(node.targets[0], ast.Name)
                  and node.targets[0].id in ("REGISTRY_REJECTIONS", "SNAPSHOT_FILES")}
        self.assertEqual(set(policy["REGISTRY_REJECTIONS"]), set(callers.ACCEPTED))
        for selected, names in policy["REGISTRY_REJECTIONS"].items():
            self.assertEqual(set(names), {name for name, row in callers.REJECTIONS.items() if row[0] == selected})
        self.assertEqual(policy["SNAPSHOT_FILES"],
                         {"nativecases": 33, "scalars": 44, "direct_calls": 35, "modules": 37})

    def test_registry_bool_int_and_signature_shape_rejections(self):
        path, original = self.registry()
        def selected(registry):
            return next(row for row in registry["cases"] if row["id"] == "scalars")
        changes = [
            lambda r: selected(r)["calls"].append(["Echo", [True], 1]),
            lambda r: selected(r)["calls"].append(["Echo", [1], False]),
            lambda r: selected(r)["calls"].append(["Echo", [1, 2], 1]),
            lambda r: selected(r)["calls"].append(["New", [1.0], 1]),
            lambda r: selected(r)["calls"].append(["New", [None], 1]),
            lambda r: selected(r)["calls"].append(["New", [1], 1 << 63]),
            lambda r: selected(r)["calls"].append(["New", [-(1 << 63) - 1], 1]),
            lambda r: selected(r)["calls"].append(["New", [0] * 33, 1]),
            lambda r: selected(r)["calls"].append(["injected();", [], 1]),
            lambda r: selected(r)["calls"].append(["New", {"a": 1}, 1]),
            lambda r: selected(r).update(calls=[]),
            lambda r: selected(r).update(calls=[["New", [], 1]] * 129),
            lambda r: selected(r).update(files=["slice_cases/scalars/Other.chibi"]),
            lambda r: selected(r).update(foreign_fixture="slice_probe.c"),
        ]
        for change in changes:
            registry = copy.deepcopy(original)
            change(registry)
            path.write_text(json.dumps(registry))
            with self.subTest(registry=registry), self.assertRaises(ValueError):
                self.prepare("scalars")
            self.assertFalse(self.output.exists())

    def test_registry_missing_duplicate_unknown_membership_and_rejection_shape(self):
        path, original = self.registry()
        mutations = []
        for name in ("scalars", "argument_type", "direct_calls"):
            value = copy.deepcopy(original)
            value["cases"] = [row for row in value["cases"] if row["id"] != name]
            mutations.append(value)
        value = copy.deepcopy(original); value["cases"][-1] = value["cases"][0]; mutations.append(value)
        value = copy.deepcopy(original); value["cases"][-1]["id"] = "unknown"; mutations.append(value)
        value = copy.deepcopy(original); value["version"] = True; mutations.append(value)
        value = copy.deepcopy(original)
        next(row for row in value["cases"] if row["id"] == "argument_type")["reject"][0] = "subset"
        mutations.append(value)
        for value in mutations:
            path.write_text(json.dumps(value))
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.prepare("scalars")
            self.assertFalse(self.output.exists())
        path.write_text(json.dumps(original))
        for case in ("unknown", "foreign", "missing_symbol", "argument_type"):
            with self.subTest(case=case), self.assertRaises(ValueError):
                self.prepare(case)

    def test_registry_source_substitution_preserves_markers_utf8_and_crlf(self):
        fixture = self.root / "tools/experiments/vnext/slice_cases/scalars/Main.chibi"
        original = fixture.read_bytes()
        fixture.write_bytes(original.replace(b"\n", b"\r\n") +
                            '// café "LIBRARY_PATH" FIXTURE_SOURCES OUTPUT_PATH\n'.encode())
        self.prepare("scalars")
        entry = (self.output / "entry/Main.chibi").read_text()
        self.assertEqual(self.source_records(entry),
                         [("slice_cases/scalars/Main.chibi", fixture.read_bytes(), 19)])
        self.assertNotIn("NativeCases.chibi", [row["path"] for row in json.loads((self.output / "source-manifest.json").read_bytes())["files"]])

    def test_registry_stale_schema_and_bad_region_rejected_without_output(self):
        generated = self.bridge / "generated/arena.rs"
        original = generated.read_bytes()
        generated.write_bytes(original + b"// stale schema\n")
        with self.assertRaises(ValueError):
            self.prepare("scalars")
        self.assertFalse(self.output.exists())
        generated.write_bytes(original)
        original_template = self.template.read_text()
        for change in ("missing", "duplicate", "reverse"):
            value = original_template
            if change == "missing": value = value.replace("// BEGIN FIXTURE_CALLS", "// removed")
            elif change == "duplicate": value += "// BEGIN FIXTURE_CALLS\n"
            else: value = value.replace("// BEGIN FIXTURE_CALLS", "// TEMP").replace("// END FIXTURE_CALLS", "// BEGIN FIXTURE_CALLS").replace("// TEMP", "// END FIXTURE_CALLS")
            self.template.write_text(value)
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.prepare("scalars")
            self.assertFalse(self.output.exists())


    def test_registry_malformed_json_bounds_and_reordered_membership(self):
        path, original = self.registry()
        for raw in (b"{", b"null", b"[]", b"[" * 10000 + b"]" * 10000, b" " * 65537):
            path.write_bytes(raw)
            with self.subTest(raw=raw[:16]), self.assertRaises(ValueError):
                self.prepare("scalars")
            self.assertFalse(self.output.exists())
        original["cases"].reverse()
        path.write_text(json.dumps(original))
        result = self.prepare("scalars")
        expected = [row["id"] for row in original["cases"]
                    if "reject" in row and len(row["files"]) == 1 and "foreign_fixture" not in row]
        self.assertEqual(result["expectations"]["rejections"], expected)


    def test_registry_exact_scalar_literal_transport(self):
        caller = load(self.bridge / "registry_callers.py")
        for value in (True, False):
            self.assertEqual(caller.scalar_type(value), "bool")
            self.assertEqual(caller.literal(value, "chibi"), str(value).lower())
            self.assertEqual(caller.literal(value, "c"), str(value).lower())
        for value in (0, 1, -1, -(1 << 63), (1 << 63) - 1):
            self.assertEqual(caller.scalar_type(value), "i64")
            self.assertEqual(caller.literal(value, "chibi"),
                             "-9223372036854775807 - 1" if value == -(1 << 63) else str(value))
            expected = "INT64_MIN" if value == -(1 << 63) else (
                f"(-INT64_C({-value}))" if value < 0 else f"INT64_C({value})")
            self.assertEqual(caller.literal(value, "c"), expected)
        for value in (None, 1.0, "1", [], {}, 1 << 63, -(1 << 63) - 1):
            with self.subTest(value=value), self.assertRaises(ValueError):
                caller.scalar_type(value)


if __name__ == "__main__":
    unittest.main()
