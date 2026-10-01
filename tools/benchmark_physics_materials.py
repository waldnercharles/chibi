#!/usr/bin/env python3
"""Validate and profile four authored Box2D/material workloads on native and WASM."""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import statistics
import subprocess

from check_neon_swarm import PROJECT, ROOT, Runner, fields
from physics_material_scenarios import DEFAULT_CROWD_COUNT, MAX_CROWD_COUNT
from physics_material_scenarios import FAN_PROBE_FRAMES, SCENARIOS, check_fan, check_load, prepare, scenario


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def fingerprint(path):
    if path.is_file():
        return hashlib.sha256(path.read_bytes()).hexdigest()
    digest = hashlib.sha256()
    files = sorted(p for p in path.rglob('*') if p.is_file() and '.chibi' not in p.parts)
    for item in files:
        digest.update(str(item.relative_to(path)).encode())
        digest.update(item.read_bytes())
    return digest.hexdigest()


def build_configuration(path):
    cache = path.parent / 'CMakeCache.txt'
    pattern = r'(CMAKE_(BUILD_TYPE|C_COMPILER|CXX_COMPILER|C_FLAGS.*|CXX_FLAGS.*|OSX_SYSROOT)|CHIBI_.*(THREAD|WORKER|SIMD|BACKEND|SANIT|PROFIL).*)'
    return {line.split(':', 1)[0]: line.split('=', 1)[1]
        for line in cache.read_text().splitlines()
        if re.fullmatch(pattern + r':[^=]+=.*', line) and ':INTERNAL=' not in line} if cache.exists() else {}


def runtime_artifacts(path):
    # Pthread releases may include a worker sidecar; embedded-worker releases do not.
    candidates = {path, path.with_suffix('.wasm'), path.with_suffix('.data')}
    candidates.update(path.parent.glob(path.stem + '.worker*.js'))
    return {str(item): fingerprint(item) for item in sorted(candidates) if item.is_file()}


class Controller:
    def __init__(self, command, project, output):
        self.command = command
        self.project = project
        self.output = output

    def call(self, operation, **arguments):
        request = dict(contract=1, operation=operation, **arguments)
        run = subprocess.run(self.command + ['--project-root', str(self.project)],
            input=json.dumps(request) + '\n', text=True, capture_output=True,
            cwd=ROOT, timeout=300, env=dict(os.environ, MallocNanoZone='0'))
        response = json.loads(run.stdout) if run.stdout.strip() else {}
        if run.returncode or not response.get('ok'):
            raise RuntimeError(f'{operation} failed: {run.stdout}\n{run.stderr}')
        if run.stderr:
            with (self.output / 'controller.log').open('a') as log:
                log.write(run.stderr)
        return response['result']


def inputs_at(fixture, frames, firing=True):
    samples = copy.deepcopy([s for s in fixture['input_timeline'] if s['frame'] < frames])
    if not firing:
        for sample in samples:
            value = sample['input']
            value.setdefault('aim', {})['firing'] = False
            value['controls'] = [control for control in value.get('controls', []) if control != 1]
    return samples


def snapshot(controller, fixture, frames, firing=True, timeline=None):
    result = controller.call('runtime_snapshot', kind='scene', name='Sandbox',
        frames=frames, input_timeline=inputs_at(fixture, frames, firing) if timeline is None else timeline,
        entity_limit=256, component_limit=64, field_limit=64)
    failures = [entry for entry in result.get('logs', []) if entry.get('failure')]
    failures += [b for e in result['entities'] for b in e['behaviors'] if b.get('last_failure')]
    if failures:
        raise RuntimeError(f'Behavior failures in workload: {failures}')
    return result


def workload_counts(state):
    categories = [fields(entity).get('GameCollider.category') for entity in state['entities']]
    return dict(frame=state['frames'], live_entities=state['live_entities'],
        inspected_entities=len(state['entities']), entities_truncated=state['entities_truncated'],
        inspected_projectiles=categories.count(4), inspected_enemies=categories.count(2),
        particle_count=state['particle_count'], materials=state['materials'],
        world_hash=state['world_hash'])


