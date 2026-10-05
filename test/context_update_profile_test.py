#!/usr/bin/env python3
"""CPU-only integration check of the opt-in Context breakdown on kit-documents."""
import argparse
import gzip
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary', type=Path)
parser.add_argument('output', type=Path, help='New evidence directory')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=False)
parts = ['model_update_seconds', 'root_update_seconds',
         'layout_position_seconds', 'release_documents_seconds']
result = dict(source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
              binary_sha256=hashlib.sha256(args.binary.read_bytes()).hexdigest(),
              command=[str(args.binary), '--kit-documents'], timeout_seconds=180, runs=[])
# Fresh processes, equal existing coverage, interleaved instrumentation modes.
for run, enabled in enumerate(['1', '0', '1', '0', '1', '0'], 1):
    env = dict(os.environ, CHIBI_EXECUTION_PROFILE='hardware',
               CHIBI_ALLOW_FULL_GPU='1', CHIBI_IFRAME_PROFILE=enabled)
    started = time.monotonic()
    p = subprocess.run(result['command'], env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True, timeout=180)
    elapsed = time.monotonic() - started
    with gzip.open(args.output / f'run-{run}.log.gz', 'wt') as log:
        log.write(p.stdout)
    assert p.returncode == 0, p.stdout[-4000:]
    assert p.stdout.count('PASS:') == 10
    rows = [json.loads(line.split(' ', 1)[1]) for line in p.stdout.splitlines()
            if line.startswith('IFRAME_PROFILE ')]
    summary = dict(run=run, enabled=enabled == '1', exit_code=p.returncode,
                   checks=10, wall_seconds=elapsed)
    if enabled == '0':
        assert not rows and 'IFRAME_BACKEND ' not in p.stdout
    else:
        assert len(rows) == 49 * 4
        counts = []
        for phase in ['pre', 'context', 'post']:
            group = [row for row in rows if row['phase'] == phase]
            counts.append([sum(row['holder_updates'] for row in group),
                           sum(probe['updates'] for row in group for probe in row['probes']),
                           sum(probe['layouts'] for row in group for probe in row['probes']),
                           sum(row['boxes'] for row in group),
                           sum(row['heights'] for row in group),
                           sum(row['passes'] for row in group)])
        assert counts == [[49, 55, 20, 5, 1, 52], [0, 196, 18, 0, 0, 0],
                          [49, 49, 0, 0, 0, 49]], counts
        context = [row for row in rows if row['phase'] == 'context']
        for row in context:
            assert row['context_calls'] == row['calls'] == 1
            assert all(math.isfinite(row[key]) and row[key] >= 0 for key in parts)
            subtotal = sum(row[key] for key in parts)
            # Printed durations round independently to nanoseconds.
            assert subtotal <= row['seconds'] + 5e-9, row
            assert row['other_context_seconds'] >= -5e-9, row
            assert abs(subtotal + row['other_context_seconds'] - row['seconds']) <= 5e-9, row
        assert all('context_calls' not in row for row in rows if row['phase'] != 'context')
        backend = [json.loads(line.split(' ', 1)[1]) for line in p.stdout.splitlines()
                   if line.startswith('IFRAME_BACKEND ')][0]
        assert backend == dict(frames=49, context_updates=49, render_captures=0,
                               render_passes=0, geometry_compiles=0), backend
        summary.update(frames=49, counts=counts, backend=backend,
                       context_seconds=sum(row['seconds'] for row in context),
                       **{key: sum(row[key] for row in context)
                          for key in parts + ['other_context_seconds']})
    result['runs'].append(summary)
    print(f'PASS: run {run}, profile={enabled}, 10 checks, {elapsed:.3f}s', flush=True)
(args.output / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
