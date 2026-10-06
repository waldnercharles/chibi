#!/usr/bin/env python3
"""Temporary command-level test quarantine; remove only after timing review."""
import argparse
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
LIMIT = 60
# Exact invocations, not selector prefixes. Historical times are not guarantees.
NATIVE = {
    ('--graph-optimize',): '0.5s; docs/testing.md baseline',
    ('--studio-documents', 'tabs'): '8s; PR164 focused evidence',
    ('--studio-shell', 'readout_follows_camera'): '3s; PR164 focused evidence',
    ('--bench-arena-scene-scale', '2048', '240', '0'): '1.4s; Arena scaling evidence',
}
NOTICE = ('TEMPORARY TEST QUARANTINE: full native/render, tooling aggregate and '
          'unadmitted checks DISABLED, NOT PASSED. See docs/testing.md#temporary-60-second-policy.')


def admitted(arguments):
    return tuple(arguments) in NATIVE


def run_commands(commands, seconds=LIMIT):
    """One deadline for the entire invocation, including descendants and setup."""
    if not 0 < seconds <= LIMIT:
        raise ValueError('test deadline must be at most 60 seconds')
    started = time.monotonic()
    deadline = started + seconds
    try:
        for command in commands:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return 124
            child = subprocess.Popen(command, cwd=ROOT, start_new_session=True)
            try:
                try:
                    result = child.wait(timeout=max(0, deadline - time.monotonic()))
                except subprocess.TimeoutExpired:
                    print('FAIL: test command exceeded its aggregate deadline', file=sys.stderr)
                    return 124
                if time.monotonic() >= deadline:
                    return 124
                if result:
                    return result if result > 0 else 128 - result
            finally:
                # Also clean up grandchildren after a failed or normally exiting parent.
                try:
                    os.killpg(child.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                child.wait()
        return 0
    finally:
        print(f'Test execution: {time.monotonic() - started:.3f}s; ceiling {seconds}s; '
              'configuration/build excluded.', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['inventory', 'disabled', 'short', 'admit', 'native'])
    parser.add_argument('arguments', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    arguments = args.arguments
    if arguments[:1] == ['--']:
        arguments = arguments[1:]
    print(NOTICE, flush=True)
    if args.action == 'inventory':
        for command, evidence in NATIVE.items():
            print('ADMITTED (60s ceiling):', ' '.join(command), '-', evidence)
        print('ADMITTED: make test / test-short (test/test_policy_test.py only)')
        print('All other native invocations and existing test-tool-* targets are quarantined pending timing review.')
        return 0
    if args.action == 'disabled':
        print('DISABLED:', ' '.join(arguments), file=sys.stderr)
        return 2
    if args.action == 'short':
        if arguments:
            parser.error('short accepts no arguments')
        return run_commands([[sys.executable, str(ROOT / 'test/test_policy_test.py')]])
    selectors = arguments[1:] if args.action == 'native' else arguments
    if not admitted(selectors):
        print('DISABLED: unadmitted native invocation: ' + ' '.join(selectors), file=sys.stderr)
        return 2
    if args.action == 'admit':
        return 0
    return run_commands([arguments])


def terminate(_signal, _frame):
    raise KeyboardInterrupt()


if __name__ == '__main__':
    signal.signal(signal.SIGTERM, terminate)
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
    except OSError as error:
        print(f'FAIL: {error}', file=sys.stderr)
        sys.exit(2)
