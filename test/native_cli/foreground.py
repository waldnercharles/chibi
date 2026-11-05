#!/usr/bin/env python3
"""Exercise native CLI process ownership and job control through real children."""
import errno
import json
import os
from pathlib import Path
import pty
import select
import signal
import subprocess
import sys
import tempfile
import termios
import time


compiler, llvm = map(os.path.realpath, sys.argv[1:3])
launcher = os.path.realpath(sys.argv[3]) if len(sys.argv) > 3 else compiler
root = Path(__file__).resolve().parents[2]
core = str(root / "packages/core")
environment = dict(os.environ, CHIBI_CORE=core, CHIBI_LLVM_BIN=llvm,
                   CHIBI_NATIVE_HOST_SOURCE=os.environ.get("CHIBI_NATIVE_HOST_SOURCE",
                       str(root / "apps/host/Host.chibi")))


def cli(source, watching=False):
    return [launcher, "run", *(["--watch"] if watching else []),
            "--core", core, "--llvm", llvm, str(source)]


def read_until(descriptor, expected, timeout=120):
    data = b""
    deadline = time.monotonic() + timeout
    while expected not in data and time.monotonic() < deadline:
        if not select.select([descriptor], [], [], 0.05)[0]:
            continue
        try:
            part = os.read(descriptor, 4096)
        except OSError as error:
            if error.errno == errno.EIO:
                break
            raise
        if not part:
            break
        data += part
    assert expected in data, (expected, data.decode(errors="replace"))
    return data


def stopped(pid):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        seen, status = os.waitpid(pid, os.WNOHANG | os.WUNTRACED)
        if seen:
            return status
        time.sleep(0.01)
    raise AssertionError("the CLI did not mirror its stopped child")


def foreground(master, parent):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        group = os.tcgetpgrp(master)
        if group > 0 and group != parent:
            return group
        time.sleep(0.01)
    raise AssertionError("the child did not acquire the foreground terminal")


def dead(pid):
    observed = subprocess.run(["/bin/ps", "-o", "stat=", "-p", str(pid)],
                              capture_output=True, text=True, check=False)
    return observed.returncode == 1 or observed.stdout.strip().startswith("Z")


def live_group_members(group):
    observed = subprocess.run(["/bin/ps", "-A", "-o", "pid=,pgid=,stat="],
                              capture_output=True, text=True, check=True)
    return [int(pid) for pid, child_group, state in
            (line.split() for line in observed.stdout.splitlines())
            if int(child_group) == group and not state.startswith("Z")]


def process_state(stat, status):
    # Linux stat's command can contain spaces and ')'; only numeric identity
    # fields after its final ')' are relevant, not the command itself.
    prefix, separator, tail = stat.rpartition(") ")
    fields = tail.split()
    if not separator or len(fields) < 20 or len(fields[0]) != 1:
        raise ValueError("invalid proc stat")
    identity = [int(prefix.split(" ", 1)[0]), *map(int, fields[1:4]), int(fields[19])]
    if identity[0] <= 0 or any(value < 0 for value in identity[1:]):
        raise ValueError("invalid proc identity")
    result = dict(zip(("pid", "ppid", "pgid", "sid", "start_ticks"), identity))
    result["state"] = fields[0]
    masks = ("SigPnd", "ShdPnd", "SigBlk", "SigIgn", "SigCgt")
    for line in status.splitlines():
        key, separator, value = line.partition(":")
        if separator and key in masks:
            value = value.strip()
            if key in result or not 1 <= len(value) <= 32 or any(
                    byte not in "0123456789abcdefABCDEF" for byte in value):
                raise ValueError("invalid proc signal mask")
            result[key] = value
    if any(key not in result for key in masks):
        raise ValueError("missing proc signal mask")
    return result


