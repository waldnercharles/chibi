#!/usr/bin/env python3
"""Compare moving optical scenes with settled renders of the identical final state."""
import argparse
import base64
import json
from pathlib import Path
import subprocess

from benchmark_hrc_lab import fingerprint, prepare, write_json
from check_neon_swarm import ROOT

SCENES = ['HRCLabMovingLens', 'HRCLabMovingPrism']


def capture(args, runner, scene, frames, frozen, output):
    output.mkdir(parents=True, exist_ok=False)
    request = dict(contract=1, operation='preview_scene', kind='scene', name=scene,
                   frames=frames + 1 if frozen else 1, render_frames=frames,
                   freeze_simulation=frozen, width=args.width, height=args.height)
    write_json(output / 'request.json', request)
    run = subprocess.run([str((ROOT / args.build / 'chibi_ctl').resolve()),
                          '--project-root', str(runner.project),
                          '--render-request', str(output / 'request.json')],
                         text=True, capture_output=True, timeout=180)
    (output / 'render.log').write_text(run.stderr)
    response = json.loads(run.stdout)
    if run.returncode or not response.get('ok'):
        raise RuntimeError(f'capture failed: {output}; {response.get("error")}')
    result = response['result']
    image = result['preview'].pop('data')
    (output / 'frame.png').write_bytes(base64.b64decode(image))
    write_json(output / 'result.json', result)
    assert result['performance']['hidden_window']
    assert result['performance']['frozen'] == frozen
    assert result['final_state']['frame'] == frames + 1
    if frozen:
        assert result['initial_state'] == result['final_state']
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', default='build-release')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--width', type=int, default=960)
    parser.add_argument('--height', type=int, default=540)
    parser.add_argument('--frames', type=int, nargs='+', default=[30, 60, 120])
    parser.add_argument('--scene', choices=SCENES + ['all'], default='all')
    args = parser.parse_args()
    if any(n < 17 or n > 600 for n in args.frames):
        parser.error('capture frames must be 17..600')
    args.output = args.output.resolve()
    args.project = ROOT / 'projects/hrc_lab'
    args.hrc_prototype = 'HRCLabHRC'
    args.max_light_size = 480
    args.trace_levels = 1
    args.upscale_mode = 1
    args.output.mkdir(parents=True, exist_ok=True)
    write_json(args.output / 'provenance.json', dict(
        git_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        git_dirty=bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT)),
        binary_sha256=fingerprint(ROOT / args.build / 'chibi_ctl'),
        shaders_sha256=fingerprint(ROOT / 'src/render/shaders')))
    reports = []
    scenes = SCENES if args.scene == 'all' else [args.scene]
    for scene in scenes:
        case = args.output / scene
        case.mkdir(exist_ok=False)
        runner = prepare(args, case, scene, .25)
        start = runner.call('runtime_snapshot', kind='scene', name=scene, frames=1,
                            entity_limit=32, component_limit=32, field_limit=32)
        finish = runner.call('runtime_snapshot', kind='scene', name=scene,
                             frames=max(args.frames) + 1,
                             entity_limit=32, component_limit=32, field_limit=32)
        write_json(case / 'start.json', start)
        write_json(case / 'finish.json', finish)
        assert start['world_hash'] != finish['world_hash']
        for frames in args.frames:
            live_dir = case / f'live-{frames}'
            settled_dir = case / f'settled-{frames}'
            live = capture(args, runner, scene, frames, False, live_dir)
            settled = capture(args, runner, scene, frames, True, settled_dir)
            assert live['final_state'] == settled['final_state'], 'motion comparisons reached different states'
            paths = [str(d / 'frame.png') for d in [settled_dir, live_dir]]
            metrics = json.loads(subprocess.check_output(
                ['node', str(ROOT / 'tools/compare_neon_frames.mjs'), *paths], text=True))
            passed = metrics['mean_absolute_rgb_8bit'] < 1 and metrics['p99_rgb_8bit'] <= 5
            report = dict(scene=scene, frames=frames, passed=passed, metrics=metrics,
                          live=str(live_dir.relative_to(args.output)),
                          settled=str(settled_dir.relative_to(args.output)),
                          performance=live['performance'])
            reports.append(report)
            write_json(args.output / 'summary.json', reports)
            print(f'{scene} frame {frames}: mean {metrics["mean_absolute_rgb_8bit"]:.3f}, '
                  f'p99 {metrics["p99_rgb_8bit"]}, {"PASS" if passed else "FAIL"}', flush=True)
    assert all(r['passed'] for r in reports), 'moving-light history exceeds the image-error bound'


if __name__ == '__main__':
    main()
