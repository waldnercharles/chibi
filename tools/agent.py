#!/usr/bin/env python3
"""Bounded agent iteration; never certifies the full hardware gate."""
import argparse
import json
import os
from pathlib import Path
import re
import signal
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
CPU = re.findall(r'CHIBI_CPU_SELECTOR\("([^"]+)"\)',
                 (ROOT / 'test/execution_selectors.h').read_text())
GPU = re.findall(r'CHIBI_GPU_SELECTOR\("([^"]+)"\)',
                 (ROOT / 'test/execution_selectors.h').read_text())
START = time.monotonic()
HEALTH = None
NEXT = 'CHIBI_EXECUTION_PROFILE=hardware CHIBI_ALLOW_FULL_GPU=1 make test test-render'


class TerminationRequested(BaseException):
    """SIGTERM must unwind the same cleanup scopes as Ctrl-C."""


def terminate(_signal, _frame):
    raise TerminationRequested()


def report(status, reason, **extra):
    value = dict(ok=status == 'passed', status=status, reason=reason,
                 coverage='focused-iteration', full_verification=False,
                 elapsed_seconds=round(time.monotonic() - START, 3), capabilities=HEALTH,
                 pending=[dict(check='full-native/render', status='pending', command=NEXT)],
                 required_checks_outstanding=['full-native', 'render'], next_command=NEXT)
    value.update(extra)
    print(json.dumps(value), flush=True)
    print(f'Agent iteration: {status} ({reason}); full hardware verification outstanding.', file=sys.stderr)
    return 0 if status == 'passed' else 2


def startup_health():
    """Keep a broken loader from hanging/crashing an otherwise CPU-only action."""
    try:
        result = subprocess.run([sys.executable, str(ROOT / 'tools/gpu_capabilities.py')],
                                capture_output=True, text=True, timeout=5, check=True)
        return json.loads(result.stdout)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        return dict(status='unavailable', devices=[], acceleration='unknown',
                    hardware_available=False, reason=str(error), discovery_timeout_seconds=5)


def test_plan(purpose, health):
    """A small purpose-based plan; discovery never authorizes a full suite."""
    plan = dict(purpose=purpose, capabilities=health, full_verification=False,
                pending=[dict(check='full-native/render', status='pending', command=NEXT)])
    if purpose == 'logic':
        return dict(plan, commands=['make agent-test AGENT_SELECTOR=--graph-optimize'],
                    gpu_startup=False, reason='logic_change_selects_CPU_assertions')
    if health.get('status') != 'enumerated' or not health.get('devices'):
        plan['discovery_note'] = 'Vulkan discovery is inconclusive; Metal/GLES/other rendering may work. Verify the actual bounded runtime result.'
    if purpose == 'temporal':
        return dict(plan, commands=['python3 tools/agent.py mp4 REQUEST.json --frames 60 --output OUTPUT.mp4'],
                    requires_explicit_mp4_request=True, warmup='set gpu_warmup_frames for the scene',
                    output_frames=60, render_seconds=120, gpu_startup=True)
    return dict(plan, commands=['make agent-gpu-test AGENT_SELECTOR=--model-sampled-gpu'],
                gpu_startup=True, render_seconds=60, reference_resolution='unchanged',
                preview_size=[320, 180] if health.get('acceleration') == 'hardware' else [96, 64],
                reason='purposeful_GPU_assertions_on_' + health.get('acceleration', 'unknown') + '_renderer')


