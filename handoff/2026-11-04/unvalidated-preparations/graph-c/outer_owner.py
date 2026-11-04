#!/usr/bin/env python3
"""Proof-local Linux child owner. No process outside its own children is signalled."""
import argparse
import ctypes
import errno
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
CLEANUP_RESERVE = 5


def require(ok, message):
    if not ok:
        raise ValueError(message)


def identity(pid):
    text = Path(f'/proc/{pid}/stat').read_text()
    tail = text.rsplit(')', 1)[1].split()
    return {'pid': pid, 'ppid': int(tail[1]), 'pgid': int(tail[2]), 'sid': int(tail[3]),
            'start_ticks': int(tail[19]), 'state': tail[0]}


def stable(row):
    return {key: row[key] for key in ('pid', 'ppid', 'pgid', 'sid', 'start_ticks')}


def save(path, value):
    temporary = path.with_suffix('.partial.json')
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')
    temporary.replace(path)


def set_subreaper():
    require(sys.platform == 'linux', 'Linux subreaper required')
    require(hasattr(os, 'pidfd_open') and hasattr(signal, 'pidfd_send_signal'), 'pidfd API required; no fallback')
    libc = ctypes.CDLL(None, use_errno=True)
    libc.prctl.argtypes = [ctypes.c_int, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong]
    libc.prctl.restype = ctypes.c_int
    if libc.prctl(36, 1, 0, 0, 0) != 0:  # PR_SET_CHILD_SUBREAPER, this process only.
        raise OSError(ctypes.get_errno(), 'PR_SET_CHILD_SUBREAPER denied or unsupported')
    value = ctypes.c_int()
    if libc.prctl(37, ctypes.addressof(value), 0, 0, 0) != 0:  # PR_GET_CHILD_SUBREAPER
        raise OSError(ctypes.get_errno(), 'PR_GET_CHILD_SUBREAPER denied or unsupported')
    require(value.value == 1, 'self subreaper was not established')
    # Exercise only this process's pidfd before creating any child.
    fd = os.pidfd_open(os.getpid())
    try:
        signal.pidfd_send_signal(fd, 0)
    finally:
        os.close(fd)


def direct_children():
    # This kernel omits /proc/<pid>/task/<tid>/children. Read only ancestry
    # identities, retain only actual direct children, and use waitpid ECHILD
    # (not this list) as the authoritative completion condition.
    result = []
    for path in Path('/proc').iterdir():
        if not path.name.isdigit(): continue
        try:
            row = identity(int(path.name))
        except (FileNotFoundError, ProcessLookupError):
            continue
        if row['ppid'] == os.getpid(): result.append(row['pid'])
    return sorted(result)


