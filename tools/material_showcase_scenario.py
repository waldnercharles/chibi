#!/usr/bin/env python3
"""Author, replay and measure independent material-world showcase scenes."""
import argparse
import base64
import hashlib
import json
import math
import subprocess
from pathlib import Path

from check_neon_swarm import PROJECT, Runner

SCENES = ('Liquid', 'LiquidDDR', 'Gas', 'GasSpeed', 'Thermal', 'ThermalClosed',
          'Destruction', 'Organic', 'OrganicStrain')
BLUE, AMBER, CYAN, MAGENTA = 0x369de8ff, 0xf3b24aff, 0x42d7dfff, 0xd363dfff
STONE = 0x697887ff


def publish(runner, kind, name, operations):
    family = {'scene': 'scenes', 'prototype': 'prototypes', 'behavior': 'behaviors'}[kind]
    path = runner.project / 'content' / family / f'{name}.kdl'
    args = dict(kind=kind, name=name, operations=operations)
    if kind == 'behavior':
        args['owner_type'] = 'ecs.entity'
    if path.exists():
        asset = runner.author('readAsset', dict(kind=kind, name=name))
        args['expected_hash'] = asset['hash']
        if kind == 'scene':
            args['operations'] = [dict(op='remove_entity', entity=e['name'])
                                  for e in asset['document']['entities']] + operations
        elif kind == 'prototype':
            args['operations'] = [dict(op='remove_component', component=c)
                                  for c in asset['document']['components']] + operations
        else:
            # Timed cuts are fixed, independently validated fixture graphs.
            runner.author('validateAsset', dict(kind=kind, name=name))
            return
    else:
        args['create'] = True
    runner.author({'scene': 'buildScene', 'prototype': 'buildPrototype',
                   'behavior': 'buildGraph'}[kind], args)
    runner.author('validateAsset', dict(kind=kind, name=name))


def entity(name, components, behaviors=()):
    return dict(op='add_entity', name=name, prototype='MaterialShowcaseEntity',
                components=components, behaviors=list(behaviors))


def wall(name, x, y, width, height, cut=None):
    components = dict(GameTransform=dict(x=x, y=-y), GameBody=dict(type='Static', allow_rotation=True),
                      GameCollider=dict(shape='Box', width=width, height=height,
                                        category=8, mask=15, friction=.6),
                      GameAppearance=dict(width=width, height=height, color=STONE),
                      GameMaterial=dict(scene=STONE, albedo=STONE, absorption=1))
    if cut or name == 'Barrier':
        components.update(GameDestructible=dict(cell_size=2, min_fragment_cells=6),
                          GameSubstance=dict(hardness=1, retain_chips=name == 'Specimen'),
                          MaterialBreakProfile=dict(chip_speed=4, fragment_speed=3,
                                                    release_speed=2, dust_limit=0,
                                                    chunk_limit=0, smoke_count=0))
    return entity(name, components, [cut] if cut else [])


def volume(name, x, y, width, height, color, velocity=(0, 0), gas=False,
           heat=None, elastic=False, fuel=False):
    spacing = .375 if gas else .25
    components = dict(GameTransform=dict(x=x, y=-y),
                      GameFluidVolume=dict(kind='Gas' if gas else 'Liquid', width=width,
                                           height=height, spacing=spacing, mass=.001 * spacing**2,
                                           color=color, velocity=dict(x=velocity[0], y=-velocity[1]),
                                           absorption=.025 if gas else .08,
                                           visibility=.25 if gas else .4, refraction=0,
                                           grain_radius=0))
    if not gas:
        components['MaterialConstitutive'] = dict(density=.001, bulk_modulus=10 if elastic else 4,
                                                  shear_modulus=6 if elastic else 0)
    if heat is not None:
        components.update(MaterialThermal=dict(capacity=128 if name == 'Coolant' else 1,
                                               conductivity=200),
                          GameIgniter=dict(heat=heat))
    if fuel:
        components['GameFuel'] = dict(amount=.04 * spacing**2)
    return entity(name, components)


def cut_graph(runner, name, delay, point, radius):
    runner.author('searchNodes', dict(domain='behavior', query='Damage Material', limit=4))
    operations = [dict(op='add_node', type='Event', event_name='Begin', ref='begin'),
                  dict(op='add_node', type='Delay', ref='wait', inputs={'Duration': delay}),
                  dict(op='add_self_node', ref='target'),
                  dict(op='add_node', type='Damage Material', ref='cut',
                       inputs={'Point': dict(x=point[0], y=-point[1]), 'Radius': radius, 'Energy': 10})]
    for source, pin, target, target_pin, kind in (
            ('begin', 'Then', 'wait', 'Exec', 'exec'),
            ('wait', 'Completed', 'cut', 'In', 'exec'),
            ('target', 'Self', 'cut', 'Entity', 'data')):
        operations.append(dict(op='connect', kind=kind, **{'from': source, 'from_pin': pin,
                                                          'to': target, 'to_pin': target_pin}))
    publish(runner, 'behavior', name, operations)