def fluid_controls(runner, fixture, output):
    """Separate volumes outside measurement to prove each contributes active material."""
    document = runner.author('readAsset', dict(kind='scene', name='Sandbox'))['document']
    volumes = {entity['prototype']: entity['name'] for entity in document['entities']
        if entity.get('prototype') in ('Cloud', 'Liquid')}
    if set(volumes) != {'Cloud', 'Liquid'}:
        raise RuntimeError('Fog/liquid workload must contain both isolated volume prototypes')
    counts = {}
    total = fixture['warmup_frames'] + fixture['frames']
    for mode, keep in [('neither', set()), ('gas_only', {'Cloud'}), ('liquid_only', {'Liquid'})]:
        directory = output / mode
        project = directory / 'project'
        shutil.copytree(runner.project, project, ignore=shutil.ignore_patterns('.chibi'))
        control = Runner(runner.build, directory, project)
        asset = control.author('readAsset', dict(kind='scene', name='Sandbox'))
        control.author('buildScene', dict(kind='scene', name='Sandbox', expected_hash=asset['hash'],
            operations=[dict(op='remove_entity', entity=name) for kind, name in volumes.items() if kind not in keep]))
        control.author('validateAsset', dict(kind='scene', name='Sandbox'))
        state = snapshot(control, fixture, total)
        write_json(directory / 'snapshot.json', state)
        counts[mode] = workload_counts(state)
    baseline = counts['neither']['materials']['active_particles']
    for mode in ('gas_only', 'liquid_only'):
        if counts[mode]['materials']['active_particles'] <= baseline:
            raise RuntimeError(f'{mode} contributes no active material particles')
    return counts


