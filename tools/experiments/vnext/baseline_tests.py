#!/usr/bin/env python3
"""Harness tests only: Python child processes are not Chibi benchmarks."""

import copy
import json
import os
import signal
import subprocess
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

import baseline


class BaselineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(__file__).resolve().parents[3]
        cls.head = baseline.git(cls.root, "rev-parse", "HEAD")
        cls.tree = baseline.git(cls.root, "rev-parse", "HEAD^{tree}")
        cls.census = baseline.census(cls.root, cls.head, cls.tree)
        cls.python = str(Path(sys.executable).resolve())
        cls.python_digest = baseline.file_digest(cls.python)

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="chibi-baseline-test-")
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        self.receipt = self.folder / "census.json"
        baseline.write_json(self.receipt, self.census)
        self.spec = {"schema": 1, "classification": "harness_self_test", "workload": "Python harness self-test",
                     "source": {"root": str(self.root), "head": self.head, "tree": self.tree,
                                "census": str(self.receipt), "census_sha256": baseline.file_digest(self.receipt)},
                     "tools": [{"path": self.python, "sha256": self.python_digest}],
                     "environment": {}, "timeout_seconds": 5,
                     "argv": [self.python, "-c", "from pathlib import Path; Path('program').write_bytes(b'ok')"],
                     "expected_outputs": [{"path": "program", "sha256": baseline.digest(b"ok")}]}

    def run_spec(self, name="run"):
        spec_path = self.folder / f"{name}.json"
        baseline.write_json(spec_path, self.spec)
        return baseline.measure(spec_path, self.folder / name)

    def test_gather_import_scanner_matches_lf_and_ascii_whitespace(self):
        cases = [
            ("", []),
            ('import item "core:first";\r\nimport item "core:second";\n', ["first", "second"]),
            (' \t\v\f\rimport item "core:trimmed";\r\n', ["trimmed"]),
            ('// ordinary comment\rimport "core:phantom";\n', []),
            ('import\titem "core:phantom";\n', []),
            ('import item "project:other";\n', []),
        ]
        for separator in ("\u0085", "\u2028", "\u2029"):
            cases.append((f'// ordinary comment{separator}import "core:phantom";\n', []))
            cases.append((f'import "core:first";{separator}import "core:phantom";\n', ["first"]))
        for whitespace in ("\u00a0", "\u1680", "\u2003", "\u2028", "\u2029", "\u3000"):
            cases.append((f'{whitespace}import "core:phantom";\n', []))
        for source, expected in cases:
            with self.subTest(source=repr(source)):
                self.assertEqual(baseline.imported_from(source, "core"), expected)

    def test_collection_root_packages_preserve_strict_output_paths(self):
        for collection in ("project", "core"):
            with self.subTest(collection=collection):
                names = baseline.imported_from(f'import root "{collection}:.";\n', collection)
                self.assertEqual(names, ["."])
                root = self.root / baseline.ROOTS[collection]
                self.assertEqual(baseline.package_path(root, names[0]), root)
                with self.assertRaises(ValueError):
                    baseline.package_path(root, "")
                with self.assertRaises(ValueError):
                    baseline.package_path(root, "../escape")
        self.spec["expected_outputs"] = [{"path": "."}]
        with self.assertRaisesRegex(ValueError, "relative contained"):
            self.run_spec()
        self.assertFalse((self.folder / "run").exists())

    def test_static_census_identity_and_totals(self):
        self.assertEqual(self.census["classification"], "static_gather_census")
        rows = self.census["source_files"]
        self.assertEqual(len({r["path"] for r in rows}), len(rows))
        self.assertEqual(self.census["files"], len(rows))
        self.assertEqual(self.census["bytes"], sum(r["bytes"] for r in rows))
        self.assertEqual(self.census["physical_lines"], sum(r["physical_lines"] for r in rows))
        self.assertEqual(rows[0]["path"], "tools/compiler/native_driver/Main.chibi")
        for row in rows:
            data = (self.root / row["path"]).read_bytes()
            self.assertFalse(row["path"].endswith("Tests.chibi"))
            self.assertEqual(row["sha256"], baseline.digest(data))
            self.assertEqual(row["physical_lines"], len(data.splitlines()))
        self.assertNotIn("elapsed_seconds", self.census)

    def test_success_fresh_outputs_and_honest_boundary(self):
        result = self.run_spec()
        self.assertTrue(result["success"], result)
        self.assertEqual(result["exit_code"], 0)
        self.assertEqual(result["classification"], "harness_self_test")
        self.assertIsNone(result["phase_times"])
        self.assertIsNone(result["peak_memory_bytes"])
        self.assertIsNone(result["allocations"])
        self.assertTrue(result["cleanup_echild"])
        self.assertTrue(result["subreaper_restored"])
        self.assertEqual(result["cleanup_limit_seconds"], 1)
        self.assertEqual(result["overall_limit_seconds"], self.spec["timeout_seconds"] + 1)
        self.assertNotIn("lines_per_second", result)
        self.assertIn("SHA-256", result["outside_timing"][0])
        self.assertIn("not a cold-cache", result["filesystem_condition"])
        self.assertEqual(result["outputs"][0]["sha256"], baseline.digest(b"ok"))
        self.assertEqual(json.loads((self.folder / "run/result.json").read_text()), result)
        self.assertEqual(list((self.folder / "run/cache").iterdir()), [])

    def test_wrong_source_and_tool_do_not_launch(self):
        original = copy.deepcopy(self.spec)
        for field, value in (("head", "0" * 40), ("tree", "0" * 40), ("census_sha256", "0" * 64)):
            with self.subTest(field=field):
                self.spec = copy.deepcopy(original)
                self.spec["source"][field] = value
                with self.assertRaises(ValueError):
                    self.run_spec(field)
                self.assertFalse((self.folder / field).exists())
        self.spec = copy.deepcopy(original)
        self.spec["tools"][0]["sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "tool identity"):
            self.run_spec("tool")
        self.assertFalse((self.folder / "tool").exists())

    def test_wrong_closure_even_with_matching_receipt_digest(self):
        changed = copy.deepcopy(self.census)
        changed["source_files"][0]["sha256"] = "0" * 64
        forged = self.folder / "changed.json"
        baseline.write_json(forged, changed)
        self.spec["source"]["census"] = str(forged)
        self.spec["source"]["census_sha256"] = baseline.file_digest(forged)
        with self.assertRaisesRegex(ValueError, "source closure"):
            self.run_spec()
        self.assertFalse((self.folder / "run").exists())

    def test_unsuccessful_process_preserves_logs_and_exit(self):
        self.spec["argv"][2] = "import sys; print('out', flush=True); print('err', file=sys.stderr, flush=True); sys.exit(7)"
        result = self.run_spec()
        self.assertFalse(result["success"])
        self.assertEqual(result["exit_code"], 7)
        self.assertFalse(result["timed_out"])
        self.assertEqual((self.folder / "run/stdout.log").read_text(), "out\n")
        self.assertEqual((self.folder / "run/stderr.log").read_text(), "err\n")
        self.assertTrue((self.folder / "run/result.json").is_file())

    def test_timeout_preserves_partial_logs(self):
        self.spec["timeout_seconds"] = 0.5
        self.spec["argv"][2] = "import sys,time; print('started',flush=True); print('waiting',file=sys.stderr,flush=True); time.sleep(30)"
        start = time.monotonic()
        result = self.run_spec()
        self.assertLess(time.monotonic() - start, 10)
        self.assertTrue(result["timed_out"])
        self.assertFalse(result["success"])
        self.assertLess(result["exit_code"], 0)
        self.assertEqual((self.folder / "run/stdout.log").read_text(), "started\n")
        self.assertEqual((self.folder / "run/stderr.log").read_text(), "waiting\n")

    def assert_signal_cleanup(self, signum):
        marker = self.folder / "late-child-work"
        self.spec["timeout_seconds"] = 0.8
        self.spec["argv"][2] = (
            "import os,sys,time; from pathlib import Path; "
            "print(os.getpid(), flush=True); print('waiting', file=sys.stderr, flush=True); "
            f"time.sleep(1.4); Path({str(marker)!r}).touch()")
        spec_path = self.folder / "interrupt.json"
        baseline.write_json(spec_path, self.spec)
        run_dir = self.folder / "interrupted"
        command = [self.python, str(Path(baseline.__file__).resolve()), "run",
                   "--spec", str(spec_path), "--run-dir", str(run_dir)]
        harness = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        child_pid = None
        try:
            log = run_dir / "stdout.log"
            deadline = time.monotonic() + 10
            while not log.exists() or not log.read_text().strip():
                if time.monotonic() >= deadline or harness.poll() is not None:
                    self.fail("child did not announce readiness before signal test")
                time.sleep(0.01)
            child_pid = int(log.read_text().strip())
            harness.send_signal(signum)
            stdout, stderr = harness.communicate(timeout=10)
            self.assertEqual(harness.returncode, 1, (stdout, stderr))
            result = json.loads((run_dir / "result.json").read_text())
            self.assertFalse(result["success"])
            self.assertEqual(result["interruption"], {"signal": signum, "name": signal.Signals(signum).name})
            self.assertEqual(result["exit_code"], -signal.SIGKILL)
            self.assertFalse(result["timed_out"])
            self.assertEqual((run_dir / "stderr.log").read_text(), "waiting\n")
            self.assertEqual(int(log.read_text().strip()), child_pid)
            time.sleep(1.5)
            self.assertFalse(marker.exists(), "interrupted child outlived the original deadline")
        finally:
            if harness.poll() is None:
                harness.terminate()
            harness.communicate(timeout=10)

    def test_external_sigint_terminates_child_and_preserves_receipt(self):
        self.assert_signal_cleanup(signal.SIGINT)

    def test_external_sigterm_terminates_child_and_preserves_receipt(self):
        self.assert_signal_cleanup(signal.SIGTERM)

    def test_timer_start_failure_terminates_child_and_preserves_receipt(self):
        marker = self.folder / "timer-failure-child-work"
        log = self.folder / "run/stdout.log"
        self.spec["argv"][2] = (
            "import os,time; from pathlib import Path; print(os.getpid(), flush=True); "
            f"time.sleep(0.6); Path({str(marker)!r}).touch()")
        child_pid = None
        handlers = {s: signal.getsignal(s) for s in (signal.SIGINT, signal.SIGTERM)}

        def fail_start(_timer):
            nonlocal child_pid
            deadline = time.monotonic() + 5
            while not log.exists() or not log.read_text().strip():
                if time.monotonic() >= deadline:
                    self.fail("child did not announce readiness before timer failure")
                time.sleep(0.01)
            child_pid = int(log.read_text().strip())
            raise RuntimeError("injected Timer.start failure")

        # Inject only the environment failure; launch/wait/logging are real.
        with patch.object(baseline.owned_process.threading.Timer, "start", fail_start):
            result = self.run_spec()
        self.assertFalse(result["success"])
        self.assertEqual(result["exception"], {"type": "RuntimeError", "message": "injected Timer.start failure"})
        self.assertEqual(result["exit_code"], -signal.SIGKILL)
        self.assertFalse(result["timed_out"])
        self.assertEqual(json.loads((self.folder / "run/result.json").read_text()), result)
        self.assertEqual(int(log.read_text().strip()), child_pid)
        self.assertEqual({s: signal.getsignal(s) for s in handlers}, handlers)
        time.sleep(0.8)
        self.assertFalse(marker.exists(), "timer startup failure left its child running")

    def test_group_signals_precede_reap(self):
        original = os.killpg
        signals = []

        def signal_unreaped(group, signum):
            # This raises ChildProcessError if a previous wait reaped the leader.
            os.waitid(os.P_PID, group, os.WEXITED | os.WNOWAIT | os.WNOHANG)
            signals.append(signum)
            return original(group, signum)

        with patch.object(baseline.owned_process.os, "killpg", signal_unreaped):
            result = self.run_spec()
        self.assertTrue(result["success"], result)
        self.assertEqual(signals, [signal.SIGKILL])

    def test_interrupt_during_popen_assignment(self):
        original = subprocess.Popen
        handlers = {s: signal.getsignal(s) for s in (signal.SIGINT, signal.SIGTERM)}
        self.spec["argv"][2] = "import time; time.sleep(10)"

        def launch_then_interrupt(*args, **kwargs):
            child = original(*args, **kwargs)
            if kwargs.get("start_new_session"):
                os.kill(os.getpid(), signal.SIGINT)
                os.kill(os.getpid(), signal.SIGTERM)
            return child

        with patch.object(baseline.owned_process.subprocess, "Popen", launch_then_interrupt):
            result = self.run_spec()
        self.assertEqual(result["interruption"]["name"], "SIGINT")
        self.assertEqual(result["exit_code"], -signal.SIGKILL)
        self.assertEqual({s: signal.getsignal(s) for s in handlers}, handlers)

    def test_timer_construction_failure_preserves_receipt(self):
        self.spec["argv"][2] = "import time; time.sleep(10)"
        with patch.object(baseline.owned_process.threading, "Timer", side_effect=RuntimeError("timer construction failed")):
            result = self.run_spec()
        self.assertFalse(result["success"])
        self.assertEqual(result["exception"]["message"], "timer construction failed")
        self.assertEqual(result["exit_code"], -signal.SIGKILL)
        self.assertEqual(json.loads((self.folder / "run/result.json").read_text()), result)

    def test_proc_read_failure_cleans_group_and_preserves_receipt(self):
        original = Path.read_text
        handlers = {s: signal.getsignal(s) for s in (signal.SIGINT, signal.SIGTERM)}

        def deny_stat(path, *args, **kwargs):
            if path.parent.parent == Path("/proc") and path.parent.name.isdigit():
                raise PermissionError("proc stat unavailable")
            return original(path, *args, **kwargs)

        with patch.object(baseline.owned_process.Path, "read_text", deny_stat):
            result = self.run_spec()
        self.assertFalse(result["success"])
        self.assertEqual(result["exit_code"], 0)
        self.assertIn("proc stat unavailable", result["inspection_error"])
        self.assertEqual({s: signal.getsignal(s) for s in handlers}, handlers)
        self.assertEqual(json.loads((self.folder / "run/result.json").read_text()), result)

    def test_receipt_write_failure_restores_handlers(self):
        original = baseline.write_json
        handlers = {s: signal.getsignal(s) for s in (signal.SIGINT, signal.SIGTERM)}

        def fail_receipt(path, value):
            if Path(path).name == "result.json":
                raise OSError("receipt storage unavailable")
            original(path, value)

        with patch.object(baseline, "write_json", fail_receipt):
            with self.assertRaisesRegex(OSError, "receipt storage unavailable"):
                self.run_spec()
        self.assertEqual({s: signal.getsignal(s) for s in handlers}, handlers)
        self.assertEqual((self.folder / "run/output/program").read_bytes(), b"ok")

    def test_unsupported_platform_rejected_before_launch(self):
        with patch.object(baseline.owned_process.sys, "platform", "darwin"):
            with self.assertRaisesRegex(ValueError, "requires Linux"):
                self.run_spec()
        self.assertFalse((self.folder / "run").exists())

    def test_cleanup_does_not_extend_elapsed_or_timeout(self):
        original = Path.glob
        self.spec["timeout_seconds"] = 0.2

        def slow_inspection(path, pattern):
            time.sleep(0.3)
            return original(path, pattern)

        with patch.object(baseline.owned_process.Path, "glob", slow_inspection):
            result = self.run_spec()
        self.assertTrue(result["success"], result)
        self.assertLess(result["elapsed_seconds"], 0.2)
        self.assertGreaterEqual(result["cleanup_seconds"], 0.3)
        self.assertFalse(result["timed_out"])

    def test_surviving_child_rejects_successful_parent(self):
        marker = self.folder / "escaped-child-work"
        child_code = f"import time; from pathlib import Path; time.sleep(0.8); Path({str(marker)!r}).touch()"
        self.spec["argv"][2] += f"; import subprocess,sys; subprocess.Popen([sys.executable, '-c', {child_code!r}])"
        result = self.run_spec()
        self.assertEqual(result["exit_code"], 0)
        self.assertFalse(result["success"])
        self.assertTrue(any("owned descendant survived" in e for e in result["errors"]))
        time.sleep(1)
        self.assertFalse(marker.exists(), "the surviving child was not terminated")

    def test_separate_group_child_is_cleaned_after_success_and_timeout(self):
        for mode in ("success", "timeout"):
            with self.subTest(mode=mode):
                marker = self.folder / (mode + "-escaped")
                ready = self.folder / (mode + "-ready")
                child = ("import os,time; from pathlib import Path; "
                         f"Path({str(ready)!r}).write_text(f'{{os.getpid()}} {{os.getpgrp()}} {{os.getsid(0)}}'); "
                         f"time.sleep(0.6); Path({str(marker)!r}).touch()")
                self.spec["argv"][2] = (
                    "import subprocess,sys,time; from pathlib import Path; Path('program').write_bytes(b'ok'); "
                    f"subprocess.Popen([sys.executable, '-c', {child!r}], process_group=0); "
                    f"deadline=time.monotonic()+1\nwhile not Path({str(ready)!r}).exists() and time.monotonic()<deadline: time.sleep(0.005)\n"
                    + ("time.sleep(1)" if mode == "timeout" else ""))
                self.spec["timeout_seconds"] = 0.2 if mode == "timeout" else 2
                result = self.run_spec(mode)
                # Even the broken implementation's child finishes on its own.
                time.sleep(0.7)
                pid, pgid, sid = map(int, ready.read_text().split())
                self.assertEqual(pid, pgid)
                self.assertNotEqual(pgid, sid)
                self.assertFalse(marker.exists(), "separate-group child escaped owned cleanup")
                self.assertFalse(result["success"], result)
                self.assertEqual(result["timed_out"], mode == "timeout")

    def test_run_directory_must_not_exist(self):
        for name in ("empty", "stale"):
            with self.subTest(name=name):
                directory = self.folder / name
                directory.mkdir()
                if name == "stale":
                    (directory / "program").write_text("old artifact")
                with self.assertRaises(FileExistsError):
                    self.run_spec(name)
                self.assertFalse((directory / "result.json").exists())

    def test_missing_empty_wrong_or_symlinked_output_fails(self):
        commands = {"missing": "pass", "empty": "from pathlib import Path; Path('program').touch()",
                    "wrong": "from pathlib import Path; Path('program').write_bytes(b'wrong')",
                    "symlink": "import os; os.symlink('/dev/null', 'program')"}
        for name, command in commands.items():
            with self.subTest(name=name):
                self.spec["argv"][2] = command
                result = self.run_spec(name)
                self.assertEqual(result["exit_code"], 0)
                self.assertFalse(result["success"])
                self.assertIn("postflight failed", result["errors"][0])

    def test_changed_tool_is_detected_after_execution(self):
        dependency = self.folder / "prebuilt-input"
        dependency.write_text("original")
        self.spec["tools"].append({"path": str(dependency), "sha256": baseline.file_digest(dependency)})
        self.spec["argv"][2] += f"; Path({str(dependency)!r}).write_text('changed')"
        result = self.run_spec()
        self.assertEqual(result["exit_code"], 0)
        self.assertFalse(result["success"])
        self.assertIn("tool identity", result["errors"][0])
        self.assertTrue((self.folder / "run/output/program").is_file())

    def test_invalid_timeouts_and_output_traversal_rejected(self):
        for index, value in enumerate((0, -1, True, float("inf"), float("nan"))):
            with self.subTest(timeout=value):
                self.spec["timeout_seconds"] = value
                # Permit nonfinite test input here; production receipts forbid it.
                spec_path = self.folder / f"invalid-{index}.json"
                spec_path.write_text(json.dumps(self.spec))
                with self.assertRaisesRegex(ValueError, "finite"):
                    baseline.measure(spec_path, self.folder / f"invalid-{index}")
        self.spec["timeout_seconds"] = 5
        self.spec["expected_outputs"] = [{"path": "../outside"}]
        with self.assertRaisesRegex(ValueError, "relative contained"):
            self.run_spec("traversal")

    def test_nonfinite_values_anywhere_in_spec_do_not_launch(self):
        marker = self.folder / "should-not-execute"
        self.spec["argv"][2] = f"from pathlib import Path; Path({str(marker)!r}).touch()"
        self.spec["unused"] = {"nested": ["NONFINITE"]}
        for index, token in enumerate(("NaN", "Infinity", "-Infinity", "1e999", "-1e999")):
            with self.subTest(token=token):
                spec_path = self.folder / f"nonfinite-{index}.json"
                spec_path.write_text(json.dumps(self.spec).replace('"NONFINITE"', token))
                run_dir = self.folder / f"nonfinite-{index}"
                with self.assertRaisesRegex(ValueError, "numbers must be finite"):
                    baseline.measure(spec_path, run_dir)
                self.assertFalse(run_dir.exists())
                self.assertFalse(marker.exists())

    def test_serialization_failure_does_not_create_partial_receipt(self):
        for index, value in enumerate(({"late": float("nan")}, {"late": float("inf")}, {"late": object()})):
            with self.subTest(value=value):
                path = self.folder / f"invalid-receipt-{index}.json"
                with self.assertRaises((ValueError, TypeError)):
                    baseline.write_json(path, value)
                self.assertFalse(path.exists())
        original = self.receipt.read_bytes()
        with self.assertRaises(ValueError):
            baseline.write_json(self.receipt, {"late": float("nan")})
        self.assertEqual(self.receipt.read_bytes(), original)

    def test_identity_receipts_cannot_be_overwritten(self):
        original = self.receipt.read_bytes()
        with self.assertRaises(FileExistsError):
            baseline.write_json(self.receipt, {})
        self.assertEqual(self.receipt.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
