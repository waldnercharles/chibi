#!/usr/bin/env python3
"""Capture static HRC fixtures at several scales and preserve comparison evidence."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess

from check_neon_swarm import PROJECT, ROOT, Runner


SCENES = [
    'NeonSwarmHRCPinhole',
    'NeonSwarmHRCBounce',
    'NeonSwarmHRCSinglePixel',
    'NeonSwarmHRCShadow',
    'NeonSwarmHRCVolume',
]

GPU_LABELS = [
    'Material tile binning',
    'Material raster batches',
    'Fluid density raster',
    'HRC prefilter',
    'HRC +X/+Y/-X/-Y: Trace, Extend, Merge',
    'HRC gather',
    'HRC filter',
    'HRC bounce',
    'HRC composite',
]


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def fingerprint(path):
    digest = hashlib.sha256()
    paths = [path] if path.is_file() else sorted(
        p for p in path.rglob('*') if p.is_file() and '.chibi' not in p.parts)
    for item in paths:
        digest.update(str(item.relative_to(path) if path.is_dir() else item.name).encode())
        digest.update(item.read_bytes())
    return digest.hexdigest()


def prepare(args, output, scene, scale):
    project = output / 'project'
    shutil.copytree(PROJECT, project, ignore=shutil.ignore_patterns('.chibi', 'build'))
    runner = Runner(args.build, output, project)
    asset = runner.author('readAsset', dict(kind='prototype', name='NeonSwarmHRC'))
    runner.author('buildPrototype', dict(
        kind='prototype', name='NeonSwarmHRC', expected_hash=asset['hash'],
        operations=[
            dict(op='set_field', component='GameHRC2D', field='adaptive', value=False),
            dict(op='set_field', component='GameHRC2D', field='scale', value=scale),
            dict(op='set_field', component='GameHRC2D', field='trace_levels', value=args.trace_levels),
            dict(op='set_field', component='GameHRC2D', field='upscale_mode', value=args.upscale_mode),
        ]))
    runner.author('validateAsset', dict(kind='prototype', name='NeonSwarmHRC'))
    runner.author('validateAsset', dict(kind='scene', name=scene))
    return runner


def render(args, output, runner, scene, scale, repeat):
    request = dict(
        contract=1,
        operation='preview_scene',
        kind='scene',
        name=scene,
        frames=1,
        render_frames=args.render_frames,
        freeze_simulation=True,
        width=args.width,
        height=args.height,
    )
    environment = {}
    if args.metal_capture:
        request.update(metal_capture=str(output / 'frame.gputrace'), capture_frame=16)
        environment['MTL_CAPTURE_ENABLED'] = '1'
    write_json(output / 'request.json', request)
    binary = (ROOT / args.build / 'chibi_ctl').resolve()
    command = [str(binary), '--project-root', str(runner.project),
               '--render-request', str(output / 'request.json')]
    write_json(output / 'launch.json', dict(argv=command, environment=environment, cwd=str(ROOT)))
    process = subprocess.run(
        command, cwd=ROOT, env=dict(os.environ, **environment),
        text=True, capture_output=True, timeout=300)
    (output / 'render.log').write_text(process.stderr)
    (output / 'response.json').write_text(process.stdout)
    if process.returncode:
        raise RuntimeError(f'render failed for {scene} scale {scale}; see {output / "render.log"}')
    response = json.loads(process.stdout)
    if not response.get('ok'):
        raise RuntimeError(response)
    result = response['result']
    if result['initial_state'] != result['final_state']:
        raise RuntimeError('frozen fixture changed simulation state')
    performance = result['performance']
    expected_samples = args.render_frames - 16
    if performance['samples'] != expected_samples:
        raise RuntimeError(f'expected {expected_samples} samples, got {performance["samples"]}')
    (output / 'frame.png').write_bytes(base64.b64decode(result['preview']['data']))
    report = dict(
        scene=scene,
        scale=scale,
        repeat=repeat,
        trace_levels=args.trace_levels,
        upscale_mode=args.upscale_mode,
        adaptive=False,
        width=args.width,
        height=args.height,
        purpose='metal-capture' if args.metal_capture else 'benchmark',
        backend=result['backend'],
        performance=performance,
        gpu_label_groups=GPU_LABELS,
        capture=result.get('metal_capture'),
        directory=output.name,
        project_sha256=fingerprint(runner.project),
    )
    write_json(output / 'report.json', report)
    print(
        f'{scene:28} scale={scale:g}: median {performance["median_ms"]:.2f} ms, '
        f'p95 {performance["p95_ms"]:.2f} ms', flush=True)
    return report


def compare_frozen(output, reports):
    comparisons = []
    for candidate in reports:
        if candidate.get('purpose') != 'benchmark' or candidate['scale'] == 1:
            continue
        reference = next((item for item in reports
                          if item.get('purpose') == 'benchmark'
                          and item['scene'] == candidate['scene']
                          and item['scale'] == 1
                          and item['repeat'] == candidate['repeat']), None)
        if not reference:
            continue
        paths = [str(output / item['directory'] / 'frame.png')
                 for item in [reference, candidate]]
        metrics = json.loads(subprocess.check_output(
            ['node', str(ROOT / 'tools/compare_neon_frames.mjs'), *paths], text=True))
        comparisons.append(dict(
            scene=candidate['scene'], scale=candidate['scale'], repeat=candidate['repeat'],
            reference=paths[0], candidate=paths[1], metrics=metrics))
    write_json(output / 'comparisons.json', comparisons)


def summary(output, reports):
    write_json(output / 'summary.json', reports)
    lines = [
        '# HRC lab results', '',
        'Frozen scenes use 240 renderer warmup frames and discard the first 16 measured frames.',
        'Frame time includes GPU completion; it is not a per-dispatch GPU timestamp.', '',
        '| Scene | Scale | Median ms | p95 ms | p99 ms |',
        '| --- | ---: | ---: | ---: | ---: |',
    ]
    for report in reports:
        performance = report.get('performance')
        if not performance:
            continue
        lines.append(
            f'| {report["scene"]} | {report["scale"]:g} | '
            f'{performance["median_ms"]:.2f} | {performance["p95_ms"]:.2f} | '
            f'{performance["p99_ms"]:.2f} |')
    lines += ['', 'GPU capture label groups:', '']
    lines.extend(f'- `{label}`' for label in GPU_LABELS)
    lines += ['', 'Per-pass numeric timings require opening the `.gputrace` in a Metal debugger or a backend timestamp-query implementation.']
    (output / 'summary.md').write_text('\n'.join(lines) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', default='build-release')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--scene', choices=SCENES + ['all'], default='all')
    parser.add_argument('--width', type=int, default=1920)
    parser.add_argument('--height', type=int, default=1080)
    parser.add_argument('--scale', type=float, nargs='+', default=[1.0, .75, .5])
    parser.add_argument('--trace-levels', type=int, choices=range(1, 6), default=1)
    parser.add_argument('--upscale-mode', type=int, choices=[0, 1, 2, 3], default=1)
    parser.add_argument('--render-frames', type=int, default=180)
    parser.add_argument('--repeat', type=int, default=1)
    parser.add_argument('--metal-capture', action='store_true')
    args = parser.parse_args()
    if not 17 <= args.render_frames <= 1200 or args.repeat < 1:
        parser.error('use 17..1200 render frames and a positive repeat count')
    if any(not .1 <= scale <= 1 for scale in args.scale) or len(set(args.scale)) != len(args.scale):
        parser.error('HRC scales must be distinct values in 0.1..1')
    if not all(64 <= size <= 2048 for size in [args.width, args.height]):
        parser.error('dimensions must be 64..2048 pixels')
    if args.metal_capture and (args.scene == 'all' or len(args.scale) != 1 or args.repeat != 1):
        parser.error('Metal capture selects one scene, scale and repeat')
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    scenes = SCENES if args.scene == 'all' else [args.scene]
    provenance = dict(
        binary=str((ROOT / args.build / 'chibi_ctl').resolve()),
        binary_sha256=fingerprint(ROOT / args.build / 'chibi_ctl'),
        shaders_sha256=fingerprint(ROOT / 'src/render/shaders'),
        host=platform.platform(),
        git_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        git_dirty=bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT)),
    )
    reports = []
    for scene in scenes:
        for scale in args.scale:
            for repeat in range(1, args.repeat + 1):
                case = args.output / f'{scene}-{scale:g}-{repeat}'
                case.mkdir(parents=True, exist_ok=False)
                runner = prepare(args, case, scene, scale)
                report = render(args, case, runner, scene, scale, repeat)
                report.update(provenance)
                write_json(case / 'report.json', report)
                reports.append(report)
                summary(args.output, reports)
    if not args.metal_capture:
        compare_frozen(args.output, reports)


if __name__ == '__main__':
    main()
