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

from native_emitter_counts import parse_counts

SOURCE = '88c4f9564dbad4e6ae239d5de1f4ffbdde715504'
BASE = '907b1fe02c5cf09fe7d0b5b76f3472f0dc9f8c05'
BASE_HASH = '5cf81e058c0dfa28b7950a10e66642e1de35d8f626b7631357f2d73928e354a9'
CANDIDATE_HASH = 'df93c501a2e48cf75f6e70f4d6dcc4349b9189f6edf16bd8b5d7367429a6d052'
SELECTED = ('lines', 'rejection', 'targets', 'interrupted', 'incomplete')


class DiagnosticFailure(Exception):
    pass


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
    require(result.returncode == 0, phase + ' failed')


def main():
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
    before = inventory(files, folders)
    tracer = shutil.which('strace')
    require(tracer is not None, 'normal tracing unavailable')
    summaries = {}
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
        run(compile_args, root, scratch / 'compile.log', phase='baseline object compile')
        link_args[link_args.index(original_object)] = str(baseline_object)
        link_args[link_args.index('-o') + 1] = str(baseline_binary)
        run(link_args, root, scratch / 'link.log', phase='baseline relink')
        require(inventory(files, folders) == before, 'shared inputs changed during baseline relink')
        binary_hashes = {'baseline': digest(baseline_binary), 'candidate': digest(candidate)}
        for role, binary, expected in (('baseline', baseline_binary, (10, 130)), ('candidate', candidate, (11, 146))):
            trace, log = scratch / (role + '.trace'), scratch / (role + '.log')
            bundle = scratch / (role + '-bundle')
            bundle.mkdir(mode=0o700)
            env = dict(os.environ, CHIBI_LLVM_BIN='/usr/lib/llvm-18/bin', CHIBI_NATIVE_BUNDLE=str(bundle))
            env.pop('CHIBI_TEST_TIMINGS', None)
            env.pop('CHIBI_TEST_NATIVE_BUILD_TIMINGS', None)
            command = [sys.executable, 'tools/test_policy.py', 'native', tracer, '-f', '-s', '4096',
                       '-e', 'trace=execve,execveat', '-o', str(trace), str(binary), '--native-build', str(launcher)]
            run(command, root, log, env, phase=role + ' full traced suite')
            text = log.read_text()
            require(all(re.fullmatch(r'strace: Process [0-9]+ attached', line)
                        for line in text.splitlines() if 'strace:' in line),
                    'tracer reported an error or unexpected detachment')
            reports = re.findall(r'Summary: Passed: (\d+) Failed: 0 Total: (\d+) Suites: 0 Asserts: (\d+)', text)
            require(reports == [(str(expected[0]), str(expected[0]), str(expected[1]))], 'full suite assertions differ')
            require('unexecuted' not in text and 'needs the native runtime test build' not in text,
                    'native coverage skipped')
            summaries[role] = {'cases': expected[0], 'assertions': expected[1], 'emitter_compiles': parse_counts(trace.read_text())}
            require(inventory(files, folders) == before, 'shared inputs changed during traced suite')
            require(binary_hashes == {'baseline': digest(baseline_binary), 'candidate': digest(candidate)},
                    'test binary changed during traced suite')
        a, b = [summaries[role]['emitter_compiles'] for role in ('baseline', 'candidate')]
        require(all(a[name] == 1 and b[name] == 0 for name in SELECTED), 'selected compile counts differ')
        require(a['emitter-seed'] == 0 and b['emitter-seed'] == 1, 'seed compile counts differ')
        require(all(a[name] == b[name] for name in a if name not in (*SELECTED, 'emitter-seed')),
                'cold control compile counts differ')
        require(all(a[name] > 0 for name in ('cache', 'tools', 'llvm', 'cross', 'other')),
                'cold control compilation unobserved')
        require(sum(a.values()) - sum(b.values()) == 4, 'net compile count differs')
    report = {'phase': 'traced-counts-only-not-performance', 'source_commit': SOURCE,
              'baseline_commit': BASE, 'baseline_source_sha256': BASE_HASH,
              'candidate_source_sha256': CANDIDATE_HASH, 'shared_inputs_unchanged': True,
              'shared_file_count': len(before),
              'shared_manifest_sha256': hashlib.sha256(json.dumps(before, sort_keys=True).encode()).hexdigest(),
              'test_binary_sha256': binary_hashes,
              'results': summaries}
    output.write_text(json.dumps(report, sort_keys=True, indent=2) + '\n')
    print(json.dumps(report, sort_keys=True))


if __name__ == '__main__':
    try:
        main()
    except DiagnosticFailure as error:
        print('Emitter diagnostic stopped: ' + str(error) + '.', file=sys.stderr)
        raise SystemExit(1)
    except (OSError, ValueError, KeyError, IndexError, subprocess.SubprocessError):
        print('Emitter diagnostic failed closed; no raw trace or command details published.', file=sys.stderr)
        raise SystemExit(1)
