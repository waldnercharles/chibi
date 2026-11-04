"""One bounded Linux process group for the baseline and frozen parser probes."""

import math
import os
from pathlib import Path
import signal
import subprocess
import sys
import threading
import time


def require_linux():
    # WNOWAIT and readable procfs are part of the ownership proof, not optional
    # fallbacks. SIGCHLD auto-reaping would release the reserved process-group ID.
    if (sys.platform != "linux" or not hasattr(os, "WNOWAIT")
            or not hasattr(os, "waitid")):
        raise ValueError("owned process supervision requires Linux waitid(WNOWAIT) and procfs")
    if signal.getsignal(signal.SIGCHLD) != signal.SIG_DFL:
        raise ValueError("owned process supervision requires the default SIGCHLD disposition")
    if int(Path("/proc/self/stat").read_text().split(" ", 1)[0]) != os.getpid():
        raise ValueError("owned process supervision requires this process's Linux procfs")


def run(argv, cwd, env, stdout, stderr, limit):
    """Return execution facts; callers own exclusive logs, markers and receipts.

    Run on the main Python thread, with no other child reaper. The command must
    not detach sessions. Elapsed time ends when leader exit is observed, before
    ordinary cleanup; cleanup_seconds separately includes timer stop, group
    inspection/termination and reap. No group signal is permitted after reap.
    """
    require_linux()
    if isinstance(limit, bool) or not math.isfinite(limit) or limit <= 0:
        raise ValueError("process deadline must be finite and positive")
    result = {"exit_code": None, "status": "not_launched", "timed_out": False,
              "elapsed_seconds": None, "cleanup_seconds": None,
              "surviving_children": False, "cleanup_errors": []}
    process = None
    timer = None
    handlers = {}
    interruption = None
    last_interruption = None
    expired = threading.Event()
    # The gate also makes a timer shutdown failure safe: a late callback cannot
    # signal after cleanup has disabled it, even if joining the thread failed.
    gate = threading.RLock()
    callbacks_active = True
    start = time.perf_counter_ns()
    end = None

    def kill_group():
        if process is not None:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            except BaseException as error:
                result["cleanup_errors"].append(f"termination: {type(error).__name__}: {error}")

    def interrupted(signum, _frame):
        nonlocal interruption, last_interruption
        with gate:
            if callbacks_active:
                # Never raise across Popen's return/assignment ownership gap.
                if interruption is None:
                    interruption = signum
                last_interruption = signum
                kill_group()

    def deadline():
        with gate:
            if callbacks_active:
                expired.set()
                kill_group()

    try:
        for signum in (signal.SIGINT, signal.SIGTERM):
            handlers[signum] = signal.signal(signum, interrupted)
        start = time.perf_counter_ns()
        if interruption is not None:
            return result  # The finally block records the signal; nothing launches.
        process = subprocess.Popen(argv, cwd=cwd, env=env, stdin=subprocess.DEVNULL,
                                   stdout=stdout, stderr=stderr, start_new_session=True)
        result["pid"] = process.pid
        if interruption is not None:
            kill_group()
        timer = threading.Timer(max(0, limit - (time.perf_counter_ns() - start) / 1e9), deadline)
        timer.start()
        # Reserve the leader PID/PGID until every possible group signal ends.
        exited = os.waitid(os.P_PID, process.pid, os.WEXITED | os.WNOWAIT)
        end = time.perf_counter_ns()
        result["status"] = "passed" if exited.si_code == os.CLD_EXITED and exited.si_status == 0 else "failed"
    except BaseException as error:
        result["status"] = "interrupted" if isinstance(error, (InterruptedError, KeyboardInterrupt)) else "error"
        result["exception"] = {"type": type(error).__name__, "message": str(error)}
    finally:
        cleanup_start = time.perf_counter_ns()
        try:
            with gate:
                callbacks_active = False
            for signum in handlers:
                try:
                    signal.signal(signum, signal.SIG_IGN)
                except BaseException as error:
                    result["cleanup_errors"].append(f"signal disable: {type(error).__name__}: {error}")
            if timer is not None:
                try:
                    timer.cancel()
                    if timer.ident is not None:
                        timer.join()
                except BaseException as error:
                    result["cleanup_errors"].append(f"timer shutdown: {type(error).__name__}: {error}")
            if process is not None:
                try:
                    # A zombie leader makes killpg(..., 0) succeed. Distinguish
                    # it from live same-group children without releasing its ID.
                    for stat in Path("/proc").glob("[0-9]*/stat"):
                        try:
                            fields = stat.read_text().rsplit(")", 1)[1].split()
                            if end is not None and int(fields[2]) == process.pid and fields[0] != "Z" and int(stat.parent.name) != process.pid:
                                result["surviving_children"] = True
                        except (FileNotFoundError, ProcessLookupError):
                            pass
                except BaseException as error:
                    result["inspection_error"] = f"{type(error).__name__}: {error}"
                finally:
                    kill_group()
                    try:
                        result["exit_code"] = process.wait()
                        if end is None:
                            end = time.perf_counter_ns()
                    except BaseException as error:
                        result["termination_error"] = f"{type(error).__name__}: {error}"
        finally:
            # Receipt I/O is outside this scope, so it cannot strand handlers.
            for signum, handler in handlers.items():
                signal.signal(signum, handler)
        finish = time.perf_counter_ns()
        result["elapsed_seconds"] = ((end if end is not None else cleanup_start) - start) / 1e9
        result["cleanup_seconds"] = (finish - cleanup_start) / 1e9
        if result["surviving_children"] and result["status"] == "passed":
            result["status"] = "surviving_children"
        if result["cleanup_errors"] or "inspection_error" in result or "termination_error" in result:
            result["status"] = "cleanup_error"
        result["timed_out"] = expired.is_set() or result["elapsed_seconds"] > limit
        if result["timed_out"]:
            result["status"] = "timeout"
        if interruption is not None:
            result["status"] = "interrupted"
            result["interruption"] = {"signal": interruption, "name": signal.Signals(interruption).name}
            # Preserve each existing receipt contract: baseline records the first
            # signal, while the parser controller records the most recent one.
            result["last_signal"] = signal.Signals(last_interruption).name
    return result
