#!/usr/bin/env python3
"""One controlled Debug pair: optimization or instrumentation; opt-in, not full CI."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import signal
import subprocess
import sys
import time

import test_policy

ROOT = Path(__file__).resolve().parents[1]
SELECTORS = ('language-package-compiler-chibi-scalar',
             'language-package-compiler-chibi-behaviors-rest',
             'language-asset-compiler-host', 'compiler-bootstrap')
# Setup plus this 35-minute supervised budget fits the existing 45-minute job.
BUDGET_SECONDS = 35 * 60
REPRESENTATIVES = {'engine': ('chibi_engine', 'src/runtime/engine.c'),
                   'sdk': ('chibi_compiler', 'src/language/lower.c'),
                   'tools': ('test_tools', 'test/tools/main.c'),
                   'language': ('test_language', 'test/language/main.c')}


def capture(*command):
    return subprocess.check_output(command, cwd=ROOT, text=True, timeout=10).strip()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def flags(arguments, level, assertions=False, sanitizers=True):
    require([arg for arg in arguments if arg.startswith('-O')] == [f'-O{level}'],
            'unexpected optimization flags')
    require('-g' in arguments and '-g0' not in arguments, 'debug information missing')
    instrumentation = [arg for arg in arguments if arg.startswith(('-fsanitize=', '-fno-sanitize='))]
    require(instrumentation == (['-fsanitize=address,undefined'] if sanitizers else []),
            'unexpected sanitizer instrumentation')
    require('-fno-omit-frame-pointer' in arguments and '-fomit-frame-pointer' not in arguments,
            'frame pointers missing or disabled')
    require(not any(arg.startswith('-DNDEBUG') or arg == 'NDEBUG' for arg in arguments),
            'NDEBUG must not be defined')
    require(not assertions or '-UNDEBUG' in arguments, 'test assertions flag missing')


def without_dependencies(arguments):
    result = []
    iterator = iter(arguments)
    for arg in iterator:
        if arg in ('-MF', '-MT', '-MQ'):
            next(iterator)
        elif arg not in ('-MD', '-MMD'):
            result.append(arg)
    return result


def verify(build, evidence, level, sanitizers=True, label=None):
    label = label or f'o{level}'
    header = build / 'test/tools/testing/include/export_compiler.h'
    for path in (build / 'compile_commands.json', build / 'CMakeCache.txt', header):
        if path.is_file():
            shutil.copyfile(path, evidence / f'{label}-{path.name}')
    database = json.loads((build / 'compile_commands.json').read_text())
    # All translation units retain the intended optimization/instrumentation and assertions.
    for entry in database:
        flags(shlex.split(entry['command']), level, sanitizers=sanitizers)
    actual = []
    for line in (evidence / f'{label}-build.log').read_text().splitlines():
        try:
            actual.append(shlex.split(line))
        except ValueError:
            pass  # Diagnostics are not shell command lines.
    verified = {}
    for name, (target, source) in REPRESENTATIVES.items():
        entries = [entry for entry in database if entry['file'].endswith('/' + source)
                   and f'CMakeFiles/{target}.dir/' in entry.get('command', '')]
        require(len(entries) == 1, f'{name}: missing or ambiguous compile database entry')
        arguments = shlex.split(entries[0]['command'])
        flags(arguments, level, name in ('tools', 'language'), sanitizers)
        expected = without_dependencies(arguments)
        require(any(expected == without_dependencies(row[row.index(arguments[0]):])
                    for row in actual if arguments[0] in row),
                f'{name}: compile database argv not found in actual verbose build log')
        verified[name] = arguments
    exported = [json.loads(line.strip().removesuffix(','))
                for line in header.read_text().splitlines() if line.strip().startswith('"')]
    flags(exported, level, assertions=True, sanitizers=sanitizers)
    verified['export_compiler'] = exported
    compilers = {shlex.split(entry['command'])[0] for entry in database}
    links = [row[row.index(compiler):] for row in actual for compiler in compilers
             if compiler in row and '-c' not in row and '-o' in row]
    require(links, 'actual linker commands missing')
    for arguments in links:
        flags(arguments, level, sanitizers=sanitizers)
    verified['link_commands'] = links
    verified['compile_database_rows'] = len(database)
    (evidence / f'{label}-verified-argv.json').write_text(json.dumps(verified, indent=2) + '\n')
    return next(line.split('=', 1)[1] for line in (build / 'CMakeCache.txt').read_text().splitlines()
                if line.startswith('CHIBI_THIRD_PARTY_DIR:'))


def build_command(build):
    # This experiment fixes the generator to Unix Makefiles. Buffer each recipe's
    # verbose output so concurrent >PIPE_BUF compiler commands remain intact.
    return ['cmake', '--build', str(build), '--target', 'test_tools',
            'test_language', '--parallel', '4', '--verbose', '--', '--output-sync=target']


def run_phase(evidence, deadline, name, command, environment):
    begin = time.monotonic()
    require(deadline > begin, 'experiment budget exhausted')
    print(name, flush=True)
    result = None
    try:
        with (evidence / f'{name}.log').open('w') as log:
            result = test_policy.run_commands([command], seconds=deadline - begin,
                                             env=environment, output=log)
    except (KeyboardInterrupt, test_policy.TerminationRequested):
        result = 130
        raise
    finally:
        record = {'phase': name, 'argv': command, 'seconds': time.monotonic() - begin, 'exit': result}
        with (evidence / 'timings.jsonl').open('a') as timings:
            timings.write(json.dumps(record) + '\n')
    require(result == 0, f'{name} failed with exit {result}; retained evidence, no comparison conclusion')
    return record['seconds']


def configure_command(build, level, sanitizers):
    # Frame pointers are independent of instrumentation, including fixture exports.
    debug = f'-g -O{level} -fno-omit-frame-pointer'
    return ['sh', 'tools/configure_native.sh', str(build),
            '-G', 'Unix Makefiles', '-DCMAKE_BUILD_TYPE=Debug',
            f'-DCHIBI_ENABLE_SANITIZERS={"ON" if sanitizers else "OFF"}',
            '-DCHIBI_SANITIZERS=address,undefined', '-DCHIBI_ENABLE_WARNINGS=ON',
            '-DCHIBI_ENABLE_CCACHE=OFF', '-DCMAKE_C_COMPILER=clang-18', '-DCMAKE_CXX_COMPILER=clang++-18',
            '-DCMAKE_C_FLAGS=', '-DCMAKE_CXX_FLAGS=', f'-DCMAKE_C_FLAGS_DEBUG={debug}',
            f'-DCMAKE_CXX_FLAGS_DEBUG={debug}', '-DCMAKE_C_COMPILER_LAUNCHER=',
            '-DCMAKE_CXX_COMPILER_LAUNCHER=', '-DCMAKE_VERBOSE_MAKEFILE=ON',
            '-DLIBCLANG_ROOT=/usr/lib/llvm-18']


def arms(comparison, sanitizers):
    if comparison == 'instrumentation':
        return [('o1-on', 1, True), ('o1-off', 1, False)]
    return [('o1', 1, sanitizers), ('o2', 2, sanitizers)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sha', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sanitizers', choices=('on', 'off'), required=True)
    parser.add_argument('--comparison', choices=('optimization', 'instrumentation'),
                        default='optimization')
    args = parser.parse_args()
    sanitizers = args.sanitizers == 'on'
    selected = arms(args.comparison, sanitizers)
    labels = [label for label, _, _ in selected]
    require(platform.system() == 'Linux' and platform.machine() == 'x86_64', 'Linux x86-64 only')
    require(re.fullmatch('[0-9a-f]{40}', args.sha) is not None, 'an exact commit SHA is required')
    require(capture('git', 'rev-parse', 'HEAD') == args.sha, 'checkout SHA differs')
    require(not capture('git', 'status', '--porcelain', '--untracked-files=no'), 'tracked tree is dirty')
    evidence = args.output.resolve()
    evidence.mkdir(parents=True, exist_ok=False)  # Never reuse objects or overwrite evidence.
    environment = dict(os.environ, CC='clang-18', CXX='clang++-18', CCACHE_DISABLE='1',
                       UBSAN_OPTIONS='halt_on_error=1', MallocNanoZone='0', VERBOSE='1')
    for key in ('CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS', 'CMAKE_TOOLCHAIN_FILE',
                'CMAKE_GENERATOR', 'CMAKE_BUILD_PARALLEL_LEVEL', 'ASAN_OPTIONS', 'LSAN_OPTIONS'):
        environment.pop(key, None)
    metadata = {'sha': args.sha, 'platform': platform.platform(), 'cpu_count': os.cpu_count(),
                'cpu_model': next((line.split(':', 1)[1].strip() for line in Path('/proc/cpuinfo').read_text().splitlines()
                                   if line.startswith('model name')), 'unknown'),
                'memory_bytes': os.sysconf('SC_PHYS_PAGES') * os.sysconf('SC_PAGE_SIZE'),
                'ccache_available': shutil.which('ccache'),
                'compiler': capture('clang-18', '--version'), 'cxx': capture('clang++-18', '--version'),
                'cmake': capture('cmake', '--version'), 'jobs': 4, 'ccache': 'disabled in both arms',
                'selectors': SELECTORS, 'repetitions': 1, 'build_order': labels,
                'comparison': args.comparison,
                'arms': {label: {'optimization': level, 'sanitizers': enabled,
                                 'dependency_variant': 'address-undefined' if enabled else 'release'}
                         for label, level, enabled in selected},
                'fixed_sanitizers': args.sanitizers if args.comparison == 'optimization' else None,
                'runner': {key: os.environ.get(key) for key in
                           ('RUNNER_NAME', 'RUNNER_OS', 'ImageOS', 'ImageVersion',
                            'GITHUB_RUN_ID', 'GITHUB_RUN_ATTEMPT', 'JOB_STARTED_UTC')},
                'selector_pair_order': [labels if index % 2 == 0 else list(reversed(labels))
                                        for index in range(len(SELECTORS))],
                'archive_manifest_sha256': hashlib.sha256((ROOT / 'third_party/archives.tsv').read_bytes()).hexdigest(),
                'note': 'First configure may download pinned dependencies; configuration is separate from build/test timing.'}
    (evidence / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    shutil.copyfile(ROOT / 'third_party/archives.tsv', evidence / 'archives.tsv')
    started = time.monotonic()

    def run(name, command, env):
        return run_phase(evidence, started + BUDGET_SECONDS, name, command, env)

    builds = {label: evidence / f'build-{label}' for label in labels}
    # Configure both before either build: only immutable pinned dependency archives
    # are shared. The first download cost cannot masquerade as compiler/build cost.
    for label, level, enabled in selected:
        run(f'{label}-configure', configure_command(builds[label], level, enabled), environment)
    archives = {}
    totals = {}
    build_seconds = {}
    test_seconds = {label: 0 for label in labels}
    environments = {}
    for label, level, enabled in selected:
        build = builds[label]
        total = run(f'{label}-build', build_command(build), environment)
        archives[label] = verify(build, evidence, level, enabled, label)
        variant = metadata['arms'][label]['dependency_variant']
        pinned = [line.split()[3] for line in (ROOT / 'third_party/archives.tsv').read_text().splitlines()
                  if line.startswith(f'linux-x64\t{variant}\t')]
        require(len(pinned) == 1 and Path(archives[label]).name == pinned[0],
                'dependency archive differs from pinned profile')
        metadata['arms'][label]['dependency_archive_sha256'] = pinned[0]
        (evidence / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
        if args.comparison == 'optimization':
            require(len(set(archives.values())) == 1, 'optimization arms use different dependency archives')
        env = dict(environment, CHIBI_TOOLS_BUILD_DIR=str(build / 'test/tools'),
                   CHIBI_LANGUAGE_BUILD_DIR=str(build / 'test/language'),
                   CHIBI_BUILD_HOST=str(build / 'sdk/chibi-build-host'),
                   CHIBI_COMPILER_HOST=str(build / 'sdk/chibi-compiler-host'))
        for executable in ('test/tools/test_tools', 'test/language/test_language',
                           'sdk/chibi-build-host', 'sdk/chibi-compiler-host'):
            require(os.access(build / executable, os.X_OK), f'missing helper: {executable}')
        totals[label] = build_seconds[label] = total
        environments[label] = env
    # Pair each selector, alternating order; retain useful pairs if a later
    # selector fails. Both builds and flag verification finish before measurement.
    for index, selector in enumerate(SELECTORS):
        for label in (labels if index % 2 == 0 else list(reversed(labels))):
            elapsed = run(f'{label}-{selector}',
                [sys.executable, 'tools/test_policy.py', 'tool', selector], environments[label])
            totals[label] += elapsed
            test_seconds[label] += elapsed
    require(capture('git', 'rev-parse', 'HEAD') == args.sha and
            not capture('git', 'status', '--porcelain', '--untracked-files=no'), 'source changed during experiment')
    (evidence / 'summary.json').write_text(json.dumps({
        'build_plus_four_selectors_seconds': totals, 'build_seconds': build_seconds,
        'four_selectors_seconds': test_seconds, 'dependency_archives': archives,
        'comparison': args.comparison,
        'fixed_sanitizers': args.sanitizers if args.comparison == 'optimization' else None, 'supervised_seconds': time.monotonic() - started,
        'interpretation': 'One ordered within-runner pair, not a general speedup claim or full CI pass; configuration excluded. Instrumentation includes dependency profile changes. Comparisons across separate jobs remain hardware-confounded.'
    }, indent=2) + '\n')


def cli():
    signal.signal(signal.SIGTERM, test_policy.terminate)
    signal.signal(signal.SIGINT, test_policy.terminate)
    try:
        main()
    except (KeyboardInterrupt, test_policy.TerminationRequested):
        return 130
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print(error, file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(cli())
