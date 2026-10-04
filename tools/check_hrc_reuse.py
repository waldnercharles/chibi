#!/usr/bin/env python3
"""Serial authored HRC reuse replay, image comparisons, and matched measurements."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

from check_sdf_optimizations import image_difference

ROOT = Path(__file__).resolve().parents[1]
OFF = dict(fuse_relief=False, cache=False, reproject=False, hierarchy=False)
ALL = dict(fuse_relief=True, cache=True, reproject=True, hierarchy=True)
CASES = {
    'fusion': ('HrcReuseChangingLights', OFF, dict(OFF, fuse_relief=True)),
    'cache-lights': ('HrcReuseChangingLights', OFF, dict(OFF, cache=True)),
    'cache-blocker': ('HrcReuseMovingBlocker', OFF, dict(OFF, cache=True)),
    'cache-fractional-pan': ('HrcReuseCameraPan', OFF, dict(OFF, cache=True)),
    'reproject-integer-pan': ('HrcReuseIntegerPan', dict(OFF, cache=True), dict(OFF, cache=True, reproject=True)),
    'hierarchy-gaps': ('HrcReuseThinGaps', OFF, dict(OFF, hierarchy=True)),
    'hierarchy-blocker': ('HrcReuseMovingBlocker', OFF, dict(OFF, hierarchy=True)),
    'combined-blocker': ('HrcReuseMovingBlocker', OFF, ALL),
    'combined-pan': ('HrcReuseIntegerPan', OFF, ALL),
    'normal-analytic-quality': ('SdfQualityStudy', OFF, OFF),
    'normal-analytic-shapes': ('SdfShapes', OFF, OFF),
    'instance-batch-control': ('SdfInstances', OFF, OFF),
    'near-plane-bounds': ('SdfNearPlaneBounds', OFF, OFF),
    'normal-analytic-gradients': ('SdfAnalyticGradientStudy', OFF, OFF),
    'normal-analytic-fallback': ('SdfAnalyticGradientFallback', OFF, OFF),
    'hidden-surface': ('SdfHiddenSurfaceStudy', OFF, OFF),
    'hidden-near-plane': ('SdfOcclusionNearPlane', OFF, OFF),
    'spatial-scratch-sparse': ('SdfSparseScratchStudy', OFF, OFF),
    'spatial-scratch-hidden': ('SdfHiddenSurfaceStudy', OFF, OFF),
    'spatial-scratch-near-plane': ('SdfOcclusionNearPlane', OFF, OFF),
}
ONE_SAMPLE = dict(samples=1, pixel_error=0, normal_samples=6, lod=False)
OVERRIDES = {
    'normal-analytic-quality': ({'surface_quality': ONE_SAMPLE}, {'surface_quality': dict(ONE_SAMPLE, normal_samples=0)}),
    'normal-analytic-shapes': ({'surface_quality': ONE_SAMPLE}, {'surface_quality': dict(ONE_SAMPLE, normal_samples=0)}),
    'instance-batch-control': ({'surface_quality': ONE_SAMPLE, 'sdf_batch_limit': 1},
                               {'surface_quality': ONE_SAMPLE, 'sdf_batch_limit': 256}),
    'near-plane-bounds': ({'surface_quality': ONE_SAMPLE}, {'surface_quality': ONE_SAMPLE}),
    'normal-analytic-gradients': ({'surface_quality': ONE_SAMPLE}, {'surface_quality': dict(ONE_SAMPLE, normal_samples=0)}),
    'normal-analytic-fallback': ({'surface_quality': dict(ONE_SAMPLE, normal_samples=4)},
                                 {'surface_quality': dict(ONE_SAMPLE, normal_samples=0)}),
}

for name in CASES:
    if name.startswith('hidden-'):
        OVERRIDES[name] = ({'hidden_rejection': False, 'spatial_scratch': True, 'surface_quality': ONE_SAMPLE},
                           {'hidden_rejection': True, 'spatial_scratch': True, 'surface_quality': ONE_SAMPLE})
    if name.startswith('spatial-scratch-'):
        OVERRIDES[name] = ({'hidden_rejection': True, 'spatial_scratch': False, 'surface_quality': ONE_SAMPLE},
                           {'hidden_rejection': True, 'spatial_scratch': True, 'surface_quality': ONE_SAMPLE})


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build-release')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/hrc-reuse-evidence/matrix')
    parser.add_argument('--phase', choices=['replay', 'images', 'measure', 'all'], default='replay')
    parser.add_argument('--case', action='append', choices=CASES)
    parser.add_argument('--trials', type=int, default=1)
    parser.add_argument('--image-frame', type=int, action='append', help='Defaults to frame120; each capture advances two frames')
    parser.add_argument('--width', type=int, default=480)
    parser.add_argument('--height', type=int, default=270)
    parser.add_argument('--graph-review', action='store_true')
    args = parser.parse_args()
    if args.trials < 1:
        parser.error('--trials must be positive')
    project = ROOT / 'projects/sdf_lab'
    controller = args.build.resolve() / 'chibi_ctl'
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    cases = args.case or list(CASES)
    def image_frames(case):
        if case == 'reproject-integer-pan':
            return [9]
        return args.image_frame or ([0, 90] if case.startswith(('hidden-', 'spatial-scratch-')) else [120])
    env = dict(os.environ, CHIBI_ROOT=str(project), CHIBI_CTL=str(controller),
               MallocNanoZone='0', ASAN_OPTIONS='detect_leaks=0')
    save(output / 'conditions.json', dict(cases={name: CASES[name] for name in cases},
         extra_request_overrides=OVERRIDES, candidate=str(controller), candidate_sha256=hashlib.sha256(controller.read_bytes()).hexdigest(),
         shader_sha256={str(p.relative_to(ROOT / 'src/render/shaders')): hashlib.sha256(p.read_bytes()).hexdigest()
                        for p in sorted((ROOT / 'src/render/shaders').rglob('*')) if p.is_file()},
         project_asset_sha256={str(p.relative_to(project)): hashlib.sha256(p.read_bytes()).hexdigest()
                               for p in sorted(project.rglob('*.kdl'))},
         width=args.width, height=args.height, trials=args.trials,
         image_capture_scope='render_frames2 fixes the requested pose; independent reprojection uses initial frame9 and three advancing frames, ending at12',
         image_render_frames={name: 3 if name == 'reproject-integer-pan' else 2 for name in cases},
         image_initial_frames={name: image_frames(name) for name in cases},
         scope='CPU pass counters; no inferred GPU cache-hit or repaired-pixel counts',
         timing_scope='scene GPU completion plus final-present CPU submission; presentation GPU completion not fenced'))

    def author(name, arguments):
        request = dict(jsonrpc='2.0', id=1, method='tools/call', params=dict(name=name, arguments=arguments))
        run = subprocess.run(['node', str(ROOT / 'tools/mcp/server.mjs')], input=json.dumps(request) + '\n',
                             text=True, capture_output=True, cwd=ROOT, env=env, timeout=600)
        if run.returncode:
            raise RuntimeError(run.stderr or run.stdout)
        response = json.loads(run.stdout)
        if response.get('error') or response['result'].get('isError'):
            raise RuntimeError(response)
        return response['result']['structuredContent']['result']

    status = author('getProjectStatus', {})
    if Path(status['project_root']).resolve() != project.resolve():
        raise RuntimeError('Unexpected authoring project root')
    save(output / 'project-status.json', status)
    scenes = sorted({CASES[name][0] for name in cases})
    for scene in scenes:
        validation = author('validateAsset', dict(kind='scene', name=scene))
        save(output / f'{scene}-validation.json', validation)
        if not validation['valid']:
            raise RuntimeError(validation)
        if args.phase in ('replay', 'all'):
            recording = author('recordScenario', dict(kind='scene', name=scene, frames=360))
            save(output / f'{scene}-recording.json', recording)
            replay = author('replayScenario', dict(recording_path=recording['recording_path']))
            save(output / f'{scene}-replay.json', replay)
            if not replay['deterministic'] or replay.get('diverged'):
                raise RuntimeError(replay)
            poses = []
            for frame in (0, 120):
                snapshot = author('captureRuntimeSnapshot', dict(kind='scene', name=scene, frames=frame,
                    entity_limit=128, fields=['GameTransform.x', 'GameTransform.y', 'GameMaterial.emission_intensity']))
                save(output / f'{scene}-frame{frame}-snapshot.json', snapshot)
                poses.append({entity['name']: {component['name'].split('.')[-1] + '.' + field['name']: field['value']
                              for component in entity['components'] for field in component['fields']}
                              for entity in snapshot['entities']})
            moving = {'HrcReuseChangingLights': 'Animated source', 'HrcReuseMovingBlocker': 'Translating blocker',
                      'HrcReuseCameraPan': 'Camera', 'HrcReuseIntegerPan': 'Camera',
                      'SdfHiddenSurfaceStudy': 'Moving blocker', 'SdfOcclusionNearPlane': 'Camera'}.get(scene)
            for name in poses[0]:
                if name != moving and poses[0][name] != poses[1][name]:
                    raise RuntimeError(f'Unexpected motion in {scene}: {name}')
            if moving and poses[0][moving] == poses[1][moving]:
                raise RuntimeError(f'Moving fixture did not move: {scene}')
            if scene == 'HrcReuseChangingLights':
                for field in ['GameTransform.x', 'GameTransform.y']:
                    if poses[0][moving][field] != poses[1][moving][field]:
                        raise RuntimeError('Light-only fixture changed source geometry')
                if poses[0][moving]['GameMaterial.emission_intensity'] == poses[1][moving]['GameMaterial.emission_intensity']:
                    raise RuntimeError('Light-only fixture did not change intensity')
            if scene == 'HrcReuseIntegerPan':
                # The authored camera steps are eight logical material pixels.
                for pose in poses:
                    material_pixels = pose['Camera']['GameTransform.x'] * 540 / 400
                    if abs(material_pixels / 8 - round(material_pixels / 8)) > .0001:
                        raise RuntimeError(f'Integer-pan pose is not aligned: {material_pixels}')
            save(output / f'{scene}-pose-check.json', dict(changing_entity=moving,
                 static_entities_preserved=True, frames=[0, 120], poses=poses))
            print(f'{scene}: deterministic 360-frame replay and pose checks passed', flush=True)
    if 'near-plane-bounds' in cases:
        snapshot = author('captureRuntimeSnapshot', dict(kind='scene', name='SdfNearPlaneBounds', frames=0,
            entity_limit=128, fields=['GameTransform.x', 'GameGeometry3D.elevation',
                                     'GameVisualTransform.scale', 'GameCamera.focal_distance']))
        save(output / 'near-plane-resolved-snapshot.json', snapshot)
        fields = {entity['name']: {component['name'].split('.')[-1] + '.' + field['name']: field['value']
                  for component in entity['components'] for field in component['fields']}
                  for entity in snapshot['entities']}
        box = fields['Near clipped side box']
        expected = {'GameTransform.x': .375, 'GameGeometry3D.elevation': 31, 'GameVisualTransform.scale': .0038}
        if any(abs(box[key] - value) > .000001 for key, value in expected.items()):
            raise RuntimeError(f'Near-plane actor overrides were lost: {box}')
        if fields['Camera']['GameCamera.focal_distance'] != 32:
            raise RuntimeError('Near-plane camera does not match the fixture')
    if args.graph_review:
        for name in ['HrcReuseLightSweep', 'HrcReuseBlockerSweep', 'HrcReuseCameraPan', 'HrcReuseIntegerPan']:
            path = output / f'{name}-graph.png'
            run = subprocess.run([str(args.build.resolve() / 'chibi'), '--project-root', str(project),
                                  '--open', name, '--screenshot', str(path)], cwd=ROOT, env=env,
                                 text=True, capture_output=True, timeout=180)
            (output / f'{name}-graph.log').write_text(run.stdout + run.stderr)
            if run.returncode or not path.is_file():
                raise RuntimeError(f'Graph capture failed: {name}')

    rows, comparisons = [], []
    def host(stem, moment):
        result = dict(timestamp=time.time(), gpu_utilization='unavailable')
        for name, command in [('load', ['uptime']), ('memory', ['vm_stat']),
                              ('processes', ['ps', '-axo', 'pid,ppid,pcpu,pmem,etime,comm'])]:
            run = subprocess.run(command, text=True, capture_output=True, timeout=30)
            result[name] = dict(returncode=run.returncode, stdout=run.stdout, stderr=run.stderr)
        save(output / f'{stem}-{moment}-host.json', result)

    def capture(case, mode, trial, frame, measured):
        scene, reference, optimized = CASES[case]
        stem = f'{case}-{mode}-trial{trial}-frame{frame}'
        request = dict(contract=1, operation='preview_scene', kind='scene', name=scene,
                       width=args.width, height=args.height, frames=frame,
                       render_frames=240 if measured else (3 if case == 'reproject-integer-pan' else 2), freeze_simulation=False,
                       horizon_directions=8, horizon_reuse=reference if mode == 'reference' else optimized)
        if case in OVERRIDES:
            request.update(OVERRIDES[case][0 if mode == 'reference' else 1])
        path = output / f'{stem}-request.json'
        save(path, request)
        if measured:
            host(stem, 'before')
        started = time.monotonic()
        run = subprocess.run([str(controller), '--project-root', str(project), '--render-request', str(path)],
                             cwd=ROOT, env=env, text=True, capture_output=True, timeout=600)
        (output / f'{stem}.log').write_text(run.stderr)
        if run.returncode:
            raise RuntimeError(run.stdout or run.stderr)
        response = json.loads(run.stdout)
        if not response.get('ok'):
            raise RuntimeError(response)
        result = response['result']
        preview = result['preview']
        png = base64.b64decode(preview.pop('data'))
        image = output / f'{stem}.png'
        image.write_bytes(png)
        save(output / f'{stem}-result.json', response)
        performance = result['performance']
        options = request['horizon_reuse']
        work = performance['horizon_work']
        expected = dict(pack_passes=0 if options['fuse_relief'] else 1,
                        fused_packs=1 if options['fuse_relief'] else 0,
                        hierarchy_passes=3 if options['hierarchy'] else 0,
                        dirty_passes=3 if options['cache'] else 0)
        if any(work[key] != value for key, value in expected.items()):
            raise RuntimeError(f'Requested horizon path was not exercised: {options}: {work}')
        if case == 'near-plane-bounds' and performance['mesh_work']['sdf_instances'] != 2:
            raise RuntimeError(f'Near-plane visible/culling fixture failed: {performance["mesh_work"]}')
        if case == 'reproject-integer-pan' and mode == 'optimized' and not measured and work['reproject_passes'] != 1:
            raise RuntimeError(f'Integer-pan fixture did not reproject: {work}')
        if not options['cache'] and (work['reuse_passes'] or work['reproject_passes']):
            raise RuntimeError(f'Disabled cache reused history: {work}')
        if not options['reproject'] and work['reproject_passes']:
            raise RuntimeError(f'Disabled reprojection shifted history: {work}')
        if measured and (performance['warmup_frames'] != 240 or performance['samples'] != 224 or performance['instrumented']):
            raise RuntimeError(f'Invalid measurement conditions: {performance}')
        row = dict(case=case, mode=mode, trial=trial, frame=frame, measured=measured,
                   wall_seconds=time.monotonic() - started, image=str(image),
                   png_sha256=hashlib.sha256(png).hexdigest(), final_state=result['final_state'], performance=performance)
        rows.append(row)
        save(output / 'summary.json', rows)
        print(json.dumps(row), flush=True)
        if measured:
            host(stem, 'after')
        return row

    def compare(case, reference, optimized):
        if reference['final_state'] != optimized['final_state']:
            raise RuntimeError(f'Simulation diverged: {case}')
        difference = image_difference(Path(reference['image']), Path(optimized['image']))
        exact = reference['png_sha256'] == optimized['png_sha256']
        # Cache/reprojection are reuse mechanisms; they must preserve final pixels.
        # Fusion/hierarchy arithmetic differences are retained for direct review.
        if case.startswith(('cache-', 'reproject-', 'hidden-', 'spatial-scratch-')) or case in ['instance-batch-control', 'near-plane-bounds', 'normal-analytic-fallback']:
            if not exact:
                raise RuntimeError(f'Exact horizon reuse image parity failed: {case}: {difference}')
        comparisons.append(dict(case=case, trial=reference['trial'], frame=reference['frame'],
                                measured=reference['measured'], identical_png=exact, difference=difference))
        save(output / 'comparisons.json', comparisons)

    if args.phase in ('images', 'all'):
        for case in cases:
            for frame in image_frames(case):
                compare(case, capture(case, 'reference', 0, frame, False),
                        capture(case, 'optimized', 0, frame, False))
    if args.phase in ('measure', 'all'):
        for trial in range(1, args.trials + 1):
            for case in cases:
                modes = ['reference', 'optimized'] if trial % 2 else ['optimized', 'reference']
                pair = {mode: capture(case, mode, trial, 120, True) for mode in modes}
                compare(case, pair['reference'], pair['optimized'])
    print('Completed all requested checks; no failed runs discarded.', flush=True)


if __name__ == '__main__':
    main()
