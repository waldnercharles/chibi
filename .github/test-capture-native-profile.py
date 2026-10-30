#!/usr/bin/env python3
"""Receipt-validation fixtures; these do not prove that a native build ran."""

import contextlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


SPEC = importlib.util.spec_from_file_location(
    "capture_native_profile", Path(__file__).with_name("capture-native-profile.py")
)
capture = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(capture)


class Flags(unittest.TestCase):
    def test_valid_profiles(self):
        common = ["clang", "-g", "-O1", "-fno-omit-frame-pointer"]
        for sanitizers in (False, True):
            for test in (False, True):
                arguments = common.copy()
                if sanitizers:
                    arguments.append("-fsanitize=address,undefined")
                if test:
                    arguments.append("-UNDEBUG")
                with self.subTest(sanitizers=sanitizers, test=test):
                    capture.check_flags(arguments, sanitizers, test)

    def test_rejects_wrong_flags(self):
        common = ["clang", "-g", "-O1", "-fno-omit-frame-pointer"]
        sanitized = common + ["-fsanitize=address,undefined"]
        cases = [
            ("no optimization", [a for a in sanitized if a != "-O1"], True, False),
            ("duplicate optimization", sanitized + ["-O1"], True, False),
            ("overriding optimization", sanitized + ["-O2"], True, False),
            ("earlier wrong optimization", ["-O0"] + sanitized, True, False),
            ("fast math optimization", sanitized + ["-Ofast"], True, False),
            ("no debug info", [a for a in sanitized if a != "-g"], True, False),
            ("disabled debug info", sanitized + ["-g0"], True, False),
            ("no frame pointers", [a for a in sanitized if a != "-fno-omit-frame-pointer"], True, False),
            ("disabled frame pointers", sanitized + ["-fomit-frame-pointer"], True, False),
            ("no sanitizers", common, True, False),
            ("address only", common + ["-fsanitize=address"], True, False),
            ("undefined only", common + ["-fsanitize=undefined"], True, False),
            ("extra sanitizer", common + ["-fsanitize=address,undefined,thread"], True, False),
            ("disabled sanitizers", sanitized + ["-fno-sanitize=all"], True, False),
            ("disabled address", sanitized + ["-fno-sanitize=address"], True, False),
            ("disabled undefined", sanitized + ["-fno-sanitize=undefined"], True, False),
            ("NDEBUG", sanitized + ["-DNDEBUG"], True, False),
            ("NDEBUG value", sanitized + ["-DNDEBUG=1"], True, False),
            ("split NDEBUG", sanitized + ["-D", "NDEBUG"], True, False),
            ("split NDEBUG value", sanitized + ["-D", "NDEBUG=1"], True, False),
            ("test assertions absent", sanitized, True, True),
            ("test assertions disabled", sanitized + ["-UNDEBUG", "-DNDEBUG"], True, True),
            ("fast sanitizer contamination", sanitized, False, False),
            ("fast address contamination", common + ["-fsanitize=address"], False, False),
            ("fast test assertions absent", common, False, True),
        ]
        for name, arguments, sanitizers, test in cases:
            with self.subTest(name=name), self.assertRaises(ValueError):
                capture.check_flags(arguments, sanitizers, test)


