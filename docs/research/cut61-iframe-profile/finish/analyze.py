#!/usr/bin/env python3
"""Analyze saved CUT61 evidence. Never build, launch tests or start graphics."""
import gzip
import json
import math
from pathlib import Path

root = Path(__file__).resolve().parent
source = '641424f550c8f78327e3e8ef232b904364688514'
parts = ['model_update_seconds', 'root_update_seconds',
         'layout_position_seconds', 'release_documents_seconds']


def read(path):
    return json.loads(path.read_text())


def log_rows(log, tag):
    return [json.loads(line.split(' ', 1)[1]) for line in log.splitlines()
            if line.startswith(tag + ' ')]


evidence = read(root / 'supervised/results.json')
assert evidence['source_commit'] == source
assert evidence['timeout_seconds'] == 60
summaries = []
for run in evidence['runs']:
    log = gzip.decompress((root / 'supervised' / (run['label'] + '.log.gz')).read_bytes()).decode()
    assert run['exit_code'] == 0 and 0 < run['wall_seconds'] < 60
    assert 'ceiling 60s' in log and log.count('FAIL:') == 0
    iframe = run['label'].startswith('iframe')
    expected = 12 if iframe else 10
    assert log.count('PASS:') == expected
    result = dict(label=run['label'], checks=expected)
    rows = log_rows(log, 'IFRAME_PROFILE')
    if iframe:
        profile = log_rows(log, 'EMPTY_DOC_PROFILE')
        assert len(profile) == int(run['profile_enabled'])
        if profile:
            value = profile[0]
            assert value['frames'] == value['document_opens'] == value['context_updates'] == 1000
            assert value['creation_model_reads'] == 0 and value['frame_model_reads'] == 1000
            assert value['render_captures'] == value['render_passes'] == value['geometry_compiles'] == 0
            result['profile'] = value
    elif run['profile_enabled']:
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
        assert counts == [[49, 55, 22, 5, 1, 52], [0, 196, 16, 0, 0, 0], [49, 49, 0, 0, 0, 49]]
        for row in rows:
            if row['phase'] != 'context':
                continue
            subtotal = sum(row[key] for key in parts)
            assert row['context_calls'] == row['calls'] == 1
            assert all(math.isfinite(row[key]) and row[key] >= 0 for key in parts)
            assert subtotal <= row['seconds'] + 5e-9 and row['other_context_seconds'] >= -5e-9
            assert abs(subtotal + row['other_context_seconds'] - row['seconds']) <= 5e-9
        backend = log_rows(log, 'IFRAME_BACKEND')[0]
        assert backend == dict(frames=49, context_updates=49, render_captures=0,
                               render_passes=0, geometry_compiles=0)
        result.update(frames=49, counts=counts, profile_reconciles=True, backend=backend)
    else:
        assert not rows and 'IFRAME_BACKEND ' not in log
    summaries.append(result)

micro = read(root / 'micro/empty-results.json')
identity = read(root / 'micro/identity.json')
assert micro['source_commit'] == identity['source_commit'] == source
assert micro['shared_deadline_seconds'] == 60 and micro['aggregate_wall_seconds'] < 60
assert [run['variant'] for run in micro['runs']] == ['before', 'after'] * 3
for run in micro['runs']:
    before = run['variant'] == 'before'
    value = run['profile']
    path = root / 'micro' / f"empty-{run['run']}-{run['variant']}.log.gz"
    log = gzip.decompress(path.read_bytes()).decode()
    assert 'ceiling 60s' in log and run['binary_sha256'] == identity['binaries'][run['variant']]
    assert run['exit_code'] == int(before)
    assert log.count('PASS:') == run['checks'] == (10 if before else 12)
    assert log.count('FAIL:') == run['failures'] == (2 if before else 0)
    assert log_rows(log, 'EMPTY_DOC_PROFILE') == [value]
    assert value['frames'] == value['document_opens'] == value['context_updates'] == 1000
    assert value['creation_model_reads'] == (1000 if before else 0)
    assert value['frame_model_reads'] == (2000 if before else 1000)
    assert value['render_captures'] == value['render_passes'] == value['geometry_compiles'] == 0
    assert all(math.isfinite(value[key]) and value[key] >= 0
               for key in ['creation_seconds', 'frame_seconds'])

result = dict(source_commit=source, validated=True,
              binary_sha256=evidence['binary_sha256'], supervised=summaries,
              micro_complete=True, micro_aggregate_wall_seconds=micro['aggregate_wall_seconds'],
              timing_speedup_established=False)
(root / 'analysis.json').write_text(json.dumps(result, indent=2) + '\n')
print('Saved evidence validated; no workloads executed.')
