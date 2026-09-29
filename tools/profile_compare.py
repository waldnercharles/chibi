#!/usr/bin/env python3
"""Profile one headless scene natively and as WASM under Node, and compare them.

Both builds run the same fixed-step frames through chibi_ctl's profile_scenario.
Each build runs several times: a costly frame that recurs at the same index in
every run comes from the content, while one that moves between runs is noise
from the machine (scheduling, first-use compilation, heap growth).
"""
import argparse
import json
import statistics
import subprocess
import sys
from pathlib import Path

import yaml

PHASES = ('behavior_ms', 'physics_ms', 'systems_ms', 'rendering_ms')
DISTRIBUTION = ('p50', 'p95', 'p99', 'max')


def start_scene(project_root):
    manifest = yaml.safe_load((Path(project_root) / 'project.yaml').read_text())
    return Path(manifest['start_scene']).stem


def profile(command, project_root, request):
    completed = subprocess.run(
        [*command, '--project-root', project_root],
        input=json.dumps(request) + '\n', capture_output=True, text=True, check=True)
    response = json.loads(completed.stdout)
    if not response.get('ok'):
        sys.exit(f'{command[-1]} failed: {response.get("error")}')
    return response['result']


def summarize(name, results):
    timings = [result['timing'] for result in results]
    frames = timings[0]['sampled_frames']
    row = {'build': name}
    for key in PHASES:
        row[key] = statistics.median(timing[key] / frames for timing in timings)
    for key in DISTRIBUTION:
        row[key] = statistics.median(timing['frame_ms'][key] for timing in timings)
    recurring = set.intersection(*({worst['frame'] for worst in timing['worst_frames']} for timing in timings))
    row['recurring'] = sorted(recurring)
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project-root', required=True)
    parser.add_argument('--native', required=True, help='native chibi_ctl')
    parser.add_argument('--wasm', required=True, help='chibi_ctl.js built for Node')
    parser.add_argument('--scene', help="scene name; defaults to the project's start scene")
    parser.add_argument('--frames', type=int, default=600)
    parser.add_argument('--warmup', type=int, default=60)
    parser.add_argument('--runs', type=int, default=3)
    args = parser.parse_args()

    scene = args.scene or start_scene(args.project_root)
    request = {'contract': 1, 'operation': 'profile_scenario', 'kind': 'scene', 'name': scene,
               'frames': args.frames, 'warmup_frames': args.warmup}
    builds = {'native': [args.native], 'wasm': ['node', args.wasm]}
    runs = {name: [] for name in builds}
    # Alternate builds so a slow period on the machine affects both.
    for _ in range(args.runs):
        for name, command in builds.items():
            runs[name].append(profile(command, args.project_root, request))

    print(f'{scene}: {args.frames} frames after {args.warmup} warmup, median of {args.runs} runs (ms per frame)')
    header = ['build', *(key.removesuffix('_ms') for key in PHASES), *DISTRIBUTION]
    print(''.join(f'{column:>10}' for column in header))
    rows = [summarize(name, results) for name, results in runs.items()]
    for row in rows:
        print(f'{row["build"]:>10}' + ''.join(f'{row[key]:10.3f}' for key in (*PHASES, *DISTRIBUTION)))
    native, wasm = rows
    print(f'{"wasm/native":>10}' + ''.join(
        f'{wasm[key] / native[key]:9.2f}x' if native[key] > 0 else f'{"-":>10}' for key in (*PHASES, *DISTRIBUTION)))
    for row in rows:
        frames = ', '.join(map(str, row['recurring'])) or 'none'
        print(f'{row["build"]}: costliest frames recurring in every run: {frames}')


if __name__ == '__main__':
    main()
