"""Bounded Linux child ownership for the baseline and frozen parser probes."""

import ctypes
import math
import os
from pathlib import Path
import signal
import subprocess
import sys
import threading
import time

CLEANUP_LIMIT_SECONDS = 1.0


def require_linux():
    # WNOWAIT and readable procfs are part of the ownership proof, not optional
    # fallbacks. SIGCHLD auto-reaping would release the reserved process-group ID.
    if (sys.platform != "linux" or not hasattr(os, "WNOWAIT")
            or not hasattr(os, "waitid") or not hasattr(os, "pidfd_open")
            or not hasattr(signal, "pidfd_send_signal")):
        raise ValueError("owned process supervision requires Linux waitid(WNOWAIT), pidfds and procfs")
    if signal.getsignal(signal.SIGCHLD) != signal.SIG_DFL:
        raise ValueError("owned process supervision requires the default SIGCHLD disposition")
    if int(Path("/proc/self/stat").read_text().split(" ", 1)[0]) != os.getpid():
        raise ValueError("owned process supervision requires this process's Linux procfs")


def run(argv, cwd, env, stdout, stderr, limit):
    """Return execution facts; callers own exclusive logs, markers and receipts.

    Run on the main Python thread, with no other threads, children or child
    reaper. Temporary self-subreaper ownership captures orphaned descendants,
    including different process groups. ECHILD, not a procfs absence census,
    proves cleanup. Elapsed time ends at observed leader exit; cleanup has a
    separate one-second allowance, making the total budget limit + 1 second.
    No group signal is permitted after leader reap. A cleanup error or missing
    ECHILD is not successful ownership closure.
    """
    require_linux()
    if isinstance(limit, bool) or not math.isfinite(limit) or limit <= 0:
        raise ValueError("process deadline must be finite and positive")
    if threading.current_thread() is not threading.main_thread() or len(list(Path("/proc/self/task").iterdir())) != 1:
        raise ValueError("owned process supervision requires a single main thread and sole child reaper")
    try:
        os.waitid(os.P_ALL, 0, os.WEXITED | os.WNOHANG | os.WNOWAIT)
    except ChildProcessError:
        pass
    else:
        # WNOWAIT checks without reaping or signalling somebody else's child.
        raise ValueError("owned process supervision requires no pre-existing children")
    result = {"exit_code": None, "status": "not_launched", "timed_out": False,
              "elapsed_seconds": None, "cleanup_seconds": None,
              "surviving_children": False, "cleanup_errors": [],
              "cleanup_echild": False, "subreaper_restored": False,
              "cleanup_limit_seconds": CLEANUP_LIMIT_SECONDS,
              "overall_limit_seconds": limit + CLEANUP_LIMIT_SECONDS}
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
    libc = ctypes.CDLL(None, use_errno=True)
    libc.prctl.argtypes = [ctypes.c_int, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong]
    libc.prctl.restype = ctypes.c_int
    previous_subreaper = None

    def subreaper(operation, value):
        if libc.prctl(operation, value, 0, 0, 0) != 0:
            raise OSError(ctypes.get_errno(), "self subreaper capability unavailable")

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
        # Python's cached disposition does not reveal native SA_NOCLDWAIT or
        # native SIG_IGN changes. Establish kernel no-auto-reap before launch.
        signal.signal(signal.SIGCHLD, signal.SIG_DFL)
        for signum in (signal.SIGINT, signal.SIGTERM):
            handlers[signum] = signal.signal(signum, interrupted)
        previous = ctypes.c_int()
        subreaper(37, ctypes.addressof(previous))  # PR_GET_CHILD_SUBREAPER.
        previous_subreaper = previous.value
        subreaper(36, 1)  # PR_SET_CHILD_SUBREAPER, this process only.
        current = ctypes.c_int()
        subreaper(37, ctypes.addressof(current))
        if current.value != 1:
            raise RuntimeError("self subreaper was not established")
        # Exercise capability on this owner before creating any children. A
        # denial is terminal; there is no numeric-PID or privilege fallback.
        fd = os.pidfd_open(os.getpid())
        try:
            signal.pidfd_send_signal(fd, 0)
        finally:
            os.close(fd)
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
        # Polling also bounds a failed kill: blocking waitid could wait forever.
        while True:
            exited = os.waitid(os.P_PID, process.pid, os.WEXITED | os.WNOWAIT | os.WNOHANG)
            if exited is not None:
                end = time.perf_counter_ns()
                result["status"] = "passed" if exited.si_code == os.CLD_EXITED and exited.si_status == 0 else "failed"
                break
            if expired.is_set() or interruption is not None:
                break
            time.sleep(0.005)
    except BaseException as error:
        result["status"] = "interrupted" if isinstance(error, (InterruptedError, KeyboardInterrupt)) else "error"
        result["exception"] = {"type": type(error).__name__, "message": str(error)}
    finally:
        cleanup_start = time.perf_counter_ns()
        cleanup_deadline = time.monotonic() + CLEANUP_LIMIT_SECONDS
        descriptors = {}
        signalled = set()
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
                        timer.join(timeout=max(0, cleanup_deadline - time.monotonic()))
                        if timer.is_alive():
                            raise TimeoutError("timer did not stop before cleanup deadline")
                except BaseException as error:
                    result["cleanup_errors"].append(f"timer shutdown: {type(error).__name__}: {error}")
            if process is not None:
                # Last numeric group operation. The sole reaper has retained
                # the leader via WNOWAIT. From here on, only owned pidfds signal.
                kill_group()
                inspect_children = True
                while True:
                    if inspect_children:
                        try:
                            for stat in Path("/proc").glob("[0-9]*/stat"):
                                if time.monotonic() >= cleanup_deadline:
                                    raise TimeoutError("child inspection exceeded cleanup deadline")
                                try:
                                    fields = stat.read_text().rsplit(")", 1)[1].split()
                                except (FileNotFoundError, ProcessLookupError):
                                    continue
                                if int(fields[1]) != os.getpid():
                                    continue
                                pid = int(stat.parent.name)
                                if pid != process.pid:
                                    result["surviving_children"] = True
                                if pid not in descriptors:
                                    # Kernel child ancestry establishes ownership.
                                    # No other reaper exists and SIGCHLD is default:
                                    # this child's PID stays reserved until waitpid.
                                    fd = os.pidfd_open(pid)
                                    try:
                                        current = stat.read_text().rsplit(")", 1)[1].split()
                                        if int(current[1]) != os.getpid() or current[19] != fields[19]:
                                            raise RuntimeError("owned child identity changed before pidfd binding")
                                    except BaseException:
                                        os.close(fd)
                                        raise
                                    descriptors[pid] = fd
                        except BaseException as error:
                            result["inspection_error"] = f"{type(error).__name__}: {error}"
                            inspect_children = False  # No retry after denied capability.
                    for pid, fd in descriptors.items():
                        if time.monotonic() >= cleanup_deadline:
                            result["termination_error"] = "owned signalling exceeded cleanup deadline"
                            break
                        if pid in signalled:
                            continue
                        signalled.add(pid)
                        try:
                            signal.pidfd_send_signal(fd, signal.SIGKILL)
                        except ProcessLookupError:
                            pass  # Still reap; disappearance alone proves nothing.
                        except BaseException as error:
                            result["cleanup_errors"].append(f"pidfd termination: {type(error).__name__}: {error}")
                    try:
                        while True:
                            if time.monotonic() >= cleanup_deadline:
                                raise TimeoutError("owned cleanup did not reach ECHILD before cleanup deadline")
                            pid, status = os.waitpid(-1, os.WNOHANG)
                            if pid == 0:
                                break
                            fd = descriptors.pop(pid, None)
                            if fd is not None:
                                os.close(fd)
                            signalled.discard(pid)
                            if pid == process.pid:
                                process.returncode = result["exit_code"] = os.waitstatus_to_exitcode(status)
                                if end is None:
                                    end = time.perf_counter_ns()
                            else:
                                result["surviving_children"] = True
                    except ChildProcessError:
                        result["cleanup_echild"] = True
                        break
                    except BaseException as error:
                        result["termination_error"] = f"{type(error).__name__}: {error}"
                        break
                    if time.monotonic() >= cleanup_deadline:
                        result["termination_error"] = "owned cleanup did not reach ECHILD before cleanup deadline"
                        break
                    time.sleep(min(0.005, max(0, cleanup_deadline - time.monotonic())))
            else:
                result["cleanup_echild"] = True  # Preflight established no children; no launch occurred.
        finally:
            for fd in descriptors.values():
                os.close(fd)
            if previous_subreaper is not None:
                try:
                    subreaper(36, previous_subreaper)
                    restored = ctypes.c_int()
                    subreaper(37, ctypes.addressof(restored))
                    result["subreaper_restored"] = restored.value == previous_subreaper
                    if not result["subreaper_restored"]:
                        raise RuntimeError("previous self subreaper state was not restored")
                except BaseException as error:
                    result["cleanup_errors"].append(f"subreaper restoration: {type(error).__name__}: {error}")
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
