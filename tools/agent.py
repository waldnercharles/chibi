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

from test_policy import admitted, NOTICE, run_commands, DEADLINE, TOOLS

ROOT = Path(__file__).resolve().parents[1]
CPU = re.findall(r'CHIBI_CPU_SELECTOR\("([^"]+)"\)',
                 (ROOT / 'test/execution_selectors.h').read_text())
GPU = re.findall(r'CHIBI_GPU_SELECTOR\("([^"]+)"\)',
                 (ROOT / 'test/execution_selectors.h').read_text())
START = time.monotonic()
HEALTH = None
NEXT = 'make test-policy-inventory'
CPU_SELECTORS = {
    '--compile-installed-space-cadet': 'compile-installed-space-cadet',
    '--source-edit': 'source-edit',
    '--source-presentation': 'source-presentation',
    '--source-systems': 'source-systems',
    '--system-schedule': 'system-schedule',
}
INSTALLED_PLAYERS = {'--player-installed-space-cadet-vm', '--player-installed-space-cadet-sljit'}
PLAYER_SELECTORS = {
    '--player-installed-space-cadet-vm': 'player-installed-space-cadet-vm',
    '--player-installed-space-cadet-sljit': 'player-installed-space-cadet-sljit',
    '--player-space-cadet-vm': 'player-space-cadet-vm',
    '--player-space-cadet-sljit': 'player-space-cadet-sljit',
}
STUDIO_SELECTORS = {'--studio-starter-startup': 'studio-starter-startup',
                    '--studio-source-edit': 'studio-source-edit',
                    '--studio-graph-visibility': 'studio-graph-visibility'}
STUDIO_ACCEPTANCE = {
    '--studio-source-edit': ('--source-edit', 4, 'Studio source edit accepted: frames=4 edits=1 undo=1 redo=1'),
    '--studio-graph-visibility': ('--graph-visibility', 10, 'Studio graph visibility accepted: frames=10'),
}
RENDER_SELECTORS = {'--interior-border': 'interior-border', '--viewport-resize': 'viewport-resize',
                    '--sdf-visibility': 'sdf-visibility',
                    '--scene3d': 'scene3d', '--scene3d-gles': 'scene3d-gles',
                    '--material-ramps': 'material-ramps'}
GPU_SELECTORS = PLAYER_SELECTORS | STUDIO_SELECTORS | RENDER_SELECTORS
PLAYER_RESOURCES = {('ui', 'HUD'), ('ui', 'PauseMenu')} | {
    ('font', f'font/Inter/{weight}/normal') for weight in (400, 500, 600, 700)
}
PLAYER_EMPTY_CATALOGS = {
    ('shader-values', 'parameters'): (12, '0ba1d1fcd4761fcb'),
    ('parts', 'parts'): (11, '9af98571205f1f12'),
}
PLAYER_RESOURCES |= PLAYER_EMPTY_CATALOGS.keys()


def selector_command(selector, mappings):
    key = mappings.get(selector)
    return TOOLS.get(key) if key else None


def runnable_selectors(classified, mappings):
    return [selector for selector in classified
            if admitted([selector]) and selector_command(selector, mappings)]


