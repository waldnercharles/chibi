#!/usr/bin/env python3
"""Pure/filesystem and bounded Python-child controls; never run native tools."""

import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest

HERE = Path(__file__).resolve().parent
SUBJECT = types.ModuleType("native_runner_test_subject")
SUBJECT.__file__ = str(HERE / "run_native.py")
exec(compile(Path(SUBJECT.__file__).read_bytes(), SUBJECT.__file__, "exec"), SUBJECT.__dict__)


class NativeRunnerTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="native-runner-controls-", dir="/tmp")
        self.addCleanup(temporary.cleanup)
        self.base = Path(temporary.name).resolve()
        self.plan = SUBJECT.stage_plan(self.base, "/tools/c5", "/tools/llvm", "/tools/clang", [])
        self.stage_names = [row["name"] for row in self.plan]
        self.expectations = {"case": "nativecases", "rejections": [], "aot_checks": 45,
                             "aot_success": SUBJECT.AOT_SUCCESS.decode()}

    def test_exact_five_stage_plan_and_direct_argv(self):
        self.assertEqual([(row["name"], row["limit_seconds"]) for row in self.plan], [
            ("chibi_compile", 179), ("jit_normal", 119), ("perf_failure", 29),
            ("c_link", 59), ("aot_run", 29)])
        base = str(self.base)
        self.assertEqual(self.plan[0]["argv"], [
            "/tools/c5", "--compiler", base + "/project", "--core", base + "/core",
            "--source", base + "/entry/Main.chibi", "--entry", "Main", "--output",
            base + "/output/native-main", "--llvm", "/tools/llvm", "--level", "0", "--jobs", "1"])
        self.assertEqual(self.plan[1]["argv"], [base + "/output/native-main"])
        self.assertEqual(self.plan[2]["argv"], [base + "/output/native-main", "expect-perf-buildid-failure"])
        self.assertEqual(self.plan[3]["argv"], [
            "/tools/clang", "-std=c11", "-O0", "-g0", "-Wall", "-Wextra", "-Werror",
            "-B/tools/llvm/", "-fuse-ld=lld", "-v", "-H", "-MD", "-MF",
            base + "/output/native-aot.d", "-Wl,--trace", base + "/inputs/native_aot_caller.c",
            base + "/run/normal/source-native.o", "-o", base + "/output/native-aot"])
        self.assertEqual(self.plan[4]["argv"], [base + "/output/native-aot"])
        self.assertEqual([row["cwd"] for row in self.plan], [base + suffix for suffix in
                         ("/output", "/run/normal", "/run/perf", "/output", "/run/aot")])
        self.assertEqual([row["artifact"] for row in self.plan], [
            base + "/output/native-main", base + "/run/normal/source-native.o", None,
            base + "/output/native-aot", None])
        self.assertEqual([row["cleanup_limit_seconds"] for row in self.plan], [1] * 5)
        # Exact argv above also excludes backend libraries, legacy callers and shells.
        self.assertEqual([arg for arg in self.plan[3]["argv"] if arg.endswith(".o")],
                         [self.plan[1]["artifact"]])
        self.assertEqual(SUBJECT.SEED_SHA256,
                         "1d1d289a2429fe2ee2a75a71a959bf21590a02ff3ebf7cd8af1037a11bf1268e")

    def test_child_environments_are_separate_and_not_inherited(self):
        original = dict(os.environ)
        expected = {"PATH": "/usr/bin:/bin", "LANG": "C.UTF-8", "LC_ALL": "C.UTF-8",
                    "HOME": str(self.base / "home"), "TMPDIR": str(self.base / "tmp"),
                    "XDG_CACHE_HOME": str(self.base / "cache")}
        for libraries in ([], [Path("/runtime/one"), Path("/runtime/two")]):
            plan = SUBJECT.stage_plan(self.base, "/tools/c5", "/tools/llvm", "/tools/clang", libraries)
            for index, stage in enumerate(plan):
                environment = dict(expected)
                if libraries:
                    environment["LD_LIBRARY_PATH"] = "/runtime/one:/runtime/two"
                if index == 2:
                    environment["PERF_BUILDID_DIR"] = str(self.base / "run/perf/forbidden-profile")
                self.assertEqual(stage["environment"], environment)
            plan[2]["environment"]["TEST_ONLY"] = "isolated"
            self.assertTrue(all("TEST_ONLY" not in row["environment"] for i, row in enumerate(plan) if i != 2))
        self.assertEqual(dict(os.environ), original)

    def test_next_stage_stops_at_every_failed_prefix_and_at_five(self):
        for count in range(6):
            results = [{"status": "passed"} for _ in range(count)]
            expected = self.plan[count] if count < 5 else None
            self.assertIs(SUBJECT.next_stage(self.plan, results), expected)
            for failed in range(count):
                for status in ("failed", "not_launched", "timed_out", "interrupted"):
                    rows = copy.deepcopy(results)
                    rows[failed]["status"] = status
                    self.assertIsNone(SUBJECT.next_stage(self.plan, rows))

    def test_next_stage_asserts_on_impossible_extra_receipts(self):
        with self.assertRaises(AssertionError):
            SUBJECT.next_stage(self.plan, [{"status": "passed"}] * 6)

    def success(self, limit=29):
        return {"status": "passed", "exit_code": 0, "timed_out": False,
                "elapsed_seconds": limit, "cleanup_seconds": 1, "surviving_children": False,
                "cleanup_errors": [], "cleanup_echild": True, "subreaper_restored": True,
                "cleanup_limit_seconds": 1, "overall_limit_seconds": limit + 1}

    def test_success_requires_closed_ownership_and_finite_bounded_times(self):
        SUBJECT.require_success(self.success(), 29)
        SUBJECT.require_success({**self.success(), "elapsed_seconds": 0, "cleanup_seconds": 0}, 29)
        invalid = {
            "status": ("failed", "timed_out", "not_launched"), "exit_code": (1, -9, None, False, 0.0),
            "timed_out": (True, None, 0), "surviving_children": (True, None, 0),
            "cleanup_errors": (["error"], None), "cleanup_echild": (False, None, 1),
            "subreaper_restored": (False, None, 1),
            "cleanup_limit_seconds": (0, 2, True, False, float("nan"), float("inf"), -float("inf"), -1, None, "1"),
            "overall_limit_seconds": (29, 31, True, False, float("nan"), float("inf"), -float("inf"), -1, None, "30"),
            "elapsed_seconds": (float("nan"), float("inf"), -float("inf"), True, False, -1, 29.001, None, "0"),
            "cleanup_seconds": (float("nan"), float("inf"), -float("inf"), True, False, -1, 1.001, None, "0"),
        }
        for field, values in invalid.items():
            for value in values:
                with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                    SUBJECT.require_success({**self.success(), field: value}, 29)
        for field in self.success():
            row = self.success()
            del row[field]
            with self.subTest(missing=field), self.assertRaises(ValueError):
                SUBJECT.require_success(row, 29)
        for field in ("exception", "inspection_error", "termination_error", "interruption"):
            with self.subTest(error=field), self.assertRaises(ValueError):
                SUBJECT.require_success({**self.success(), field: None}, 29)

    def test_exact_aot_success_record(self):
        self.assertEqual(SUBJECT.AOT_SUCCESS,
                         b'{"aot_c_caller_checks":45,"status":"passed","input":"source-produced-copied-object"}\n')

    def test_write_new_is_exclusive_and_validates_before_creating(self):
        path = self.base / "receipt.json"
        SUBJECT.write_new(path, {"status": "passed"})
        original = path.read_bytes()
        self.assertEqual(json.loads(original), {"status": "passed"})
        with self.assertRaises(FileExistsError):
            SUBJECT.write_new(path, {"status": "failed"})
        self.assertEqual(path.read_bytes(), original)
        for value in (float("nan"), float("inf"), -float("inf")):
            for destination in (path, self.base / "absent.json"):
                with self.subTest(value=value, path=destination), self.assertRaises(ValueError):
                    SUBJECT.write_new(destination, {"nested": [value]})
            self.assertFalse((self.base / "absent.json").exists())
            self.assertEqual(path.read_bytes(), original)

    def test_file_identity_rejects_symlinks_and_detects_content_and_replacement(self):
        path = self.base / "file"
        path.write_bytes(b"before")
        original = SUBJECT.file_identity(path)
        self.assertEqual(original["sha256"], hashlib.sha256(b"before").hexdigest())
        self.assertEqual(original["bytes"], 6)
        link = self.base / "link"
        link.symlink_to(path)
        with self.assertRaises(ValueError):
            SUBJECT.file_identity(link)
        with self.assertRaises(ValueError):
            SUBJECT.file_identity(self.base)
        path.write_bytes(b"after!")
        self.assertNotEqual(SUBJECT.file_identity(path), original)
        path.write_bytes(b"before")
        replacement = self.base / "replacement"
        replacement.write_bytes(b"before")
        replacement.replace(path)
        current = SUBJECT.file_identity(path)
        self.assertEqual(current["sha256"], original["sha256"])
        self.assertNotEqual(current["inode"], original["inode"])
        self.assertNotEqual(current, original)

    def snapshot(self):
        directory = self.base / "prepared"
        directory.mkdir()
        names = ["project/source" + str(i) + ".chibi" for i in range(29)]
        names += ["core/Core.chibi", "inputs/caller.c", "entry/Main.chibi"]
        implementation = ["controller/owned_process.py", "controller/run_native.py"]
        rows = []
        for name in names + implementation:
            path = directory / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(("identity fixture: " + name).encode())
            rows.append({"path": name, **SUBJECT.file_identity(path)})
        backend = self.base / "backend.so"
        backend.write_bytes(b"identity only; not a loadable library")
        manifest = {"schema": 2, "directory": str(directory),
                    "expectations": self.expectations,
                    "stages": {name: "not_run" for name in self.stage_names},
                    "files": rows[:32], "implementation": rows[32:],
                    "backend": {"path": str(backend), "resolved": str(backend), **SUBJECT.file_identity(backend)}}
        return directory, manifest

    def test_check_sources_exact_membership_and_manifest(self):
        directory, manifest = self.snapshot()
        identities = SUBJECT.check_sources(directory, manifest, self.stage_names)
        self.assertEqual(len(identities), 35)
        for change in ("schema", "directory", "stages", "missing_row", "duplicate", "implementation", "escape"):
            altered = copy.deepcopy(manifest)
            if change in ("schema", "directory", "stages"):
                altered[change] = None
            elif change == "missing_row":
                altered["files"].pop()
            elif change == "duplicate":
                altered["files"][1] = altered["files"][0]
            elif change == "implementation":
                altered["implementation"][0]["path"] = "controller/other.py"
            else:
                altered["files"][0]["path"] = "project/../outside"
            with self.subTest(change=change), self.assertRaises(ValueError):
                SUBJECT.check_sources(directory, altered, self.stage_names)
        unexpected = directory / "core/unexpected.chibi"
        unexpected.write_bytes(b"unexpected")
        with self.assertRaises(ValueError):
            SUBJECT.check_sources(directory, manifest, self.stage_names)
        unexpected.unlink()
        source = directory / manifest["files"][0]["path"]
        saved = source.read_bytes()
        source.unlink()
        with self.assertRaises(ValueError):
            SUBJECT.check_sources(directory, manifest, self.stage_names)
        source.write_bytes(saved)
        source.write_bytes(b"changed source")
        with self.assertRaises(ValueError):
            SUBJECT.check_sources(directory, manifest, self.stage_names)
        source.unlink()
        source.symlink_to(directory / "core/Core.chibi")
        with self.assertRaises(ValueError):
            SUBJECT.check_sources(directory, manifest, self.stage_names)

    def test_check_sources_rejects_changed_backend(self):
        directory, manifest = self.snapshot()
        backend = Path(manifest["backend"]["path"])
        for content in (b"changed backend", b""):
            backend.write_bytes(content)
            with self.subTest(content=content), self.assertRaises(ValueError):
                SUBJECT.check_sources(directory, manifest, self.stage_names)


    def test_malformed_manifest_roots_and_required_fields(self):
        directory, manifest = self.snapshot()
        for root in (None, [], 1, True, "manifest"):
            with self.subTest(root=root), self.assertRaisesRegex(ValueError, "manifest"):
                SUBJECT.check_sources(directory, root, self.stage_names)
        for key in ("schema", "directory", "stages", "files", "implementation", "backend", "expectations"):
            altered = copy.deepcopy(manifest)
            del altered[key]
            with self.subTest(missing=key), self.assertRaisesRegex(ValueError, key):
                SUBJECT.check_sources(directory, altered, self.stage_names)
        for key, bad_values in {
            "schema": (True, "1", None, 1.0),
            "directory": ([], None, 1), "stages": ([], None, "not_run"),
            "files": (None, {}, "files"), "implementation": (None, {}, "implementation"),
            "backend": (None, [], "backend"), "expectations": (None, [], "expectations"),
        }.items():
            for value in bad_values:
                altered = copy.deepcopy(manifest)
                altered[key] = value
                with self.subTest(field=key, value=value), self.assertRaisesRegex(ValueError, key):
                    SUBJECT.check_sources(directory, altered, self.stage_names)

    def test_malformed_manifest_rows_and_backend_fields(self):
        directory, manifest = self.snapshot()
        for collection in ("files", "implementation"):
            for value in (None, [], "row"):
                altered = copy.deepcopy(manifest)
                altered[collection][0] = value
                with self.subTest(collection=collection, row=value), self.assertRaises(ValueError):
                    SUBJECT.check_sources(directory, altered, self.stage_names)
        for collection in ("files", "implementation", "backend"):
            for key, bad_values in {
                "path": (None, [], 1, ""), "bytes": (None, True, -1, 1.5, "1"),
                "sha256": (None, [], "x" * 64, "0" * 63),
            }.items():
                for value in bad_values:
                    altered = copy.deepcopy(manifest)
                    row = altered[collection] if collection == "backend" else altered[collection][0]
                    row[key] = value
                    with self.subTest(collection=collection, field=key, value=value), self.assertRaisesRegex(ValueError, key):
                        SUBJECT.check_sources(directory, altered, self.stage_names)
                altered = copy.deepcopy(manifest)
                row = altered[collection] if collection == "backend" else altered[collection][0]
                del row[key]
                with self.subTest(collection=collection, missing=key), self.assertRaisesRegex(ValueError, key):
                    SUBJECT.check_sources(directory, altered, self.stage_names)
        for value in (None, [], "relative", "/different/backend.so"):
            altered = copy.deepcopy(manifest)
            altered["backend"]["resolved"] = value
            with self.subTest(resolved=value), self.assertRaises(ValueError):
                SUBJECT.check_sources(directory, altered, self.stage_names)

    def test_bad_helper_is_not_executed_on_import_help_or_rejected_run(self):
        directory, manifest = self.snapshot()
        controller = directory / "controller/run_native.py"
        controller.write_bytes(Path(SUBJECT.__file__).read_bytes())
        manifest["implementation"][1] = {"path": "controller/run_native.py", **SUBJECT.file_identity(controller)}
        manifest_path = directory / "source-manifest.json"
        manifest_path.write_text(json.dumps(manifest))
        helper = directory / "controller/owned_process.py"
        marker = self.base / "unexpected-helper-execution"
        sentinel = f"from pathlib import Path\nPath({str(marker)!r}).write_text('executed')\nraise AssertionError('helper executed')\n"
        # An old fallback search would find this file when the copied helper is missing.
        (self.base / "owned_process.py").write_text(sentinel)
        for damage in ("changed", "missing", "symlink"):
            if helper.exists() or helper.is_symlink():
                helper.unlink()
            if damage == "changed":
                helper.write_text(sentinel)
            elif damage == "symlink":
                helper.symlink_to(self.base / "owned_process.py")
            module = types.ModuleType("frozen_native_control")
            module.__file__ = str(controller)
            with self.subTest(damage=damage):
                exec(compile(controller.read_bytes(), str(controller), "exec"), module.__dict__)
                self.assertFalse(marker.exists())
                help_result = subprocess.run([sys.executable, "-B", str(controller), "--help"],
                                             capture_output=True, timeout=5, check=False)
                self.assertEqual(help_result.returncode, 0, help_result.stderr.decode())
                self.assertFalse(marker.exists())
                with self.assertRaises((ValueError, OSError)):
                    module.run(directory, self.base / "no-seed", self.base / "no-llvm",
                               self.base / "no-cc", [])
                self.assertFalse(marker.exists())
                self.assertFalse((directory / "attempt").exists())

    def test_cli_rejects_malformed_json_without_loading_helper(self):
        directory, manifest = self.snapshot()
        controller = directory / "controller/run_native.py"
        controller.write_bytes(Path(SUBJECT.__file__).read_bytes())
        marker = self.base / "unexpected-json-helper"
        (directory / "controller/owned_process.py").write_text(
            f"from pathlib import Path\nPath({str(marker)!r}).touch()\n")
        for content in ("{", "[]", "null", '{"schema": 1}', "[" * 10000 + "0" + "]" * 10000):
            (directory / "source-manifest.json").write_text(content)
            result = subprocess.run([sys.executable, "-B", str(controller), str(directory),
                                     "--seed", str(self.base / "no-seed"),
                                     "--llvm-bin", str(self.base / "no-llvm"),
                                     "--cc", str(self.base / "no-cc")],
                                    capture_output=True, timeout=5, check=False)
            with self.subTest(content=content):
                self.assertEqual(result.returncode, 1)
                self.assertIn(b"native reproduction:", result.stderr)
                self.assertNotIn(b"Traceback", result.stderr)
                self.assertFalse(marker.exists())
                self.assertFalse((directory / "attempt").exists())

    def signal_control(self, phase):
        keys = ("CHIBI_NATIVE_CONTROL_SEED", "CHIBI_NATIVE_CONTROL_LLVM_BIN", "CHIBI_NATIVE_CONTROL_CC")
        paths = [os.environ.get(key) for key in keys]
        if not all(paths):
            self.skipTest("explicit native-control identity paths not supplied; no native execution is permitted")
        directory, manifest = self.snapshot()
        for name, source in (("run_native.py", Path(SUBJECT.__file__)),
                             ("owned_process.py", HERE.parent.parent / "owned_process.py")):
            destination = directory / "controller" / name
            destination.write_bytes(source.read_bytes())
            row = next(row for row in manifest["implementation"] if row["path"] == "controller/" + name)
            row.update(SUBJECT.file_identity(destination))
        (directory / "source-manifest.json").write_text(json.dumps(manifest))
        script = """import json, os, runpy, signal, sys
from pathlib import Path
root = Path(sys.argv[1])
phase = sys.argv[2]
subject = runpy.run_path(str(root / 'controller/run_native.py'))
state = {'popen': 0, 'signal_sent': False}
def audit(event, args):
    if event == 'subprocess.Popen':
        state['popen'] += 1
        raise RuntimeError('native child execution is forbidden by this control')
    if event != 'open' or state['signal_sent']:
        return
    target = args[0]
    if not isinstance(target, (str, bytes, os.PathLike)):
        return
    target = os.fsdecode(target)
    hit = ((phase == 'prelaunch' and target == str(root / 'attempt/chibi_compile.stdout'))
           or (phase == 'postflight' and state['popen'] == 1 and target == str(root / 'project/source0.chibi'))
           or (phase == 'receipt' and target == str(root / 'attempt/chibi_compile-result.json')))
    if hit:
        state['signal_sent'] = True
        os.kill(os.getpid(), signal.SIGTERM)
sys.addaudithook(audit)
def trace(frame, event, argument):
    if (phase == 'reservation' and not state['signal_sent'] and event == 'line'
            and frame.f_code.co_filename == str(root / 'controller/run_native.py')
            and frame.f_code.co_name == 'run'
            and frame.f_locals.get('attempt_owned') is False
            and (root / 'attempt').is_dir()):
        state['signal_sent'] = True
        os.kill(os.getpid(), signal.SIGTERM)
    return trace
if phase == 'reservation':
    sys.settrace(trace)
prior_handlers = {sig: signal.getsignal(sig) for sig in (signal.SIGINT, signal.SIGTERM)}
prior_mask = signal.pthread_sigmask(signal.SIG_BLOCK, [])
result = subject['run'](root, Path(sys.argv[3]), Path(sys.argv[4]), Path(sys.argv[5]), [])
sys.settrace(None)
assert {sig: signal.getsignal(sig) for sig in prior_handlers} == prior_handlers, 'signal handlers changed'
assert signal.pthread_sigmask(signal.SIG_BLOCK, []) == prior_mask, 'signal mask changed'
try:
    os.waitid(os.P_ALL, 0, os.WEXITED | os.WNOHANG | os.WNOWAIT)
except ChildProcessError:
    state['cleanup_echild'] = True
else:
    raise AssertionError('unexpected child or descendant remains')
print(json.dumps({'result': result, 'control': state}))
"""
        result = subprocess.run([sys.executable, "-B", "-c", script, str(directory), phase, *paths],
                                capture_output=True, timeout=45, check=False)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        facts = json.loads(result.stdout)
        self.assertTrue(facts["control"]["signal_sent"])
        self.assertTrue(facts["control"]["cleanup_echild"])
        self.assertEqual(facts["control"]["popen"], 0 if phase in ("reservation", "prelaunch") else 1)
        self.assertEqual(facts["result"]["status"], "failed")
        self.assertEqual(facts["result"]["controller_signals"], ["SIGTERM"])
        attempted = 0 if phase == "reservation" else 1
        self.assertEqual(len(facts["result"]["stages"]), attempted)
        if attempted:
            self.assertEqual(facts["result"]["stages"][0]["status"], "failed")
        self.assertEqual(facts["result"]["not_run"], self.stage_names[attempted:])
        attempt = directory / "attempt"
        self.assertEqual(json.loads((attempt / "result.json").read_text()), facts["result"])
        if attempted:
            self.assertTrue((attempt / "attempt.json").is_file())
            self.assertTrue((attempt / "chibi_compile-result.json").is_file())
        for name in self.stage_names[attempted:]:
            self.assertFalse((attempt / (name + "-launch.json")).exists())
        with self.assertRaises(FileExistsError):
            SUBJECT.write_new(attempt / "result.json", {})
        return facts["result"]

    def test_signal_during_attempt_reservation_preserves_failed_receipt(self):
        self.signal_control("reservation")

    def test_signal_before_launch_preserves_failed_receipt(self):
        self.signal_control("prelaunch")

    def test_signal_during_postflight_preserves_failed_receipt(self):
        self.signal_control("postflight")

    def test_signal_during_receipt_preserves_failed_receipt(self):
        self.signal_control("receipt")

    def test_stage_output_checks_require_empty_perf_directory(self):
        perf = self.base / "run/perf"
        perf.mkdir(parents=True)
        attempt = self.base / "attempt"
        attempt.mkdir()
        SUBJECT.check_stage_outputs(self.plan[2], self.base, attempt, self.expectations)
        for kind in ("file", "directory"):
            unexpected = perf / "forbidden-profile"
            if kind == "file":
                unexpected.write_bytes(b"unexpected profile")
            else:
                unexpected.mkdir()
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                SUBJECT.check_stage_outputs(self.plan[2], self.base, attempt, self.expectations)
            if kind == "file":
                unexpected.unlink()
            else:
                unexpected.rmdir()

    def test_stage_output_checks_exact_and_bounded_aot_record(self):
        attempt = self.base / "attempt"
        attempt.mkdir()
        stdout = attempt / "aot_run.stdout"
        stdout.write_bytes(SUBJECT.AOT_SUCCESS)
        SUBJECT.check_stage_outputs(self.plan[4], self.base, attempt, self.expectations)
        for content in (b"", SUBJECT.AOT_SUCCESS[:-1], SUBJECT.AOT_SUCCESS + b"x",
                        b" " + SUBJECT.AOT_SUCCESS):
            stdout.write_bytes(content)
            with self.subTest(content=content), self.assertRaises(ValueError):
                SUBJECT.check_stage_outputs(self.plan[4], self.base, attempt, self.expectations)
        # Sparse data exercises a large trailing log without allocating it in the test.
        with stdout.open("wb") as stream:
            stream.write(SUBJECT.AOT_SUCCESS)
            stream.truncate(64 * 1024 * 1024)
        with self.assertRaises(ValueError):
            SUBJECT.check_stage_outputs(self.plan[4], self.base, attempt, self.expectations)
        # A bounded read must reject even when the log exceeds available address space.
        script = """import resource, runpy, sys
from pathlib import Path
subject = runpy.run_path(sys.argv[1])
resource.setrlimit(resource.RLIMIT_AS, (48 * 1024 * 1024, 48 * 1024 * 1024))
try:
    subject['check_stage_outputs']({'name': 'aot_run'}, Path(sys.argv[2]), Path(sys.argv[3]), {'aot_success': subject['AOT_SUCCESS'].decode()})
except ValueError:
    pass
else:
    raise AssertionError('accepted trailing log bytes')
"""
        result = subprocess.run([sys.executable, "-B", "-c", script, SUBJECT.__file__,
                                 str(self.base), str(attempt)], capture_output=True, timeout=5, check=False)
        self.assertEqual(result.returncode, 0, result.stderr.decode())


    def test_registry_metadata_tampering_and_exact_aot_counts(self):
        prepare = types.ModuleType("native_preparation")
        prepare.__file__ = str(HERE / "prepare_native.py")
        exec(compile(Path(prepare.__file__).read_bytes(), prepare.__file__, "exec"), prepare.__dict__)
        backend = self.base / "identity-backend.so"
        backend.write_bytes(b"identity only, never loaded")
        for case, count in (("scalars", 15), ("direct_calls", 23)):
            directory = self.base / case
            manifest = prepare.prepare(directory, backend, case)
            SUBJECT.check_sources(directory, manifest, self.stage_names)
            attempt = directory / "attempt"
            attempt.mkdir()
            output = attempt / "aot_run.stdout"
            expected = manifest["expectations"]
            success = expected["aot_success"].encode()
            output.write_bytes(success)
            SUBJECT.check_stage_outputs(self.plan[4], directory, attempt, expected)
            for content in (SUBJECT.AOT_SUCCESS, success + b"x", success[:-1], b" " + success,
                            success.replace(str(count).encode(), b"0", 1)):
                output.write_bytes(content)
                with self.subTest(case=case, content=content), self.assertRaises(ValueError):
                    SUBJECT.check_stage_outputs(self.plan[4], directory, attempt, expected)
            for key, value in (("aot_checks", count + 1), ("aot_checks", True),
                               ("aot_success", SUBJECT.AOT_SUCCESS.decode()),
                               ("case", "modules"), ("rejections", ["unknown"]), ("rejections", None)):
                altered = copy.deepcopy(manifest)
                altered["expectations"][key] = value
                with self.subTest(case=case, key=key), self.assertRaises(ValueError):
                    SUBJECT.check_sources(directory, altered, self.stage_names)
            # Changing both metadata fields still cannot disagree with the registry.
            altered = copy.deepcopy(manifest)
            altered["expectations"]["aot_checks"] = count + 1
            altered["expectations"]["aot_success"] = expected["aot_success"].replace(str(count), str(count + 1), 1)
            with self.assertRaisesRegex(ValueError, "prepared registry"):
                SUBJECT.check_sources(directory, altered, self.stage_names)
            for name in ("inputs/registry_callers.py", "inputs/slice_cases.json", "inputs/native_aot_caller.c", "entry/Main.chibi"):
                path = directory / name
                original = path.read_bytes()
                path.write_bytes(original + b" ")
                with self.subTest(case=case, name=name), self.assertRaisesRegex(ValueError, "snapshot changed"):
                    SUBJECT.check_sources(directory, manifest, self.stage_names)
                path.write_bytes(original)


if __name__ == "__main__":
    unittest.main()
