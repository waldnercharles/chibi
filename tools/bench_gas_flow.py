#!/usr/bin/env python3
"""Compare gas pressure batching and workers through the stable fluid API."""
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


def gas_dependencies(build, cache, threaded):
    package = re.search(r'^CHIBI_DEPENDENCY_PACKAGE:[^=]+=(.+)$', cache, re.M)
    dependency_build = Path(package[1]).parent if package else build
    deps = [build / 'libruntime_support.a', build / 'libcore.a']
    for name in ('chibi_thirdparty', 'ckdl', 'chibi_sljit'):
        library = dependency_build / f'lib{name}.a'
        if library.is_file():
            deps.append(library)
    if threaded:
        deps.append(dependency_build / '_deps/enkits-build/libenkiTS.a')
    return deps


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before-build', type=Path, required=True)
    parser.add_argument('--after-build', type=Path, default=Path('build-release'))
    parser.add_argument('--sizes', type=int, nargs='+', default=[64, 128, 256])
    parser.add_argument('--frames', type=int, default=30)
    parser.add_argument('--trials', type=int, default=3)
    parser.add_argument('--workers', type=int, default=8)
    parser.add_argument('--web', action='store_true',
                        help='compare serial and worker Emscripten builds under Node')
    parser.add_argument('--compare-scalar', action='store_true',
                        help='also compile the candidate pressure kernel without SIMD')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.trials < 3 or args.frames < 10 or not 2 <= args.workers <= 32:
        parser.error('use at least three trials, ten frames and 2..32 workers')
    if any(not 16 <= side <= 256 for side in args.sizes):
        parser.error('use grid sides 16..256')
    root = Path(__file__).resolve().parent.parent
    builds = {'before': args.before_build.resolve(), 'after': args.after_build.resolve()}
    if args.compare_scalar:
        builds['after_scalar'] = builds['after']
    source = root / 'test/gas_flow_benchmark.c'
    records, checksums, libraries, compilers, binary_hashes = [], {}, {}, {}, {}
    wasm_hashes = {}
    expected_workers = {}
    source_hash = digest(source)
    flow_hash = digest(root / 'src/runtime/material_flow.c')
    pattern = re.compile(r'gas side=(\d+) frames=(\d+) workers=(\d+) flow_ms=([\d.]+) tick_ms=([\d.]+) hash=(\d+)')
    with tempfile.TemporaryDirectory(prefix='gas-flow-') as temp:
        binaries = {}
        for version, build in builds.items():
            cache = (build / 'CMakeCache.txt').read_text()
            if ((not args.web and 'CHIBI_ENABLE_SANITIZERS:BOOL=OFF' not in cache) or
                    not re.search(r'^CMAKE_BUILD_TYPE:STRING=(Release|RelWithDebInfo)$', cache, re.M)):
                parser.error('use optimized builds without sanitizers')
            if args.web and any('-fsanitize' in (build / f'CMakeFiles/{target}.dir/flags.make').read_text()
                                for target in ('core', 'runtime_support')):
                parser.error('web libraries must be built without sanitizers')
            compiler_match = re.search(r'^CMAKE_C_COMPILER:[^=]+=(.+)$', cache, re.M)
            if compiler_match:
                compiler = compiler_match.group(1)
            else:
                descriptions = list(build.glob('CMakeFiles/*/CMakeCCompiler.cmake'))
                if not descriptions:
                    parser.error('build does not record its C compiler')
                description = max(descriptions, key=lambda path: path.stat().st_mtime).read_text()
                compiler = re.search(r'set\(CMAKE_C_COMPILER "([^"]+)"\)', description).group(1)
            compilers[version] = subprocess.check_output([compiler, '--version'], text=True).strip()
            threaded = args.web and 'CHIBI_WEB_THREADS:BOOL=ON' in cache
            deps = gas_dependencies(build, cache, not args.web or threaded)
            flags = []
            if args.web:
                flags = ['-D_POSIX_C_SOURCE=200809L', '-sENVIRONMENT=node', '-sALLOW_MEMORY_GROWTH=1',
                         '-sINITIAL_MEMORY=67108864', '-sEXIT_RUNTIME=1']
                if threaded:
                    pool = int(re.search(r'^CHIBI_WEB_WORKERS:[^=]+=(\d+)$', cache, re.M).group(1))
                    if args.workers != pool:
                        parser.error('web worker count must match the configured pool budget')
                    flags += ['-pthread', '-msimd128', f'-sPTHREAD_POOL_SIZE={pool - 1}']
            expected_workers[version] = args.workers if not args.web or threaded else 1
            libraries[version] = {os.path.relpath(path, build): digest(path) for path in deps}
            binary = Path(temp) / (version + ('.js' if args.web else ''))
            scalar = []
            if version == 'after_scalar':
                object_file = Path(temp) / 'scalar.o'
                subprocess.run([compiler, '-std=c23', '-O3', '-DNDEBUG', '-ffp-contract=off',
                                *(['-pthread', '-msimd128'] if threaded else []),
                                '-DCHIBI_FLOW_SCALAR', '-I' + str(root / 'src'),
                                '-c', str(root / 'src/runtime/material_flow.c'), '-o', str(object_file)], check=True)
                scalar = [str(object_file)]
            subprocess.run([compiler, '-std=c23', '-O3', '-ffp-contract=off',
                            *flags,
                            '-I' + str(root / 'src'), str(source), *scalar, *map(str, deps),
                            '-lc++', *(['-lc++abi'] if args.web else []),
                            '-lm', '-lpthread', '-o', str(binary)], check=True)
            binaries[version] = binary
            binary_hashes[version] = digest(binary)
            if args.web:
                wasm_hashes[version] = digest(binary.with_suffix('.wasm'))
        for trial in range(args.trials):
            order = list(builds) if trial % 2 else list(reversed(builds))
            for side in args.sizes:
                for version in order:
                    for workers in ([1, args.workers] if trial % 2 else [args.workers, 1]):
                        command = (['node'] if args.web else []) + [str(binaries[version])]
                        log = subprocess.check_output([*command, str(side), str(args.frames), str(workers)],
                                                      text=True, env=dict(os.environ, MallocNanoZone='0'))
                        match = pattern.fullmatch(log.strip())
                        if not match:
                            raise SystemExit('FAIL: incomplete gas benchmark output: ' + log)
                        size, frames, used, flow, tick, checksum = match.groups()
                        actual = 1 if workers == 1 else expected_workers[version]
                        if int(size) != side or int(frames) != args.frames or int(used) != actual:
                            raise SystemExit('FAIL: workload/worker mismatch')
                        if checksums.setdefault(side, checksum) != checksum:
                            raise SystemExit('FAIL: serial/parallel or baseline/candidate state mismatch')
                        records.append(dict(trial=trial, version=version, side=side, workers=workers,
                                            actual_workers=int(used),
                                            flow_ms=float(flow), tick_ms=float(tick), checksum=checksum))
                        print(version + ' ' + log.strip(), flush=True)
        if binary_hashes != {version: digest(binary) for version, binary in binaries.items()}:
            raise SystemExit('FAIL: a benchmark executable changed during measurement')
        if args.web and wasm_hashes != {
                version: digest(binary.with_suffix('.wasm')) for version, binary in binaries.items()}:
            raise SystemExit('FAIL: a Wasm benchmark changed during measurement')
    if source_hash != digest(source) or flow_hash != digest(root / 'src/runtime/material_flow.c'):
        raise SystemExit('FAIL: benchmark or pressure source changed during measurement')
    for version, build in builds.items():
        if any(digest(build / path) != checksum for path, checksum in libraries[version].items()):
            raise SystemExit('FAIL: a linked library changed during measurement')
    summary = {}
    for side in args.sizes:
        summary[str(side)] = {}
        for version in builds:
            for workers in (1, args.workers):
                selected = [r for r in records if r['side'] == side and r['version'] == version and r['workers'] == workers]
                summary[str(side)][f'{version}/{workers}'] = {
                    key: {'median': statistics.median(r[key] for r in selected),
                          'min': min(r[key] for r in selected), 'max': max(r[key] for r in selected)}
                    for key in ('flow_ms', 'tick_ms')}
    args.output.write_text(json.dumps(dict(source_sha256=source_hash, flow_source_sha256=flow_hash,
                                          libraries=libraries, binary_sha256=binary_hashes, compilers=compilers,
                                          wasm_sha256=wasm_hashes,
                                          node=subprocess.check_output(['node', '--version'], text=True).strip() if args.web else None,
                                          execution='Node WebAssembly' if args.web else 'native',
                                          frames=args.frames, samples=records, summary=summary), indent=2) + '\n')


if __name__ == '__main__':
    main()
