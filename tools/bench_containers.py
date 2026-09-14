#!/usr/bin/env python3
"""Compare automatic Set indexing with the compiled flat lookup on identical inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import statistics
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--trials', type=int, default=5)
p.add_argument('--repeats', type=int, default=10000)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
if a.trials < 3 or a.repeats < 1000:
    p.error('use at least 3 trials and 1000 repeats')
root = Path(__file__).resolve().parent.parent
binary = root / 'build-performance/test_suite'
cache = (binary.parent / 'CMakeCache.txt').read_text()
if 'CHIBI_ENABLE_SANITIZERS:BOOL=OFF' not in cache or not re.search(r'^CMAKE_BUILD_TYPE:STRING=(Release|RelWithDebInfo)$', cache, re.M):
    p.error('use an optimized build without sanitizers')
compiler = re.search(r'^CMAKE_C_COMPILER:[^=]+=(.+)$', cache, re.M).group(1)
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
binary_hash = digest(binary)
samples, builds, makes = [], [], []
with tempfile.TemporaryDirectory(prefix='container-code-') as temp:
    source, library = Path(temp) / 'set.c', Path(temp) / 'set.so'
    subprocess.run([binary, '--emit-container-c', source], cwd=root, check=True)
    source_hash = digest(source)
    subprocess.run([compiler, '-std=c23', '-O3', '-ffp-contract=off', '-shared', '-fPIC', '-I'+str(root/'src'), source, '-o', library], check=True)
    for trial in range(a.trials):
        output = subprocess.check_output([binary, '--bench-containers', library, str(a.repeats), str(trial % 6)], cwd=root, text=True)
        expected = {(kind, size, backend, storage) for kind in (0, 1) for size in (8, 64, 4096, 20000) for backend in ('SLJIT', 'C', 'BYTECODE') for storage in ('flat', 'auto')}
        for kind, size, backend, storage, ns, count, checksum in re.findall(r'CONTAINER kind=(\d+) size=(\d+) backend=(\w+) storage=(\w+) ns=([\d.]+) iterations=(\d+) checksum=(\d+)', output):
            key = (int(kind), int(size), backend, storage)
            if key not in expected or int(checksum) != (int(count) + 1) // 2:
                raise ValueError(output)
            expected.remove(key)
            samples.append(dict(trial=trial, kind=int(kind), size=int(size), backend=backend, storage=storage, ns=float(ns), iterations=int(count)))
        if expected:
            raise ValueError(f'missing measurements: {expected}')
        for kind, size, us, size_bytes in re.findall(r'INDEX kind=(\d+) size=(\d+) build_us=([\d.]+) bytes=(\d+)', output):
            builds.append(dict(trial=trial, kind=int(kind), size=int(size), us=float(us), bytes=int(size_bytes)))
        for kind, size, storage, us, checksum in re.findall(r'MAKE kind=(\d+) size=(\d+) storage=(\w+) us=([\d.]+) checksum=(\d+)', output):
            if int(checksum) != int(size) * (1000 if int(size) <= 64 else 8):
                raise ValueError(output)
            makes.append(dict(trial=trial, kind=int(kind), size=int(size), storage=storage, us=float(us)))
        print(f'trial {trial+1} passed', flush=True)
if digest(binary) != binary_hash:
    raise ValueError('benchmark executable changed')
summary = {}
for kind in (0, 1):
    for size in (8, 64, 4096, 20000):
        summary[f'{kind}/{size}'] = {f'{backend}/{storage}': statistics.median(s['ns'] for s in samples if (s['kind'], s['size'], s['backend'], s['storage']) == (kind, size, backend, storage)) for backend in ('SLJIT', 'C', 'BYTECODE') for storage in ('flat', 'auto')}
a.output.write_text(json.dumps(dict(binary_sha256=binary_hash, generated_c_sha256=source_hash, compiler=subprocess.check_output([compiler, '--version'], text=True), summary_ns=summary, samples=samples, builds=builds, construction=makes, methodology='Same lowered Set lookup, borrowed flat versus immutable indexed input. Last-element hit and miss alternate. Construction excluded from lookup timing and reported separately. Bytecode uses fewer repeats for large collections.'), indent=2)+'\n')
print(json.dumps(summary, indent=2))
