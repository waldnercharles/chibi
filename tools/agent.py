#!/usr/bin/env python3
"""Bounded agent iteration; never certifies the full hardware gate."""
import argparse
from contextlib import nullcontext
import hashlib
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

from test_policy import run_commands, timeout_deadline, TOOLS

ROOT = Path(__file__).resolve().parents[1]
CPU = re.findall(r'CHIBI_CPU_SELECTOR\("([^"]+)"\)',
                 (ROOT / 'test/execution_selectors.h').read_text())
GPU = re.findall(r'CHIBI_GPU_SELECTOR\("([^"]+)"\)',
                 (ROOT / 'test/execution_selectors.h').read_text())
START = time.monotonic()
HEALTH = None
NEXT = 'make test-list'
CPU_SELECTORS = {
    '--execution-scope': 'execution-scope',
    '--audio-analysis': 'audio-analysis',
    '--arena': 'arena',
    
    '--asset-values': 'asset-values',
    
    
    
    
    '--fmt': 'fmt',
    
    
    
    '--material-benchmark': 'material-benchmark',
    '--material-destruction': 'material-destruction',
    '--material-fluids': 'material-fluids',
    '--material-mpm': 'material-mpm',
    
    '--material-simulation': 'material-simulation',
    '--material-ecs': 'material-ecs',
    '--particles': 'particles',
    '--project-document': 'project-document',
    '--prototype-documents': 'prototype-documents',
    
    '--shader-bake': 'shader-bake',
    '--shader-source-debug': 'shader-source-debug',
    '--asset-compiler-host': 'language-asset-compiler-host',
    '--asset-packaging-chibi': 'language-asset-packaging-chibi',
    '--shader-source-chibi': 'language-shader-source-chibi',
    '--material-bake': 'material-bake',
    '--environment-authoring': 'environment-authoring',
    '--material-surface-mesh': 'material-surface-mesh',
    '--terrain-checkpoint': 'terrain-checkpoint',
    '--terrain-seams': 'terrain-seams',
    '--terrain-storage': 'terrain-storage',
    '--terrain-storage-commands': 'terrain-storage-commands',
    
    
    '--source-systems': 'source-systems',
    
    '--text-files': 'text-files',
    '--ui-document': 'ui-document',
    '--vector-nodes': 'vector-nodes',
}
STUDIO_SELECTORS = {'--studio-source-edit': 'studio-source-edit',
                    '--studio-graph-visibility': 'studio-graph-visibility',
                    '--studio-scene-move': 'studio-scene-move',
                    '--studio-outliner-move': 'studio-outliner-move',
                    '--studio-graph-pointer': 'studio-graph-pointer',
                    '--studio-asset-pointer': 'studio-asset-pointer',
                    '--studio-debugger-source-history': 'studio-debugger-source-history',
                    '--studio-scene-camera': 'studio-scene-camera',
                    '--studio-scene-gizmo3d': 'studio-scene-gizmo3d',
                    '--studio-scene-collider3d': 'studio-scene-collider3d'}
STUDIO_ACCEPTANCE = {
    '--studio-scene-collider3d': ('--scene-collider3d', 26, 'Studio 3D collider handles accepted: frames=26'),
    '--studio-scene-gizmo3d': ('--scene-gizmo3d', 42, 'Studio 3D gizmo accepted: frames=42'),
    '--studio-scene-camera': ('--scene-camera', 30, 'Studio 3D camera accepted: frames=30'),
    '--studio-scene-move': ('--scene-move', 2, 'Studio scene move accepted: frames=2'),
    '--studio-outliner-move': ('--outliner-move', 21, 'Studio outliner move accepted: frames=21'),
    '--studio-source-edit': ('--source-edit', 4, 'Studio source edit accepted: frames=4 edits=1 undo=1 redo=1'),
    '--studio-graph-visibility': ('--graph-visibility', 10, 'Studio graph visibility accepted: frames=10'),
    '--studio-graph-pointer': ('--graph-pointer', 19, 'Studio graph pointer accepted: frames=19'),
    '--studio-asset-pointer': ('--asset-pointer', 20, 'Studio asset pointer accepted: frames=20'),
    '--studio-debugger-source-history': ('--debugger-source-history', 47, 'Studio debugger source history accepted: frames=47'),
}
RENDER_SELECTORS = {'--scene3d-gles': 'scene3d-gles',
                    '--scene3d-gi': 'scene3d-gi', '--scene3d-gi-gles': 'scene3d-gi-gles',
                    '--material-ramps': 'material-ramps', '--texture-transform': 'texture-transform'}
