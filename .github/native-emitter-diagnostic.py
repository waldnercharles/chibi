#!/usr/bin/env python3
"""One frozen, traced full-suite pair. Counts only; never a speed benchmark."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from native_emitter_counts import TraceError, parse_counts

SOURCE = '88c4f9564dbad4e6ae239d5de1f4ffbdde715504'
BASE = '907b1fe02c5cf09fe7d0b5b76f3472f0dc9f8c05'
BASE_HASH = '5cf81e058c0dfa28b7950a10e66642e1de35d8f626b7631357f2d73928e354a9'
CANDIDATE_HASH = 'df93c501a2e48cf75f6e70f4d6dcc4349b9189f6edf16bd8b5d7367429a6d052'
SELECTED = ('lines', 'rejection', 'targets', 'interrupted', 'incomplete')


class DiagnosticFailure(Exception):
    status = None


def require(condition, reason):
    if not condition:
        raise DiagnosticFailure(reason)


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def inventory(files, folders):
    paths = files + [path for folder in folders for path in folder.rglob('*') if path.is_file()]
    return {str(path): digest(path) for path in sorted(set(paths))}


def argv(entry):
    require(isinstance(entry.get('arguments'), list), 'recorded argv unavailable')
    return entry['arguments'][:]


def one(entries):
    require(len(entries) == 1, 'ambiguous recorded command')
    return entries[0]


def run(command, root, log, env=None, phase='diagnostic command'):
    with log.open('wb') as stream:
        result = subprocess.run(command, cwd=root, env=env, stdout=stream, stderr=stream)
    if result.returncode:
        error = DiagnosticFailure(phase + ' failed')
        error.status = result.returncode
        raise error


def mark(report, stage):
    report['stage'] = stage
    print(json.dumps({'event': 'diagnostic-stage', 'stage': stage}), flush=True)


def main(report=None):
    report = report if report is not None else initial_report()
    mark(report, 'source-identity')
    root = Path(sys.argv[1]).resolve()
    output = Path(sys.argv[2]).resolve()
    require(subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip() == SOURCE,
            'source identity differs')
    require(not subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=normal'], cwd=root),
            'source is not clean')
    source = root / 'test/tools/native_build.c'
    require(digest(source) == CANDIDATE_HASH, 'candidate source differs')
    baseline = subprocess.check_output(['git', 'show', BASE + ':test/tools/native_build.c'], cwd=root)
    require(hashlib.sha256(baseline).hexdigest() == BASE_HASH, 'baseline source differs')
    mark(report, 'command-records')
    compiler = one([entry for entry in json.loads((root / 'build/compile_commands.json').read_text())
                    if entry['file'] == str(source) and '/objects/test_tools/' in entry['output']])
    linker = one([entry for entry in json.loads((root / 'build/link_commands.json').read_text())
                  if Path(entry['output']).name == 'test_tools'])
    compile_args, link_args = argv(compiler), argv(linker)
    require('-UNDEBUG' in compile_args and '-DCHIBI_TEST_NATIVE_RUNTIME=1' in compile_args and
            [arg for arg in compile_args if arg.startswith('-O')] == ['-O1'] and
            '-g' in compile_args and '-fno-omit-frame-pointer' in compile_args and
            not any(arg.startswith(('-DNDEBUG', '-fsanitize=', '@')) for arg in compile_args + link_args),
            'unexpected profile or response file')
    original_object = compiler['output']
    require(link_args.count(original_object) == 1, 'baseline object not unique')
    candidate = Path(linker['output'])
    launcher = root / 'build-release/sdk/chibi'
    files = [Path(arg) for arg in link_args if Path(arg).is_absolute() and Path(arg).is_file()]
    files += [candidate, source, root / 'build/compile_commands.json', root / 'build/link_commands.json']
    folders = [root / name for name in ('build-release/sdk', 'build-release/share/chibi')]
    mark(report, 'shared-input-snapshot')
    before = inventory(files, folders)
    report.update(shared_file_count=len(before),
                  shared_manifest_sha256=hashlib.sha256(json.dumps(before, sort_keys=True).encode()).hexdigest())
    mark(report, 'tracing-preflight')
    tracer = shutil.which('strace')
    require(tracer is not None, 'normal tracing unavailable')
    summaries = {}
    report['results'] = summaries
    with tempfile.TemporaryDirectory(prefix='emitter-diagnostic-', dir=os.environ['RUNNER_TEMP']) as owned:
        scratch = Path(owned)
        run([tracer, '-f', '-e', 'trace=execve,execveat', '-o', str(scratch / 'probe'), '/bin/true'],
            root, scratch / 'probe.log', phase='normal tracing preflight')
        baseline_source, baseline_object, baseline_binary = [scratch / name for name in ('baseline.c', 'baseline.o', 'test_tools')]
        baseline_source.write_bytes(baseline)
        require(compile_args[compile_args.index('-c') + 1] == str(source) and
                compile_args[compile_args.index('-o') + 1] == original_object, 'unexpected compile shape')
        compile_args[compile_args.index('-c') + 1] = str(baseline_source)
        compile_args[compile_args.index('-o') + 1] = str(baseline_object)
        for flag in ('-MF', '-MT', '-MQ'):
            if flag in compile_args:
                compile_args[compile_args.index(flag) + 1] = str(scratch / ('baseline' + flag))
        mark(report, 'baseline-compile')
        run(compile_args, root, scratch / 'compile.log', phase='baseline object compile')
        link_args[link_args.index(original_object)] = str(baseline_object)
        link_args[link_args.index('-o') + 1] = str(baseline_binary)
        mark(report, 'baseline-link')
        run(link_args, root, scratch / 'link.log', phase='baseline relink')
        mark(report, 'post-link-identity')
        require(inventory(files, folders) == before, 'shared inputs changed during baseline relink')
        binary_hashes = {'baseline': digest(baseline_binary), 'candidate': digest(candidate)}
        report['test_binary_sha256'] = binary_hashes
        for role, binary, expected in (('baseline', baseline_binary, (10, 130)), ('candidate', candidate, (11, 146))):
            trace, log = scratch / (role + '.trace'), scratch / (role + '.log')
            bundle = scratch / (role + '-bundle')
            bundle.mkdir(mode=0o700)
            env = dict(os.environ, CHIBI_LLVM_BIN='/usr/lib/llvm-18/bin', CHIBI_NATIVE_BUNDLE=str(bundle))
            env.pop('CHIBI_TEST_TIMINGS', None)
            env.pop('CHIBI_TEST_NATIVE_BUILD_TIMINGS', None)
            command = [sys.executable, 'tools/test_policy.py', 'native', tracer, '-f', '-s', '4096',
                       '-e', 'trace=execve,execveat', '-o', str(trace), str(binary), '--native-build', str(launcher)]
            mark(report, role + '-suite')
            run(command, root, log, env, phase=role + ' full traced suite')
            summaries[role] = {'exit_status': 0, 'emitter_launches': None}
            mark(report, role + '-summary')
            text = log.read_text()
            require(all(re.fullmatch(r'strace: Process [0-9]+ attached', line)
                        for line in text.splitlines() if 'strace:' in line),
                    'tracer reported an error or unexpected detachment')
            reports = re.findall(r'Summary: Passed: (\d+) Failed: 0 Total: (\d+) Suites: 0 Asserts: (\d+)', text)
            summaries[role]['summary_rows'] = len(reports)
            summaries[role]['observed_summary'] = [[int(value) for value in row]
                                                   for row in reports[:2] if all(len(value) < 7 for value in row)]
            require(reports == [(str(expected[0]), str(expected[0]), str(expected[1]))], 'full suite assertions differ')
            require('unexecuted' not in text and 'needs the native runtime test build' not in text,
                    'native coverage skipped')
            summaries[role].update(cases=expected[0], assertions=expected[1])
            mark(report, role + '-trace')
            summaries[role]['emitter_launches'] = parse_counts(trace.read_text())
            mark(report, role + '-identity')
            require(inventory(files, folders) == before, 'shared inputs changed during traced suite')
            require(binary_hashes == {'baseline': digest(baseline_binary), 'candidate': digest(candidate)},
                    'test binary changed during traced suite')
        mark(report, 'count-oracles')
        a, b = [summaries[role]['emitter_launches'] for role in ('baseline', 'candidate')]
        require(all(a[name] == 1 and b[name] == 0 for name in SELECTED), 'selected compile counts differ')
        require(a['emitter-seed'] == 0 and b['emitter-seed'] == 1, 'seed compile counts differ')
        require(all(a[name] == b[name] for name in a if name not in (*SELECTED, 'emitter-seed')),
                'cold control compile counts differ')
        require(all(a[name] > 0 for name in ('cache', 'tools', 'llvm', 'cross', 'other')),
                'cold control compilation unobserved')
        require(sum(a.values()) - sum(b.values()) == 4, 'net compile count differs')
    report.update(status='success', evidence_valid=True, reason=None, shared_inputs_unchanged=True)
    mark(report, 'complete')
    output.write_text(json.dumps(report, sort_keys=True, indent=2) + '\n')
    print(json.dumps(report, sort_keys=True))


def initial_report():
    return {'phase': 'traced-counts-only-not-performance', 'status': 'failure',
            'evidence_valid': False, 'stage': 'starting', 'reason': None,
            'source_commit': SOURCE, 'baseline_commit': BASE,
            'baseline_source_sha256': BASE_HASH, 'candidate_source_sha256': CANDIDATE_HASH,
            'shared_inputs_unchanged': False,
            'expected': {'baseline': {'cases': 10, 'assertions': 130, 'exit_status': 0},
                         'candidate': {'cases': 11, 'assertions': 146, 'exit_status': 0},
                         'selected_launches': {'baseline': 1, 'candidate': 0},
                         'seed_launches': {'baseline': 0, 'candidate': 1}, 'net_fewer_launches': 4},
            'results': {}}


def entry():
    report = initial_report()
    try:
        main(report)
        require(report['status'] == 'success' and report['evidence_valid'] is True, 'incomplete evidence')
        return 0
    except Exception as error:
        reason = (error.reason if isinstance(error, TraceError) else
                  'command_nonzero' if isinstance(error, DiagnosticFailure) and error.status is not None else
                  {'normal tracing unavailable': 'tracing_unavailable',
                   'tracer reported an error or unexpected detachment': 'tracer_diagnostic',
                   'full suite assertions differ': 'suite_assertions_mismatch',
                   'native coverage skipped': 'coverage_skipped',
                   'selected compile counts differ': 'selected_counts_mismatch',
                   'seed compile counts differ': 'seed_counts_mismatch',
                   'cold control compile counts differ': 'cold_counts_mismatch',
                   'cold control compilation unobserved': 'cold_counts_missing',
                   'net compile count differs': 'net_count_mismatch'}.get(str(error), 'guard_rejected')
                  if isinstance(error, DiagnosticFailure) else
                  'io_error' if isinstance(error, OSError) else
                  'malformed_evidence' if isinstance(error, ValueError) else
                  'unexpected_record_shape' if isinstance(error, (KeyError, IndexError)) else
                  'child_setup_error' if isinstance(error, subprocess.SubprocessError) else 'unexpected_error')
        report.update(status='failure', evidence_valid=False, reason=reason, shared_inputs_unchanged=False)
        if (isinstance(error, DiagnosticFailure) and isinstance(error.status, int) and
                -(2 ** 31) <= error.status < 2 ** 31):
            report['observed_command_status'] = error.status
        try:
            Path(sys.argv[2]).write_text(json.dumps(report, sort_keys=True, indent=2) + '\n')
        except OSError:
            report['reason'] = 'summary_write_error'
        print(json.dumps(report, sort_keys=True), flush=True)
        return 1


if __name__ == '__main__':
    raise SystemExit(entry())