def studio_resource_preflight(build):
    resources = {'studio': (261, 436675, '3ae7676c6de4a45db6dc2faadd29a9cf7dde2ea09409f1a008af4df4a4a65689'),
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


def studio_startup_preflight():
    source = Path('/Users/cwaldner/Personal/chibi-studio')
    build = ROOT / 'build-verify/sdk-migration/external-studio-build'
    evidence_path = ROOT / '.chibi/architecture/external-studio-native-acceptance.json'
    if evidence_path.stat().st_size > 1024 * 1024:
        raise ValueError('studio_startup_evidence_oversize')
    evidence = json.loads(evidence_path.read_text())
    def checksum(path, limit):
        if not path.is_file() or not 0 < path.stat().st_size <= limit:
            raise ValueError('studio_startup_input_missing_or_oversize')
        return hashlib.sha256(path.read_bytes()).hexdigest()
    if (evidence.get('build_exit') != 0 or evidence.get('configure_exit') != 0
            or evidence.get('source') != str(source)
            or evidence.get('private_engine_source_include') is not False
            or checksum(build / 'bin/chibi-dev', 64 * 1024 * 1024) != evidence.get('binary_sha256')
            or checksum(source / 'EXTRACTION.json', 1024 * 1024) != evidence.get('extraction_sha256')
            or checksum(ROOT / 'build-verify/sdk-migration/desktop-sdk-install/lib/cmake/Chibi/ChibiConfig.cmake',
                        1024 * 1024) != evidence.get('sdk_config_sha256')):
        raise ValueError('studio_startup_build_provenance_mismatch')
    starter = source / 'projects/starter'
    expected = {'project.kdl': b'version 21\nname "New Project"\ncontent "content"\nstart_scene "scenes/Start.kdl"\n',
                'content/scenes/Start.kdl': b'version 1\nname "Start"\n(array)entities\n'}
    if {p.relative_to(starter).as_posix() for p in starter.rglob('*') if p.is_file()} != set(expected):
        raise ValueError('studio_starter_membership_changed')
    for name, value in expected.items():
        if (starter / name).stat().st_size != len(value) or (starter / name).read_bytes() != value:
            raise ValueError('studio_starter_content_changed')
    studio_resource_preflight(build)
    return dict(starter_files=2, starter_bytes=118, frames=2, window=[1920, 1080],
                studio_resource_files=261, studio_resource_bytes=436675)


def bounded_read(path, limit):
    if not path.is_file() or path.is_symlink() or not 0 < path.stat().st_size <= limit:
        raise ValueError('studio_preflight_file_outside_bound')
    with path.open('rb') as stream:
        data = stream.read(limit + 1)
    if not data or len(data) > limit:
        raise ValueError('studio_preflight_file_outside_bound')
    return data


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
    records = evidence.get('files')
    if not isinstance(records, dict) or len(records) != 8:
        raise ValueError('studio_acceptance_source_records_changed')
    for name, record in records.items():
        relative = Path(name)
        if relative.is_absolute() or '..' in relative.parts or not name.startswith('tests/acceptance/'):
            raise ValueError('studio_acceptance_source_path_invalid')
        path = source / relative
        if path.is_symlink() or path.stat().st_size != record['bytes'] or record['bytes'] > 65536:
            raise ValueError('studio_acceptance_source_size_changed')
        if hashlib.sha256(path.read_bytes()).hexdigest() != record['sha256']:
            raise ValueError('studio_acceptance_source_changed')
    sdk = Path(evidence['sdk'])
    if hashlib.sha256(bounded_read(sdk / 'lib/cmake/Chibi/ChibiConfig.cmake', 1024 * 1024)).hexdigest() != native.get('sdk_config_sha256'):
        raise ValueError('studio_acceptance_sdk_changed')
    fixture = source / 'tests/acceptance/project'
    expected = {
        'project.kdl': b'version 21\nname "New Project"\ncontent "content"\nstart_scene "scenes/Start.kdl"\n',
        'content/scenes/Start.kdl': b'version 1\nname "Start"\n(array)entities\n',
        'content/behaviors/Counter.chibi': b'behavior Counter {\n    state count: int = 1\n    on Tick {\n        count = count + 1\n    }\n}\n',
    }
    paths = list(fixture.rglob('*'))
    if any(p.is_symlink() for p in paths) or {p.relative_to(fixture).as_posix() for p in paths if p.is_file()} != set(expected):
        raise ValueError('studio_acceptance_fixture_membership_changed')
    for name, value in expected.items():
        if (fixture / name).stat().st_size != len(value) or bounded_read(fixture / name, len(value)) != value:
            raise ValueError('studio_acceptance_fixture_changed')
    studio_resource_preflight(build)
    return build / 'bin/studio-acceptance', fixture, dict(fixture_files=3, fixture_bytes=210, frames=frames, window=[1920,1080])


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


def player_artifact_preflight(artifact=None):
    artifact = artifact or ROOT / '.chibi/architecture/player-space-cadet/project.chibip'
    manifest_path = Path(str(artifact) + '.resources.json')
    if not 0 < artifact.stat().st_size <= 64 * 1024 * 1024:
        raise ValueError('player_artifact_size_outside_reviewed_bound')
    with manifest_path.open('rb') as stream:
        metadata = stream.read(1024 * 1024 + 1)
    if len(metadata) > 1024 * 1024:
        raise ValueError('player_manifest_exceeds_1MiB')
    manifest = json.loads(metadata)
    if (not isinstance(manifest, dict) or type(manifest.get('version')) is not int
            or manifest['version'] != 1 or manifest.get('external') != []):
        raise ValueError('player_manifest_requires_version1_embedded_resources_only')
    records = manifest.get('embedded')
    if not isinstance(records, list) or len(records) != len(PLAYER_RESOURCES):
        raise ValueError('player_manifest_resource_count_outside_reviewed_bound')
    identities = set()
    for record in records:
        if (not isinstance(record, dict) or record.get('kind') not in ('ui', 'font', 'shader-values', 'parts')
                or not isinstance(record.get('key'), str) or not record['key']
                or len(record['key'].encode()) > 65536
                or type(record.get('bytes')) is not int
                or not 0 < record['bytes'] <= 64 * 1024 * 1024
                or not isinstance(record.get('content_hash'), str)
                or not re.fullmatch(r'[0-9a-f]{16}', record['content_hash'])):
            raise ValueError('player_manifest_resource_outside_reviewed_ui_font_scope')
        identity = (record['kind'], record['key'])
        if (identity in PLAYER_EMPTY_CATALOGS
                and (record['bytes'], record['content_hash']) != PLAYER_EMPTY_CATALOGS[identity]):
            raise ValueError('player_manifest_catalog_must_match_reviewed_empty_envelope')
        if identity in identities:
            raise ValueError('player_manifest_duplicate_resource_identity')
        identities.add(identity)
    if identities != PLAYER_RESOURCES or sum(record['bytes'] for record in records) > 4 * 1024 * 1024:
        raise ValueError('player_manifest_requires_bounded_ui_and_font_resources')
    fingerprint = 14695981039346656037
    length = 0
    with artifact.open('rb') as stream:
        while chunk := stream.read(65536):
            deadline = min(START + 60, float(os.environ.get(DEADLINE, START + 60)))
            if time.monotonic() >= deadline:
                raise ValueError('player_artifact_preflight_deadline_exceeded')
            length += len(chunk)
            if length > 64 * 1024 * 1024:
                raise ValueError('player_artifact_exceeds_64MiB')
            for byte in chunk:
                fingerprint = ((fingerprint ^ byte) * 1099511628211) & 0xffffffffffffffff
    if not length or manifest.get('artifact_hash') != f'{fingerprint:016x}':
        raise ValueError('player_artifact_manifest_hash_mismatch')
    return dict(artifact_bytes=length, resource_count=len(records),
                resource_kinds=sorted({kind for kind, _ in identities}))


def installed_space_cadet(selector):
    from package_player import validate_package
    from package_sdk import previous_package
    def configured(name):
        value = os.environ.get(name)
        if not value or not Path(value).is_absolute():
            raise ValueError(name + '_requires_explicit_absolute_path')
        path = Path(value)
        if path.is_symlink():
            raise ValueError(name + '_must_not_be_symlink')
        return path
    project = configured('CHIBI_SPACE_CADET_ROOT')
    sdk = configured('CHIBI_COMPILER_SDK')
    for package in (sdk, project / '.chibi/runtime'):
        files = list(package.rglob('*'))
        if len(files) > 8192 or sum(p.stat().st_size for p in files if p.is_file()) > 1024**3:
            raise ValueError('installed_package_inventory_exceeds_reviewed_bound')
    compiler = previous_package(sdk, 'sdk')
    player = validate_package(project / '.chibi/runtime')
    contracts = ('runtime_headers_sha256', 'artifact_formats', 'project_format',
                 'prepared_format', 'system', 'processor', 'pointer_bits', 'byte_order')
    if any(compiler['requirements'].get(key) is None or
           compiler['requirements'][key] != player['requirements'].get(key) for key in contracts):
        raise ValueError('compiler_player_runtime_contract_mismatch')
    pin_path = project / '.chibi-engine.json'
    if pin_path.stat().st_size > 65536:
        raise ValueError('game_pin_oversize')
    pin = json.loads(pin_path.read_text())
    expected = dict(format=2, kind='player', build_id=player['build_id'],
                    engine_build_id=player['engine']['build_id'], sdk_build_id=player['sdk']['build_id'],
                    package_version=player['package_version'], requirements=player['requirements'])
    if pin != expected:
        raise ValueError('game_pin_differs_from_installed_manifest')
    paths = [project / 'project.kdl'] + sorted(p for p in (project / 'content').rglob('*') if p.is_file())
    if len(paths) != 36 or sum(p.stat().st_size for p in paths) != 144741:
        raise ValueError('SpaceCadet_source_membership_or_bytes_changed')
    hashes = {p.relative_to(project).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    fingerprint = hashlib.sha256(json.dumps(hashes, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    if fingerprint != '764be3d1fdd3b42741e8f68285ce480e9060ab6b2b98c6dd4d3572223eb0ef0b':
        raise ValueError('SpaceCadet_source_fingerprint_changed')
    if time.monotonic() >= min(START + 60, float(os.environ.get(DEADLINE, START + 60))):
        raise ValueError('installed_preflight_deadline_exceeded')
    artifact = project / '.chibi/prepared/project.chibip'
    for path in (project / '.chibi', artifact.parent, artifact, Path(str(artifact) + '.resources.json')):
        if path.is_symlink():
            raise ValueError('installed_prepared_handoff_must_not_be_symlink')
    evidence = dict(game_root=str(project), player_build_id=player['build_id'],
                    compiler_build_id=compiler['build_id'], source_fingerprint=fingerprint)
    if selector == '--compile-installed-space-cadet':
        binary = sdk / 'bin/chibi-compile'
        if 'bin/chibi-compile' not in compiler['files'] or not os.access(binary, os.X_OK):
            raise ValueError('packaged_compiler_missing')
        artifact.parent.mkdir(parents=True, exist_ok=True)
        return [str(binary), '--project-root', str(project), '--output', str(artifact)], evidence, artifact
    evidence.update(player_artifact_preflight(artifact))
    backend = 'sljit' if selector.endswith('-sljit') else 'vm'
    return [str(project / '.chibi/runtime/bin/chibi-player'), str(artifact), '--backend', backend,
            '--hidden', '--frames', '4', '--fixed-dt', '0.0166667'], evidence, artifact


class TerminationRequested(BaseException):
    """SIGTERM must unwind the same cleanup scopes as Ctrl-C."""


def terminate(_signal, _frame):
    raise TerminationRequested()


def report(status, reason, **extra):
    value = dict(ok=status == 'passed', status=status, reason=reason,
                 coverage='focused-iteration', full_verification=False,
                 elapsed_seconds=round(time.monotonic() - START, 3), capabilities=HEALTH,
                 quarantined_checks=['full-native', 'render'],
                 next_command=NEXT)
    value.update(extra)
    print(json.dumps(value), flush=True)
    print(f'Agent iteration: {status} ({reason}); full native/render coverage disabled, not passed.', file=sys.stderr)
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
                quarantined_checks=['full-native', 'render'])
    if purpose == 'logic':
        if '--source-edit' not in runnable_selectors(CPU, CPU_SELECTORS):
            return dict(plan, commands=[], status='blocked', gpu_startup=False,
                        reason='source_edit_policy_route_unavailable')
        return dict(plan, commands=['make agent-test AGENT_SELECTOR=--source-edit'],
                    gpu_startup=False, reason='logic_change_selects_CPU_source_assertions')
    if health.get('status') != 'enumerated' or not health.get('devices'):
        plan['discovery_note'] = 'Vulkan discovery is inconclusive; Metal/GLES/other rendering may work. Verify the actual bounded runtime result.'
    if purpose == 'temporal':
        return dict(plan, commands=['python3 tools/agent.py mp4 REQUEST.json --frames 60 --output OUTPUT.mp4'],
                    requires_explicit_mp4_request=True, warmup='set gpu_warmup_frames for the scene',
                    output_frames=60, render_seconds=120, gpu_startup=True)
    return dict(plan, commands=['make test-policy-inventory'], status='blocked',
                quarantine=NOTICE,
                gpu_startup=True, render_seconds=60, reference_resolution='unchanged',
                preview_size=[320, 180] if health.get('acceleration') == 'hardware' else [96, 64],
                reason='purposeful_GPU_assertions_on_' + health.get('acceleration', 'unknown') + '_renderer')


def run(command, timeout, env=None, request=None, rss_mib=None):
    if DEADLINE in os.environ:
        timeout = min(timeout, max(0, float(os.environ[DEADLINE]) - time.monotonic()))
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
        render.add_argument('--project-root', required=True)
        render.add_argument('--output', type=Path, required=True)
        if action == 'mp4':
            render.add_argument('--frames', type=int, default=60, help='3..180 output frames at 60 fps (max 3 seconds)')
    args = parser.parse_args()
    if args.action == 'validation-plan':
        return validation_plan(args, ROOT, runnable_selectors(CPU, CPU_SELECTORS),
                               runnable_selectors(GPU, GPU_SELECTORS))
    if args.action in ['test', 'gpu-test'] and not admitted([args.selector]):
        return report('blocked', NOTICE, selector=args.selector)
    if args.action in ['doctor', 'plan', 'gpu-test', 'preview', 'mp4']:
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
                                                     if not admitted([s]) or not
                                                     selector_command(s, CPU_SELECTORS if s in CPU else GPU_SELECTORS)],
                              temporary_quarantine=NOTICE,
                              other_selectors='temporarily quarantined; hardware opt-in cannot authorize them',
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
        mappings = CPU_SELECTORS if args.action == 'test' else GPU_SELECTORS
        command = selector_command(args.selector, mappings)
        if not command:
            return report('blocked', 'selector_has_no_migrated_policy_route', selector=args.selector)
        env['CHIBI_EXECUTION_PROFILE'] = 'iteration' if args.action == 'test' else 'gpu-check'
        preflight = None
        installed_artifact = None
        if args.selector in INSTALLED_PLAYERS or args.selector == '--compile-installed-space-cadet':
            command, preflight, installed_artifact = installed_space_cadet(args.selector)
        elif args.selector in PLAYER_SELECTORS:
            preflight = player_artifact_preflight()
        elif args.selector in STUDIO_ACCEPTANCE:
            acceptance_binary, acceptance_fixture, preflight = studio_acceptance_preflight(args.selector)
        elif args.selector in STUDIO_SELECTORS:
            preflight = studio_startup_preflight()
        elif args.selector in RENDER_SELECTORS:
            # Render consumers link the desktop SDK, so only the main build has them.
            command = [str(ROOT / args.build_dir / 'test/tools/test_tools'), args.selector]
            if not Path(command[0]).is_file():
                return report('blocked', 'render_tools_not_built', selector=args.selector,
                              build_command=f'make build BUILD_DIR={args.build_dir} BUILD_TARGET=test_tools')
            if args.selector == '--scene3d-gles':
                # GLES needs a display, and Xvfb provides one on Linux.
                if not shutil.which('xvfb-run'):
                    return report('blocked', 'gles_requires_xvfb_run', selector=args.selector)
                command = ['xvfb-run', '-a'] + command
        execution_evidence = None
        scratch_context = tempfile.TemporaryDirectory(prefix='chibi-studio-acceptance-') if args.selector in STUDIO_ACCEPTANCE else nullcontext(None)
        with scratch_context as scratch:
            if scratch:
                project_copy = Path(scratch) / 'project'
                shutil.copytree(acceptance_fixture, project_copy)
                env['CHIBI_STUDIO_PREFS_PATH'] = str(project_copy / '.acceptance-prefs.json')
                env['CHIBI_THUMBNAIL_CACHE'] = str(project_copy / '.acceptance-thumbnails')
                command = [str(acceptance_binary), STUDIO_ACCEPTANCE[args.selector][0], '--project-root', str(project_copy)]
            with tempfile.TemporaryFile(mode='w+') as log:
                code = run_commands([command],
                                    max(.001, 60 - (time.monotonic() - START)), env, log)
                if code:
                    log.seek(0)
                    raise RuntimeError(f'child_exit_{code}: {log.read()[-4000:]}')
                if args.selector == '--compile-installed-space-cadet':
                    preflight.update(player_artifact_preflight(installed_artifact))
                if args.selector in PLAYER_SELECTORS:
                    log.seek(0)
                    output = log.read()
                    completed = re.findall(r'^frames=([0-9]+) steps=([0-9]+)$', output, re.M)
                    if (len(completed) != 1 or int(completed[0][0]) != 4 or
                            (args.selector in INSTALLED_PLAYERS and int(completed[0][1]) != 3)):
                        raise RuntimeError(f'child_exit_incomplete_player_frames: {output[-4000:]}')
                    execution_evidence = dict(frames=int(completed[0][0]), steps=int(completed[0][1]))
                if args.selector in STUDIO_SELECTORS:
                    log.seek(0)
                    output = log.read()
                    if args.selector in STUDIO_ACCEPTANCE:
                        execution_evidence = studio_acceptance_output(args.selector, output)
                    elif any(message in output for message in ('Studio UI:', 'runtime frame failed', 'Could not initialize')):
                        raise RuntimeError(f'child_exit_UI_initialization_failure: {output[-4000:]}')
        reason = ('admitted_studio_graph_visibility_completed' if args.selector == '--studio-graph-visibility' else
                  'admitted_studio_source_edit_completed' if args.selector == '--studio-source-edit' else
                  'admitted_two_frame_studio_startup_completed' if args.selector in STUDIO_SELECTORS else
                  'admitted_installed_compile_completed' if args.selector == '--compile-installed-space-cadet' else
                  'admitted_four_frame_player_completed' if preflight else 'selected_assertions_passed')
        return report('passed', reason,
                      selector=args.selector, artifact_preflight=preflight,
                      execution_evidence=execution_evidence,
                      skipped=[dict(check='full-native/render', reason='temporary_60_second_quarantine')])
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