def terminal_failure_state(parent, group):
    # Failure-only observations, never signals/waits. The time budget is
    # cooperative: /proc reads and the existing stderr sink are not interruptible.
    report = {"terminal_failure": {"parent": parent, "group": group}, "processes": [], "errors": []}
    if sys.platform != "linux":
        report["errors"].append("proc diagnostics require Linux")
        return report
    deadline = time.monotonic() + 0.25
    remaining = 131072

    def read(path):
        nonlocal remaining
        if time.monotonic() >= deadline:
            raise ValueError("diagnostic time budget exhausted")
        if remaining < 4097:
            raise ValueError("diagnostic byte budget exhausted")
        with open(path, "rb", buffering=0) as stream:
            data = stream.read(4097)
        remaining -= len(data)
        if len(data) > 4096:
            raise ValueError("proc file exceeds 4096 bytes")
        if time.monotonic() >= deadline:
            raise ValueError("diagnostic time budget exhausted")
        return data.decode("ascii")

    pending = [parent] + ([group] if group > 0 and group != parent else [])
    seen = set(pending)
    for pid in pending:
        try:
            base = f"/proc/{pid}"
            first = read(base + "/stat")
            state = process_state(first, read(base + "/status"))
            if state["pid"] != pid or state["sid"] != parent:
                raise ValueError("process identity/session mismatch")
            try:
                channel = read(base + "/wchan").strip()
                if len(channel) > 256:
                    raise ValueError("proc wait channel exceeds 256 characters")
                state["wchan"] = channel
            except (OSError, ValueError) as error:
                report["errors"].append({"pid": pid, "field": "wchan",
                                         "error": type(error).__name__, "detail": str(error)[:160]})
            # A second identity read detects PID reuse during this observation.
            after = process_state(read(base + "/stat"), read(base + "/status"))
            if any(after[key] != state[key] for key in ("pid", "sid", "start_ticks")):
                raise ValueError("process identity changed during observation")
            state["state_after"] = after["state"]
            report["processes"].append(state)
            # Some Linux environments omit task/children. Keep the verified
            # row above and still sample the other independently known PID.
            children = read(base + f"/task/{pid}/children").split()
            if len(children) > 32:
                raise ValueError("diagnostic child count exceeds 32")
            for child in children:
                number = int(child)
                if number <= 0:
                    raise ValueError("invalid child pid")
                if number not in seen:
                    if len(seen) >= 32:
                        raise ValueError("diagnostic process count exceeds 32")
                    seen.add(number)
                    pending.append(number)
        except (OSError, ValueError) as error:
            report["errors"].append({"pid": pid, "error": type(error).__name__,
                                     "detail": str(error)[:160]})
            if time.monotonic() >= deadline or remaining < 4097:
                break
    return report


def kill_owned(session):
    # Both launch paths create a fresh session. Find its verified groups even
    # when readiness failed before the child printed its PID, or the CLI was
    # already reaped before an orphan assertion failed.
    observed = subprocess.run(["/bin/ps", "-A", "-o", "pid="],
                              capture_output=True, text=True, check=True)
    groups = set()
    for text in observed.stdout.split():
        try:
            member = int(text)
            if os.getsid(member) == session:
                groups.add(os.getpgid(member))
        except ProcessLookupError:
            pass
    for group in groups:
        try:
            os.killpg(group, signal.SIGKILL)
        except (ProcessLookupError, PermissionError):
            pass