def supervise(command, cwd, output, execution_cutoff, deadline, metadata=None):
    """Single-purpose process owns one launch; ECHILD covers only its children.

    The execution cutoff is deadline minus the five-second cleanup reserve.
    No new work starts after cutoff. Termination, reaping and receipts use only
    the remaining original phase; no deadline is extended. Never kill by PID,
    name, process group, baseline novelty, or unrelated /proc membership.
    """
    require(deadline - execution_cutoff == CLEANUP_RESERVE and time.time() < execution_cutoff,
            'fixed deadline with internal cleanup reserve required')
    require(not direct_children(), 'owner must begin with no children')
    require(len(list(Path('/proc/self/task').iterdir())) == 1, 'single-threaded sole child reaper required')
    signal.signal(signal.SIGCHLD, signal.SIG_DFL)  # Clear inherited SIG_IGN/SA_NOCLDWAIT before creating any child.
    output = Path(output); output.mkdir()
    report = {'schema_version': 2, 'status': 'running', 'owner': stable(identity(os.getpid())),
              'subreaper': False, 'command': list(map(str, command)), 'child_environment': {},
              'execution_cutoff': execution_cutoff, 'deadline': deadline, 'cleanup_reserve_seconds': CLEANUP_RESERVE,
              'ownership_scope': 'This subreaper process and only its launched/adopted kernel children; no global absence claim',
              'launch': metadata, 'observed_children': [], 'wait_statuses': [], 'signals': [], 'errors': [],
              'cleanup_echild': False, 'cancel_signal': None}
    root = None; root_status = None; descriptors = {}; tracked = {}; reason = None; cancelled = None; blocked = False
    end_mono = time.monotonic() + (deadline - time.time())
    cutoff_mono = end_mono - CLEANUP_RESERVE
    old_handlers = {}
    def cancel(number, _frame):
        nonlocal cancelled
        if cancelled is None: cancelled = number
    for number in (signal.SIGTERM, signal.SIGINT):
        old_handlers[number] = signal.signal(number, cancel)
    def snapshot():
        nonlocal reason
        for pid in direct_children():
            if pid in descriptors: continue
            # Children are not auto-reaped. A dead child's PID remains reserved
            # until this owner calls waitpid, and the pidfd pins that identity.
            fd = os.pidfd_open(pid)
            row = identity(pid)
            require(row['ppid'] == os.getpid(), 'kernel child ancestry changed before pidfd observation')
            descriptors[pid] = fd; tracked[pid] = row
            report['observed_children'].append(row)
            if root is not None and pid != root.pid:
                reason = reason or 'adopted_descendant'
    def reap():
        nonlocal root_status, reason
        while True:
            try:
                pid, status = os.waitpid(-1, os.WNOHANG)
            except ChildProcessError:
                return True
            if pid == 0: return False
            row = {'pid': pid, 'raw_wait_status': status, 'exit_status': os.waitstatus_to_exitcode(status),
                   'identity': tracked.get(pid), 'adopted_before_identity': pid not in tracked}
            report['wait_statuses'].append(row)
            fd = descriptors.pop(pid, None)
            if fd is not None: os.close(fd)
            if root is not None and pid == root.pid:
                root_status = status; root.returncode = row['exit_status']
                if root.returncode != 0: reason = reason or 'child_failed'
            else:
                reason = reason or 'adopted_descendant'
    def terminate():
        # Only direct children obtained from the kernel ownership relation.
        # Killing a parent transfers surviving descendants to this subreaper;
        # the next iteration obtains fresh pidfds for those adopted children.
        for pid, fd in list(descriptors.items()):
            if any(value['pid'] == pid for value in report['signals']): continue
            try:
                signal.pidfd_send_signal(fd, signal.SIGKILL)
                report['signals'].append({'pid': pid, 'start_ticks': tracked[pid]['start_ticks'], 'signal': 'SIGKILL'})
            except ProcessLookupError:
                pass  # Still call waitpid; an exit is not a reaping claim.
    try:
        save(output / 'receipt.json', report)
        set_subreaper()  # Failure is recorded; no alternative capability or privilege route.
        report['subreaper'] = True
        require(not cancelled and time.time() < execution_cutoff and time.monotonic() < cutoff_mono,
                'cancelled or expired before launch')
        with (output / 'raw.log').open('xb', buffering=0) as log:
            root = subprocess.Popen(command, cwd=cwd, env={}, stdout=log, stderr=subprocess.STDOUT,
                                    start_new_session=True, close_fds=True)
            launched = stable(identity(root.pid))
            require(launched['ppid'] == os.getpid() and launched['pid'] == launched['pgid'] == launched['sid'],
                    'outer child must own a fresh detached session/group')
            launch = {**report, 'root': launched}
            save(output / 'launch.json', launch)
            report['root'] = launched
            while True:
                if cancelled is not None:
                    report['cancel_signal'] = cancelled; reason = reason or 'cancelled'
                if time.time() >= execution_cutoff or time.monotonic() >= cutoff_mono:
                    reason = reason or 'deadline'
                snapshot()
                empty = reap()
                if empty:
                    report['cleanup_echild'] = True
                    break
                if time.time() >= deadline or time.monotonic() >= end_mono:
                    reason = reason or 'deadline'; break
                if reason: terminate()
                save(output / 'receipt.json', report)
                time.sleep(0.02)
    except BaseException as error:
        reason = reason or 'owner_error'; report['errors'].append(repr(error))
        blocked = isinstance(error, OSError) and error.errno in (errno.ENOSYS, errno.EPERM, errno.EACCES)
        report['ownership_syscall_blocked'] = blocked
    finally:
        # Also preserve cleanup after an owner-side exception or a failed launch.
        while not report['cleanup_echild'] and time.time() < deadline and time.monotonic() < end_mono:
            try:
                if not blocked: snapshot()
                if reap(): report['cleanup_echild'] = True; break
                if not blocked: terminate()
            except BaseException as error:
                report['errors'].append(repr(error)); break  # Report denied syscalls; never bypass.
            time.sleep(0.02)
        report['remaining_direct_children'] = direct_children()
        report['reason'] = reason
        report['root_wait_status'] = root_status
        report['cancel_signal'] = cancelled
        if (output / 'raw.log').exists():
            report['raw_log_sha256'] = hashlib.sha256((output / 'raw.log').read_bytes()).hexdigest()
        report['finished_epoch'] = time.time()
        report['deadline_respected'] = report['finished_epoch'] <= deadline and time.monotonic() <= end_mono
        if not report['cleanup_echild']:
            report['status'] = 'unresolved_owned_cleanup'
        elif cancelled is not None:
            report['status'] = 'cancelled'
        elif reason == 'deadline' or not report['deadline_respected']:
            report['status'] = 'timeout'
        elif reason is not None or root_status is None or os.waitstatus_to_exitcode(root_status) != 0:
            report['status'] = 'failed'
        else:
            report['status'] = 'pass'
        for fd in descriptors.values(): os.close(fd)
        for number, handler in old_handlers.items(): signal.signal(number, handler)
        save(output / 'receipt.json', report)
    return 0 if report['status'] == 'pass' else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', required=True, type=Path)
    args = parser.parse_args()
    require(sys.flags.isolated and sys.flags.no_site and sys.dont_write_bytecode,
            'isolated Python -I -S -B required')
    require(Path('/proc/self/environ').read_bytes() == b'', 'owner initial environment must be empty')
    import importlib.util
    spec = importlib.util.spec_from_file_location('proof', HERE / 'proof.py')
    proof = importlib.util.module_from_spec(spec); spec.loader.exec_module(proof)
    path, config, start, deadline = proof.run_config(args.run)
    source = proof.source_guard(config); binding = proof.acceptance_guard(config, source)
    # ctypes/libffi are part of this proof-local owner, not bootstrap inputs.
    mapped = {line.split()[-1] for line in Path('/proc/self/maps').read_text().splitlines()
              if len(line.split()) >= 6 and line.split()[-1].startswith('/')}
    pinned = {v['realpath'] for v in binding['toolchain']['files']} | {v['realpath'] for v in binding['toolchain']['programs'].values()}
    imported = {str(Path(tree['root']) / row['path']) for tree in binding['toolchain']['trees']
                for row in json.loads(proof.pin(tree['manifest']).read_text())}
    require(mapped <= pinned | imported, 'owner actual mapped runtime/locale/ctypes closure omitted')
    require(Path(sys.executable).resolve() == proof.PYTHON.resolve(), 'system Python required')
    proof.output_paths(config)
    command = [str(proof.NODE), str(proof.SOURCE / '.github/ci/budget.js'), str(int(deadline - CLEANUP_RESERVE)),
               str(proof.PYTHON), '-I', '-S', '-B', str(HERE / 'proof.py'), '--run', str(path)]
    return supervise(command, proof.SOURCE, config['owner_output_root'], deadline - CLEANUP_RESERVE, deadline,
                     {'config': proof.fact(path), 'owner_file': proof.fact(__file__), 'phase': config['phase']})


if __name__ == '__main__':
    raise SystemExit(main())
