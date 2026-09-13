#!/usr/bin/env python3
"""Compare the authored encounter reduction with its prior graph and generated C."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import tempfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', type=Path, required=True)
    parser.add_argument('--before-root', type=Path, required=True)
    parser.add_argument('--after', type=Path, default=Path('build-performance/test_suite'))
    parser.add_argument('--compiler', help='override the compiler from the after build cache')
    parser.add_argument('--counts', nargs='+', type=int, default=[1000, 5000, 20000])
    parser.add_argument('--trials', type=int, default=3)
    parser.add_argument('--repeats', type=int, default=1000)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.trials < 3 or args.repeats < 100 or any(not 0 <= n <= 20000 for n in args.counts):
        parser.error('use at least 3 trials, 100 repeats, and 0..20000 enemies')
    root = Path(__file__).resolve().parent.parent
    binaries = {'before': args.before.resolve(), 'after': args.after.resolve()}
    roots = {'before': args.before_root.resolve(), 'after': root}
    for binary in binaries.values():
        cache = (binary.parent / 'CMakeCache.txt').read_text()
        if 'CHIBI_ENABLE_SANITIZERS:BOOL=OFF' not in cache or not re.search(
                r'^CMAKE_BUILD_TYPE:STRING=(Release|RelWithDebInfo)$', cache, re.M):
            parser.error('use optimized builds without sanitizers')
    if not args.compiler:
        cache = (binaries['after'].parent / 'CMakeCache.txt').read_text()
        args.compiler = re.search(r'^CMAKE_C_COMPILER:[^=]+=(.+)$', cache, re.M).group(1)
    paths = dict(binaries)
    paths.update({v + '_asset': p / 'content/behaviors/ArenaWaveBehavior.json' for v, p in roots.items()})
    hashes = {name: digest(path) for name, path in paths.items()}
    env = dict(os.environ, MallocNanoZone='0')
    samples = []
    with tempfile.TemporaryDirectory(prefix='encounter-query-') as directory:
        source, library = Path(directory) / 'query.c', Path(directory) / 'query.so'
        subprocess.run([binaries['after'], '--emit-encounter-query-c', source], cwd=root, env=env, check=True)
        subprocess.run([args.compiler, '-std=c23', '-O3', '-ffp-contract=off', '-shared', '-fPIC',
                        '-I' + str(root / 'src'), source, '-o', library], check=True)
        hashes['generated_c'] = digest(source)
        for count in args.counts:
            for trial in range(args.trials):
                for version in (('before', 'after') if trial % 2 == 0 else ('after', 'before')):
                    # The original graph exceeds production's 10,000-step budget.
                    # Raise it only in the reference measurement, never in runtime.
                    budget = 1000000 if version == 'before' else 0
                    log = subprocess.check_output([binaries[version], '--encounter-query', str(count),
                        str(budget), str(args.repeats)], cwd=roots[version], env=env, text=True)
                    match = re.search(r'ms=([0-9.]+) backend=SLJIT oracle_calls=0', log)
                    if not match:
                        raise ValueError(log)
                    samples.append(dict(count=count, trial=trial, kind='activation', backend=version,
                                        ms=float(match[1]), log=log))
                env['CBLUEPRINT_BENCH_QUERY_ROTATION'] = str(trial % 3)
                log = subprocess.check_output([binaries['after'], '--encounter-query-code', library,
                    str(count), str(args.repeats // 10)], cwd=root, env=env, text=True)
                rows = re.findall(r'backend=(BYTECODE|SLJIT|C) entities=\d+ repeats=\d+ ms=([0-9.]+)', log)
                if len(rows) != 3:
                    raise ValueError(log)
                for backend, ms in rows:
                    samples.append(dict(count=count, trial=trial, kind='body', backend=backend, ms=float(ms), log=log))
                print(f'entities={count} trial={trial + 1} passed', flush=True)
    if any(digest(path) != hashes[name] for name, path in paths.items()):
        raise ValueError('binary or authored graph changed during measurement')
    summary = {str(count): {label: statistics.median(s['ms'] for s in samples
        if s['count'] == count and s['backend'] == label)
        for label in ('before', 'after', 'BYTECODE', 'SLJIT', 'C')} for count in args.counts}
    args.output.write_text(json.dumps(dict(summary_ms=summary, samples=samples, sha256=hashes,
        trials=args.trials, activation_repeats=args.repeats, body_repeats=args.repeats // 10,
        compiler=subprocess.check_output([args.compiler, '--version'], text=True),
        workload='One player and N live enemies; same authored counter results. Activation includes setup/free. '
                 'Body reuses compiled context and buffers; includes snapshot production. Rendering excluded.',
        before_root=str(roots['before'])), indent=2) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
