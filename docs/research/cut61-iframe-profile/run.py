#!/usr/bin/env python3
"""Reproduce the CUT-61 initial CPU profile; never run a GPU/Studio suite."""
import gzip
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()


def summarize(text):
    rows = [json.loads(line.split(' ', 1)[1]) for line in text.splitlines()
            if line.startswith('IFRAME_PROFILE ')]
    result = {}
    for phase in ['outside', 'pre', 'context', 'post']:
        group = [r for r in rows if r['phase'] == phase]
        result[phase] = {key: sum(row[key] for row in group) for key in
                         ['calls', 'seconds', 'holder_updates', 'holder_seconds',
                          'boxes', 'heights', 'passes']}
        for callback in ['updates', 'layouts']:
            result[phase]['probe_' + callback] = sum(
                p[callback] for row in group for p in row['probes'])
    stable = []
    for frame in sorted({r['frame'] for r in rows}):
        group = [r for r in rows if r['frame'] == frame]
        if not any(r['boxes'] or r['heights'] or
                   any(p['layouts'] for p in r['probes']) for r in group):
            stable.append(group)
    result['stable'] = dict(
        frames=len(stable),
        phase_seconds=sum(r['seconds'] for group in stable for r in group),
        post_holder_seconds=sum(r['holder_seconds'] for group in stable
                                for r in group if r['phase'] == 'post'))
    result['slowest_context'] = sorted(
        [dict(frame=r['frame'], seconds=r['seconds']) for r in rows
         if r['phase'] == 'context'], key=lambda r: r['seconds'], reverse=True)[:3]
    result['backend'] = [json.loads(line.split(' ', 1)[1])
                         for line in text.splitlines()
                         if line.startswith('IFRAME_BACKEND ')][0]
    result['passed_checks'] = sum(line.startswith('PASS:') for line in text.splitlines())
    return result


assert not git('diff', '--name-only', 'HEAD', '--', 'src', 'test'), 'Commit measured source first'
env = dict(os.environ, CHIBI_EXECUTION_PROFILE='hardware',
           CHIBI_ALLOW_FULL_GPU='1', CHIBI_IFRAME_PROFILE='1')
command = ['build/test_suite', '--kit-documents']
result = dict(source_commit=git('rev-parse', 'HEAD'),
              base_commit='7b35b66da8fbaf94012e18ed78cfd26e5ee09f2a',
              rmlui_commit=git('-C', 'build/_deps/rmlui-src', 'rev-parse', 'HEAD'),
              rmlui_patch_diff_sha256=hashlib.sha256(
                  subprocess.check_output(['git', '-C', 'build/_deps/rmlui-src', 'diff'], cwd=ROOT)).hexdigest(),
              binary_sha256=hashlib.sha256((ROOT/'build/test_suite').read_bytes()).hexdigest(),
              platform=platform.platform(), command=command,
              environment={key: env[key] for key in
                           ['CHIBI_EXECUTION_PROFILE', 'CHIBI_ALLOW_FULL_GPU', 'CHIBI_IFRAME_PROFILE']},
              timeout_seconds_per_run=180, runs=[])
for i in range(1, 6):
    start = time.monotonic()
    completed = subprocess.run(command, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True, timeout=180)
    elapsed = time.monotonic() - start
    with gzip.open(OUT/f'profile-{i}.log.gz', 'wt') as file:
        file.write(completed.stdout)
    assert completed.returncode == 0, completed.stdout[-4000:]
    run = dict(run=i, exit_code=completed.returncode, wall_seconds=elapsed,
               **summarize(completed.stdout))
    result['runs'].append(run)
    print(json.dumps(dict(run=i, wall_seconds=elapsed,
                          post_holder_ms=1000*run['post']['holder_seconds'])), flush=True)
(OUT/'results.json').write_text(json.dumps(result, indent=2)+'\n')