class Symbols(unittest.TestCase):
    def test_linux_and_macos_symbols(self):
        for prefix in ("__", "___"):
            for kind, address in (("U", ""), ("T", "0000000000001000")):
                nm_text = (
                    f"{address} {kind} {prefix}asan_init\n"
                    f"{address} {kind} {prefix}ubsan_handle_type_mismatch_v1\n"
                    "0000000000002000 T ordinary_function\n"
                )
                with self.subTest(prefix=prefix, kind=kind):
                    self.assertTrue(capture.check_symbols(nm_text, True))

    def test_fast_without_sanitizer_symbols(self):
        for nm_text in ("", "0000000000001000 T ordinary_function\n"):
            with self.subTest(nm_text=nm_text):
                self.assertEqual(capture.check_symbols(nm_text, False), [])

    def test_objects_require_imported_sanitizers(self):
        for prefix in ("__", "___"):
            for asan_kind, ubsan_kind in (("U", "U"), ("T", "T"), ("U", "T"), ("T", "U")):
                nm_text = (f"{asan_kind} {prefix}asan_init\n"
                           f"{ubsan_kind} {prefix}ubsan_handle_add_overflow\n")
                with self.subTest(prefix=prefix, asan=asan_kind, ubsan=ubsan_kind):
                    self.assertTrue(capture.check_symbols(nm_text, True))
                    if asan_kind == ubsan_kind == "U":
                        self.assertTrue(capture.check_symbols(nm_text, True, compiled=True))
                    else:
                        with self.assertRaises(ValueError):
                            capture.check_symbols(nm_text, True, compiled=True)

    def test_rejects_missing_or_contaminating_symbols(self):
        cases = [
            ("no symbols", "", True),
            ("address only", " U __asan_init\n", True),
            ("undefined only", " U __ubsan_handle_add_overflow\n", True),
            ("mac address only", " U ___asan_init\n", True),
            ("mac undefined only", " U ___ubsan_handle_add_overflow\n", True),
            ("filename is not symbol", "__asan_init.o:\n__ubsan_handle_add_overflow.o:\n", True),
            ("symbol suffix is not instrumentation", " T not__asan_init\n T not__ubsan_handle_add_overflow\n", True),
            ("fast address contamination", " U __asan_init\n", False),
            ("fast undefined contamination", " U __ubsan_handle_add_overflow\n", False),
            ("fast mac address contamination", " U ___asan_init\n", False),
            ("fast mac undefined contamination", " U ___ubsan_handle_add_overflow\n", False),
        ]
        for name, nm_text, sanitizers in cases:
            with self.subTest(name=name), self.assertRaises(ValueError):
                capture.check_symbols(nm_text, sanitizers)


