#!/usr/bin/env python3
"""Bounded local OBJ -> sampled SDF. Run with ordinary Python; launches Blender."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def arguments():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('destination', type=Path, help='new output directory (must not exist)')
    p.add_argument('--axes', choices=['blender', 'engine'], required=True)
    p.add_argument('--scale', type=float, required=True, help='engine units per source unit, positive')
    p.add_argument('--translate', type=float, nargs=3, default=[0, 0, 0], metavar=('X', 'Y', 'Z'))
    p.add_argument('--padding', type=float, default=0.25, help='engine units around origin-centered bounds')
    p.add_argument('--resolution', type=int, default=17)
    p.add_argument('--max-seconds', type=float, default=60)
    p.add_argument('--max-memory-mib', type=int, default=4096, help='Linux child virtual address-space cap')
    p.add_argument('--max-work', type=int, default=20_000_000, help='voxel * triangle sign evaluations')
    p.add_argument('--blender', default=os.environ.get('BLENDER', 'blender'))
    return p.parse_args()


def main():
    a = arguments()
    start = time.monotonic()
    try:
        if not sys.platform.startswith('linux'):
            raise ValueError('bounded launcher currently requires Linux resource limits')
        if a.source.suffix.lower() != '.obj':
            raise ValueError('supported input: triangulated Wavefront .obj only')
        if a.destination.exists():
            raise ValueError('destination exists; choose a new directory')
        if not 3 <= a.resolution <= 128:
            raise ValueError('resolution must be 3..128 on each axis')
        if not all(math.isfinite(x) for x in [a.scale, a.padding, a.max_seconds, *a.translate]):
            raise ValueError('scale, padding, translation and time budget must be finite')
        if min(a.scale, a.padding, a.max_seconds, a.max_work, a.max_memory_mib) <= 0:
            raise ValueError('scale, padding and budgets must be positive')
        if a.source.stat().st_size > 8 * 1024 * 1024:
            raise ValueError('source exceeds 8 MiB input limit')
        # Snapshot once: provenance and conversion use exactly the same bytes.
        source = a.source.read_bytes()
        settings = {k: v for k, v in vars(a).items() if k not in ('source', 'destination', 'blender')}
        settings['source_sha256'] = hashlib.sha256(source).hexdigest()
        settings['converter_sha256'] = hashlib.sha256(b''.join(
            (Path(__file__).parent / f).read_bytes() for f in ['convert.py', 'worker.py', 'atlas.py'])).hexdigest()
        a.destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='.mesh-sdf-', dir=a.destination.parent) as stage:
            stage = Path(stage)
            (stage / 'source.obj').write_bytes(source)
            (stage / 'settings.json').write_text(json.dumps(settings))
            env = {**os.environ, 'OMP_NUM_THREADS': '1', 'OPENBLAS_NUM_THREADS': '1'}
            def limits():
                import resource
                cap = a.max_memory_mib * 1024 * 1024
                resource.setrlimit(resource.RLIMIT_AS, (cap, cap))
                resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
            run = subprocess.run([a.blender, '--background', '--factory-startup', '--threads', '1',
                                  '--python-exit-code', '1', '--python', str(Path(__file__).with_name('worker.py')),
                                  '--', str(stage)], capture_output=True, text=True, env=env,
                                 timeout=a.max_seconds, preexec_fn=limits)
            if run.returncode:
                raise ValueError('Blender conversion failed (input or resource budget):\n' + run.stdout[-4000:] + run.stderr[-4000:])
            result = stage / 'result'
            if not (result / 'provenance.json').exists():
                raise ValueError('worker produced no completed asset')
            if time.monotonic() - start > a.max_seconds:
                raise ValueError('wall-time budget exhausted before publication')
            if a.destination.exists():
                raise ValueError('destination appeared during conversion; refusing overwrite')
            result.rename(a.destination)
        print(json.dumps({'destination': str(a.destination), 'seconds': round(time.monotonic()-start, 3),
                          'samples': a.resolution ** 3, 'status': 'complete'}))
    except (ValueError, OSError, subprocess.TimeoutExpired) as e:
        print(f'mesh-sdf: {e}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
