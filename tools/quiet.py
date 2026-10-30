#!/usr/bin/env python3
"""Save build output and print bounded diagnostics; VERBOSE=1 streams it."""
import argparse
from collections import deque
import fcntl
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time


MAX_LOGS = 20
MAX_LOG_BYTES = 64 * 1024 * 1024
FAILURE_BYTES = 8000


def active(path):
    marker = path.with_suffix('.running')
    try:
        with marker.open('r+b') as lock:
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                return True
            marker.unlink(missing_ok=True)
    except FileNotFoundError:
        pass
    return False


def prune(directory, current):
    # Another make process can prune a completed log at the same time.
    for marker in directory.glob('*.running'):
        if not marker.with_suffix('.log').exists():
            active(marker.with_suffix('.log'))
    logs = []
    for path in directory.glob('*.log'):
        try:
            logs.append((path.stat().st_mtime, path.stat().st_size, path))
        except FileNotFoundError:
            continue
    kept = 0
    size = 0
    for _, length, path in sorted(logs, reverse=True):
        if path == current or active(path):
            continue
        size += length
        kept += 1
        if kept >= MAX_LOGS or size > MAX_LOG_BYTES:
            path.unlink(missing_ok=True)


def diagnostics(path):
    tail = deque(maxlen=25)
    errors = deque(maxlen=25)
    context = 0
    with path.open(errors='replace') as source:
        for line in source:
            # Reflection progress runs beside failing steps and would fill the tail.
            if line.startswith('reflect:'):
                continue
            tail.append(line)
            if re.search(r'error:|fatal error|CMake Error|FAILED|FAIL:|failed:|AssertionError|Traceback', line):
                context = 5
            if context:
                errors.append(line[:500])
                context -= 1
    return ''.join(errors)[-FAILURE_BYTES // 2:] + ''.join(tail)[-FAILURE_BYTES // 2:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log-dir', default='.chibi/logs')
    parser.add_argument('phase')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command[:1] == ['--']:
        command = command[1:]
    if not command:
        parser.error('a command is required')
    directory = Path(args.log_dir)
    directory.mkdir(parents=True, exist_ok=True)
    phase = re.sub(r'[^a-zA-Z0-9_.-]', '-', args.phase)
    path = directory / f'{time.time_ns()}-{os.getpid()}-{phase}.log'
    marker = path.with_suffix('.running')
    lock = marker.open('wb')
    fcntl.flock(lock, fcntl.LOCK_EX)
    started = time.monotonic()
    verbose = os.environ.get('VERBOSE') == '1'
    print(f'{args.phase}: running (log: {path})', file=sys.stderr, flush=True)
    try:
        with path.open('wb') as log:
            if verbose:
                child = subprocess.Popen(command, stdout=subprocess.PIPE,
                                         stderr=subprocess.STDOUT)
                for line in child.stdout:
                    log.write(line)
                    sys.stderr.buffer.write(line)
                    sys.stderr.buffer.flush()
                result = child.wait()
            else:
                result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT).returncode
    except OSError as error:
        path.write_text(f'{error}\n')
        result = 127
    finally:
        lock.close()
        try:
            marker.unlink(missing_ok=True)
            prune(directory, path)
        except OSError as error:
            print(f'Log cleanup: {error}', file=sys.stderr)
    print(f'{args.phase}: {"ok" if result == 0 else "FAILED"} '
          f'({time.monotonic() - started:.1f}s; log: {path})', file=sys.stderr)
    if result and not verbose:
        print(diagnostics(path), file=sys.stderr, end='')
    if result < 0:
        signal.signal(-result, signal.SIG_DFL)
        os.kill(os.getpid(), -result)
    return result


if __name__ == '__main__':
    sys.exit(main())
