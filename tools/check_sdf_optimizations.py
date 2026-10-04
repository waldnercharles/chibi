#!/usr/bin/env python3
"""Serial SDF test-scene replay, image comparisons and alternating measurements."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import statistics
import struct
import subprocess
import time
import zlib

ROOT = Path(__file__).resolve().parents[1]
QUALITY = dict(samples=2, pixel_error=0, normal_samples=6, lod=False)
CASES = {
    'cpu-static': ('SdfStaticInstances1000', {}, {}, True),
    'cpu-moving': ('SdfInstanceSwarm1000', {}, {}, True),
    'surface-resolution': ('SdfQualityStudy', {'surface_quality': QUALITY},
                           {'surface_quality': dict(QUALITY, samples=1)}, False),
    'sdf-tolerance': ('SdfQualityStudy', {'surface_quality': QUALITY},
                      {'surface_quality': dict(QUALITY, pixel_error=.25)}, False),
    'sdf-normal': ('SdfQualityStudy', {'surface_quality': QUALITY},
                   {'surface_quality': dict(QUALITY, normal_samples=4)}, False),
    'sdf-lod': ('SdfQualityStudy', {'surface_quality': dict(QUALITY, lod=False)},
                {'surface_quality': dict(QUALITY, lod=True)}, False),
    'horizon4': ('SdfHorizonStudy', {'horizon_directions': 0}, {'horizon_directions': 4}, False),
    'horizon8': ('SdfHorizonStudy', {'horizon_directions': 0}, {'horizon_directions': 8}, False),
}


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def pixels(path):
    """Decode the controller's 8-bit RGB/RGBA PNGs using only the standard library."""
    png = path.read_bytes()
    if png[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'Not a PNG: {path}')
    offset, compressed, width, height, channels = 8, bytearray(), 0, 0, 0
    while offset < len(png):
        size = struct.unpack_from('>I', png, offset)[0]
        kind, data = png[offset + 4:offset + 8], png[offset + 8:offset + 8 + size]
        if kind == b'IHDR':
            width, height, depth, color, compression, filtering, interlace = struct.unpack('>IIBBBBB', data)
            if depth != 8 or color not in (2, 6) or compression or filtering or interlace:
                raise ValueError(f'Unsupported PNG layout: {path}')
            channels = 3 if color == 2 else 4
        elif kind == b'IDAT':
            compressed.extend(data)
        offset += size + 12
    raw = zlib.decompress(compressed)
    stride = width * channels
    if len(raw) != height * (stride + 1):
        raise ValueError(f'Malformed PNG: {path}')
    output, previous = bytearray(), bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        method = raw[start]
        row = bytearray(raw[start + 1:start + 1 + stride])
        for i in range(stride):
            left = row[i - channels] if i >= channels else 0
            up = previous[i]
            corner = previous[i - channels] if i >= channels else 0
            if method == 1:
                predict = left
            elif method == 2:
                predict = up
            elif method == 3:
                predict = (left + up) // 2
            elif method == 4:
                p = left + up - corner
                distances = [abs(p - left), abs(p - up), abs(p - corner)]
                predict = [left, up, corner][distances.index(min(distances))]
            elif method == 0:
                predict = 0
            else:
                raise ValueError(f'Invalid PNG filter: {method}')
            row[i] = (row[i] + predict) & 255
        output.extend(row)
        previous = row
    return width, height, channels, output