def validate_workload(controller, fixture, output, author=None):
    warmup, frames = fixture['warmup_frames'], fixture['frames']
    points = {'initial': 1, 'warm': warmup, 'middle': warmup + frames // 2, 'final': warmup + frames}
    if fixture['name'] in ('terrain_barrage', 'enemy_barrage'):
        points['fan'] = warmup + FAN_PROBE_FRAMES
    states = {}
    for label, frame in points.items():
        states[label] = snapshot(controller, fixture, frame)
        write_json(output / f'{label}-snapshot.json', states[label])
    gates = dict(snapshots={label: workload_counts(state) for label, state in states.items()})
    idle = states['final']
    if fixture['name'] in ('terrain_barrage', 'enemy_barrage'):
        check_fan(states['fan'])
        idle = snapshot(controller, fixture, points['final'], firing=False)
        write_json(output / 'no-fire-snapshot.json', idle)
        gates['no_fire'] = workload_counts(idle)
    if fixture['name'] == 'fog_liquid':
        idle = snapshot(controller, fixture, points['final'], timeline=fixture['control_input_timeline'])
        write_json(output / 'stationary-snapshot.json', idle)
        gates['stationary'] = workload_counts(idle)
        if states['final']['particle_count'] <= idle['particle_count']:
            raise RuntimeError('Moving fluid probe produces no additional contact particles')
    check_load(fixture['name'], states['warm'], states['middle'], states['final'], idle,
        fixture['check'].get('crowd_count', DEFAULT_CROWD_COUNT))
    if fixture['name'] == 'fog_liquid' and author:
        gates['fluid_controls'] = fluid_controls(author, fixture, output / 'controls')
    return gates


def summarize(runs):
    timing = [run['timing'] for run in runs]
    result = dict(frame_ms={key: statistics.median(row['frame_ms'][key] for row in timing)
        for key in ('p50', 'p95', 'p99', 'max')})
    result['runtime_average_ms'] = {key: statistics.median(row[key] / row['sampled_frames']
        for row in timing) for key in ('behavior_ms', 'physics_ms', 'events_ms', 'systems_ms', 'rendering_ms', 'total_ms')}
    result['phase_average_ms'] = {key: statistics.median(row['physics_detail_ms'][key] / row['sampled_frames']
        for row in timing) for key in ('synchronize', 'solve', 'sweeps', 'pairs', 'dispatch', 'fluid', 'writeback')}
    result['fluid_average_ms'] = {key: statistics.median(row['physics_detail_ms']['material_fluid_ms'][key] / row['sampled_frames']
        for row in timing) for key in timing[0]['physics_detail_ms']['material_fluid_ms']}
    result['box2d_average_ms'] = {key: statistics.median(row['physics_detail_ms']['box2d_ms'][key] / row['sampled_frames']
        for row in timing) for key in timing[0]['physics_detail_ms']['box2d_ms']}
    result['allocations'] = {key: statistics.median(run['allocations'][key] for run in runs)
        for key in ('average_calls_per_frame', 'average_requested_bytes_per_frame')}
    result['allocation_scope'] = runs[0]['allocations']['scope']
    return result


def check_profile(fixture, result):
    timing = result['timing']
    if timing['sampled_frames'] != fixture['frames'] or timing['simulation_steps'] != fixture['frames'] or timing['dropped_seconds'] != 0:
        raise RuntimeError('Profile lost measured frames or simulation time')
    if fixture['name'] == 'enemy_collisions':
        crowd_count = fixture['check']['crowd_count']
        if min(row['physics_bodies'] for row in timing['worst_frames']) < crowd_count:
            raise RuntimeError('Crowd profile has fewer active physics bodies than fixture members')
        if min(row['physics_pairs'] for row in timing['worst_frames']) < crowd_count // 2:
            raise RuntimeError('Crowd profile has insufficient active contacts')
    # Contact event graphs are outside this profiler's Tick behavior rows.
    # Barrage impact coverage comes from cuts in both halves of the measured
    # window and the unchanged no-fire control in validate_workload.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', default='build-release', help='Native authoring/controller build')
    parser.add_argument('--wasm-build', help='Also run this serial or threaded WASM controller build')
    parser.add_argument('--scenario', choices=list(SCENARIOS) + ['all'], default='all')
    parser.add_argument('--crowd-count', type=int, default=DEFAULT_CROWD_COUNT)
    parser.add_argument('--repeat', type=int, default=3)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.repeat < 1 or not 16 <= args.crowd_count <= MAX_CROWD_COUNT:
        parser.error(f'Use a positive repeat count and 16..{MAX_CROWD_COUNT} crowd members')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    native = (ROOT / args.build / 'chibi_ctl').resolve()
    commands = {'native': [str(native)]}
    if args.wasm_build:
        commands['wasm'] = ['node', str((ROOT / args.wasm_build / 'chibi_ctl.js').resolve())]
    binaries = {name: dict(command=command, sha256=fingerprint(Path(command[-1])),
        build_configuration=build_configuration(Path(command[-1]))) for name, command in commands.items()}
    if args.wasm_build:
        binaries['wasm']['wasm_sha256'] = fingerprint(Path(commands['wasm'][-1]).with_suffix('.wasm'))
        binaries['wasm']['artifacts'] = runtime_artifacts(Path(commands['wasm'][-1]))
    cpu = subprocess.check_output(['sysctl', '-n', 'machdep.cpu.brand_string'], text=True).strip() if platform.system() == 'Darwin' else platform.processor()
    report = dict(schema_version=1, git_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        git_dirty=bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT)),
        host=platform.platform(), cpu=cpu, logical_cpu_count=os.cpu_count(), binaries=binaries, repeat=args.repeat,
        tool_sha256={name: fingerprint(ROOT / 'tools' / name) for name in
            ('benchmark_physics_materials.py', 'physics_material_scenarios.py', 'physics_material_crowd.py', 'check_neon_swarm.py')},
        measurement_scope='Fixed-step CPU controller profiles; includes profiling overhead, excludes rendering and GPU completion. Other user activity may remain.',
        scenarios={})
    for name in SCENARIOS if args.scenario == 'all' else [args.scenario]:
        directory = output / name
        directory.mkdir()
        project = directory / 'project'
        shutil.copytree(PROJECT, project, ignore=shutil.ignore_patterns('.chibi'))
        author = Runner(args.build, directory, project)
        fixture = scenario(name, args.crowd_count)
        prepare(author, fixture)
        project_hash = fingerprint(project)
        write_json(directory / 'fixture.json', fixture)
        controllers = {}
        case = dict(fixture=fixture, project_sha256=project_hash, targets={})
        for target, command in commands.items():
            target_output = directory / target
            target_output.mkdir()
            controller = Controller(command, project, target_output)
            controllers[target] = controller
            validation = validate_workload(controller, fixture, target_output, author if target == 'native' else None)
            replay_request = dict(kind='scene', name='Sandbox', frames=fixture['warmup_frames'] + fixture['frames'],
                input_timeline=inputs_at(fixture, fixture['warmup_frames'] + fixture['frames']), hash_parts=True)
            replay = controller.call('run_scenario', **replay_request)
            if replay['behavior_failures']:
                raise RuntimeError(f'{name}: replay has behavior failures')
            write_json(target_output / 'replay.json', replay)
            write_json(directory / 'replay-request.json', dict(contract=1, operation='run_scenario', **replay_request))
            case['targets'][target] = dict(validation=validation, replay=replay, profiles=[])
        request = dict(kind='scene', name='Sandbox', frames=fixture['frames'], warmup_frames=fixture['warmup_frames'],
            input_timeline=inputs_at(fixture, fixture['warmup_frames'] + fixture['frames']))
        write_json(directory / 'profile-request.json', dict(contract=1, operation='profile_scenario', **request))
        for repeat in range(args.repeat):
            order = list(commands) if repeat % 2 == 0 else list(reversed(commands))
            for target in order:
                result = controllers[target].call('profile_scenario', **request)
                write_json(directory / target / f'profile-{repeat + 1}.json', result)
                check_profile(fixture, result)
                if result['world_hash'] != case['targets'][target]['replay']['world_hash']:
                    raise RuntimeError(f'{name}/{target}: profiling changed replay state')
                case['targets'][target]['profiles'].append(result)
                print(f'{name} {target} repeat={repeat + 1}: p50/p95/p99 '
                    + '/'.join(f'{result["timing"]["frame_ms"][key]:.3f}' for key in ('p50', 'p95', 'p99'))
                    + f' ms hash={result["world_hash"]}', flush=True)
        if fingerprint(project) != project_hash:
            raise RuntimeError('Prepared project changed during measurement')
        for target in commands:
            case['targets'][target]['summary'] = summarize(case['targets'][target]['profiles'])
        if args.wasm_build:
            case['native_wasm_replay_equal'] = case['targets']['native']['replay'] == case['targets']['wasm']['replay']
        report['scenarios'][name] = case
        write_json(output / 'summary.json', report)
        if args.wasm_build and not case['native_wasm_replay_equal']:
            print(f'{name}: native/WASM replay mismatch retained in summary.json', flush=True)
    for name, command in commands.items():
        if fingerprint(Path(command[-1])) != binaries[name]['sha256']:
            raise RuntimeError('Controller changed during measurement')
    if args.wasm_build and fingerprint(Path(commands['wasm'][-1]).with_suffix('.wasm')) != binaries['wasm']['wasm_sha256']:
        raise RuntimeError('WASM binary changed during measurement')
    if args.wasm_build and runtime_artifacts(Path(commands['wasm'][-1])) != binaries['wasm']['artifacts']:
        raise RuntimeError('WASM runtime artifacts changed during measurement')
    write_json(output / 'summary.json', report)
    diverged = [name for name, case in report['scenarios'].items()
        if case.get('native_wasm_replay_equal') is False]
    if diverged:
        raise RuntimeError(f'Native/WASM replay diverged in {", ".join(diverged)}; all reports saved in summary.json')
    print(f'Validated reports: {output / "summary.json"}')


if __name__ == '__main__':
    main()
