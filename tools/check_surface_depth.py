#!/usr/bin/env python3
"""Capture and compare frozen surface lighting modes in disposable SDF projects.

The emitter fixture measures a fixed screen-space work bound, not game performance.
All authoring goes through public MCP tools; original project assets are preserved.
"""
import argparse
import base64
import json
import math
import os
from pathlib import Path
import platform
import shutil
import subprocess

from benchmark_hrc_lab import fingerprint, write_json
from check_neon_swarm import ROOT, Runner

PROJECT = ROOT / 'projects/sdf_lab'
MODES = ['Off', 'Blur', 'Depth thickness']
SCENE_OPERATION_LIMIT = 128


def lighting(mode):
    return dict(surface_mode='Blur' if mode == 'Off' else mode,
                surface_radius=0 if mode == 'Off' else 24,
                surface_depth_radius=128, surface_thickness=12,
                adaptive=False, radiance_hysteresis=1, bounce_hysteresis=1,
                feedback=False, bounce_intensity=0, ambient=0, fog=0,
                shadow_floor=0, bloom=0)


def emitter_operations(document, count, mode):
    operations = [dict(op='remove_entity', entity=entity['name'])
                  for entity in document['entities']]
    operations += [dict(op='add_entity', name='Camera', prototype='SdfOptimizationCamera',
                        components=dict(GameCamera=dict(width=500, height=320, focal_distance=0),
                                        GameHRC2D=lighting(mode))),
                   dict(op='add_entity', name='Receiver', prototype='SdfRoundedBox',
                        components=dict(GameTransform=dict(x=0, y=0),
                                        GameVisualTransform=dict(scale=3, pitch=0, yaw=0, roll=0),
                                        GameShader=dict(material=''),
                                        GameMaterial=dict(scene=0xffffffff, albedo=0xffffffff,
                                                          absorption=1, edge_light=1)))]
    columns = math.ceil(math.sqrt(count * 450 / 260))
    rows = math.ceil(count / columns)
    for index in range(count):
        x = -225 + 450 * ((index % columns) + .5) / columns
        y = -130 + 260 * ((index // columns) + .5) / rows
        operations.append(dict(op='add_entity', name=f'Emitter {index + 1}', prototype='SdfSphere',
            components=dict(GameTransform=dict(x=x, y=y),
                            GameGeometry3D=dict(elevation=150),
                            GameVisualTransform=dict(scale=.04, pitch=0, yaw=0, roll=0),
                            GameShader=dict(shape='SdfSphere', material=''),
                            GameMaterial=dict(scene=0xffffffff, albedo=0xffffffff,
                                emission_color=0x38d9edff, emission_intensity=.45,
                                emission_multiplier=1, absorption=0))))
    return operations


def prepare(args, case, scene, mode, prepared=None):
    project = case / 'project'
    shutil.copytree(prepared or PROJECT, project, ignore=shutil.ignore_patterns('.chibi', 'build'))
    runner = Runner(args.build, case, project)
    asset = runner.author('readAsset', dict(kind='scene', name=scene))
    if prepared is not None:
        sources = []
        for entity in asset['document']['entities']:
            components = entity.get('components', {})
            material = next((value for key, value in components.items()
                             if key.split('.')[-1] == 'GameMaterial'), {})
            if entity.get('prototype') == 'SdfSphere' and material.get('emission_intensity', 0) > 0:
                sources.append(entity)
        if len(sources) != args.emitters:
            raise RuntimeError(f'Prepared {scene} has {len(sources)} sources; expected {args.emitters}')
    if args.emitters is not None and prepared is None:
        operations = emitter_operations(asset['document'], args.emitters, mode)
    else:
        camera = next(entity for entity in asset['document']['entities'] if entity['name'] == 'Camera')
        components = dict(camera.get('components', {}))
        key = next((name for name in components if name.split('.')[-1] == 'GameHRC2D'), 'GameHRC2D')
        components[key] = dict(components.get(key, {}), **lighting(mode))
        operations = [dict(op='set_entity_components', entity='Camera', components=components)]
    for start in range(0, len(operations), SCENE_OPERATION_LIMIT):
        if start:
            asset = runner.author('readAsset', dict(kind='scene', name=scene))
        runner.author('buildScene', dict(kind='scene', name=scene, expected_hash=asset['hash'],
                                        operations=operations[start:start + SCENE_OPERATION_LIMIT]))
    validation = runner.author('validateAsset', dict(kind='scene', name=scene))
    write_json(case / 'validation.json', validation)
    if not validation['valid']:
        raise RuntimeError(validation)
    return runner


def render(args, case, runner, scene, mode, repeat):
    request = dict(contract=1, operation='preview_scene', kind='scene', name=scene,
                   frames=args.frames, render_frames=args.render_frames, freeze_simulation=True,
                   pass_timing=args.render_frames > 2, width=args.width, height=args.height)
    write_json(case / 'request.json', request)
    command = [str(args.build / 'chibi_ctl'), '--project-root', str(runner.project),
               '--render-request', str(case / 'request.json')]
    write_json(case / 'launch.json', dict(argv=command, cwd=str(ROOT)))
    process = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=600)
    (case / 'render.log').write_text(process.stderr)
    (case / 'response.json').write_text(process.stdout)
    if process.returncode:
        raise RuntimeError(f'Render failed; see {case / "render.log"}')
    response = json.loads(process.stdout)
    if not response.get('ok'):
        raise RuntimeError(response)
    result = response['result']
    if result['initial_state'] != result['final_state']:
        raise RuntimeError('Frozen simulation state changed')
    performance = result['performance']
    if performance['samples'] != max(0, args.render_frames - 16):
        raise RuntimeError(f'Unexpected measured sample count: {performance["samples"]}')
    (case / 'frame.png').write_bytes(base64.b64decode(result['preview']['data']))
    passes = performance.get('passes', [])
    surface_ms = sum(item['ms'] for item in passes if item['name'] == 'Surface light')
    work = performance.get('surface_work', {})
    if mode == 'Depth thickness' and work and work.get('candidates') != work.get('cells', 0) * 81:
        raise RuntimeError(f'Unexpected depth field work bound: {work}')
    report = dict(scene=scene, mode=mode, repeat=repeat, emitters=args.emitters,
                  frame=args.frames, render_frames=args.render_frames,
                  width=args.width, height=args.height, backend=result['backend'],
                  purpose='capture' if args.render_frames <= 16 else 'benchmark',
                  surface_gpu_ms=surface_ms, surface_work=work,
                  source_work=performance.get('mesh_work', {}), performance=performance,
                  project_sha256=fingerprint(runner.project), directory=case.name)
    write_json(case / 'report.json', report)
    print(f'{scene} {mode}: {surface_ms:.3f} ms surface GPU, '
          f'{performance.get("median_ms", 0):.3f} ms frame median; work {work}', flush=True)
    return report


def summary(output, reports):
    write_json(output / 'summary.json', reports)
    lines = ['# Surface lighting comparison', '',
             'Frozen simulation after the requested frame. Benchmark measurements discard the first 16 rendered frames.',
             'Surface work reports deterministic submission bounds; it does not count actual texture fetches.',
             'Depth field work is bounded by 81 source tiles and 96 visibility taps per field cell (up to four child tiles per mixed-depth source tile) regardless of emitter count.', '',
             '| Scene | Mode | Repeat | Emitters | Frame median ms | Frame p95 ms | Surface GPU ms | Cells | Candidates | SDF instances | SDF draws | Image |',
             '| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |']
    for report in reports:
        p, w = report['performance'], report['surface_work']
        measured = report['purpose'] == 'benchmark'
        median = f'{p.get("median_ms", 0):.3f}' if measured else 'capture only'
        p95 = f'{p.get("p95_ms", 0):.3f}' if measured else 'capture only'
        surface = f'{report["surface_gpu_ms"]:.3f}' if measured else 'capture only'
        lines.append(f'| {report["scene"]} | {report["mode"]} | {report["repeat"]} | '
                     f'{report["emitters"] if report["emitters"] is not None else "scene"} | '
                     f'{median} | {p95} | {surface} | {w.get("cells", 0)} | '
                     f'{w.get("candidates", 0)} | {report["source_work"].get("sdf_instances", 0)} | '
                     f'{report["source_work"].get("sdf_draw_calls", 0)} | '
                     f'[frame]({report["directory"]}/frame.png) |')
    (output / 'summary.md').write_text('\n'.join(lines) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--width', type=int, default=960)
    parser.add_argument('--height', type=int, default=632)
    parser.add_argument('--frames', type=int, default=240)
    parser.add_argument('--render-frames', type=int, default=2,
                        help='Use 2 for captures or 120 for benchmarks')
    parser.add_argument('--scene', nargs='+',
                        help='Defaults to Orbit and Study, or Orbit alone for emitter fixtures')
    parser.add_argument('--modes', nargs='+', choices=MODES, default=MODES)
    parser.add_argument('--repeats', type=int, default=1)
    parser.add_argument('--emitters', type=int,
                        help='Replace each copied scene with a fixed-radius source-count fixture')
    parser.add_argument('--prepared-project', type=Path,
                        help='Reuse an already authored emitter fixture; requires --emitters')
    args = parser.parse_args()
    if not 64 <= args.width <= 2048 or not 64 <= args.height <= 2048:
        parser.error('Dimensions must be 64..2048')
    if args.frames < 0 or not 2 <= args.render_frames <= 1200 or args.repeats < 1:
        parser.error('Use nonnegative frames, 2..1200 render frames, and positive repeats')
    if args.emitters is not None and not 1 <= args.emitters <= 1022:
        parser.error('Emitter count must be 1..1022; the 1024-entity scene limit also includes camera and receiver')
    if args.prepared_project is not None:
        if args.emitters is None:
            parser.error('--prepared-project requires --emitters')
        args.prepared_project = args.prepared_project.resolve()
        if not (args.prepared_project / 'project.kdl').is_file():
            parser.error('Prepared project must contain project.kdl')
    if args.scene is None:
        args.scene = ['SdfDepthThicknessOrbit']
        if args.emitters is None:
            args.scene.append('SdfDepthThicknessStudy')
    args.build = (ROOT / args.build).resolve()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    before = fingerprint(PROJECT)
    write_json(args.output / 'conditions.json', dict(binary=str(args.build / 'chibi_ctl'),
        binary_sha256=fingerprint(args.build / 'chibi_ctl'), shaders_sha256=fingerprint(ROOT / 'src/render/shaders'),
        host=platform.platform(), load_average=os.getloadavg(), project_sha256=before,
        git_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        arguments={key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items()}))
    reports = []
    prepared_fixtures = {}
    try:
        for repeat in range(1, args.repeats + 1):
            modes = args.modes if repeat % 2 else list(reversed(args.modes))
            for scene in args.scene:
                for mode in modes:
                    case = args.output / f'{scene}-{mode.replace(" ", "-")}-{repeat}'
                    case.mkdir(exist_ok=False)
                    prepared = prepared_fixtures.get(scene, args.prepared_project)
                    runner = prepare(args, case, scene, mode, prepared)
                    if args.emitters is not None:
                        prepared_fixtures.setdefault(scene, runner.project)
                    reports.append(render(args, case, runner, scene, mode, repeat))
                    summary(args.output, reports)
    finally:
        if fingerprint(PROJECT) != before:
            raise RuntimeError('Original SDF Lab assets changed during validation')


if __name__ == '__main__':
    main()