def image_difference(reference, candidate):
    a, b = pixels(reference), pixels(candidate)
    if a[:3] != b[:3]:
        raise ValueError('Image dimensions or channels differ')
    width, height, channels = a[:3]
    errors, changed = [], 0
    for p in range(width * height):
        values = [abs(a[3][p * channels + c] - b[3][p * channels + c]) for c in range(3)]
        errors.extend(values)
        changed += max(values) > 2
    ordered = sorted(errors)
    return dict(width=width, height=height, rgb_mae_255=statistics.mean(errors),
                rgb_max_255=max(errors), rgb_p95_255=ordered[int(.95 * (len(ordered) - 1))],
                changed_pixels_over_2=changed, changed_fraction=changed / (width * height))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build-release')
    parser.add_argument('--baseline', type=Path, default=ROOT / 'build/sdf-optimization-evidence/baseline/chibi_ctl')
    parser.add_argument('--baseline-shaders', type=Path, default=ROOT / 'build/sdf-optimization-evidence/baseline/shaders')
    parser.add_argument('--project', type=Path, default=ROOT / 'projects/sdf_lab')
    parser.add_argument('--baseline-project', type=Path, default=ROOT / 'build/sdf-optimization-evidence/baseline-project')
    parser.add_argument('--cpu-shaders', type=Path, default=ROOT / 'build/sdf-optimization-evidence/cpu-shaders')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/sdf-optimization-evidence/matrix')
    parser.add_argument('--case', action='append', choices=CASES)
    parser.add_argument('--phase', choices=['replay', 'images', 'measure', 'all'], default='all')
    parser.add_argument('--trials', type=int, default=3)
    parser.add_argument('--width', type=int, default=960)
    parser.add_argument('--height', type=int, default=540)
    parser.add_argument('--trace-debug', action='store_true', help='Save a separate instrumented capture, never timed')
    parser.add_argument('--graph-review', action='store_true', help='Save separate native graph layout screenshots')
    args = parser.parse_args()
    if args.trials < 1:
        parser.error('--trials must be positive')
    project = args.project.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    candidate = args.build.resolve() / 'chibi_ctl'
    env = dict(os.environ, CHIBI_ROOT=str(project), CHIBI_CTL=str(candidate),
               MallocNanoZone='0', ASAN_OPTIONS='detect_leaks=0')
    cases = args.case or list(CASES)
    baseline_project = args.baseline_project.resolve()
    cpu_shaders = args.cpu_shaders.resolve()
    if any(CASES[name][3] for name in cases):
        shutil.copytree(args.baseline_shaders, cpu_shaders, dirs_exist_ok=True)
        # Current HRC eagerly loads these resources even in Reference mode.
        # Keep archived shaders intact; the added horizon stages are not drawn.
        for name in ['horizon.frag', 'relief_pack.frag']:
            destination = cpu_shaders / 'hrc' / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / 'src/render/shaders/hrc' / name, destination)
        for path in args.baseline_shaders.rglob('*'):
            if path.is_file() and path.read_bytes() != (cpu_shaders / path.relative_to(args.baseline_shaders)).read_bytes():
                raise RuntimeError(f'Archived CPU shader changed: {path}')
    if any(CASES[name][3] for name in cases) and project != baseline_project:
        # Historical controllers load every prototype. Exclude newly reflected
        # quality components by copying the preserved original asset set.
        preservation = ROOT / 'build/sdf-optimization-evidence/authored-preservation.json'
        original = json.loads(preservation.read_text())
        sources = [Path(path) for path in original]
        sources += [project / 'project.kdl', project / 'content/scenes/SdfStaticInstances1000.kdl']
        for source in sources:
            if str(source) in original and hashlib.sha256(source.read_bytes()).hexdigest() != original[str(source)]:
                raise RuntimeError(f'Historical asset changed: {source}')
            destination = baseline_project / source.relative_to(ROOT / 'projects/sdf_lab')
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
    manifest = dict(cases={name: CASES[name] for name in cases}, candidate=str(candidate),
                    candidate_sha256=hashlib.sha256(candidate.read_bytes()).hexdigest(),
                    baseline=str(args.baseline.resolve()),
                    baseline_sha256=hashlib.sha256(args.baseline.read_bytes()).hexdigest(),
                    baseline_shaders=str(args.baseline_shaders.resolve()),
                    baseline_shader_sha256={str(path.relative_to(args.baseline_shaders)): hashlib.sha256(path.read_bytes()).hexdigest()
                                            for path in sorted(args.baseline_shaders.rglob('*')) if path.is_file()},
                    cpu_shaders=str(cpu_shaders),
                    cpu_shader_reason='archived tree plus only horizon.frag and relief_pack.frag, needed for eager loading but unused in Reference mode',
                    cpu_shader_sha256={str(path.relative_to(cpu_shaders)): hashlib.sha256(path.read_bytes()).hexdigest()
                                       for path in sorted(cpu_shaders.rglob('*')) if path.is_file()},
                    current_shaders=str(ROOT / 'src/render/shaders'),
                    current_shader_sha256={str(path.relative_to(ROOT / 'src/render/shaders')): hashlib.sha256(path.read_bytes()).hexdigest()
                                           for path in sorted((ROOT / 'src/render/shaders').rglob('*')) if path.is_file()},
                    project_asset_sha256={str(path.relative_to(project)): hashlib.sha256(path.read_bytes()).hexdigest()
                                          for path in sorted(project.rglob('*.kdl'))},
                    project=str(project), baseline_project=str(baseline_project),
                    cpu_shader_scope='same archived-plus-unused-resources shader tree on both binaries; quality cases use current shaders',
                    width=args.width, height=args.height, trials=args.trials,
                    timing_scope='scene GPU completion and final present CPU submission; final presentation GPU completion is not fenced')
    save(output / 'conditions.json', manifest)

    def author(tool, arguments, controller=candidate, target=project):
        request = dict(jsonrpc='2.0', id=1, method='tools/call', params=dict(name=tool, arguments=arguments))
        run = subprocess.run(['node', str(ROOT / 'tools/mcp/server.mjs')], input=json.dumps(request) + '\n',
                             text=True, capture_output=True, cwd=ROOT,
                             env=dict(env, CHIBI_CTL=str(controller), CHIBI_ROOT=str(target)), timeout=600)
        if run.returncode:
            raise RuntimeError(run.stderr or run.stdout)
        response = json.loads(run.stdout)
        if response.get('error') or response['result'].get('isError'):
            raise RuntimeError(response)
        return response['result']['structuredContent']['result']

    status = author('getProjectStatus', {})
    if Path(status['project_root']).resolve() != project.resolve():
        raise RuntimeError('Unexpected project root')
    save(output / 'project-status.json', status)
    if args.phase in ('replay', 'all'):
        for scene in sorted({CASES[name][0] for name in cases}):
            historical = scene in [CASES[name][0] for name in cases if CASES[name][3]]
            target = baseline_project if historical else project
            validation = author('validateAsset', dict(kind='scene', name=scene), target=target)
            save(output / f'{scene}-validation.json', validation)
            if not validation['valid']:
                raise RuntimeError(validation)
            recording = author('recordScenario', dict(kind='scene', name=scene, frames=360), target=target)
            save(output / f'{scene}-recording.json', recording)
            replay = author('replayScenario', dict(recording_path=recording['recording_path']), target=target)
            save(output / f'{scene}-replay.json', replay)
            if not replay['deterministic'] or replay.get('diverged'):
                raise RuntimeError(replay)
            print(f'{scene}: recorded/replayed 360 steps', flush=True)
            if historical:
                snapshots = []
                for mode, controller in [('reference', args.baseline.resolve()), ('optimized', candidate)]:
                    snapshot = author('captureRuntimeSnapshot', dict(kind='scene', name=scene, frames=360,
                        entity_limit=256, fields=['GameTransform.x', 'GameTransform.y',
                            'GameOrientation.radians', 'GameVisualTransform.scale', 'GameVisualTransform.pitch',
                            'GameVisualTransform.yaw', 'GameVisualTransform.roll', 'GameGeometry3D.elevation',
                            'GameGeometry3D.width', 'GameGeometry3D.height', 'GameGeometry3D.depth',
                            'GameShader.shape', 'GameShader.material', 'GameShader.parameters',
                            'GameMaterial.scene', 'GameMaterial.albedo']), controller=controller, target=target)
                    save(output / f'{scene}-{mode}-snapshot.json', snapshot)
                    state = {key: snapshot[key] for key in ['saved_state', 'sound_cues', 'visibility',
                             'particle_count', 'live_entities', 'entity_count', 'timing']}
                    state['entities'] = [dict(name=entity.get('name'), prototype=entity.get('prototype'),
                        fields={component['name'] + '.' + field['name']: field['value']
                                for component in entity['components'] for field in component['fields']})
                        for entity in snapshot['entities']]
                    snapshots.append(state)
                if snapshots[0] != snapshots[1]:
                    raise RuntimeError(f'Historical CPU semantic snapshot differs: {scene}')
                save(output / f'{scene}-semantic-parity.json', dict(equal=True, inspected_entities=len(snapshots[0]['entities']),
                     scope='first 256 ordered entities: common poses, geometry, shader bindings, material colors; global saved state/cues/visibility/entity counts'))

    rows = []
    def host_conditions(stem, moment):
        # Names only: do not collect unrelated command-line arguments.
        conditions = dict(timestamp=time.time(), scope='host CPU/process observations; GPU utilization unavailable')
        for name, command in [('load', ['uptime']), ('memory', ['vm_stat']),
                              ('processes', ['ps', '-axo', 'pid,ppid,pcpu,pmem,etime,comm'])]:
            run = subprocess.run(command, text=True, capture_output=True, timeout=30)
            conditions[name] = dict(returncode=run.returncode, stdout=run.stdout, stderr=run.stderr)
        save(output / f'{stem}-{moment}-host.json', conditions)

    def capture(case, mode, trial, frame, measured=False, debug=False):
        scene, reference, optimized, historical = CASES[case]
        use_baseline = historical and mode == 'reference'
        controller = args.baseline.resolve() if use_baseline else candidate
        request = dict(contract=1, operation='preview_scene', kind='scene', name=scene,
                       width=args.width, height=args.height, frames=frame,
                       render_frames=240 if measured else 2, freeze_simulation=not measured,
                       sdf_batch_limit=256, **(reference if mode == 'reference' else optimized))
        if debug:
            request['sdf_trace_debug'] = True
        stem = f'{case}-{mode}-trial{trial}-frame{frame}' + ('-debug' if debug else '')
        path = output / f'{stem}-request.json'
        save(path, request)
        environment = dict(env)
        if historical:
            environment['CHIBI_RENDER_SHADER_DIR'] = str(cpu_shaders)
        if measured:
            host_conditions(stem, 'before')
        started = time.monotonic()
        target_project = baseline_project if historical else project
        run = subprocess.run([str(controller), '--project-root', str(target_project), '--render-request', str(path)],
                             cwd=ROOT, env=environment, text=True, capture_output=True, timeout=600)
        (output / f'{stem}.log').write_text(run.stderr)
        if run.returncode:
            raise RuntimeError(run.stdout or run.stderr)
        response = json.loads(run.stdout)
        if not response.get('ok'):
            raise RuntimeError(response)
        result = response['result']
        png = base64.b64decode(result['preview'].pop('data'))
        image = output / f'{stem}.png'
        image.write_bytes(png)
        save(output / f'{stem}-result.json', response)
        performance = result['performance']
        if measured and (performance.get('instrumented') or performance['samples'] != 224 or performance['warmup_frames'] != 240):
            raise RuntimeError(f'Invalid timing scope: {performance}')
        row = dict(case=case, scene=scene, mode=mode, trial=trial, frame=frame, measured=measured,
                   debug=debug, wall_seconds=time.monotonic() - started, image=str(image),
                   png_sha256=hashlib.sha256(png).hexdigest(), final_state=result['final_state'], performance=performance)
        rows.append(row)
        save(output / 'summary.json', rows)
        print(json.dumps(row), flush=True)
        if measured:
            host_conditions(stem, 'after')
        return row

    comparisons = []
    def compare(case, reference, optimized):
        historical = CASES[case][3]
        states = [dict(row['final_state']) for row in [reference, optimized]]
        if historical:
            for state in states:
                state.pop('world_hash', None)
        if states[0] != states[1]:
            raise RuntimeError(f'Simulation diverged for {case}')
        difference = image_difference(Path(reference['image']), Path(optimized['image']))
        if CASES[case][3] and reference['png_sha256'] != optimized['png_sha256']:
            raise RuntimeError(f'Exact CPU optimization image parity failed: {case}')
        comparisons.append(dict(case=case, trial=reference['trial'], frame=reference['frame'],
                                measured=reference['measured'], difference=difference,
                                world_hash_equal=reference['final_state']['world_hash'] == optimized['final_state']['world_hash'],
                                identical_png=reference['png_sha256'] == optimized['png_sha256']))
        save(output / 'comparisons.json', comparisons)

    if args.phase in ('images', 'all'):
        if args.graph_review:
            graphs = output / 'graphs'
            graphs.mkdir(exist_ok=True)
            for asset in ['SdfQualityThinRing', 'SdfQualityNarrowCut', 'SdfHorizonCanopy', 'SdfQualityScaleSweep']:
                image = graphs / f'{asset}.png'
                run = subprocess.run([str(args.build.resolve() / 'chibi'), '--project-root', str(project),
                                      '--open', asset, '--screenshot', str(image)],
                                     cwd=ROOT, env=env, text=True, capture_output=True, timeout=180)
                (graphs / f'{asset}.log').write_text(run.stdout + run.stderr)
                if run.returncode or not image.is_file():
                    raise RuntimeError(f'Graph screenshot failed: {asset}: {run.stdout}{run.stderr}')
        for case in cases:
            lod_counts = []
            for frame in ((0, 120, 202) if case == 'sdf-lod' else (0, 120)):
                reference = capture(case, 'reference', 0, frame)
                optimized = capture(case, 'optimized', 0, frame)
                compare(case, reference, optimized)
                if case == 'sdf-lod':
                    reference_count = reference['performance']['mesh_work']['sdf_lod_instances']
                    optimized_count = optimized['performance']['mesh_work']['sdf_lod_instances']
                    if reference_count != 0:
                        raise RuntimeError('Disabled LOD selected a low-detail graph')
                    lod_counts.append(dict(frame=frame, selected=optimized_count))
            if case == 'sdf-lod':
                # Fixed small control always qualifies; fixed large control never does.
                # The moving control crosses the authored 24-logical-pixel threshold.
                if {row['frame']: row['selected'] for row in lod_counts} != {0: 1, 120: 1, 202: 2}:
                    raise RuntimeError(f'LOD fixture did not cross its threshold: {lod_counts}')
                save(output / 'lod-transition.json', dict(cutoff_logical_pixels=24,
                     fixed_large_scale=.6, fixed_small_scale=.08,
                     moving_scale_formula='.23 + .2 * sin(elapsed * 1.4)', poses=lod_counts))
            if args.trace_debug and not CASES[case][3]:
                capture(case, 'optimized', 0, 120, debug=True)
    if args.phase in ('measure', 'all'):
        for trial in range(1, args.trials + 1):
            for case in cases:
                modes = ['reference', 'optimized'] if trial % 2 else ['optimized', 'reference']
                pair = {mode: capture(case, mode, trial, 120, measured=True) for mode in modes}
                compare(case, pair['reference'], pair['optimized'])
    print('Completed every requested case; no failed or interrupted runs discarded.', flush=True)


if __name__ == '__main__':
    main()