def verify_enclosure(entities):
    walls = {e['name']: e['components'] for e in entities
             if e['name'] in ('Floor', 'Ceiling', 'Left wall', 'Right wall')}
    def bounds(name):
        transform, collider = walls[name]['GameTransform'], walls[name]['GameCollider']
        return (transform['x'] - collider['width'] / 2,
                transform['x'] + collider['width'] / 2,
                transform['y'] - collider['height'] / 2,
                transform['y'] + collider['height'] / 2)
    for side in ('Left wall', 'Right wall'):
        a = bounds(side)
        for cap in ('Floor', 'Ceiling'):
            b = bounds(cap)
            assert min(a[1], b[1]) > max(a[0], b[0]) and \
                   min(a[3], b[3]) > max(a[2], b[2]), f'Open corner: {side}/{cap}'


def author(runner, scenes=SCENES):
    runner.author('searchAssets', dict(query='MaterialDetail', limit=8))
    runner.author('describeComponents', {})
    publish(runner, 'prototype', 'MaterialShowcaseEntity',
            [dict(op='add_component', component='GameTransform')])
    cut_graph(runner, 'MaterialShowcaseRelease', .8, (-50, 18), 23)
    cut_graph(runner, 'MaterialShowcaseBreach', .8, (0, 0), 28)
    cut_graph(runner, 'MaterialShowcaseFracture', 1, (0, 0), 36)
    for short in scenes:
        name = f'MaterialShowcase{short}'
        gravity = (0, 0) if short.startswith('Thermal') else (0, 30)
        if short.startswith('Organic'):
            gravity = (0, 10)
        world = dict(MaterialFluidWorld=dict(chunk_size=64, buffer=32,
                     kernel=1.125 if short.startswith('Gas') else .75,
                     particle_limit=131072, solver='Ddr' if short == 'LiquidDDR' else 'Mpm',
                     mpm_cell_size=3 if short.startswith(('Liquid', 'Thermal')) or short == 'Destruction' else
                                   2 if short.startswith('Organic') else 1,
                     mpm_max_substeps=32,
                     gravity=dict(x=gravity[0], y=gravity[1]),
                     gas_cell_size=4 if short.startswith('Gas') else 0,
                     gas_vorticity=2 if short.startswith('Gas') else 0, gas_drag=.1,
                     gas_buoyancy=1 if short.startswith('Gas') else 0, gas_ambient_heat=0,
                     debug_view='Strain' if short == 'OrganicStrain' else
                                'Speed' if short == 'GasSpeed' else 'Material'),
                     MaterialDestructionWorld=dict(fragment_limit=96, debris_limit=512),
                     GameHRC2D=dict(resolution=.5, min_resolution=.5, target_fps=60,
                                    adaptive=False, ambient=.2, trace_levels=1))
        if short.startswith('Thermal'):
            world['GameCombustion'] = dict(ignition=.9, extinction=.8, burn_rate=.0004 * .25**2,
                                           heat_release=1, cooling=0, flame_color=0xffa135ff,
                                           flame_emission=.6, smoke_lifetime=6,
                                           blast_radius=0, blast_speed=0, blast_damage=0)
        publish(runner, 'prototype', f'{name}World',
                [dict(op='add_component', component=c, fields=v) for c, v in world.items()])
        entities = [dict(op='add_entity', name='Materials', prototype=f'{name}World'),
                    entity('Camera', dict(GameCamera=dict(width=260, height=180))),
                    wall('Floor', 0, -64, 204, 8), wall('Left wall', -100, -8, 8, 120),
                    wall('Right wall', 100, -8, 8, 120)]
        for e in entities:
            if e['name'] in ('Left wall', 'Right wall'):
                e['components']['GameTransform']['y'] = -4
                e['components']['GameCollider']['height'] = 144
                e['components']['GameAppearance']['height'] = 144
        entities.append(wall('Ceiling', 0, 58 if short.startswith(('Gas', 'Thermal')) else 72, 204, 8))
        if short.startswith('Liquid'):
            entities += [wall('Reservoir left', -76, 43, 8, 50),
                         wall('Reservoir right', -24, 43, 8, 50),
                         wall('Release gate', -50, 18, 48, 6, 'MaterialShowcaseRelease'),
                         wall('Upper ledge', -65, 2, 46, 6), wall('Lower ledge', -20, -27, 46, 6),
                         wall('Basin left', -95, -48, 6, 24), wall('Basin right', 25, -48, 6, 24),
                         volume('Water', -50, 43, 40, 38, BLUE)]
        elif short.startswith('Gas'):
            entities += [wall('Left baffle', -48, 20, 38, 6), wall('Right baffle', 48, 42, 38, 6),
                         volume('Cyan gas', -18, -40, 40, 34, CYAN, (30, 12), gas=True, heat=.8),
                         volume('Magenta gas', 18, -40, 40, 34, MAGENTA, (-30, -12), gas=True, heat=.8)]
            paddle = wall('Mixing paddle', 0, -20, 64, 6)
            paddle['components']['GameBody'] = dict(type='Kinematic', angular_velocity=1.3,
                                                    allow_rotation=True)
            paddle['components']['GameOrientation'] = dict(radians=0)
            entities.append(paddle)
        elif short.startswith('Thermal'):
            entities += [wall('Barrier', 0, -3, 6, 114,
                              'MaterialShowcaseBreach' if short == 'Thermal' else None),
                         volume('Coolant', -42, -8, 40, 64, BLUE, (20, 2), heat=0),
                         volume('Fuel', 42, 8, 40, 64, AMBER, (-20, -2), heat=1, fuel=True)]
        elif short == 'Destruction':
            specimen = wall('Specimen', -10, 4, 40, 114, 'MaterialShowcaseFracture')
            specimen['components']['GameOrientation'] = dict(radians=.22)
            specimen['components']['MaterialBreakProfile'].update(chip_speed=80, fragment_speed=40)
            ledge = wall('Landing ledge', 55, -38, 38, 6)
            ledge['components']['GameOrientation'] = dict(radians=-.3)
            entities += [specimen, ledge, volume('Water', -60, -40, 44, 24, BLUE, (8, 0))]
        else:
            entities += [wall('Contact pillar', 0, -42, 28, 36),
                         volume('Organic specimen', 0, 0, 42, 34, 0x87cc8aff,
                                (0, 0), elastic=True)]
        verify_enclosure(entities)
        publish(runner, 'scene', name, entities)
        for suffix in ('rml', 'rcss'):
            (runner.project / 'content/ui' / f'{name}.{suffix}').unlink(missing_ok=True)
    subprocess.run([str(runner.build / 'chibi_ctl'), '--project-root', str(runner.project), '--format'],
                   check=True, capture_output=True, text=True)


