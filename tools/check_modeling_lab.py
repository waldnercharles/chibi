#!/usr/bin/env python3
"""Validate saved modeling studies and capture native scenes through Chibi MCP."""
import argparse
import base64
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / 'projects/modeling_lab'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/model-evidence')
    parser.add_argument('--scene', action='append')
    parser.add_argument('--width', type=int, default=1080)
    parser.add_argument('--height', type=int, default=740)
    parser.add_argument('--frames', type=int, default=0)
    parser.add_argument('--benchmark', action='store_true')
    parser.add_argument('--animate', action='store_true', help='Advance scene behaviors during the measured render interval')
    parser.add_argument('--render-frames', type=int, default=2)
    parser.add_argument('--sdf-batch-limit', type=int, default=0)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, CHIBI_ROOT=str(PROJECT),
                       CHIBI_CTL=str(args.build.resolve() / 'chibi_ctl'),
                       MallocNanoZone='0', ASAN_OPTIONS='detect_leaks=0')

    def call(tool, arguments):
        request = dict(jsonrpc='2.0', id=1, method='tools/call',
                       params=dict(name=tool, arguments=arguments))
        run = subprocess.run(['node', str(ROOT / 'tools/mcp/server.mjs')],
                             input=json.dumps(request) + '\n', text=True,
                             capture_output=True, env=environment, cwd=ROOT,
                             timeout=900)
        if run.returncode:
            raise RuntimeError(run.stderr or run.stdout)
        response = json.loads(run.stdout)
        if response.get('error') or response['result'].get('isError'):
            raise RuntimeError(response)
        return response['result']

    status = call('getProjectStatus', {})['structuredContent']['result']
    if Path(status['project_root']).resolve() != PROJECT.resolve():
        raise RuntimeError('Unexpected authoring project root')
    (output / 'project-status.json').write_text(json.dumps(status, indent=2) + '\n')
    scenes = args.scene or sorted(p.stem for p in (PROJECT / 'content/scenes').glob('*.kdl'))
    results = []
    for name in scenes:
        validation = call('validateAsset', dict(kind='scene', name=name))['structuredContent']['result']
        if not validation['valid']:
            raise RuntimeError(validation)
        request = dict(kind='scene', name=name, width=args.width, height=args.height,
                       frames=args.frames, render_frames=240 if args.benchmark else args.render_frames,
                       freeze_simulation=not args.animate, sdf_batch_limit=args.sdf_batch_limit)
        (output / f'{name}-request.json').write_text(json.dumps(request, indent=2) + '\n')
        response = call('previewScene', request)
        for item in response.get('content', []):
            if item['type'] == 'image':
                (output / f'{name}.png').write_bytes(base64.b64decode(item['data']))
        result = response['structuredContent']
        (output / f'{name}-result.json').write_text(json.dumps(result, indent=2) + '\n')
        summary = dict(scene=name, backend=result['result'].get('backend'),
                       performance=result['result'].get('performance'))
        results.append(summary)
        print(json.dumps(summary), flush=True)
    (output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
