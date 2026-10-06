#!/usr/bin/env python3
"""Temporary command-level test quarantine; remove only after timing review."""
import argparse
import ctypes
import json
import math
import os
from pathlib import Path
import signal
import shlex
import subprocess
import sys
import time

from quiet import child_fds

ROOT = Path(__file__).resolve().parents[1]
LIMIT = 60
DEADLINE = 'CHIBI_TEST_DEADLINE'
PROCESS_GROUP = 'CHIBI_TEST_PROCESS_GROUP'
# Exact invocations, not selector prefixes. Historical times are not guarantees.
NATIVE = {
    ('--compiled-behaviors',): '47.2s; docs/testing.md selector evidence',
    ('--automation-search',): '17.2s; docs/testing.md selector evidence',
    ('--automation-refactor',): '5.0s; docs/testing.md selector evidence',
    ('--automation-profile-scenario-inputs',): '15.8s; docs/testing.md selector evidence',
    ('--editor-commands',): '12.5s; docs/testing.md selector evidence',
    ('--material-thermal',): '4.8s; docs/testing.md selector evidence',
    ('--material-mpm',): '3.6s; docs/testing.md selector evidence',
    ('--whole-function-audit',): '1.3s; docs/testing.md baseline',
    ('--graph-optimize',): '0.5s; docs/testing.md baseline',
    ('--studio-documents', 'tabs'): '8s; PR164 focused evidence',
    ('--studio-shell', 'readout_follows_camera'): '3s; PR164 focused evidence',
    ('--bench-arena-scene-scale', '2048', '240', '0'): '1.4s; Arena scaling evidence',
}
TOOLS = {'native-tools': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build-tools') / 'test_tools')]}
NOTICE = ('TEMPORARY TEST QUARANTINE: full native/render and '
          'unadmitted checks DISABLED, NOT PASSED. See docs/testing.md#temporary-60-second-policy.')


def admitted(arguments):
    return tuple(arguments) in NATIVE


def selector_arguments(name):
    if name not in ('TEST_ARGS', 'AGENT_SELECTOR'):
        raise ValueError('unsupported selector environment variable')
    # Tokenize quoted positional arguments without executing shell syntax.
    return shlex.split(os.environ.get(name, ''))


def absolute_deadline(seconds, env):
    if not math.isfinite(seconds) or not 0 < seconds <= LIMIT:
        raise ValueError('test deadline must be at most 60 seconds')
    deadline = time.monotonic() + seconds
    if DEADLINE in env:
        inherited = float(env[DEADLINE])
        if not math.isfinite(inherited):
            raise ValueError('invalid inherited test deadline')
        deadline = min(deadline, inherited)
    return deadline


def process_children():
    # Shared portable PID/parent snapshot. Exclude the probe itself: it has
    # already been reaped when we use the snapshot and is not owned test work.
    probe = subprocess.Popen(['ps', '-A', '-o', 'pid=,ppid='],
                             stdout=subprocess.PIPE, text=True)
    try:
        rows, _ = probe.communicate(timeout=1)
    finally:
        if probe.poll() is None:
            probe.kill()
        probe.wait()
    if probe.returncode:
        raise RuntimeError('test cleanup process inspection failed')
    children = {}
    own_process_seen = False
    for row in rows.splitlines():
        pid, parent = map(int, row.split())
        own_process_seen |= pid == os.getpid()
        if pid != probe.pid:
            children.setdefault(parent, []).append(pid)
    if not own_process_seen:
        raise RuntimeError('test cleanup cannot inspect its own process')
    return children


def proc_children():
    path = Path(f'/proc/self/task/{os.getpid()}/children')
    return [int(pid) for pid in path.read_text().split()]


def cleanup_reader(subreaper):
    # Verify the selected discovery capability before launching test work.
    if subreaper:
        try:
            proc_children()
            return proc_children
        except (OSError, ValueError):
            pass
    try:
        process_children()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        raise RuntimeError('test cleanup unavailable: no usable proc children reader or ps; no test started') from error
    return lambda: process_children().get(os.getpid(), [])


def cleanup_children(read_children):
    # This dedicated subreaper owns all its children, including double-forked or
    # setsid descendants adopted when their parent/nested supervisor is killed.
    # Kill parents, reap, then repeat for newly adopted children until none remain.
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    while True:
        children = read_children()
        for pid in children:
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        try:
            pid, _ = os.waitpid(-1, os.WNOHANG)
        except ChildProcessError:
            return
        if not pid:
            time.sleep(.001)


def cleanup_subtree(child):
    # Portable nested cleanup cannot kill the shared ancestor process group.
    # Freeze the live child tree before killing it so parent death cannot hide
    # descendants by reparenting them while we discover the owned subtree.
    if child.poll() is not None:
        return
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    owned, pending = [child.pid], [child.pid]
    try:
        while pending:
            for pid in pending:
                try:
                    os.kill(pid, signal.SIGSTOP)
                except ProcessLookupError:
                    pass
            children = process_children()
            pending = []
            frontier = list(owned)
            seen = set(owned)
            for parent in frontier:
                for pid in children.get(parent, []):
                    if pid not in seen:
                        seen.add(pid)
                        frontier.append(pid)
                        pending.append(pid)
            owned.extend(pending)
    finally:
        for pid in reversed(owned):
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        child.wait()


def wait_until(child, deadline):
    # Popen.wait adds a KeyboardInterrupt grace period; do not grant test work
    # extra runtime before cleanup on an interrupt.
    while child.poll() is None:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise subprocess.TimeoutExpired(child.args, 0)
        time.sleep(min(.005, remaining))
    return child.returncode


def enable_subreaper():
    # Linux additionally owns detached/orphaned descendants. Other supported
    # POSIX hosts retain the portable process-group deadline path.
    if not sys.platform.startswith('linux'):
        return False
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(36, 1, 0, 0, 0):  # PR_SET_CHILD_SUBREAPER
        raise OSError(ctypes.get_errno(), 'cannot own test descendants')
    return True


def inherited_group(environment):
    return environment.get(PROCESS_GROUP) == str(os.getpgrp())


def supervise(commands):
    subreaper = enable_subreaper()
    read_children = cleanup_reader(subreaper)
    os.environ[PROCESS_GROUP] = str(os.getpgrp())
    child = None
    deadline = absolute_deadline(LIMIT, os.environ)
    try:
        for command in commands:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return 124
            child = subprocess.Popen(command, cwd=ROOT, pass_fds=child_fds())
            try:
                result = wait_until(child, deadline)
            except subprocess.TimeoutExpired:
                print('FAIL: test command exceeded its aggregate deadline', file=sys.stderr)
                return 124
            if time.monotonic() >= deadline:
                return 124
            if result:
                return result if result > 0 else 128 - result
        return 0
    finally:
        if subreaper:
            cleanup_children(read_children)
        elif child is not None:
            cleanup_subtree(child)


def run_commands(commands, seconds=LIMIT, env=None, output=None):
    """One inherited absolute deadline and one isolated descendant owner."""
    environment = dict(os.environ if env is None else env)
    # A caller-supplied environment may not erase an enclosing command's limit.
    if DEADLINE in os.environ:
        environment[DEADLINE] = str(min(float(environment.get(DEADLINE, 'inf')),
                                      float(os.environ[DEADLINE])))
    deadline = absolute_deadline(seconds, environment)
    environment[DEADLINE] = str(deadline)
    started = time.monotonic()
    if deadline <= started:
        return 124
    owns_group = not inherited_group(environment)
    child = subprocess.Popen([sys.executable, str(Path(__file__).resolve()),
                              '_supervise', json.dumps(commands)], cwd=ROOT,
                             env=environment, start_new_session=owns_group,
                             stdout=output, stderr=output, pass_fds=child_fds())
    try:
        try:
            result = wait_until(child, deadline)
        except subprocess.TimeoutExpired:
            return 124
        if time.monotonic() >= deadline:
            return 124
        return result if result >= 0 else 128 - result
    finally:
        if child.poll() is None:
            # Let the supervisor unwind first; Linux also reaps descendants
            # which escaped the portable process group.
            child.terminate()
        child.wait()
        if owns_group:
            # Also used on macOS: nested managed wrappers stay in this group.
            # Arbitrary descendants that call setsid need Linux subreaping.
            try:
                os.killpg(child.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        print(f'Test command wall time: {time.monotonic() - started:.3f}s; ceiling {seconds}s; '
              'includes in-test fixture setup; external configuration/build excluded.', file=output or sys.stdout, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['inventory', 'disabled', 'short', 'tool', 'admit', 'native', 'admit-env', 'native-env', 'agent-env', 'studio-selectors', '_supervise'])
    parser.add_argument('arguments', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    arguments = args.arguments
    if arguments[:1] == ['--']:
        arguments = arguments[1:]
    if args.action == '_supervise':
        return supervise(json.loads(arguments[0]))
    print(NOTICE, flush=True)
    if args.action == 'inventory':
        for command, evidence in NATIVE.items():
            print('ADMITTED (60s ceiling):', ' '.join(command), '-', evidence)
        print('ADMITTED: make test / test-short / test-tools (essential C pico_unit tooling, one deadline)')
        print('ADMITTED: make test-studio-selectors (C CLI checks, one deadline)')
        print('Retired script suites are absent. Other native commands remain quarantined; see docs/test-quarantine.md.')
        return 0
    if args.action == 'disabled':
        print('DISABLED:', ' '.join(arguments), file=sys.stderr)
        return 2
    if args.action == 'short':
        if len(arguments) != 1:
            parser.error('short requires the native C tooling test binary')
        return run_commands([[str(Path(arguments[0]).resolve())]])
    if args.action == 'studio-selectors':
        if len(arguments) != 2:
            parser.error('studio-selectors requires the C tooling driver and test_suite binary')
        return run_commands([[str(Path(arguments[0]).resolve()), '--studio-selectors', str(Path(arguments[1]).resolve())]])
    if args.action == 'tool':
        if len(arguments) != 1 or arguments[0] not in TOOLS:
            parser.error('unadmitted tooling command')
        return run_commands([TOOLS[arguments[0]]])
    if args.action in ('admit-env', 'native-env', 'agent-env'):
        expected = 1 if args.action == 'admit-env' else 2
        if len(arguments) != expected:
            parser.error('invalid environment-selector invocation')
        name = 'AGENT_SELECTOR' if args.action == 'agent-env' else arguments[0]
        selectors = selector_arguments(name)
        if not admitted(selectors):
            print('DISABLED: unadmitted native invocation: ' + ' '.join(selectors), file=sys.stderr)
            return 2
        if args.action == 'admit-env':
            return 0
        if args.action == 'agent-env':
            if arguments[1] not in ('test', 'gpu-test'):
                parser.error('invalid agent test mode')
            return run_commands([[sys.executable, str(ROOT / 'tools/agent.py'),
                                  '--build-dir', arguments[0], arguments[1], '--', *selectors]])
        return run_commands([[arguments[1], *selectors]])
    selectors = arguments[1:] if args.action == 'native' else arguments
    if not admitted(selectors):
        print('DISABLED: unadmitted native invocation: ' + ' '.join(selectors), file=sys.stderr)
        return 2
    if args.action == 'admit':
        return 0
    return run_commands([arguments])


class TerminationRequested(BaseException):
    pass


def terminate(_signal, _frame):
    raise TerminationRequested()


if __name__ == '__main__':
    signal.signal(signal.SIGTERM, terminate)
    signal.signal(signal.SIGINT, terminate)
    try:
        sys.exit(main())
    except (KeyboardInterrupt, TerminationRequested):
        sys.exit(130)
    except (OSError, ValueError, RuntimeError) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        sys.exit(2)
