#!/usr/bin/env python3
"""Compare optimized native builds on identical fixed-step authored scenes."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', type=Path, required=True)
    parser.add_argument('--after', type=Path, default=Path('build-release/chibi_ctl'))
    parser.add_argument('--project-root', type=Path, required=True)
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument('--scene')
    target.add_argument('--scenario')
    parser.add_argument('--frames', type=int, default=600)
    parser.add_argument('--warmup', type=int, default=60)
    parser.add_argument('--trials', type=int, default=5)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.trials < 3 or args.frames < 60 or not 0 <= args.warmup <= 1800:
        parser.error('use at least three trials, 60 frames, and 0..1800 warmup frames')
    binaries = {'before': args.before.resolve(), 'after': args.after.resolve()}
    for binary in binaries.values():
        cache = (binary.parent / 'CMakeCache.txt').read_text()
        if 'CHIBI_ENABLE_SANITIZERS:BOOL=OFF' not in cache or not re.search(
                r'^CMAKE_BUILD_TYPE:STRING=(Release|RelWithDebInfo)$', cache, re.M):
            parser.error('use optimized native builds without sanitizers')
    hashes = {name: digest(path) for name, path in binaries.items()}
    request = {'contract': 1, 'operation': 'profile_scenario',
               'frames': args.frames, 'warmup_frames': args.warmup}
    if args.scenario:
        request['scenario'] = args.scenario
    else:
        request.update(kind='scene', name=args.scene)
    runs = {name: [] for name in binaries}
    world_hash = None
    for trial in range(args.trials):
        for name in (('before', 'after') if trial % 2 == 0 else ('after', 'before')):
            response = subprocess.run(
                [binaries[name], '--project-root', args.project_root.resolve()],
                input=json.dumps(request) + '\n', capture_output=True, text=True,
                check=True, env=dict(os.environ, MallocNanoZone='0'))
            reply = json.loads(response.stdout)
            if not reply.get('ok'):
                raise SystemExit('FAIL: profile request: ' + response.stdout)
            result = reply['result']
            timing = result['timing']
            if (timing['sampled_frames'] != args.frames or
                    timing['simulation_steps'] != args.frames or timing['dropped_seconds']):
                raise SystemExit('FAIL: incomplete fixed-step simulation')
            if world_hash is None:
                world_hash = result['world_hash']
            if result['world_hash'] != world_hash:
                raise SystemExit('FAIL: baseline/candidate or repeated scene state differs')
            runs[name].append(result)
            print(f"trial={trial} {name} average={timing['average_frame_ms']:.6f} "
                  f"p95={timing['frame_ms']['p95']:.6f} hash={world_hash}", flush=True)
    if hashes != {name: digest(path) for name, path in binaries.items()}:
        raise SystemExit('FAIL: a controller executable changed during measurement')
    summary = {}
    for name, results in runs.items():
        metrics = {}
        for result in results:
            timing = result['timing']
            values = {key: timing[key] / args.frames for key in
                      ('behavior_ms', 'physics_ms', 'systems_ms', 'rendering_ms')}
            values['average_frame_ms'] = timing['average_frame_ms']
            values.update({key: timing['frame_ms'][key] for key in ('p50', 'p95', 'p99', 'max')})
            values.update({key + '_ms': value / args.frames for key, value in
                           timing['physics_detail_ms']['material_fluid_ms'].items()})
            for key, value in values.items():
                if not math.isfinite(value) or value < 0:
                    raise SystemExit('FAIL: invalid timing')
                metrics.setdefault(key, []).append(value)
        summary[name] = {key: {'median_ms': statistics.median(values),
                              'min_ms': min(values), 'max_ms': max(values)}
                         for key, values in metrics.items()}
    args.output.write_text(json.dumps({
        'machine': {'system': platform.platform(), 'architecture': platform.machine(),
                    'logical_cpus': os.cpu_count()},
        'binary_sha256': hashes, 'project_root': str(args.project_root.resolve()),
        'request': request, 'world_hash': world_hash, 'world_hash_parity': True,
        'summary': summary, 'runs': runs,
        'scope': 'Headless fixed-step scene simulation; rendering/GPU presentation excluded.',
    }, indent=2) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
