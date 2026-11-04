#!/usr/bin/env python3
"""Controller/preparation tests only. Never starts Chibi or LLVM."""

import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

import prepare
import run


class ProbeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)

    def command(self, code, limit=2):
        return run.run_command([sys.executable, "-c", code], self.folder,
                               {"PATH": "/usr/bin:/bin"}, "probe", limit)

    def test_frozen_parser_and_corpus(self):
        output = self.folder / "frozen"
        fence = prepare.prepare(output)
        self.assertEqual(len(fence["parser_sources"]), 8)
        self.assertEqual(len(fence["corpus_sources"]), 15)
        run.check_sources(output, fence)
        generated = (output / "ParserHarness.chibi").read_text()
        for row in fence["corpus_sources"]:
            source = (prepare.HERE.parent / row["path"]).read_bytes().decode("utf-8")
            self.assertIn(f"CheckParsed({prepare.literal(source)})", generated)
        original = (output / "source-fence.json").read_bytes()
        with self.assertRaises(FileExistsError):
            prepare.prepare(output)
        self.assertEqual((output / "source-fence.json").read_bytes(), original)
        (output / "compiler/source_parser/Parser.chibi").write_text("changed")
        with self.assertRaisesRegex(ValueError, "source changed"):
            run.check_sources(output, fence)

    def test_frozen_shared_operation_is_copied_fenced_and_used(self):
        output = self.folder / "frozen"
        fence = prepare.prepare(output)
        copied = output / "controller/owned_process.py"
        self.assertEqual(prepare.identity(copied), prepare.identity(prepare.HERE.parent / "owned_process.py"))
        code = (f"import sys; sys.path.insert(0, {str(output / 'controller')!r}); "
                "import run; from pathlib import Path; "
                f"assert Path(run.owned_process.__file__).resolve() == Path({str(copied)!r}); "
                f"result = run.run_command([sys.executable, '-c', 'pass'], Path({str(output)!r}), "
                "{'PATH': '/usr/bin:/bin'}, 'python-control', 2); assert result['status'] == 'passed', result")
        subprocess.run([sys.executable, "-I", "-c", code], check=True, timeout=5)
        run.check_sources(output, fence)
        copied.write_text("changed")
        with self.assertRaisesRegex(ValueError, "implementation changed"):
            run.check_sources(output, fence)

    def test_fence_cannot_drop_shared_operation_row(self):
        output = self.folder / "frozen"
        fence = prepare.prepare(output)
        fence["implementation"] = [row for row in fence["implementation"] if row["path"] != "controller/owned_process.py"]
        with self.assertRaisesRegex(ValueError, "implementation fence membership"):
            run.check_sources(output, fence)

    def test_source_literal_roundtrip(self):
        for source in ("quoted \"x\"\n", "backslash \\", "café\r\n\t"):
            self.assertEqual(json.loads(prepare.literal(source)), source)
        with self.assertRaises(ValueError):
            prepare.literal("nul\x00")

    def test_streaming_fingerprint_matches_bytes(self):
        for size in (0, 1, 262143, 262144, 262145, 524301):
            data = bytes(range(256)) * (size // 256) + bytes(range(size % 256))
            path = self.folder / "fingerprint"
            path.write_bytes(data)
            self.assertEqual(prepare.identity(path), prepare.byte_identity(data))

    def test_fence_cannot_drop_source_row(self):
        output = self.folder / "frozen"
        fence = prepare.prepare(output)
        fence["parser_sources"].pop()
        with self.assertRaisesRegex(ValueError, "membership"):
            run.check_sources(output, fence)

    def test_alias_retarget_detected_even_with_identical_bytes(self):
        output = self.folder / "frozen"
        fence = prepare.prepare(output)
        first, second, alias = [self.folder / name for name in ("first", "second", "alias")]
        first.write_bytes(b"same")
        second.write_bytes(b"same")
        alias.symlink_to(first)
        row = {"path": str(alias), "resolved": str(first), **prepare.identity(alias)}
        alias.unlink()
        alias.symlink_to(second)
        with self.assertRaisesRegex(ValueError, "input changed"):
            run.check_inputs(output, fence, [row])

    def test_success_failure_and_no_overwrite(self):
        result = self.command("print('preserved'); raise SystemExit(7)")
        self.assertEqual((result["status"], result["exit_code"]), ("failed", 7))
        self.assertEqual((self.folder / "probe.stdout").read_text(), "preserved\n")
        self.assertEqual(json.loads((self.folder / "probe-result.json").read_text()), result)
        with self.assertRaises(FileExistsError):
            self.command("raise SystemExit(0)")

    def test_timeout(self):
        result = self.command("import time; time.sleep(10)", 0.1)
        self.assertEqual(result["status"], "timeout")
        self.assertEqual(result["exit_code"], -signal.SIGKILL)

    def test_interrupt_during_popen_assignment(self):
        original = subprocess.Popen
        handlers = {s: signal.getsignal(s) for s in (signal.SIGINT, signal.SIGTERM)}

        def launch_then_interrupt(*args, **kwargs):
            child = original(*args, **kwargs)
            # Real child exists but run_command has not received its handle yet.
            os.kill(os.getpid(), signal.SIGINT)
            os.kill(os.getpid(), signal.SIGTERM)
            return child

        with patch.object(run.owned_process.subprocess, "Popen", launch_then_interrupt):
            result = self.command("import time; time.sleep(10)")
        self.assertEqual(result["status"], "interrupted")
        self.assertEqual(result["signal"], "SIGTERM")
        self.assertEqual(result["exit_code"], -signal.SIGKILL)
        self.assertEqual({s: signal.getsignal(s) for s in handlers}, handlers)

    def test_interrupt_during_handler_setup_does_not_launch(self):
        original = signal.signal
        handlers = {s: signal.getsignal(s) for s in (signal.SIGINT, signal.SIGTERM)}

        def install_then_interrupt(signum, handler):
            previous = original(signum, handler)
            if signum == signal.SIGTERM and getattr(handler, "__name__", "") == "interrupted":
                os.kill(os.getpid(), signal.SIGINT)
            return previous

        with patch.object(run.owned_process.signal, "signal", install_then_interrupt):
            with patch.object(run.owned_process.subprocess, "Popen") as launch:
                result = self.command("pass")
        launch.assert_not_called()
        self.assertEqual(result["status"], "interrupted")
        self.assertEqual(result["signal"], "SIGINT")
        self.assertIsNone(result["exit_code"])
        self.assertNotIn("pid", result)
        self.assertEqual({s: signal.getsignal(s) for s in handlers}, handlers)

    def test_timer_start_failure_kills_real_child(self):
        with patch.object(run.owned_process.threading.Timer, "start", side_effect=RuntimeError("timer unavailable")):
            result = self.command("import time; time.sleep(10)")
        self.assertEqual(result["status"], "error")
        self.assertEqual(result["exit_code"], -signal.SIGKILL)

    def test_interrupt_during_start_clock_does_not_launch(self):
        original = run.owned_process.time.perf_counter_ns
        calls = 0

        def start_then_interrupt():
            nonlocal calls
            calls += 1
            if calls == 2:
                os.kill(os.getpid(), signal.SIGINT)
            return original()

        with patch.object(run.owned_process.time, "perf_counter_ns", start_then_interrupt):
            with patch.object(run.owned_process.subprocess, "Popen") as launch:
                result = self.command("pass")
        launch.assert_not_called()
        self.assertEqual(result["status"], "interrupted")
        self.assertEqual(result["signal"], "SIGINT")
        self.assertIsNone(result["exit_code"])
        self.assertNotIn("pid", result)

    def test_surviving_child_is_killed(self):
        marker = self.folder / "escaped"
        child = f"import time; from pathlib import Path; time.sleep(0.3); Path({str(marker)!r}).touch()"
        result = self.command(f"import subprocess, sys; subprocess.Popen([sys.executable, '-c', {child!r}])")
        self.assertEqual(result["status"], "surviving_children")
        time.sleep(0.4)
        self.assertFalse(marker.exists())

    def test_receipt_failure_restores_handlers(self):
        handlers = {s: signal.getsignal(s) for s in (signal.SIGINT, signal.SIGTERM)}
        (self.folder / "probe-result.json").write_text("preserve")
        with self.assertRaises(FileExistsError):
            self.command("pass")
        self.assertEqual({s: signal.getsignal(s) for s in handlers}, handlers)
        self.assertEqual((self.folder / "probe-result.json").read_text(), "preserve")

    def test_proc_inspection_failure_still_cleans_child_and_handlers(self):
        handlers = {s: signal.getsignal(s) for s in (signal.SIGINT, signal.SIGTERM)}
        with patch.object(run.owned_process.Path, "glob", side_effect=PermissionError("procfs unavailable")):
            result = self.command("pass")
        self.assertEqual(result["status"], "cleanup_error")
        self.assertEqual(result["exit_code"], 0)
        self.assertEqual({s: signal.getsignal(s) for s in handlers}, handlers)

    def test_group_signals_precede_reap(self):
        original = os.killpg

        def signal_unreaped(group, signum):
            os.waitid(os.P_PID, group, os.WEXITED | os.WNOWAIT | os.WNOHANG)
            return original(group, signum)

        with patch.object(run.owned_process.os, "killpg", signal_unreaped):
            result = self.command("pass")
        self.assertEqual(result["status"], "passed")

    def test_late_timer_callback_cannot_signal_reaped_group(self):
        original = run.owned_process.threading.Timer
        timers = []

        def make_timer(*args, **kwargs):
            timer = original(*args, **kwargs)
            timers.append(timer)
            return timer

        with patch.object(run.owned_process.threading, "Timer", make_timer):
            with patch.object(original, "join", side_effect=RuntimeError("join failed")):
                result = self.command("pass")
        self.assertEqual(result["status"], "cleanup_error")
        self.assertEqual(result["exit_code"], 0)
        with patch.object(run.owned_process.os, "killpg", side_effect=AssertionError("signal after reap")) as kill:
            timers[0].function()
            kill.assert_not_called()
        timers[0].join()

    def test_unsupported_platform_does_not_launch(self):
        with patch.object(run.owned_process.sys, "platform", "darwin"):
            with patch.object(run.owned_process.subprocess, "Popen") as launch:
                result = self.command("pass")
        self.assertEqual(result["status"], "error")
        self.assertIn("requires Linux", result["error"])
        launch.assert_not_called()

    def test_launch_failure_preserves_receipt_and_handlers(self):
        handlers = {s: signal.getsignal(s) for s in (signal.SIGINT, signal.SIGTERM)}
        with patch.object(run.owned_process.subprocess, "Popen", side_effect=OSError("launch unavailable")):
            result = self.command("pass")
        self.assertEqual(result["status"], "error")
        self.assertIsNone(result["exit_code"])
        self.assertEqual({s: signal.getsignal(s) for s in handlers}, handlers)
        self.assertEqual(json.loads((self.folder / "probe-result.json").read_text()), result)

    def test_serialization_failure_creates_no_partial_receipt(self):
        for value in (float("nan"), object()):
            path = self.folder / "invalid.json"
            with self.assertRaises((ValueError, TypeError)):
                run.write_new(path, {"invalid": value})
            self.assertFalse(path.exists())


if __name__ == "__main__":
    unittest.main()
