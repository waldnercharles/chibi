#!/usr/bin/env python3
"""Validate SDF lab scenes and save previews or warmed native measurements."""
import argparse
import base64
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SCENES = ['SdfShapes', 'SdfMotion', 'SdfHrcComparison', 'SdfInstances',
          'SdfSwarm256', 'SdfSwarm1000', 'SdfInstanceSwarm1000',
          'SdfQualityStudy', 'SdfHorizonStudy', 'SdfStaticInstances1000']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/sdf-lab-evidence')
    parser.add_argument('--scene', action='append', choices=SCENES)
    parser.add_argument('--width', type=int, default=960)
    parser.add_argument('--height', type=int, default=540)
    parser.add_argument('--sdf-batch-limit', type=int, choices=range(257), default=0,
                        metavar='0..256', help='Zero uses the default; one disables grouping for comparison')
    parser.add_argument('--frames', type=int, default=120,
                        help='Simulation frames before capture or measurement')
    parser.add_argument('--benchmark', action='store_true',
                        help='Measure 240 advancing frames after 240 frozen warmup frames')
    parser.add_argument('--surface-samples', type=int, choices=[1, 2])
    parser.add_argument('--sdf-pixel-error', type=float)
    parser.add_argument('--normal-samples', type=int, choices=[4, 6])
    parser.add_argument('--horizon-directions', type=int, choices=[0, 4, 8])
    parser.add_argument('--sdf-trace-debug', action='store_true')
    parser.add_argument('--sdf-lod', action=argparse.BooleanOptionalAction, default=None)
    args = parser.parse_args()
    project = ROOT / 'projects/sdf_lab'
    controller = args.build.resolve() / 'chibi_ctl'
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, CHIBI_ROOT=str(project), CHIBI_CTL=str(controller),
                       MallocNanoZone='0', ASAN_OPTIONS='detect_leaks=0')

    def author(name, arguments):
        request = dict(jsonrpc='2.0', id=1, method='tools/call',
                       params=dict(name=name, arguments=arguments))
        run = subprocess.run(['node', str(ROOT / 'tools/mcp/server.mjs')],
                             input=json.dumps(request) + '\n', text=True,
                             capture_output=True, env=environment, cwd=ROOT, timeout=180)
        if run.returncode:
            raise RuntimeError(run.stderr or run.stdout)
        response = json.loads(run.stdout)
        if response.get('error') or response['result'].get('isError'):
            raise RuntimeError(response)
        return response['result']['structuredContent']['result']

    status = author('getProjectStatus', {})
    (output / 'project-status.json').write_text(json.dumps(status, indent=2) + '\n')
    if Path(status['project_root']).resolve() != project.resolve():
        raise RuntimeError('MCP did not confirm the SDF lab project root')
    results = []
    for scene in args.scene or SCENES:
        validation = author('validateAsset', dict(kind='scene', name=scene))
        (output / f'{scene}-validation.json').write_text(json.dumps(validation, indent=2) + '\n')
        if not validation['valid']:
            raise RuntimeError(validation)
        request = dict(contract=1, operation='preview_scene', kind='scene', name=scene,
                       width=args.width, height=args.height, frames=args.frames,
                       render_frames=240 if args.benchmark else 2,
                       sdf_batch_limit=args.sdf_batch_limit,
                       freeze_simulation=not args.benchmark)
        if any(value is not None for value in
               [args.surface_samples, args.sdf_pixel_error, args.normal_samples, args.sdf_lod]):
            request['surface_quality'] = dict(
                samples=args.surface_samples if args.surface_samples is not None else 2,
                pixel_error=args.sdf_pixel_error if args.sdf_pixel_error is not None else 0,
                normal_samples=args.normal_samples if args.normal_samples is not None else 6,
                lod=args.sdf_lod if args.sdf_lod is not None else True)
        if args.horizon_directions is not None:
            request['horizon_directions'] = args.horizon_directions
        if args.sdf_trace_debug:
            request['sdf_trace_debug'] = True
        request_path = output / f'{scene}-request.json'
        request_path.write_text(json.dumps(request, indent=2) + '\n')
        run = subprocess.run([str(controller), '--project-root', str(project),
                              '--render-request', str(request_path)],
                             text=True, capture_output=True, cwd=ROOT, env=environment,
                             timeout=600)
        (output / f'{scene}.log').write_text(run.stderr)
        if run.returncode:
            raise RuntimeError(run.stdout or run.stderr)
        response = json.loads(run.stdout)
        if not response.get('ok'):
            raise RuntimeError(response)
        result = response['result']
        preview = result['preview']
        (output / f'{scene}.png').write_bytes(base64.b64decode(preview.pop('data')))
        (output / f'{scene}-result.json').write_text(json.dumps(response, indent=2) + '\n')
        summary = dict(scene=scene, backend=result.get('backend'),
                       performance=result.get('performance'))
        results.append(summary)
        print(json.dumps(summary), flush=True)
    (output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