with tempfile.TemporaryDirectory(prefix="chibi-native-foreground-") as temporary:
    work = Path(temporary)
    child = work / "tty child"
    triple = "aarch64-apple-darwin" if sys.platform == "darwin" else "x86_64-unknown-linux-gnu"
    scope_probe = work / "foreground scope"
    subprocess.run([compiler, "--compiler", str(root / "tools/compiler"), "--core", core,
                    "--source", str(root / "test/native_cli/fixtures/foreground_scope.chibi"),
                    "--entry", "Main", "--output", str(scope_probe), "--llvm", llvm,
                    "--target", triple, "--level", "2", "--jobs", "1"], check=True)

    def inherited_scope_signals():
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        signal.signal(signal.SIGTERM, signal.SIG_DFL)
        signal.signal(signal.SIGHUP, signal.SIG_DFL)

    for tail, expected in [(None, 0), ("interrupt", 0),
                           ("terminate", -signal.SIGTERM), ("hangup", -signal.SIGHUP)]:
        command = [str(scope_probe)] + ([tail] if tail else [])
        observed = subprocess.run(command, preexec_fn=inherited_scope_signals, check=False)
        assert observed.returncode == expected, (tail, expected, observed.returncode)
    subprocess.run([compiler, "--compiler", str(root / "tools/compiler"), "--core", core,
                    "--source", str(root / "test/native_cli/fixtures/tty_child.chibi"),
                    "--entry", "Main", "--output", str(child), "--llvm", llvm,
                    "--target", triple], env=environment, check=True)

    scripts = [
        'printf "CHILD_READY %s 0 READY_END\\n" "$$"; exec /bin/sleep 30',
        'trap "" INT TERM; /bin/sh -c \'trap "" INT TERM; while :; do /bin/sleep 1; done\' & '
        'descendant=$!; printf "CHILD_READY %s %s READY_END\\n" "$$" "$descendant"; wait',
    ]
    for index, script in enumerate(scripts):
        source = work / f"Signals{index}.chibi"
        source.write_text('package signals\nimport os "core:os"\n'
                          'Main :: () -> int { return os.Run(["/bin/sh", "-c", '
                          + json.dumps(script) + ']); }\n')
        for number in (signal.SIGINT, signal.SIGTERM):
            parent = subprocess.Popen(cli(source), env=environment, stdout=subprocess.PIPE,
                                      stderr=subprocess.STDOUT, start_new_session=True)
            passed = False
            descendants = []
            try:
                output = read_until(parent.stdout.fileno(), b"READY_END")
                marker = output[output.index(b"CHILD_READY "):].splitlines()[0].split()
                descendants = [int(marker[1]), int(marker[2])]
                group = os.getpgid(descendants[0])
                assert group != parent.pid and os.getsid(descendants[0]) == parent.pid
                os.kill(parent.pid, number)
                status = parent.wait(timeout=5)
                assert status == 128 + number, status
                assert all(pid == 0 or dead(pid) for pid in descendants), descendants
                assert not live_group_members(group), "the CLI left a running descendant"
                assert not live_group_members(parent.pid), "the CLI group did not retire"
                passed = True
            finally:
                if not passed:
                    kill_owned(parent.pid)
                    parent.wait(timeout=5)
    print("native foreground: parent-only INT/TERM and ignored-signal descendants passed", flush=True)

    for index in range(6):
        watching = index in (1, 3)
        suspend = index in (2, 3, 4)
        redirected = index == 4
        arguments = [str(child), *(["tty"] if redirected else [])] if suspend else ["/bin/cat"]
        before = 'prior := os.Run(["/usr/bin/true"]); ' if index == 5 else ""
        source = work / f"Terminal{index}.chibi"
        source.write_text('package terminal\nimport os "core:os"\nMain :: () -> int { '
                          + before + 'os.WriteOutput("TTY_READY\\n"); return os.Run('
                          + json.dumps(arguments) + '); }\n')
        print(f"native foreground: terminal case {index}", flush=True)
        parent, master = pty.fork()
        if parent == 0:
            if redirected:
                null = os.open("/dev/null", os.O_RDONLY)
                os.dup2(null, 0)
                os.close(null)
            os.execve(launcher, cli(source, watching), environment)
        group = 0
        finished = False
        passed = False
        try:
            settings = termios.tcgetattr(master)
            settings[3] &= ~termios.ECHO
            termios.tcsetattr(master, termios.TCSANOW, settings)
            read_until(master, b"TTY_READY")
            group = foreground(master, parent)
            os.write(master, b"foreground-before-stop\n")
            read_until(master, b"foreground-before-stop", 5)
            if suspend:
                os.write(master, b"\x1a")
                stop_status = stopped(parent)
                if not os.WIFSTOPPED(stop_status):
                    finished = True
                assert os.WIFSTOPPED(stop_status), stop_status
                assert os.tcgetpgrp(master) == parent, "the stopped CLI did not reclaim the terminal"
                os.killpg(parent, signal.SIGCONT)
            os.write(master, b"foreground-input\n")
            read_until(master, b"foreground-input", 5)
            assert os.tcgetpgrp(master) == group, "the resumed child did not regain the terminal"
            os.write(master, b"\x03")
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                seen, status = os.waitpid(parent, os.WNOHANG)
                if seen:
                    finished = True
                    break
                time.sleep(0.01)
            if not finished:
                try:
                    diagnostic = json.dumps(terminal_failure_state(parent, group), sort_keys=True)
                    if len(diagnostic) > 32768:
                        diagnostic = '{"terminal_failure": "diagnostic output exceeds 32768 bytes"}'
                    print(diagnostic, file=sys.stderr, flush=True)
                except Exception as error:
                    try:
                        print("terminal failure diagnostic error: " + type(error).__name__, file=sys.stderr)
                    except Exception:
                        pass  # A broken diagnostic sink must not replace the assertion.
            assert finished, "the CLI did not finish after its terminal child"
            assert os.WIFEXITED(status), status
            assert os.WEXITSTATUS(status) == (0 if watching else 130), status
            assert not live_group_members(group), "the CLI left its terminal child running"
            assert not live_group_members(parent), "the CLI group did not retire"
            passed = True
        finally:
            if not passed:
                kill_owned(parent)
            if not finished:
                os.waitpid(parent, 0)
            os.close(master)

print("native foreground: parent-only cancellation, descendants, terminal handoff, stop/continue and redirected stdin passed")
