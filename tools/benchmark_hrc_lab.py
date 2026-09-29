#!/usr/bin/env python3
"""Capture static HRC fixtures at several resolutions and preserve comparison evidence.

Each case copies the project and fixes its HRC prototype with adaptive off: the
finest light grid at one --resolution, and the shared color resolution, gather
resolution, trace levels and upscale mode. Every case records frame
time (median, p95, p99) and lighting GPU milliseconds per frame, the sum of the
HRC passes, Surface light, Emission mipmaps and Composite. Frames are compared
with the resolution 1 case of the same scene and repeat.
"""
import argparse
import base64
import hashlib
import html
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess

from check_neon_swarm import ROOT, Runner


DEFAULT_PROJECT = ROOT / 'projects/arena2d'
DEFAULT_HRC_PROTOTYPE = 'HRC'
ARENA_SCENES = [
    'HRCPinhole',
    'HRCBounce',
    'HRCSinglePixel',
    'HRCShadow',
    'HRCVolume',
]
CLEAN_SCENES = [
    'HRCLabPinhole',
    'HRCLabBounce',
    'HRCLabSinglePixel',
    'HRCLabShadow',
    'HRCLabVolume',
    'HRCLabTwoLights',
    'HRCLabColorBoundary',
    'HRCLabOpaqueInterior',
]

# Passes outside the "HRC " prefix that lighting also pays for.
LIGHTING_PASSES = ['Surface light', 'Emission mipmaps', 'Composite']
COLOR_RESOLUTIONS = ['Full', 'Half', 'Quarter', 'Eighth']
GATHER_RESOLUTIONS = ['Full', 'Half']

UPSCALE_MODE_NAMES = {
    0: 'plain bilinear',
    1: 'edge-aware bilinear to nearest',
    2: 'joint bilateral',
    3: 'five-tap cross',
}


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


def lighting_ms(passes):
    return sum(item['ms'] for item in passes
               if item['name'].startswith('HRC ') or item['name'] in LIGHTING_PASSES)


def prepare(args, output, scene, resolution):
    project = output / 'project'
    shutil.copytree(args.project, project, ignore=shutil.ignore_patterns('.chibi', 'build'))
    runner = Runner(args.build, output, project)
    asset = runner.author('readAsset', dict(kind='prototype', name=args.hrc_prototype))
    fields = dict(adaptive=False, resolution=resolution,
                  color_resolution=args.color_resolution,
                  gather_resolution=args.gather_resolution,
                  trace_levels=args.trace_levels, upscale_mode=args.upscale_mode)
    runner.author('buildPrototype', dict(
        kind='prototype', name=args.hrc_prototype, expected_hash=asset['hash'],
        operations=[dict(op='set_field', component='GameHRC2D', field=field, value=value)
                    for field, value in fields.items()]))
    runner.author('validateAsset', dict(kind='prototype', name=args.hrc_prototype))
    runner.author('validateAsset', dict(kind='scene', name=scene))
    return runner


