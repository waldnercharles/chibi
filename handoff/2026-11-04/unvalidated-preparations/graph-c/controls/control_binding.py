"""Fixed, UNARMED graph-c control binding. Import alone launches nothing.

No CLI payload, selected executable, command string, environment override,
compiler call, or retry-until-pass loop exists. main always refuses. A future
independent source + ELF/closure + budget admission must pin this exact source
and its caller before the separately named admitted function may be invoked.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import select
import signal
import time
import types

HERE = Path('/workspace/shared/chibi-graph-c-runtime-20261104-revision01/controls')
PRODUCTION = HERE.parent
CORE = PRODUCTION / 'graph_c_attribution.py'
CORE_SHA256 = 'aeb3759d27e523aeb0257e73790ffce8cca89a1e64feb02290a2442d903914b4'
FIXTURE = HERE / 'artifacts/fixture'
SYNTHETIC_SCRIPT = b'"' + os.fsencode(FIXTURE) + b'" --branch & wait'
REAL_SCRIPT = b'(sleep 0.2; printf late > "$1") & wait'
ENVIRONMENT = {'PATH': '/usr/bin:/bin', 'LC_ALL': 'C', 'LANG': 'C', 'TZ': 'UTC'}
KERNEL_CASES = (1, 2, 3, 4, 5, 10, 11, 14, 15, 16, 18, 19, 23, 24, 30, 31, 32, 33, 35, 37)
# These use the same exact source-bound production owner as the kernel cases.
CALLER_CASES = (21, 22, 25, 26, 29, 34, 36)
MODEL_CASES = (6, 7, 8, 9, 12, 13, 17, 20, 27, 28, 38)


def require_admission():
    plan = json.loads((HERE/'CONTROL-PLAN.json').read_text())
    if plan['enabled'] is not True:
        raise RuntimeError('UNARMED: control plan disabled; independent admissions absent')
    path = HERE/'control_controller.py'
    if hashlib.sha256(path.read_bytes()).hexdigest() != plan['source_sha256']['control_controller.py']:
        raise RuntimeError('control controller source changed')
    spec = importlib.util.spec_from_file_location('fixed_control_admission', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.admission()


def load_bound_core(case_id):
    """Exact production method bodies, isolated module globals, fixed bindings."""
    require_admission()
    if case_id not in KERNEL_CASES + CALLER_CASES:
        raise ValueError('not a declared kernel case')
    if hashlib.sha256(CORE.read_bytes()).hexdigest() != CORE_SHA256:
        raise RuntimeError('reviewed attribution core changed')
    spec = importlib.util.spec_from_file_location('graph_c_control_core', CORE)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    # These are the complete ordinary substitutions. os/select/time are private
    # namespace copies so a fault cannot change the controller's own syscalls.
    module.RUNTIME = HERE / 'work' / ('case-%02d' % case_id)
    marker = module.RUNTIME / 'late-child'
    script = REAL_SCRIPT if case_id in (18, 19) else SYNTHETIC_SCRIPT
    module.SHELL_ARGV = b'\0'.join((b'/bin/sh', b'-c', script, b'deadline', os.fsencode(marker))) + b'\0'
    module.os = types.SimpleNamespace(**vars(os))
    module.select = types.SimpleNamespace(**vars(select))
    module.time = types.SimpleNamespace(**vars(time))
    return module


def read_fixture(fd, pending, records):
    """At most 32 fixture records/16KiB; no terminal evidence is inferred."""
    while select.select([fd], [], [], 0)[0]:
        chunk = os.read(fd, 4096)
        if not chunk:
            break
        pending += chunk
        if len(pending) > 16384:
            raise RuntimeError('fixture evidence bound exceeded')
        while b'\n' in pending:
            line, pending = pending.split(b'\n', 1)
            records.append(json.loads(line))
            if len(records) > 32:
                raise RuntimeError('fixture record count exceeded')
    return pending


def fixture_pids(records):
    return {r['role']: r['pid'] for r in records if r['event'] == 'created'}


def wait_retirement(command_fd, evidence_fd, records, pending, stop):
    os.write(command_fd, b'R')
    while not any(r['event'] == 'shell_retired' for r in records):
        if time.monotonic() >= stop:
            raise RuntimeError('binding-boundary retirement not witnessed')
        pending = read_fixture(evidence_fd, pending, records)
        time.sleep(0.0005)
    return pending


def apply_kernel_fault(core, state, case_id, channels, records, original_mono, variant=None):
    """One explicitly enumerated fault site, never a production mode switch."""
    command_fd, evidence_fd = channels
    pids = fixture_pids(records)
    if case_id == 2:
        register = state.register
        def suppress_leaf(pid, ancestry):
            if pid != pids[3]:
                register(pid, ancestry)
        state.register = suppress_leaf
    elif case_id in (10, 11):
        shell = pids[1]
        real_open = os.pidfd_open
        def retire_around_open(pid, flags):
            if pid != shell:
                return real_open(pid, flags)
            if case_id == 10:
                wait_retirement(command_fd, evidence_fd, records, b'', original_mono + 5)
                return real_open(pid, flags)  # Must fail; no replacement retry.
            descriptor = real_open(pid, flags)
            wait_retirement(command_fd, evidence_fd, records, b'', original_mono + 5)
            return descriptor  # The second stat uses the old opened proc dir.
        core.os.pidfd_open = retire_around_open
    elif case_id == 15:
        live = state.live
        reads = 0
        def adoption_between_rounds(node):
            nonlocal reads
            row = live(node)
            reads += 1
            if reads == 2:  # Root and shell first round, before second round.
                os.write(command_fd, b'S')
                stop = original_mono + 5
                pending = b''
                while not any(r['event'] == 'shell_retired' for r in records):
                    if time.monotonic() >= stop:
                        raise RuntimeError('ancestry race witness missing')
                    pending = read_fixture(evidence_fd, pending, records)
            return row
        state.live = adoption_between_rounds
    elif case_id == 23:
        register = state.register
        def cancel_registration(pid, ancestry):
            state.cancelled = 'control cancellation during registration'
            state.check()
            register(pid, ancestry)
        state.register = cancel_registration
    elif case_id == 30 and variant == 'mismatch':
        capture = state.capture_owned
        def mismatch_ownership(pid):
            if pid == state.root:
                return capture(pid)
            fd_pid = state.fd_pid
            try:
                state.fd_pid = lambda node: -1
                return capture(pid)
            finally:
                state.fd_pid = fd_pid
        state.capture_owned = mismatch_ownership
    elif case_id == 30:
        capture = state.capture_owned
        def deny_ownership(pid):
            if pid != state.root:
                raise PermissionError('synthetic ownership read denial, not a real permission request')
            return capture(pid)
        state.capture_owned = deny_ownership
    elif case_id == 31 and variant == 'open':
        def deny_open(pid, flags):
            raise PermissionError('synthetic pidfd_open denial')
        core.os.pidfd_open = deny_open
    elif case_id == 31:
        def deny_signal(pidfd, signum):
            raise PermissionError('synthetic pidfd_send_signal denial')
        core.signal = types.SimpleNamespace(**vars(signal))
        core.signal.pidfd_send_signal = deny_signal
        state.fail('force failure-only cleanup before synthetic signal denial')
    elif case_id == 32 and variant in ('registration', 'terminal'):
        stream = state.journal
        target = 'registered' if variant == 'registration' else 'ownership_success_provisional'
        def deny_write(line):
            if json.loads(line)['event'] == target:
                raise OSError('synthetic journal write failure: ' + target)
            return stream.write(line)
        state.journal = types.SimpleNamespace(write=deny_write, flush=stream.flush,
                                              fileno=stream.fileno, close=stream.close)
    elif case_id == 32:
        def deny_fsync(fd):
            raise OSError('synthetic journal fsync failure')
        core.os.fsync = deny_fsync


# Kernel and lifecycle cases now share the one source-bound control_owner.py.
# caller_support.drive invokes these exact finite faults before observe/drain.




def main():
    raise SystemExit('UNARMED: source-only controls; no launch, admission, or acceptance')


if __name__ == '__main__':
    main()