GPU_SELECTORS = STUDIO_SELECTORS | RENDER_SELECTORS | {
    }


def selector_command(selector, mappings):
    key = mappings.get(selector)
    return TOOLS.get(key) if key else None


def runnable_selectors(classified, mappings):
    return [selector for selector in classified
            if selector_command(selector, mappings)]


def studio_resource_preflight(build, resources=None):
    if resources is not None and (
            not isinstance(resources, dict) or set(resources) != {'studio', 'ui'} or
            any(not isinstance(row, list) or len(row) != 3 or
                type(row[0]) is not int or not 0 < row[0] <= 320 or
                type(row[1]) is not int or not 0 < row[1] <= 1024 * 1024 or
                not isinstance(row[2], str) or not re.fullmatch(r'[0-9a-f]{64}', row[2])
                for row in resources.values())):
        raise ValueError('studio_acceptance_resource_bounds_invalid')
    if resources is None:
        resources = {'studio': (264, 450406, 'be09f90b2472bf48a7e3486fbd26759da8eff47886d7914558f3209d0a995296'),
                     'ui': (2, 297, '8505446e5ec71ec7c070f17b6729e0f6c41e2f9666dc7218b3c80a2f2d184e92')}
    for name, (count, length, digest) in resources.items():
        base = build / 'share/chibi' / name
        files = sorted(p for p in base.rglob('*') if p.is_file())
        if len(files) != count or sum(p.stat().st_size for p in files) != length:
            raise ValueError('studio_startup_resource_bounds_changed')
        if any(p.is_symlink() or not 0 < p.stat().st_size <= 1024 * 1024 for p in files):
            raise ValueError('studio_resource_file_invalid')
        rows = {p.relative_to(base).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
        actual = hashlib.sha256(json.dumps(rows, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
        if actual != digest:
            raise ValueError('studio_startup_resource_generation_changed')


def clone_project(source, target):
    """Copies a project to a scratch target, copy-on-write where the filesystem can."""
    if os.name == 'nt':
        shutil.copytree(source, target)
        return
    clone = '-c' if sys.platform == 'darwin' else '--reflink=auto'
    subprocess.run(['cp', '-R', clone, str(source), str(target)], check=True)


def external_project(data):
    """Whether `data` is Studio's fixture project.kdl at the version the engine
    opens."""
    header = (ROOT / 'src/assets/project.h').read_text()
    current = int(re.search(r'^#define CHIBI_PROJECT_VERSION (\d+)$', header, re.M)[1])
    line, _, rest = data.partition(b'\n')
    version = re.fullmatch(rb'version ([1-9][0-9]*)', line)
    return (version is not None and int(version[1]) == current and
            rest == b'name "New Project"\ncontent "content"\nstart_scene "scenes/Start.kdl"\n')


def bounded_read(path, limit):
    if not path.is_file() or path.is_symlink() or not 0 < path.stat().st_size <= limit:
        raise ValueError('studio_preflight_file_outside_bound')
    with path.open('rb') as stream:
        data = stream.read(limit + 1)
    if not data or len(data) > limit:
        raise ValueError('studio_preflight_file_outside_bound')
    return data


def studio_acceptance_sources(source, records):
    acceptance = source / 'tests/acceptance'
    if (source / 'tests').is_symlink() or acceptance.is_symlink():
        raise ValueError('studio_acceptance_source_size_changed')
    paths = list(acceptance.rglob('*'))
    if any(p.is_symlink() for p in paths):
        raise ValueError('studio_acceptance_source_size_changed')
    acceptance_files = {p.relative_to(source).as_posix() for p in paths if p.is_file()}
    if not isinstance(records, dict) or not 8 <= len(records) <= 32 or set(records) != acceptance_files:
        raise ValueError('studio_acceptance_source_records_changed')
    for name, record in records.items():
        relative = Path(name)
        if relative.is_absolute() or '..' in relative.parts or not name.startswith('tests/acceptance/'):
            raise ValueError('studio_acceptance_source_path_invalid')
        path = source / relative
        if path.is_symlink() or path.stat().st_size != record['bytes'] or record['bytes'] > 256 * 1024:
            raise ValueError('studio_acceptance_source_size_changed')
        if hashlib.sha256(path.read_bytes()).hexdigest() != record['sha256']:
            raise ValueError('studio_acceptance_source_changed')


def studio_acceptance_preflight(selector):
    _, frames, success = STUDIO_ACCEPTANCE[selector]
    source = Path(os.environ.get('CHIBI_STUDIO_SOURCE', ''))
    build = Path(os.environ.get('CHIBI_STUDIO_BUILD', ''))
    evidence_path = Path(os.environ.get('CHIBI_STUDIO_ACCEPTANCE_EVIDENCE', ''))
    if not all(p.is_absolute() for p in (source, build, evidence_path)):
        raise ValueError('studio_acceptance_requires_explicit_absolute_paths')
    if evidence_path.stat().st_size > 1024 * 1024:
        raise ValueError('studio_acceptance_evidence_oversize')
    evidence = json.loads(evidence_path.read_text())
    for path, key in ((build / 'bin/studio-acceptance', 'binary_sha256'),
                      (source / 'EXTRACTION.json', 'extraction_sha256')):
        if not path.is_file() or path.is_symlink() or not 0 < path.stat().st_size <= 64 * 1024 * 1024:
            raise ValueError('studio_acceptance_provenance_input_invalid')
        if hashlib.sha256(path.read_bytes()).hexdigest() != evidence.get(key):
            raise ValueError('studio_acceptance_provenance_mismatch')
    native = json.loads(bounded_read(
        ROOT / '.chibi/architecture/external-studio-native-acceptance.json', 1024 * 1024))
    if (native.get('configure_exit') != 0 or native.get('build_exit') != 0 or
            native.get('private_engine_source_include') is not False or
            evidence.get('source') != str(source) or evidence.get('build') != str(build) or
            native.get('source') != str(source) or native.get('sdk') != evidence.get('sdk') or
            evidence.get('binary') != str(build / 'bin/studio-acceptance') or
            evidence.get('frames') != frames or evidence.get('hidden') is not True or
            evidence.get('window') != [1920, 1080] or
            evidence.get('success') != success):
        raise ValueError('studio_acceptance_build_not_verified')
    if hashlib.sha256(bounded_read(build / 'compile_commands.json', 64 * 1024 * 1024)).hexdigest() != evidence.get('compile_commands_sha256'):
        raise ValueError('studio_acceptance_compile_inputs_changed')
    studio_acceptance_sources(source, evidence.get('files'))
    sdk = Path(evidence['sdk'])
    if hashlib.sha256(bounded_read(sdk / 'lib/cmake/Chibi/ChibiConfig.cmake', 1024 * 1024)).hexdigest() != native.get('sdk_config_sha256'):
        raise ValueError('studio_acceptance_sdk_changed')
    fixture = source / 'tests/acceptance/project'
    expected = {
        'content/scenes/Start.kdl': b'version 1\nname "Start"\n(array)entities\n',
        'content/behaviors/Counter.chibi': b'Counter :: behavior {\n    count: retain int = 1\n    on Tick :: () {\n        count = count + 1\n    }\n}\n',
    }
    paths = list(fixture.rglob('*'))
    if any(p.is_symlink() for p in paths) or {p.relative_to(fixture).as_posix() for p in paths if p.is_file()} != {'project.kdl', *expected}:
        raise ValueError('studio_acceptance_fixture_membership_changed')
    if not external_project(bounded_read(fixture / 'project.kdl', 1024)):
        raise ValueError('studio_acceptance_fixture_changed')
    for name, value in expected.items():
        if (fixture / name).stat().st_size != len(value) or bounded_read(fixture / name, len(value)) != value:
            raise ValueError('studio_acceptance_fixture_changed')
    studio_resource_preflight(build, evidence.get('resources'))
    return build / 'bin/studio-acceptance', fixture, dict(fixture_files=3, fixture_bytes=220, frames=frames, window=[1920,1080])


def studio_acceptance_output(selector, output):
    _, frames, success = STUDIO_ACCEPTANCE[selector]
    if output.splitlines().count(success) != 1:
        raise RuntimeError(f'child_exit_incomplete_studio_acceptance: {output[-4000:]}')
    if any(message in output for message in ('Studio UI:', 'runtime frame failed', 'Could not initialize')):
        raise RuntimeError(f'child_exit_UI_initialization_failure: {output[-4000:]}')
    evidence = dict(frames=frames)
    if selector == '--studio-source-edit':
        evidence.update(edits=1, undo=1, redo=1)
    return evidence


class TerminationRequested(BaseException):
    """SIGTERM must unwind the same cleanup scopes as Ctrl-C."""


def terminate(_signal, _frame):
    raise TerminationRequested()


def report(status, reason, **extra):
    value = dict(ok=status == 'passed', status=status, reason=reason,
                 coverage='focused-iteration', full_verification=False,
                 elapsed_seconds=round(time.monotonic() - START, 3), capabilities=HEALTH,
                 next_command=NEXT)
    value.update(extra)
    print(json.dumps(value), flush=True)
    print(f'Agent iteration: {status} ({reason}); coverage is limited to the selected check.', file=sys.stderr)
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
    plan = dict(purpose=purpose, capabilities=health, full_verification=False)
    if purpose == 'logic':
        if '--source-edit' not in runnable_selectors(CPU, CPU_SELECTORS):
            return dict(plan, commands=[], status='blocked', gpu_startup=False,
                        reason='source_edit_command_unavailable')
        return dict(plan, commands=['make agent-test AGENT_SELECTOR=--source-edit'],
                    gpu_startup=False, reason='logic_change_selects_CPU_source_assertions')
    if health.get('status') != 'enumerated' or not health.get('devices'):
        plan['discovery_note'] = 'Vulkan discovery is inconclusive; Metal/GLES/other rendering may work. Verify the actual bounded runtime result.'
    return dict(plan, commands=['make agent-gpu-test AGENT_SELECTOR=--scene3d'],
                gpu_startup=True, reference_resolution='unchanged',
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
            # A child may exit between polls just after the deadline.
            if time.monotonic() - start >= timeout:
                reason = 'time_budget_exceeded'
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
    plan = sub.add_parser('plan', help='Choose focused logic/render work using discovered capabilities')
    plan.add_argument('purpose', choices=['logic', 'render'])
    sub.add_parser('doctor', help='Read-only Vulkan device discovery; no rendering or driver changes')
    sub.add_parser('list', help='CPU classification, explicit GPU checks and budgets')
    sub.add_parser('hardware-check', help='Refuse unless full hardware validation is opted in')
    from validation_plan import add_parser, validation_plan
    add_parser(sub)
    for action in ['test', 'gpu-test']:
        test = sub.add_parser(action)
        test.add_argument('--timeout', type=float, help='Optional positive timeout in seconds')
        test.add_argument('selector', help='Pass -- before the native --selector')
    args = parser.parse_args()
    if args.action in ('test', 'gpu-test'):
        timeout_deadline(args.timeout)
    if args.action == 'validation-plan':
        return validation_plan(args, ROOT, runnable_selectors(CPU, CPU_SELECTORS),
                               runnable_selectors(GPU, GPU_SELECTORS))
    if args.action in ['doctor', 'plan', 'gpu-test']:
        HEALTH = startup_health()
    if args.action == 'doctor':
        print(json.dumps(HEALTH))
        return 0 if HEALTH.get('status') == 'enumerated' else 2
    if args.action == 'plan':
        plan = test_plan(args.purpose, HEALTH)
        print(json.dumps(plan))
        return 2 if plan.get('status') == 'blocked' else 0
    if args.action == 'list':
        print(json.dumps(dict(cpu_selectors=runnable_selectors(CPU, CPU_SELECTORS),
                              opt_in_gpu_selectors=runnable_selectors(GPU, GPU_SELECTORS),
                              unavailable_selectors=[s for s in CPU + GPU
                                                     if not selector_command(s, CPU_SELECTORS if s in CPU else GPU_SELECTORS)],
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
        mappings = CPU_SELECTORS if args.action == 'test' else GPU_SELECTORS
        command = selector_command(args.selector, mappings)
        if not command:
            return report('blocked', 'selector_has_no_command', selector=args.selector)
        env['CHIBI_EXECUTION_PROFILE'] = 'iteration' if args.action == 'test' else 'gpu-check'
        preflight = None
        if args.selector in STUDIO_SELECTORS:
            acceptance_binary, acceptance_fixture, preflight = studio_acceptance_preflight(args.selector)
        elif args.selector in RENDER_SELECTORS:
            # Render consumers link the desktop SDK, so only the main build has them.
            command = [str(ROOT / args.build_dir / 'test/tools/test_tools'), args.selector]
            if not Path(command[0]).is_file():
                return report('blocked', 'render_tools_not_built', selector=args.selector,
                              build_command=f'make build BUILD_DIR={args.build_dir} BUILD_TARGET=test_tools')
            if args.selector.endswith('-gles'):
                # GLES needs a display, and Xvfb provides one on Linux.
                if not shutil.which('xvfb-run'):
                    return report('blocked', 'gles_requires_xvfb_run', selector=args.selector)
                command = ['xvfb-run', '-a'] + command
        execution_evidence = None
        scratch_context = tempfile.TemporaryDirectory(prefix='chibi-studio-') if args.selector in STUDIO_SELECTORS else nullcontext(None)
        with scratch_context as scratch:
            if scratch:
                project_copy = Path(scratch) / 'project'
                clone_project(acceptance_fixture, project_copy)
                env['CHIBI_STUDIO_PREFS_PATH'] = str(project_copy / '.acceptance-prefs.json')
                env['CHIBI_THUMBNAIL_CACHE'] = str(project_copy / '.acceptance-thumbnails')
                command = [str(acceptance_binary), STUDIO_ACCEPTANCE[args.selector][0], '--project-root', str(project_copy)]
            with tempfile.TemporaryFile(mode='w+') as log:
                code = run_commands([command], seconds=args.timeout, env=env, output=log)
                if code:
                    log.seek(0)
                    raise RuntimeError(f'child_exit_{code}: {log.read()[-4000:]}')
                if args.selector in STUDIO_SELECTORS:
                    log.seek(0)
                    execution_evidence = studio_acceptance_output(args.selector, log.read())
        reason = ('studio_acceptance_completed' if args.selector in STUDIO_SELECTORS else
                  'selected_assertions_passed')
        return report('passed', reason,
                      selector=args.selector, artifact_preflight=preflight,
                      execution_evidence=execution_evidence)
    return report('blocked', 'unknown_action')


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
