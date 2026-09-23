#!/usr/bin/env python3
"""Fixed-quality NeonSwarm benchmarks, frozen lighting comparisons and GPU captures."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import tempfile

from check_neon_swarm import PROJECT, ROOT, Runner
from neon_material_scenarios import SCENARIOS, check_load, scenario


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def fingerprint(path):
    digest = hashlib.sha256()
    paths = sorted(p for p in path.rglob('*') if p.is_file() and '.chibi' not in p.parts) if path.is_dir() else [path]
    for item in paths:
        digest.update(str(item.relative_to(path) if path.is_dir() else item.name).encode())
        digest.update(item.read_bytes())
    return digest.hexdigest()


def prepare(build, output, fixture, scale, trace_levels):
    project = output / 'project'
    shutil.copytree(PROJECT, project, ignore=shutil.ignore_patterns('.chibi', 'build'))
    runner = Runner(build, output, project)
    asset = runner.author('readAsset', dict(kind='prototype', name='HRC'))
    runner.author('buildPrototype', dict(kind='prototype', name='HRC',
        expected_hash=asset['hash'], operations=[
            dict(op='set_field', component='GameHRC2D', field='adaptive', value=False),
            dict(op='set_field', component='GameHRC2D', field='scale', value=scale),
            dict(op='set_field', component='GameHRC2D', field='trace_levels', value=trace_levels)]))
    runner.author('validateAsset', dict(kind='prototype', name='HRC'))
    if fixture['operations']:
        asset = runner.author('readAsset', dict(kind='scene', name='Sandbox'))
        runner.author('buildScene', dict(kind='scene', name='Sandbox',
            expected_hash=asset['hash'], operations=fixture['operations']))
    runner.author('validateAsset', dict(kind='scene', name='Sandbox'))
    return runner


def snapshot(runner, frames, inputs):
    return runner.call('runtime_snapshot', kind='scene', name='Sandbox', frames=frames,
        input_timeline=[s for s in inputs if s['frame'] < frames],
        entity_limit=16, component_limit=64, field_limit=64)


def replay_files(output, binary, project, request, environment):
    write_json(output / 'request.json', request)
    command = [str(binary), '--project-root', str(project), '--render-request', str(output / 'request.json')]
    write_json(output / 'launch.json', dict(argv=command, environment=environment, cwd=str(ROOT)))
    shell = ['#!/bin/sh', 'set -eu', 'exec ' + shlex.join(['python3',
        str(ROOT / 'tools/benchmark_neon_materials.py'), '--replay', str(output)])]
    (output / 'replay.sh').write_text('\n'.join(shell) + '\n')
    (output / 'replay.sh').chmod(0o755)
    return command


def render(command, output, environment, tracy):
    capture = None
    with (output / 'tracy.log').open('w') as log:
        if tracy:
            capture = subprocess.Popen([str(tracy.resolve()), '-f', '-s', '60',
                '-o', str(output / 'capture.tracy')], stdout=log, stderr=log)
        try:
            run = subprocess.run(command, cwd=ROOT, env=dict(os.environ, **environment),
                text=True, capture_output=True, timeout=300)
            (output / 'render.log').write_text(run.stderr)
            (output / 'response.json').write_text(run.stdout)
            assert run.returncode == 0, f'Render failed; see {output / "render.log"}: {run.stdout[:1000]}'
            response = json.loads(run.stdout)
            assert response['ok'], response
            return response['result']
        finally:
            if capture:
                try:
                    assert capture.wait(timeout=65) == 0, 'Tracy capture failed; see tracy.log'
                except subprocess.TimeoutExpired:
                    capture.terminate()
                    capture.wait()
                    raise


def run_case(args, name, mode, scale, repeat, provenance):
    output = args.output / f'{name}-{mode}-{scale:g}-{repeat}'
    output.mkdir(parents=True, exist_ok=False)
    fixture = scenario(name)
    runner = prepare(args.build, output, fixture, scale, args.trace_levels)
    frozen = mode == 'frozen'
    initial_frames = fixture['frames'] + (args.render_frames if frozen else 0)
    final_frames = initial_frames + (0 if frozen else args.render_frames)
    inputs = [s for s in fixture['input_timeline'] if s['frame'] < final_frames]
    request = dict(contract=1, operation='preview_scene', kind='scene', name='Sandbox',
        frames=initial_frames, render_frames=args.render_frames, freeze_simulation=frozen,
        width=args.width, height=args.height, input_timeline=inputs)
    environment = {}
    if args.metal_capture:
        request.update(metal_capture=str(output / 'frame.gputrace'), capture_frame=16)
        environment['MTL_CAPTURE_ENABLED'] = '1'
    binary = (ROOT / args.build / 'chibi_ctl').resolve()
    command = replay_files(output, binary, runner.project, request, environment)
    manifest = dict(provenance, project_sha256=fingerprint(runner.project), scenario=name,
        fixture=fixture, mode=mode, scale=scale, trace_levels=args.trace_levels,
        adaptive=False, width=args.width, height=args.height)
    write_json(output / 'manifest.json', manifest)
    if args.prepare_only:
        return dict(scenario=name, mode=mode, scale=scale, prepared=str(output))
    result = render(command, output, environment, args.tracy_capture)
    before, after = result['initial_state'], result['final_state']
    assert before['frame'] == initial_frames and after['frame'] == final_frames
    if frozen:
        assert before == after, 'Frozen render changed simulation state'
        assert result['performance']['simulation_average_ms'] == 0
    # Compare against a separate headless run, including changes to input during
    # measured frames. This inspection work stays outside the measured interval.
    expected = snapshot(runner, final_frames, inputs)
    assert after['world_hash'] == expected['world_hash'], 'Rendered inputs diverged from headless replay'
    assert after['materials'] == expected['materials'], 'Rendered material state diverged'
    write_json(output / 'snapshot.json', expected)
    check_load(name, after)
    if name == 'ignition':
        check_load(name, before)
    performance = result['performance']
    assert abs(performance['hrc_scale'] - scale) < 1e-6, 'HRC scale changed during benchmark'
    assert performance['samples'] == args.render_frames - 16
    (output / 'frame.png').write_bytes(base64.b64decode(result['preview']['data']))
    report = dict(scenario=name, mode=mode, scale=scale, repeat=repeat,
        trace_levels=args.trace_levels, adaptive=False, width=args.width, height=args.height,
        purpose='metal-capture' if args.metal_capture else 'tracy-capture' if args.tracy_capture else 'benchmark',
        backend=result['backend'], initial_state=before, final_state=after, performance=performance,
        directory=output.name, manifest='manifest.json', capture=result.get('metal_capture'))
    write_json(output / 'report.json', report)
    print(f"{name:12} {mode:6} scale={scale:g}: median {performance['median_ms']:.2f} ms, "
        f"p95 {performance['p95_ms']:.2f} ms; material {after['materials']['hash']}", flush=True)
    return report


def compare_frozen(output, reports):
    comparisons = []
    for candidate in reports:
        if candidate['mode'] != 'frozen' or candidate['scale'] == 1:
            continue
        reference = next((r for r in reports if r['mode'] == 'frozen' and r['scale'] == 1
            and r['scenario'] == candidate['scenario'] and r['repeat'] == candidate['repeat']), None)
        if reference:
            paths = [str(output / r['directory'] / 'frame.png') for r in [reference, candidate]]
            metrics = json.loads(subprocess.check_output(['node', str(ROOT / 'tools/compare_neon_frames.mjs'), *paths], text=True))
            comparisons.append(dict(scenario=candidate['scenario'], scale=candidate['scale'],
                repeat=candidate['repeat'], reference=paths[0], candidate=paths[1], metrics=metrics))
    write_json(output / 'comparisons.json', comparisons)


def replay(bundle, allow_renderer_changes):
    manifest = json.loads((bundle / 'manifest.json').read_text())
    launch = json.loads((bundle / 'launch.json').read_text())
    assert launch['environment'].get('MTL_CAPTURE_ENABLED') == '1' or os.environ.get('MTL_CAPTURE_ENABLED') != '1', \
        'Unset MTL_CAPTURE_ENABLED for an ordinary replay'
    assert fingerprint(bundle / 'project') == manifest['project_sha256'], 'Saved project changed; prepare a new benchmark'
    if not allow_renderer_changes:
        assert fingerprint(Path(manifest['binary'])) == manifest['binary_sha256'], 'Binary changed; use --allow-renderer-changes for an intentional comparison'
        assert fingerprint(ROOT / 'src/render/shaders') == manifest['shaders_sha256'], 'Engine shaders changed; use --allow-renderer-changes for an intentional comparison'
    output = Path(tempfile.mkdtemp(prefix='replay-', dir=bundle))
    request = json.loads((bundle / 'request.json').read_text())
    if 'metal_capture' in request:
        request['metal_capture'] = str(output / 'frame.gputrace')
    write_json(output / 'request.json', request)
    command = [manifest['binary'], '--project-root', str(bundle / 'project'),
        '--render-request', str(output / 'request.json')]
    result = render(command, output, launch['environment'], None)
    if (bundle / 'report.json').exists():
        expected = json.loads((bundle / 'report.json').read_text())
        assert result['final_state']['world_hash'] == expected['final_state']['world_hash'], 'Replay changed the simulation state'
        assert result['final_state'].get('materials') == expected['final_state'].get('materials'), 'Replay changed material state'
    (output / 'frame.png').write_bytes(base64.b64decode(result['preview']['data']))
    result.pop('preview')
    write_json(output / 'report.json', dict(result, binary_sha256=fingerprint(Path(command[0])),
        shaders_sha256=fingerprint(ROOT / 'src/render/shaders'), allow_renderer_changes=allow_renderer_changes))
    print(output)


def summary(output, reports):
    write_json(output / 'summary.json', reports)
    lines = ['# NeonSwarm benchmark results', '',
        'Frame work includes GPU completion and excludes pacing. Render/present is host elapsed time, not GPU pass time.', '',
        '| Scenario | Mode | HRC scale | Median ms | p95 ms | Simulation mean ms | Render/present mean ms |',
        '| --- | --- | --- | --- | --- | --- | --- |']
    for r in reports:
        if 'performance' not in r:
            continue
        p = r['performance']
        lines.append(f"| {r['scenario']} | {r['mode']} | {r['scale']:g} | {p['median_ms']:.2f} | {p['p95_ms']:.2f} | "
            f"{p['simulation_average_ms']:.2f} | {p['render_average_ms']:.2f} |")
    if any(r.get('purpose', 'benchmark') != 'benchmark' or r.get('performance', {}).get('instrumented') for r in reports):
        lines += ['', '**Instrumented capture run. Its timings are not benchmark results.**']
    (output / 'summary.md').write_text('\n'.join(lines) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', default='build-release')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--replay', type=Path)
    parser.add_argument('--allow-renderer-changes', action='store_true')
    parser.add_argument('--scenario', choices=SCENARIOS + ['all'], default='travel')
    parser.add_argument('--mode', choices=['live', 'frozen', 'both'], default='both')
    parser.add_argument('--width', type=int, default=1920)
    parser.add_argument('--height', type=int, default=1080)
    parser.add_argument('--scale', type=float, nargs='+', default=[.5, 1])
    parser.add_argument('--trace-levels', type=int, choices=range(1, 6), default=1)
    parser.add_argument('--render-frames', type=int, default=180)
    parser.add_argument('--repeat', type=int, default=1)
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--metal-capture', action='store_true')
    parser.add_argument('--tracy-capture', type=Path)
    args = parser.parse_args()
    if args.replay:
        replay(args.replay.resolve(), args.allow_renderer_changes)
        return
    if not args.output or args.allow_renderer_changes:
        parser.error('Use --output for a new run; --allow-renderer-changes requires --replay')
    if not 17 <= args.render_frames <= 1200 or args.repeat < 1:
        parser.error('Use 17..1200 render frames and a positive repeat count')
    if any(not .1 <= scale <= 1 for scale in args.scale) or len(set(args.scale)) != len(args.scale):
        parser.error('HRC scales must be distinct values in 0.1..1')
    if not all(64 <= size <= 2048 for size in [args.width, args.height]):
        parser.error('Dimensions must be 64..2048 pixels')
    if args.metal_capture and args.tracy_capture:
        parser.error('Capture GPU and Tracy traces in separate runs')
    if args.metal_capture or args.tracy_capture:
        if args.scenario == 'all' or args.mode == 'both' or len(args.scale) != 1 or args.repeat != 1:
            parser.error('Select one scenario, mode, scale and repeat for an instrumented capture')
    if not args.metal_capture and os.environ.get('MTL_CAPTURE_ENABLED') == '1':
        parser.error('Unset MTL_CAPTURE_ENABLED for ordinary benchmarks')
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    binary = (ROOT / args.build / 'chibi_ctl').resolve()
    provenance = dict(binary=str(binary), binary_sha256=fingerprint(binary),
        shaders_sha256=fingerprint(ROOT / 'src/render/shaders'), host=platform.platform(),
        git_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        git_dirty=bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT)))
    if platform.system() == 'Darwin':
        provenance['gpu'] = json.loads(subprocess.check_output(['system_profiler', 'SPDisplaysDataType', '-json'], text=True))
    reports = []
    for name in SCENARIOS if args.scenario == 'all' else [args.scenario]:
        for mode in ['live', 'frozen'] if args.mode == 'both' else [args.mode]:
            for scale in args.scale:
                for repeat in range(1, args.repeat + 1):
                    report = run_case(args, name, mode, scale, repeat, provenance)
                    if not args.prepare_only:
                        peers = [r for r in reports if r['scenario'] == name]
                        assert all(r['final_state']['materials'] == report['final_state']['materials'] for r in peers), \
                            'Material state changed across quality settings, modes or repeats'
                    reports.append(report)
                    summary(args.output, reports)
    if not args.prepare_only:
        compare_frozen(args.output, reports)


if __name__ == '__main__':
    main()