def check(runner, scenes, preview, profile):
    report = {}
    controller = runner.build / 'chibi_ctl'
    controller_hash = hashlib.sha256(controller.read_bytes()).hexdigest()
    for short in scenes:
        name = f'MaterialShowcase{short}'
        runner.author('validateAsset', dict(kind='scene', name=name))
        samples = {}
        for frame in (1, 60, 180, 360, 480):
            value = runner.call('runtime_snapshot', kind='scene', name=name, frames=frame,
                                entity_limit=64, component_limit=32, field_limit=32)
            assert all(not b['last_failure'] for e in value['entities'] for b in e['behaviors'])
            samples[str(frame)] = value
        replay = runner.call('runtime_snapshot', kind='scene', name=name, frames=480)
        assert replay['materials']['hash'] == samples['480']['materials']['hash'], name
        initial, final = samples['1']['materials'], samples['480']['materials']
        minimum = 8000 if short.startswith('Gas') else 60000 if short.startswith('Thermal') else 12000
        assert initial['particles'] >= minimum, (name, initial)
        assert all(v['materials']['particles'] == initial['particles'] for v in samples.values()), name
        assert all(not v['materials']['mpm']['limited'] for v in samples.values()), name
        radius = 1.125 if short.startswith('Gas') else .375
        # Swept contacts normally retain the full support radius. Existing
        # penetrations resolve using 0.3 * radius in material_fluid.c; this is
        # the minimum geometric center clearance promised by the solver.
        skin = .3 * radius
        epsilon = .005
        ceiling = -54 if short.startswith(('Gas', 'Thermal')) else -68
        for frame, value in samples.items():
            bounds = value['materials']['particle_bounds']
            assert bounds['x'] >= -96 + skin - epsilon and \
                   bounds['x'] + bounds['width'] <= 96 - skin + epsilon and \
                   bounds['y'] >= ceiling + skin - epsilon and \
                   bounds['y'] + bounds['height'] <= 60 - skin + epsilon, \
                   (name, frame, 'Particles escaped the closed tank', bounds)
        assert all(math.isfinite(v['materials']['volume_max']) and
                   v['materials']['volume_min'] > 0 for v in samples.values()), name
        if short in ('Liquid', 'LiquidDDR', 'Thermal'):
            assert final['cells'] < initial['cells'], f'{name}: timed cut did not remove cells'
        if short == 'Destruction':
            assert final['fragments'] > initial['fragments'], 'Cut did not release rigid fragments'
            assert final['cells'] <= initial['cells'], 'Destruction created solid matter'
        if short.startswith('Organic'):
            assert final['elastic_particles'] == initial['particles'], name
            assert max(v['materials']['strain_max'] for v in samples.values()) > .02, \
                f'{name}: specimen did not visibly deform'
        if short == 'ThermalClosed':
            assert math.isclose(final['thermal_energy'], initial['thermal_energy'], rel_tol=1e-5), \
                'Heat crossed the sealed barrier'
        if preview:
            for frame in (60, 180, 480):
                rendered = runner.call('preview_scene', True, kind='scene', name=name,
                                       frames=frame, width=1280, height=720)
                (runner.output / f'{name}-{frame}.png').write_bytes(base64.b64decode(rendered['preview']['data']))
        asset_paths = [runner.project / 'content/scenes' / f'{name}.kdl',
                       runner.project / 'content/prototypes' / f'{name}World.kdl',
                       runner.project / 'content/prototypes/MaterialShowcaseEntity.kdl']
        asset_paths.extend(runner.project / 'content/behaviors' / f'MaterialShowcase{suffix}.kdl'
                           for suffix in ('Release', 'Breach', 'Fracture'))
        report[name] = dict(samples=samples, validation='case_passed',
                           full_interval_verified=False, provenance=dict(
            controller=str(controller), controller_sha256=controller_hash,
            fixed_step_seconds=1 / 60,
            assets={str(path.relative_to(runner.project)): hashlib.sha256(path.read_bytes()).hexdigest()
                    for path in asset_paths}))
        if profile:
            report[name]['cpu_profile'] = runner.call('profile_scenario', kind='scene', name=name,
                                                      frames=480, warmup_frames=0, behavior_details=False)
            measured = report[name]['cpu_profile']
            measured['measurement_context'] = 'correctness evidence; concurrent work may affect timing'
            assert measured['timing']['sampled_frames'] == 480, name
            assert measured['timing']['material_fluid_work']['mpm_limited_steps'] == 0, \
                f'{name}: MPM failed to advance a complete fixed step'
            assert measured['world_hash'] == samples['480']['world_hash'], \
                f'{name}: full-interval profile changed deterministic state'
            report[name]['full_interval_verified'] = True
        (runner.output / 'material-showcase.json').write_text(json.dumps(report, indent=2) + '\n')
        print(f'PASS: {name}, replayed 480 fixed steps', flush=True)
    if 'MaterialShowcaseThermal' in report and 'MaterialShowcaseThermalClosed' in report:
        assert report['MaterialShowcaseThermal']['samples']['1']['materials']['hash'] == \
               report['MaterialShowcaseThermalClosed']['samples']['1']['materials']['hash'], \
               'Thermal fixtures differ before the breach'
        opened = report['MaterialShowcaseThermal']['samples']['480']['materials']
        closed = report['MaterialShowcaseThermalClosed']['samples']['480']['materials']
        assert opened['burning_particles'] < closed['burning_particles'], 'Contact did not quench fuel'
        assert opened['fuel_remaining'] > closed['fuel_remaining'], 'Quenching did not preserve fuel'
    for original, debug in (('Gas', 'GasSpeed'), ('Organic', 'OrganicStrain')):
        original, debug = f'MaterialShowcase{original}', f'MaterialShowcase{debug}'
        if original in report and debug in report:
            for frame in report[original]['samples']:
                assert report[original]['samples'][frame]['materials']['hash'] == \
                       report[debug]['samples'][frame]['materials']['hash'], \
                       f'{debug}: debug view changed simulated material'
    assert hashlib.sha256(controller.read_bytes()).hexdigest() == controller_hash, \
        'Controller changed during the acceptance run'
    for value in report.values():
        value['validation'] = 'passed'
    (runner.output / 'material-showcase.json').write_text(json.dumps(report, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', default='build-release')
    parser.add_argument('--project', type=Path, default=PROJECT)
    parser.add_argument('--output', type=Path, default=Path('/tmp/chibi-material-showcase'))
    parser.add_argument('--author', action='store_true')
    parser.add_argument('--scene', action='append', choices=SCENES)
    parser.add_argument('--skip-preview', action='store_true')
    parser.add_argument('--profile', action='store_true')
    args = parser.parse_args()
    runner = Runner(args.build, args.output, args.project)
    if args.author:
        author(runner, args.scene or SCENES)
    check(runner, args.scene or SCENES, not args.skip_preview, args.profile)


if __name__ == '__main__':
    main()
