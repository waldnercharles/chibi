"""Run existing selectors against one clean, explicitly named candidate."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time


root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--head', required=True)
parser.add_argument('--label', required=True)
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('--kind', choices=('native', 'wasm', 'sanitizer'), required=True)
parser.add_argument('selectors', nargs='+')
args = parser.parse_args()
if not args.label or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-_' for c in args.label):
    parser.error('--label must contain lowercase letters, numbers, hyphens or underscores')
if any(not s or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for c in s)
       for s in args.selectors):
    parser.error('selectors must be ordinary test-policy selector names')
build = (root / args.build).resolve()
destination = Path(__file__).resolve().parent / ('validation-' + args.label)
destination.mkdir(exist_ok=False)


def git(*arguments):
    return subprocess.check_output(['git', *arguments], cwd=root, text=True).strip()


def identity():
    return {'head': git('rev-parse', 'HEAD'), 'tree': git('rev-parse', 'HEAD^{tree}'),
            'status': git('status', '--porcelain=v1')}


before = identity()
assert before['head'] == args.head and not before['status'], before
environment = os.environ.copy()
selected = {}
if args.kind == 'wasm':
    selected['CHIBI_WASM_BUILD_DIR'] = str(build)
    binaries = [build / 'test_language.js', build / 'test_language.wasm']
    assert all(s.startswith('wasm-') for s in args.selectors), args.selectors
else:
    assert not any(s.startswith('wasm-') for s in args.selectors), args.selectors
    selected['CHIBI_LANGUAGE_BUILD_DIR'] = str(build / 'test/language')
    selected['CHIBI_TOOLS_BUILD_DIR'] = str(build / 'test/tools')
    for key, relative in {
        'CHIBI_COMPILER_HOST': 'sdk/chibi-compiler-host',
        'CHIBI_RELEASE_COMPILER_HOST': 'sdk/chibi-compiler-host',
        'CHIBI_BUILD_HOST': 'sdk/chibi-build-host',
        'CHIBI_CLI': 'sdk/chibi',
        'CHIBI_COMPILE': 'sdk/chibi-compile',
        'CHIBI_CTL': 'bin/chibi_ctl',
        'CHIBI_RELEASE_LANGUAGE_BUILD_DIR': 'test/language',
    }.items():
        selected[key] = str(build / relative)
    binaries = [build / 'test/language/test_language', build / 'test/tools/test_tools']
    binaries.extend(Path(p) for p in selected.values() if Path(p).is_file())
    binaries = sorted(set(binaries))
environment.update(selected)
receipt = {'kind': args.kind, 'before': before, 'build': str(build),
           'environment': selected, 'steps': [],
           'qualification': 'Build provenance must be supplied separately; binary hashes identify the executed files.'}
receipt['binary_sha256_before'] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                                   for p in binaries if p.is_file()}
if args.kind == 'sanitizer':
    receipt['sanitizer_option_presence'] = {
        k: k in environment for k in ('ASAN_OPTIONS', 'UBSAN_OPTIONS', 'LSAN_OPTIONS')}
    assert not any(receipt['sanitizer_option_presence'].values()), receipt['sanitizer_option_presence']
    environment['MallocNanoZone'] = '0'
    selected['MallocNanoZone'] = '0'
proof = destination / 'receipt.json'


def save():
    proof.write_text(json.dumps(receipt, indent=2) + '\n')


exit_code = 0
try:
    for selector in args.selectors:
        assert identity() == before, 'Candidate changed before ' + selector
        command = ['python3', 'tools/test_policy.py', 'tool', selector]
        log = destination / (selector + '.log')
        started = time.monotonic()
        print('RUN', selector, flush=True)
        with log.open('w') as output:
            result = subprocess.run(command, cwd=root, env=environment,
                                    stdout=output, stderr=subprocess.STDOUT)
        text = log.read_text(errors='replace')
        step = {'selector': selector, 'command': command, 'exit_code': result.returncode,
                'seconds': time.monotonic() - started, 'log': str(log),
                'log_sha256': hashlib.sha256(log.read_bytes()).hexdigest(),
                'summary': [line for line in text.splitlines()
                            if line.startswith(('Summary:', 'Test command wall time:'))]}
        receipt['steps'].append(step)
        save()
        print(selector, result.returncode, step['summary'], flush=True)
        if result.returncode:
            exit_code = result.returncode
            break
finally:
    receipt['after'] = identity()
    receipt['candidate_unchanged'] = receipt['after'] == before
    receipt['binary_sha256_after'] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                                     for p in binaries if p.is_file()}
    receipt['binaries_unchanged'] = receipt['binary_sha256_before'] == receipt['binary_sha256_after']
    save()
assert receipt['candidate_unchanged'], 'Candidate changed during validation'
assert receipt['binaries_unchanged'], 'Executed binaries changed during validation'
raise SystemExit(exit_code)