class Records(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source.c"
        self.output = self.root / "source.c.o"
        self.record = Path(str(self.output) + ".chibi-record")
        self.source.write_text("int sample;\n")
        self.output.write_bytes(b"fixture output, not a compiled object")
        self.arguments = ["clang", "-c", str(self.source), "-o", str(self.output)]

    def write_record(self, arguments=None, paths=None):
        if arguments is None:
            arguments = self.arguments
        if paths is None:
            paths = [self.source, self.output]
        lines = [*arguments, ""]
        for path in paths:
            lines.extend((str(path), capture.stamp(path)))
        self.record.write_text("\n".join(lines) + "\n")

    def test_stamp_matches_native_record_format(self):
        info = self.source.stat()
        modified_sec, modified_ns = divmod(info.st_mtime_ns, 1_000_000_000)
        changed_sec, changed_ns = divmod(info.st_ctime_ns, 1_000_000_000)
        expected = (
            f"{info.st_dev}:{info.st_ino}:{info.st_size}:{info.st_mode:o}:"
            f"{modified_sec}.{modified_ns:09}:{changed_sec}.{changed_ns:09}"
        )
        self.assertEqual(capture.stamp(self.source), expected)

    def test_valid_record(self):
        self.write_record()
        self.assertEqual(capture.read_record(self.output, self.arguments), self.record)

    def test_rebased_relative_dependencies_preserve_source_identity(self):
        source = self.root / "src/support/arena.c"
        wrong_source = self.root / "other/arena.c"
        generated = self.root / "build/generated/include.h"
        system_header = self.root / "system/include.h"
        for path in (source, wrong_source, generated, system_header):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture input\n")
        arguments = ["clang", "-c", str(source), "-o", str(self.output)]
        with contextlib.chdir(self.root):
            for relative_source, valid in (("src/support/arena.c", True), ("other/arena.c", False)):
                self.write_record(arguments, [Path(relative_source), Path("build/generated/include.h"),
                                              system_header, self.output])
                with self.subTest(relative_source=relative_source):
                    if valid:
                        self.assertEqual(capture.read_record(self.output, arguments), self.record)
                    else:
                        with self.assertRaises(ValueError):
                            capture.read_record(self.output, arguments)

    def test_empty_argument_tokens_are_not_footer_separators(self):
        dependency = self.root / "dependency.o"
        dependency.write_bytes(b"fixture input object")
        arguments = ["clang", "", str(dependency), "-o", str(self.output), ""]
        self.write_record(arguments, [dependency, self.output])
        self.assertEqual(capture.read_record(self.output, arguments), self.record)

    def test_missing_record_is_not_build_evidence(self):
        with self.assertRaises((ValueError, OSError)):
            capture.read_record(self.output, self.arguments)

    def test_missing_output_is_not_build_evidence(self):
        self.write_record()
        self.output.unlink()
        with self.assertRaises((ValueError, OSError)):
            capture.read_record(self.output, self.arguments)

    def test_missing_input_is_not_current_evidence(self):
        self.write_record()
        self.source.unlink()
        with self.assertRaises((ValueError, OSError)):
            capture.read_record(self.output, self.arguments)

    def test_rejects_argument_mismatch(self):
        self.write_record()
        for arguments in (
            ["different-clang", *self.arguments[1:]],
            [*self.arguments, "-O1"],
            self.arguments[:-1],
        ):
            with self.subTest(arguments=arguments), self.assertRaises(ValueError):
                capture.read_record(self.output, arguments)

    def test_rejects_malformed_footer(self):
        header = "\n".join([*self.arguments, ""]) + "\n"
        footers = [
            "",
            str(self.output) + "\n",
            str(self.output) + "\nnot-a-stamp\n",
            "\n" + capture.stamp(self.output) + "\n",
        ]
        for footer in footers:
            self.record.write_text(header + footer)
            with self.subTest(footer=footer), self.assertRaises(ValueError):
                capture.read_record(self.output, self.arguments)

    def test_rejects_nonblank_header_separator(self):
        self.write_record()
        lines = self.record.read_text().splitlines()
        lines[len(self.arguments)] = "not the separator"
        self.record.write_text("\n".join(lines) + "\n")
        with self.assertRaises(ValueError):
            capture.read_record(self.output, self.arguments)

    def test_rejects_stale_input_and_output_stamps(self):
        for path in (self.source, self.output):
            self.write_record()
            path.write_bytes(path.read_bytes() + b"changed")
            with self.subTest(path=path.name), self.assertRaises(ValueError):
                capture.read_record(self.output, self.arguments)

    def test_output_must_be_final_binding(self):
        for paths in ([self.source], [self.output], [self.output, self.source]):
            self.write_record(paths=paths)
            with self.subTest(paths=paths), self.assertRaises(ValueError):
                capture.read_record(self.output, self.arguments)

    def test_compile_source_must_be_bound(self):
        unrelated = self.root / "unrelated.c"
        unrelated.write_text("int unrelated;\n")
        self.write_record(paths=[unrelated, self.output])
        with self.assertRaises(ValueError):
            capture.read_record(self.output, self.arguments)

    def test_rejects_wrong_command_output_or_source(self):
        cases = [
            ["clang", "-c", str(self.source), "-o", str(self.root / "wrong.o")],
            ["clang", "-c", str(self.source)],
            [*self.arguments, "-o", str(self.output)],
            ["clang", "-c", str(self.root / "wrong.c"), "-o", str(self.output)],
        ]
        for arguments in cases:
            self.write_record(arguments)
            with self.subTest(arguments=arguments), self.assertRaises(ValueError):
                capture.read_record(self.output, arguments)

    def test_link_inputs_must_be_bound(self):
        dependency = self.root / "dependency.o"
        dependency.write_bytes(b"fixture input object")
        arguments = ["cc", str(dependency), "-o", str(self.output)]
        self.write_record(arguments, [dependency, self.output])
        self.assertEqual(capture.read_record(self.output, arguments), self.record)
        self.write_record(arguments, [self.source, self.output])
        with self.assertRaises(ValueError):
            capture.read_record(self.output, arguments)

    def test_macos_sdk_operand_is_not_a_link_input(self):
        sdk = self.root / "MacOSX.sdk"
        sdk.mkdir()
        dependency = self.root / "dependency.o"
        library = self.root / "library.a"
        dependency.write_bytes(b"fixture input object")
        library.write_bytes(b"fixture input library")
        arguments = ["clang", "-isysroot", str(sdk), str(dependency),
                     "-o", str(self.output), str(library)]
        self.write_record(arguments, [dependency, library, self.output])
        self.assertEqual(capture.read_record(self.output, arguments), self.record)
        self.write_record(arguments, [dependency, self.output])
        with self.assertRaises(ValueError):
            capture.read_record(self.output, arguments)


class CommandLine(unittest.TestCase):
    def test_real_compiler_and_nm_receipt_fixtures(self):
        # These tiny real objects exercise the CLI, not Chibi's build producer.
        # Their constructed records only supply the receipt format under test.
        compiler = shutil.which(os.environ.get("CC", "cc"))
        llvm = Path(os.environ.get("LLVM", "/usr/lib/llvm-18"))
        nm = shutil.which(str(llvm / "bin/llvm-nm")) or shutil.which("nm")
        self.assertIsNotNone(compiler, "CLI fixtures need an installed C compiler")
        self.assertIsNotNone(nm, "CLI fixtures need nm")
        helper = Path(__file__).with_name("capture-native-profile.py").resolve()
        objects = ["objects/chibi_support/src/support/arena.c.o",
                   "objects/test_tools/test/tools/main.c.o",
                   "objects/test_language/test/language/main.c.o"]
        links = ["sdk/chibi", "sdk/chibi-compile", "sdk/chibi-build-host",
                 "test/tools/test_tools", "test/language/test_language"]
        for sanitizers in (False, True):
            with self.subTest(sanitizers=sanitizers), tempfile.TemporaryDirectory() as directory:
                root = Path(directory).resolve()
                sources = [root / "src/support/arena.c", root / "test/tools/main.c", root / "test/language/main.c"]
                for source in sources:
                    source.parent.mkdir(parents=True, exist_ok=True)
                    source.write_text("int probe(int *p, int i) { return p[i] + 1; }\n"
                                      "int main(int n, char **v) { (void)v; return probe(&n, 0) == 0; }\n")
                for command in (["git", "init", "-q"], ["git", "add", "."],
                                ["git", "-c", "user.name=CI fixture", "-c", "user.email=fixture@example.invalid",
                                 "-c", "commit.gpgsign=false", "commit", "-qm", "fixture"]):
                    subprocess.run(command, cwd=root, check=True, capture_output=True)
                head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
                flags = ["-g", "-O1", "-fno-omit-frame-pointer", "-UNDEBUG"]
                if sanitizers:
                    flags.append("-fsanitize=address,undefined")
                build = root / "build"
                databases = [[], []]
                for index, paths in enumerate((objects, links)):
                    for source_index, relative in enumerate(paths):
                        output = build / relative
                        output.parent.mkdir(parents=True, exist_ok=True)
                        dependency = sources[source_index] if index == 0 else build / objects[0]
                        command = [compiler, *flags]
                        if index == 0:
                            command.extend(["-o", str(output), "-c", str(dependency)])
                        else:
                            command.extend([str(dependency), "-o", str(output)])
                        subprocess.run(command, check=True, capture_output=True)
                        databases[index].append({"output": str(output), "arguments": command,
                                                 "file": str(dependency), "directory": str(root)})
                        recorded_dependency = dependency.relative_to(root) if index == 0 else dependency
                        Path(str(output) + ".chibi-record").write_text("\n".join([
                            *command, "", str(recorded_dependency), capture.stamp(dependency),
                            str(output), capture.stamp(output)]) + "\n")
                for name, database in zip(("compile_commands.json", "link_commands.json"), databases):
                    (build / name).write_text(json.dumps(database))
                header = build / "test/tools/testing/include/export_compiler.h"
                header.parent.mkdir(parents=True)
                header.write_text("\n".join(json.dumps(flag) + "," for flag in flags) + "\n")
                environment = dict(os.environ, GITHUB_SHA=head, CHIBI_CI_SANITIZERS="ON" if sanitizers else "OFF",
                                   BUILD_PROFILE="sanitizer" if sanitizers else "debug", BUILD_DEBUG_OPTIMIZATION="1",
                                   BUILD_DEBUG_FRAME_POINTERS="" if sanitizers else "on", CHIBI_NATIVE_BUILD_OUTCOME="success")

                def run_capture(name, expected_status, overrides=None):
                    destination = root / name
                    result = subprocess.run([sys.executable, str(helper), "--build", str(build), "--output",
                                             str(destination), "--nm", nm], cwd=root,
                                            env=environment | (overrides or {}), capture_output=True, text=True)
                    receipt = json.loads((destination / "receipt.json").read_text())
                    self.assertEqual(receipt["status"], expected_status, result.stdout + result.stderr)
                    self.assertEqual(result.returncode, 0 if expected_status == "verified" else 1)
                    return receipt

                receipt = run_capture("complete", "verified")
                self.assertEqual(receipt["checkout"], head)
                self.assertEqual(len(receipt["checked"]), 8)
                self.assertTrue((root / "complete" / (objects[0] + ".chibi-record")).is_file())
                self.assertTrue((root / "complete" / (links[0] + ".symbols.txt")).is_file())
                record = Path(str(build / objects[0]) + ".chibi-record")
                wrong_source_database = json.loads(json.dumps(databases[0]))
                wrong_source_database[0]["file"] = str(sources[1])
                for name, path, replacement in (
                    ("unbuilt-command", record, None),
                    ("wrong-exported-flags", header, b'"-O2",\n'),
                    ("wrong-planned-source", build / "compile_commands.json", json.dumps(wrong_source_database).encode()),
                ):
                    original = path.read_bytes()
                    if replacement is None:
                        path.unlink()
                    else:
                        path.write_bytes(replacement)
                    with self.subTest(failure=name):
                        self.assertIn("error", run_capture(name, "incomplete"))
                    path.write_bytes(original)
                run_capture("wrong-source", "incomplete", {"GITHUB_SHA": "0" * 40})
                run_capture("failed-build", "incomplete", {"CHIBI_NATIVE_BUILD_OUTCOME": "failure"})
                if sanitizers:
                    # A claimed sanitizer command and fresh stamps cannot make
                    # an actually unsanitized object acceptable evidence.
                    declared = databases[0][0]["arguments"]
                    actual = [arg for arg in declared if not arg.startswith("-fsanitize=")]
                    subprocess.run(actual, check=True, capture_output=True)
                    output = build / objects[0]
                    record.write_text("\n".join([
                        *declared, "", str(sources[0].relative_to(root)), capture.stamp(sources[0]),
                        str(output), capture.stamp(output)]) + "\n")
                    receipt = run_capture("unsanitized-built-output", "incomplete")
                    self.assertIn("sanitizer symbols", receipt["error"])

                    # Cross-compiled Mach-O objects need no macOS SDK. Replacing
                    # a recorded link input should validate its symbols, then
                    # reject the old link's now-stale input stamp.
                    macho = [str(llvm / "bin/clang"), "--target=arm64-apple-macos15", *declared[1:]]
                    subprocess.run(macho, check=True, capture_output=True)
                    databases[0][0]["arguments"] = macho
                    (build / "compile_commands.json").write_text(json.dumps(databases[0]))
                    record.write_text("\n".join([
                        *macho, "", str(sources[0].relative_to(root)), capture.stamp(sources[0]),
                        str(output), capture.stamp(output)]) + "\n")
                    receipt = run_capture("macho-object-stale-link", "incomplete")
                    self.assertEqual(len(receipt["checked"]), 3, receipt)
                    self.assertIn("stale input/output", receipt["error"])
                    self.assertTrue(all(symbol.startswith("___") for symbol in receipt["checked"][0]["symbols"]))

                    # The Mach-O path must also reject an uninstrumented object
                    # even when fresh constructed records claim sanitizer flags.
                    subprocess.run([arg for arg in macho if not arg.startswith("-fsanitize=")],
                                   check=True, capture_output=True)
                    record.write_text("\n".join([
                        *macho, "", str(sources[0].relative_to(root)), capture.stamp(sources[0]),
                        str(output), capture.stamp(output)]) + "\n")
                    receipt = run_capture("macho-unsanitized-output", "incomplete")
                    self.assertEqual(receipt["checked"], [])
                    self.assertIn("sanitizer symbols", receipt["error"])

                    subprocess.run([str(llvm / "bin/clang"), "--target=arm64-apple-macos15",
                                    "-x", "c", "-", "-o", str(output), "-c"],
                                   input="void __asan_init(void) {}\nvoid __ubsan_handle_add_overflow(void) {}\n",
                                   text=True, check=True, capture_output=True)
                    record.write_text("\n".join([
                        *macho, "", str(sources[0].relative_to(root)), capture.stamp(sources[0]),
                        str(output), capture.stamp(output)]) + "\n")
                    receipt = run_capture("macho-defined-stubs", "incomplete")
                    self.assertEqual(receipt["checked"], [])
                    self.assertIn("sanitizer symbols", receipt["error"])


if __name__ == "__main__":
    unittest.main()