def render(args, output, runner, scene, resolution, repeat):
    request = dict(
        contract=1,
        operation='preview_scene',
        kind='scene',
        name=scene,
        frames=1,
        render_frames=args.render_frames,
        freeze_simulation=True,
        pass_timing=True,
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
        raise RuntimeError(f'render failed for {scene} resolution {resolution}; see {output / "render.log"}')
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
    if abs(performance['hrc_resolution'] - resolution) > 1e-6:
        raise RuntimeError(f'{scene} rendered at HRC resolution {performance["hrc_resolution"]:g}; '
                           'does the scene override GameHRC2D?')
    lighting =lighting_ms(performance['passes'])
    (output / 'frame.png').write_bytes(base64.b64decode(result['preview']['data']))
    report = dict(
        scene=scene,
        resolution=resolution,
        color_resolution=args.color_resolution,
        gather_resolution=args.gather_resolution,
        repeat=repeat,
        trace_levels=args.trace_levels,
        upscale_mode=args.upscale_mode,
        upscale_mode_name=UPSCALE_MODE_NAMES[args.upscale_mode],
        adaptive=False,
        width=args.width,
        height=args.height,
        purpose='metal-capture' if args.metal_capture else 'benchmark',
        project=str(args.project),
        hrc_prototype=args.hrc_prototype,
        backend=result['backend'],
        performance=performance,
        lighting_gpu_ms=lighting,
        capture=result.get('metal_capture'),
        directory=output.name,
        project_sha256=fingerprint(runner.project),
    )
    write_json(output / 'report.json', report)
    print(
        f'{scene:28} resolution={resolution:g} color={args.color_resolution} '
        f'gather={args.gather_resolution}: '
        f'median {performance["median_ms"]:.2f} ms, p95 {performance["p95_ms"]:.2f} ms, '
        f'p99 {performance["p99_ms"]:.2f} ms, lighting GPU {lighting:.2f} ms', flush=True)
    return report


def compare_frozen(output, reports):
    comparisons = []
    for candidate in reports:
        if candidate.get('purpose') != 'benchmark' or candidate['resolution'] == 1:
            continue
        reference = next((item for item in reports
                          if item.get('purpose') == 'benchmark'
                          and item['scene'] == candidate['scene']
                          and item['resolution'] == 1
                          and item['repeat'] == candidate['repeat']), None)
        if not reference:
            continue
        paths = [str(output / item['directory'] / 'frame.png')
                 for item in [reference, candidate]]
        metrics = json.loads(subprocess.check_output(
            ['node', str(ROOT / 'tools/compare_neon_frames.mjs'), *paths], text=True))
        comparisons.append(dict(
            scene=candidate['scene'], resolution=candidate['resolution'],
            repeat=candidate['repeat'],
            reference=paths[0], candidate=paths[1], metrics=metrics))
    write_json(output / 'comparisons.json', comparisons)
    return comparisons


def summary(output, reports, comparisons=()):
    write_json(output / 'summary.json', reports)
    comparison_by_key = {
        (item['scene'], item['resolution'], item['repeat']): item['metrics']
        for item in comparisons
    }
    lines = [
        '# HRC lab results', '',
        'Frozen scenes use 240 renderer warmup frames and discard the first 16 measured frames.',
        'Frame time includes GPU completion; it is not a per-dispatch GPU timestamp.',
        'Lighting GPU ms is the per-frame average of the passes named `HRC …`, '
        + ', '.join(f'`{name}`' for name in LIGHTING_PASSES) + ', measured with '
        f'`{reports[0]["performance"]["pass_timing_source"]}`; pass timing also runs '
        'during the frame-time samples.' if reports else '', '',
        f'Reconstruction mode: {reports[0]["upscale_mode"]} ({reports[0]["upscale_mode_name"]}).' if reports else '',
        '',
        'Every row links to its captured `frame.png`; `performance.passes` in each `report.json` lists every pass.', '',
        '| Scene | Resolution | Color | Gather | Median ms | p95 ms | p99 ms '
        '| Lighting GPU ms | Mean RGB error vs resolution 1 | Screenshot |',
        '| --- | ---: | --- | --- | ---: | ---: | ---: | ---: | ---: | --- |',
    ]
    for report in reports:
        performance = report.get('performance')
        if not performance:
            continue
        metrics = comparison_by_key.get(
            (report['scene'], report['resolution'], report['repeat']))
        error = '—' if metrics is None else f'{metrics["mean_absolute_rgb_8bit"]:.2f}'
        link = f'[{report["directory"]}/frame.png]({report["directory"]}/frame.png)'
        lines.append(
            f'| {report["scene"]} | {report["resolution"]:g} | {report["color_resolution"]} | '
            f'{report["gather_resolution"]} | '
            f'{performance["median_ms"]:.2f} | {performance["p95_ms"]:.2f} | '
            f'{performance["p99_ms"]:.2f} | {report["lighting_gpu_ms"]:.2f} | {error} | {link} |')
    (output / 'summary.md').write_text('\n'.join(lines) + '\n')

    gallery = [
        '# HRC lab screenshot gallery', '',
        'These are the captured frames from the matching `summary.md` run.', '',
    ]
    for report in reports:
        image = (output / report['directory'] / 'frame.png').resolve()
        gallery += [
            f'## {report["scene"]} — resolution {report["resolution"]:g}, '
            f'color {report["color_resolution"]}, gather {report["gather_resolution"]}', '',
            f'![{report["scene"]} resolution {report["resolution"]:g}]({image})', '',
        ]
    (output / 'gallery.md').write_text('\n'.join(gallery) + '\n')

    comparison_by_key = {
        (item['scene'], item['resolution'], item['repeat']): item['metrics']
        for item in comparisons
    }
    rows = []
    for report in reports:
        performance = report.get('performance')
        if not performance:
            continue
        metrics = comparison_by_key.get(
            (report['scene'], report['resolution'], report['repeat']))
        rows.append(dict(
            scene=report['scene'],
            resolution=report['resolution'],
            color_resolution=report['color_resolution'],
            gather_resolution=report['gather_resolution'],
            repeat=report['repeat'],
            image=(Path(report['directory']) / 'frame.png').as_posix(),
            median_ms=performance['median_ms'],
            p95_ms=performance['p95_ms'],
            lighting_ms=report['lighting_gpu_ms'],
            error=None if metrics is None else metrics['mean_absolute_rgb_8bit'],
        ))
    data = json.dumps(rows, separators=(',', ':')).replace('<', '\\u003c')
    title = html.escape(output.name)
    (output / 'compare.html').write_text(f'''<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>HRC before and after — {title}</title>
  <style>
    :root {{ color-scheme: light dark; font-family: system-ui, sans-serif; }}
    body {{ margin: 0; padding: 24px; background: #16181b; color: #f2f3f5; }}
    main {{ max-width: 1120px; margin: auto; }}
    h1 {{ margin: 0 0 16px; font-size: 1.35rem; font-weight: 600; }}
    .controls {{ display: flex; flex-wrap: wrap; gap: 12px 20px; align-items: end; margin-bottom: 16px; }}
    label {{ display: grid; gap: 5px; color: #c7cbd1; font-size: .88rem; }}
    select, input[type="range"] {{ accent-color: #78a9ff; }}
    select {{ min-width: 220px; padding: 6px 8px; border: 1px solid #4b515b; border-radius: 5px; background: #22262b; color: inherit; }}
    .stage {{ --position: 50%; position: relative; overflow: hidden; aspect-ratio: 16 / 9; background: #0a0b0d; }}
    .stage img {{ position: absolute; inset: 0; width: 100%; height: 100%; object-fit: contain; }}
    .stage .after {{ clip-path: polygon(var(--position) 0, 100% 0, 100% 100%, var(--position) 100%); }}
    .divider {{ position: absolute; inset: 0 auto 0 var(--position); width: 2px; transform: translateX(-1px); background: #fff; box-shadow: 0 0 0 1px #111; pointer-events: none; }}
    .range {{ position: absolute; inset: 0; width: 100%; height: 100%; margin: 0; opacity: 0; cursor: ew-resize; }}
    .labels {{ display: flex; justify-content: space-between; margin: 8px 0 12px; color: #d9dce1; font-size: .9rem; }}
    .stats {{ color: #b7bdc6; font-size: .9rem; }}
    .stats strong {{ color: #fff; font-weight: 600; }}
    .hint {{ margin: 10px 0 0; color: #8f97a3; font-size: .82rem; }}
  </style>
</head>
<body>
  <main>
    <h1>HRC before and after</h1>
    <div class="controls">
      <label>Scene<select id="scene"></select></label>
      <label>Candidate<select id="candidate"></select></label>
    </div>
    <div class="stage" id="stage">
      <img id="before" alt="Reference render">
      <img id="after" class="after" alt="Candidate render">
      <div class="divider" aria-hidden="true"></div>
      <input id="range" class="range" type="range" min="0" max="100" value="50" aria-label="Reveal candidate render">
    </div>
    <div class="labels"><span id="before-label"></span><span id="after-label"></span></div>
    <div id="stats" class="stats" aria-live="polite"></div>
    <p class="hint">Drag the divider, or use the left and right arrow keys after focusing the image.</p>
  </main>
  <script>
    const rows = {data};
    const scene = document.getElementById('scene');
    const candidate = document.getElementById('candidate');
    const stage = document.getElementById('stage');
    const range = document.getElementById('range');
    const before = document.getElementById('before');
    const after = document.getElementById('after');
    const beforeLabel = document.getElementById('before-label');
    const afterLabel = document.getElementById('after-label');
    const stats = document.getElementById('stats');

    const scenes = [...new Set(rows.map(row => row.scene))];
    for (const name of scenes) scene.add(new Option(name, name));

    function caseLabel(row) {{
      return `resolution ${{Number(row.resolution)}} · color ${{row.color_resolution}} · ` +
        `gather ${{row.gather_resolution}}`;
    }}

    function timing(row) {{
      return `${{row.median_ms.toFixed(2)}} ms median · ${{row.lighting_ms.toFixed(2)}} ms lighting GPU`;
    }}

    function sceneRows() {{
      return rows.filter(row => row.scene === scene.value);
    }}

    function referenceRow(items) {{
      return items.find(row => row.resolution === 1) || items[0];
    }}

    function updateCandidates() {{
      const items = sceneRows();
      const reference = referenceRow(items);
      candidate.replaceChildren();
      for (const row of items) {{
        if (row === reference) continue;
        candidate.add(new Option(caseLabel(row), `${{row.resolution}}:${{row.repeat}}`));
      }}
      if (!candidate.options.length) candidate.add(new Option(caseLabel(reference), 'reference'));
      update();
    }}

    function update() {{
      const items = sceneRows();
      const reference = referenceRow(items);
      const selected = items.find(row => `${{row.resolution}}:${{row.repeat}}` === candidate.value) ||
        items.find(row => row !== reference) || reference;
      before.src = reference.image;
      after.src = selected.image;
      before.alt = `${{reference.scene}} reference, ${{caseLabel(reference)}}`;
      after.alt = `${{selected.scene}} candidate, ${{caseLabel(selected)}}`;
      beforeLabel.textContent = `Reference · ${{caseLabel(reference)}}`;
      afterLabel.textContent = `Candidate · ${{caseLabel(selected)}}`;
      const error = selected.error === null ? 'not measured' : `${{selected.error.toFixed(2)}} RGB error`;
      stats.innerHTML = `<strong>Reference:</strong> ${{timing(reference)}} · ` +
        `<strong>Candidate:</strong> ${{timing(selected)}} · ` +
        `${{selected.p95_ms.toFixed(2)}} ms p95 · ${{error}}`;
      stage.style.setProperty('--position', `${{range.value}}%`);
    }}

    range.addEventListener('input', () => stage.style.setProperty('--position', `${{range.value}}%`));
    scene.addEventListener('change', updateCandidates);
    candidate.addEventListener('change', update);
    scene.selectedIndex = 0;
    updateCandidates();
  </script>
</body>
</html>
''')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', default='build-release')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--project', type=Path, default=DEFAULT_PROJECT)
    parser.add_argument('--hrc-prototype', default=DEFAULT_HRC_PROTOTYPE)
    parser.add_argument('--scene', default='all')
    parser.add_argument('--width', type=int, default=1920)
    parser.add_argument('--height', type=int, default=1080)
    parser.add_argument('--resolution', type=float, nargs='+', default=[1.0, .5],
                        help='Finest light grid as a fraction of the canvas, one case each; '
                             'distinct values in 0.1..1, and 1 is the comparison reference')
    parser.add_argument('--color-resolution', choices=COLOR_RESOLUTIONS, default='Full',
                        help='Color grid relative to --resolution; below Full, brightness solves '
                             'at --resolution and color 2, 4 or 8 times coarser')
    parser.add_argument('--gather-resolution', choices=GATHER_RESOLUTIONS, default='Full',
                        help='Received-light image (gather, filter, bounce) at the full canvas or half')
    parser.add_argument('--trace-levels', type=int, choices=range(1, 6), default=1)
    parser.add_argument('--upscale-mode', type=int, choices=[0, 1, 2, 3], default=1)
    parser.add_argument('--render-frames', type=int, default=180)
    parser.add_argument('--repeat', type=int, default=1)
    parser.add_argument('--metal-capture', action='store_true')
    args = parser.parse_args()
    if not 17 <= args.render_frames <= 1200 or args.repeat < 1:
        parser.error('use 17..1200 render frames and a positive repeat count')
    if any(not .1 <= value <= 1 for value in args.resolution) or \
            len(set(args.resolution)) != len(args.resolution):
        parser.error('HRC resolutions must be distinct values in 0.1..1')
    if not all(64 <= size <= 2048 for size in [args.width, args.height]):
        parser.error('dimensions must be 64..2048 pixels')
    if args.metal_capture and (args.scene == 'all' or len(args.resolution) != 1 or args.repeat != 1):
        parser.error('Metal capture selects one scene, resolution and repeat')
    args.project = args.project.resolve()
    if not args.project.is_dir():
        parser.error(f'project does not exist: {args.project}')
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    default_scenes = CLEAN_SCENES if args.project.name == 'hrc_lab' else ARENA_SCENES
    scenes = default_scenes if args.scene == 'all' else [args.scene]
    unknown = sorted(set(scenes) - set(ARENA_SCENES + CLEAN_SCENES))
    if unknown:
        parser.error(f'unknown scene(s): {", ".join(unknown)}')
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
        for resolution in args.resolution:
            for repeat in range(1, args.repeat + 1):
                case = args.output / f'{scene}-{resolution:g}-{repeat}'
                case.mkdir(parents=True, exist_ok=False)
                runner = prepare(args, case, scene, resolution)
                report = render(args, case, runner, scene, resolution, repeat)
                report.update(provenance)
                write_json(case / 'report.json', report)
                reports.append(report)
                summary(args.output, reports)
    if not args.metal_capture:
        comparisons = compare_frozen(args.output, reports)
        summary(args.output, reports, comparisons)


if __name__ == '__main__':
    main()