def run(command, timeout, env=None, request=None, rss_mib=None):
    # A file avoids pipe deadlocks and keeps arbitrary child logs out of JSON stdout.
    with tempfile.TemporaryFile(mode='w+') as log, tempfile.TemporaryFile(mode='w+') as source:
        if request is not None:
            source.write(json.dumps(request)); source.seek(0)
        child = subprocess.Popen(command, cwd=ROOT, env=env, stdin=source,
                                 stdout=log, stderr=log, start_new_session=True)
        reason = None
        try:
            start = time.monotonic()
            while child.poll() is None:
                if time.monotonic() - start > timeout:
                    reason = 'time_budget_exceeded'
                if rss_mib is not None:
                    try:
                        status = Path(f'/proc/{child.pid}/status').read_text()
                        rss = re.search(r'^VmRSS:\s+(\d+)', status, re.M)
                        if rss and int(rss[1]) > rss_mib * 1024:
                            reason = 'resident_memory_budget_exceeded'
                    except FileNotFoundError:
                        pass
                if reason:
                    break
                time.sleep(.05)
        finally:
            # Own the detached group, including descendants of a normally
            # exiting child. Also runs for Ctrl-C and monitoring exceptions.
            try:
                os.killpg(child.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            finally:
                child.wait()
        log.seek(0)
        output = log.read()
        if reason:
            raise RuntimeError(reason)
        if child.returncode:
            raise RuntimeError(f'child_exit_{child.returncode}: {output[-4000:]}')
        return output


def main():
    global HEALTH
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', default='build')
    sub = parser.add_subparsers(dest='action', required=True)
    plan = sub.add_parser('plan', help='Choose focused logic/render/temporal work using discovered capabilities')
    plan.add_argument('purpose', choices=['logic', 'render', 'temporal'])
    sub.add_parser('doctor', help='Read-only Vulkan device discovery; no rendering or driver changes')
    sub.add_parser('list', help='CPU classification, explicit GPU checks and budgets')
    sub.add_parser('hardware-check', help='Refuse unless full hardware validation is opted in')
    from validation_plan import add_parser, validation_plan
    add_parser(sub)
    for action in ['test', 'gpu-test']:
        test = sub.add_parser(action)
        test.add_argument('selector', help='Pass -- before the native --selector')
    for action in ['preview', 'mp4']:
        render = sub.add_parser(action)
        render.add_argument('request', type=Path, help='Existing preview_scene/preview_shader JSON request')
        render.add_argument('--project-root', default='.')
        render.add_argument('--output', type=Path, required=True)
        if action == 'mp4':
            render.add_argument('--frames', type=int, default=60, help='3..180 output frames at 60 fps (max 3 seconds)')
    args = parser.parse_args()
    if args.action == 'validation-plan':
        return validation_plan(args, ROOT, CPU, GPU)
    if args.action in ['doctor', 'plan', 'test', 'gpu-test', 'preview', 'mp4']:
        HEALTH = startup_health()
    if args.action == 'doctor':
        print(json.dumps(HEALTH))
        return 0 if HEALTH.get('status') == 'enumerated' else 2
    if args.action == 'plan':
        plan = test_plan(args.purpose, HEALTH)
        print(json.dumps(plan))
        return 2 if plan.get('status') == 'blocked' else 0
    if args.action == 'list':
        print(json.dumps(dict(cpu_selectors=CPU, opt_in_gpu_selectors=GPU,
                              other_selectors='hardware-required or unclassified (never silently run)',
                              authoring_workloads=dict(
                                  cpu=['asset inspection/edit/validation', 'preview_field', 'run_scenario', 'profile_scenario (simulation)'],
                                  bounded_gpu=['preview_shader', 'preview_scene'],
                                  explicit_video=['mp4'], full_opt_in=['test/render suites', 'performance profiling']),
                              preview=dict(output_frames=1, warmup_frames='0..32, default 1 for scene',
                                           pixels=640*360, max_side=640, seconds=30),
                              mp4=dict(output_frames='3..180', fps=60, duration_seconds=3,
                                       warmup_frames='0..32', seconds=120, raw_bytes=256*1024*1024),
                              render_rss_mib=2048, render_mode='hidden-window',
                              display_context='Linux prefers supported SDL offscreen; explicit drivers respected; bounded Xvfb fallback documented',
                              next_command=NEXT)))
        return 0
    if args.action == 'hardware-check':
        if os.environ.get('CHIBI_EXECUTION_PROFILE') == 'hardware' and os.environ.get('CHIBI_ALLOW_FULL_GPU') == '1':
            return 0
        return report('blocked', 'full_gate_requires_explicit_hardware_opt_in')
    env = os.environ.copy()
    if args.action in ['test', 'gpu-test']:
        allowed = CPU if args.action == 'test' else GPU
        if args.selector not in allowed:
            return report('blocked', 'selector_not_classified_for_requested_capability', selector=args.selector)
        env['CHIBI_EXECUTION_PROFILE'] = 'iteration' if args.action == 'test' else 'gpu-check'
        run([str(ROOT / args.build_dir / 'test_suite'), args.selector], 180 if args.action == 'test' else 60, env)
        return report('passed', 'selected_assertions_passed', selector=args.selector,
                      skipped=[dict(check='full-native/render', reason='requires_hardware_opt_in')])
    if not Path('/proc/self/status').exists():
        return report('blocked', 'render_RSS_budget_requires_Linux_proc_use_hardware_profile_on_other_hosts')
    request = json.loads(args.request.read_text())
    if not isinstance(request, dict):
        return report('blocked', 'request_must_be_object')
    env['CHIBI_EXECUTION_PROFILE'] = 'mp4' if args.action == 'mp4' else 'iteration'
    env.setdefault('SDL_LOGGING', 'gpu=info')  # Observe the selected device; do not change it.
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.output.exists():
        return report('blocked', 'output_exists_choose_new_path')
    if args.action == 'mp4' and (not 3 <= args.frames <= 180 or args.output.suffix != '.mp4'):
        return report('blocked', 'mp4_requires_3_to_180_frames_and_mp4_output')
    if args.action == 'mp4' and not shutil.which('ffmpeg'):
        return report('blocked', 'mp4_requires_ffmpeg_no_render_started')
    with tempfile.TemporaryDirectory(prefix='chibi-capture-') as directory:
        if args.action == 'mp4':
            request.update(render_frames=args.frames, capture_frames=f'{directory}/frames.rgba')
        output = run([str(ROOT / args.build_dir / 'chibi_ctl'), '--render', '--project-root',
                      str(Path(args.project_root).resolve())],
                     120 if args.action == 'mp4' else 30, env, request, 2048)
        responses = []
        for line in output.splitlines():
            try:
                responses.append(json.loads(line))
            except json.JSONDecodeError:
                pass
        if not responses or not responses[-1].get('ok'):
            return report('blocked', 'render_failed', response=responses[-1] if responses else output[-2000:])
        response = responses[-1]
        if args.action == 'mp4':
            width, height = request.get('width', 640), request.get('height', 360)
            raw = Path(request['capture_frames'])
            if raw.stat().st_size != width * height * 4 * args.frames:
                return report('blocked', 'incomplete_frame_capture')
            staged = Path(directory) / 'capture.mp4'
            run(['ffmpeg', '-v', 'error', '-f', 'rawvideo', '-pixel_format', 'rgba',
                 '-video_size', f'{width}x{height}', '-framerate', '60', '-i', str(raw),
                 '-frames:v', str(args.frames), '-c:v', 'libx264', '-pix_fmt', 'yuv420p', str(staged)], 30)
            args.output.write_bytes(staged.read_bytes())
        else:
            # Preserve the existing authoring response, including its encoded image and diagnostics.
            args.output.write_text(json.dumps(response) + '\n')
    return report('passed', 'bounded_artifact_created_not_full_verification', output=str(args.output),
                  width=response['result']['preview']['width'],
                  height=response['result']['preview']['height'],
                  window_hidden=response.get('window_hidden'),
                  planned_gpu_frames=response.get('planned_gpu_frames', 1),
                  render_time_budget_seconds=120 if args.action == 'mp4' else 30,
                  encode_time_budget_seconds=30 if args.action == 'mp4' else 0,
                  rss_budget_mib=2048,
                  duration_seconds=args.frames / 60 if args.action == 'mp4' else None,
                  active_backend=response['result'].get('backend'),
                  active_device=(re.search(r'Vulkan Device: ([^\r\n]+)', output).group(1)
                                 if re.search(r'Vulkan Device: ([^\r\n]+)', output) else 'not_reported'),
                  video_driver=response['result'].get('video_driver', 'not_reported'),
                  render_mode=('offscreen' if response['result'].get('video_driver') == 'offscreen'
                               else 'hidden-window' if response.get('window_hidden') is True
                               else 'visible-window' if response.get('window_hidden') is False
                               else 'not_reported'),
                  display_context=('offscreen' if response['result'].get('video_driver') == 'offscreen'
                                   else os.environ.get('CHIBI_PREVIEW_DISPLAY_CONTEXT', response['result'].get('video_driver', 'not_reported'))),
                  output_frames=args.frames if args.action == 'mp4' else 1,
                  warmup_frames=request.get('gpu_warmup_frames', 1 if request.get('operation') == 'preview_scene' else 0))


if __name__ == '__main__':
    signal.signal(signal.SIGTERM, terminate)
    try:
        sys.exit(main())
    except TerminationRequested:
        report('blocked', 'terminated_by_SIGTERM')
        sys.exit(143)
    except KeyboardInterrupt:
        report('blocked', 'interrupted')
        sys.exit(130)
    except (OSError, ValueError, RuntimeError) as error:
        sys.exit(report('failed' if str(error).startswith('child_exit_') else 'blocked', str(error)))
