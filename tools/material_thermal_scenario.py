#!/usr/bin/env python3
"""Author and verify a zero-gravity, noncombat thermal/barrier experiment."""
import argparse
import base64
import json
import math
from pathlib import Path

from check_neon_swarm import PROJECT, Runner


def publish(runner, tool, kind, name, operations):
    path = runner.project / 'content' / {
        'prototype': 'prototypes', 'behavior': 'behaviors', 'scene': 'scenes'
    }[kind] / f'{name}.kdl'
    arguments = dict(kind=kind, name=name, operations=operations)
    if kind == 'behavior':
        arguments['owner_type'] = 'ecs.entity'
    if path.exists():
        asset = runner.author('readAsset', dict(kind=kind, name=name))
        arguments['expected_hash'] = asset['hash']
        if kind == 'scene':
            arguments['operations'] = [dict(op='remove_entity', entity=e['name'])
                                       for e in asset['document']['entities']] + operations
        elif kind == 'behavior':
            graph = runner.author('inspectGraph', dict(kind=kind, name=name))
            delay = next(node for node in graph['graph']['nodes'] if node['type'] == 'Delay')
            arguments['operations'] = [dict(op='set_input', node=delay['id'], pin='Duration', value=.5)]
            runner.author(tool, arguments)
            runner.author('validateAsset', dict(kind=kind, name=name))
            return
        else:
            arguments['operations'] = [dict(op='remove_component', component=c)
                                       for c in asset['document']['components']] + operations
    else:
        arguments['create'] = True
    runner.author(tool, arguments)
    runner.author('validateAsset', dict(kind=kind, name=name))


def author(runner):
    runner.author('searchAssets', dict(query='MaterialStudy', limit=8))
    runner.author('inspectGraph', dict(kind='behavior', name='CutMaterial'))
    for query in ('Delay', 'Damage Material'):
        runner.author('searchNodes', dict(domain='behavior', query=query, limit=8))
    runner.author('describeComponents', {})
    world = {
        'MaterialFluidWorld': dict(chunk_size=128, buffer=64, kernel=24, particle_limit=2048,
                                  gas_cell_size=0, liquid_splash_count=0, gas_wake_count=0),
        'MaterialDestructionWorld': dict(fragment_limit=32, debris_limit=0),
        'GameCombustion': dict(ignition=.9, extinction=.8, cooling=0, burn_rate=.01,
                              heat_release=1, flame_color=0xffa135ff, flame_emission=1,
                              smoke_lifetime=6, smoke_color=0x575d70ff,
                              smoke_absorption=.012, blast_radius=0,
                              blast_speed=0, blast_damage=0),
        'GameHRC2D': dict(resolution=.5, min_resolution=.5, target_fps=60, adaptive=False,
                         ambient=.15, intensity=1, trace_levels=1),
    }
    publish(runner, 'buildPrototype', 'prototype', 'ThermalStudyWorld',
            [dict(op='add_component', component=c, fields=v) for c, v in world.items()])
    publish(runner, 'buildPrototype', 'prototype', 'ThermalStudyEntity',
            [dict(op='add_component', component='GameTransform')])
    operations = [
        dict(op='add_node', type='Event', event_name='Begin', ref='begin'),
        dict(op='add_node', type='Delay', ref='wait', inputs={'Duration': .5}),
        dict(op='add_self_node', ref='barrier'),
        dict(op='add_node', type='Damage Material', ref='breach',
             inputs={'Point': {'x': 0, 'y': 0}, 'Radius': 36, 'Energy': 10}),
        dict(op='connect', kind='exec', **{'from': 'begin', 'from_pin': 'Then', 'to': 'wait', 'to_pin': 'Exec'}),
        dict(op='connect', kind='exec', **{'from': 'wait', 'from_pin': 'Completed', 'to': 'breach', 'to_pin': 'In'}),
        dict(op='connect', **{'from': 'barrier', 'from_pin': 'Self', 'to': 'breach', 'to_pin': 'Entity'}),
    ]
    publish(runner, 'buildGraph', 'behavior', 'ThermalStudyBreach', operations)
    for breach in (False, True):
        entities = [dict(op='add_entity', name='Materials', prototype='ThermalStudyWorld'),
                    dict(op='add_entity', name='Camera', components={
                        'GameTransform': {'x': 0, 'y': 0},
                        'GameCamera': {'width': 260, 'height': 180}})]
        for name, x, y, width, height in (
                ('Left wall', -75, 0, 8, 112), ('Right wall', 75, 0, 8, 112),
                ('Top wall', 0, 56, 158, 8), ('Bottom wall', 0, -56, 158, 8),
                ('Barrier', 0, 0, 8, 104)):
            components = {
                'GameTransform': dict(x=x, y=y), 'GameBody': dict(type='Static'),
                'GameCollider': dict(shape='Box', width=width, height=height, category=8, mask=15),
                'GameAppearance': dict(width=width, height=height, color=0x667080ff),
                'GameMaterial': dict(scene=0x667080ff, albedo=0x667080ff, absorption=1),
            }
            if name == 'Barrier':
                components.update(GameDestructible=dict(cell_size=2, min_fragment_cells=6),
                                  GameSubstance=dict(hardness=1, retain_chips=False),
                                  MaterialBreakProfile=dict(chip_speed=0, fragment_speed=0,
                                                           release_speed=0, dust_limit=0,
                                                           chunk_limit=0, smoke_count=0))
            entities.append(dict(op='add_entity', name=name, components=components,
                                 behaviors=['ThermalStudyBreach'] if breach and name == 'Barrier' else []))
        for fuel in (False, True):
            components = {
                'GameTransform': dict(x=42 if fuel else -42, y=0),
                'GameFluidVolume': dict(kind=1, width=40, height=64, spacing=8, mass=.004,
                                       velocity=dict(x=-12 if fuel else 12, y=0),
                                       color=0xf4a547ff if fuel else 0x399fe8ff,
                                       absorption=.035, visibility=.08, refraction=2),
                'MaterialThermal': dict(capacity=1 if fuel else 128, conductivity=600),
            }
            if fuel:
                components.update(GameFuel=dict(amount=1), GameIgniter=dict(heat=1))
            entities.append(dict(op='add_entity', name='Fuel' if fuel else 'Coolant', components=components))
        for entity in entities:
            entity.setdefault('prototype', 'ThermalStudyEntity')
        publish(runner, 'buildScene', 'scene', 'ThermalStudy' if breach else 'ThermalStudyClosed', entities)


def snapshot(runner, name, frame):
    result = runner.call('runtime_snapshot', kind='scene', name=name, frames=frame,
                         entity_limit=32, component_limit=32, field_limit=32)
    for entity in result['entities']:
        assert all(not b['last_failure'] for b in entity['behaviors']), entity
    return result


def check(runner, previews):
    for kind, name in (('prototype', 'ThermalStudyWorld'), ('prototype', 'ThermalStudyEntity'),
                       ('behavior', 'ThermalStudyBreach'),
                       ('scene', 'ThermalStudy'), ('scene', 'ThermalStudyClosed')):
        runner.author('validateAsset', dict(kind=kind, name=name))
    samples = {}
    for name in ('ThermalStudyClosed', 'ThermalStudy'):
        samples[name] = {frame: snapshot(runner, name, frame) for frame in (1, 29, 240, 480)}
    report = {name: {str(frame): value['materials'] for frame, value in frames.items()}
              for name, frames in samples.items()}
    (runner.output / 'thermal-study.json').write_text(json.dumps(report, indent=2) + '\n')
    closed, opened = samples['ThermalStudyClosed'], samples['ThermalStudy']
    assert closed[29]['materials']['hash'] == opened[29]['materials']['hash'], 'Scenes differ before timed breach'
    initial = opened[1]['materials']
    end, control = opened[480]['materials'], closed[480]['materials']
    assert initial['thermal_particles'] > 0 and initial['burning_particles'] > 0
    assert end['thermal_particles'] == control['thermal_particles'] == initial['thermal_particles'], \
        'Barrier experiment created or discarded thermal matter'
    assert end['cells'] < control['cells'], 'Timed damage did not open the barrier'
    assert control['burning_particles'] > 0, 'Closed control lost combustion without coolant contact'
    assert math.isclose(control['thermal_energy'], initial['thermal_energy'], rel_tol=1e-6), \
        'Heat crossed the intact barrier'
    assert end['burning_particles'] < control['burning_particles'] / 2, 'Opening failed to quench fuel'
    assert end['fuel_remaining'] > control['fuel_remaining'], 'Quenching did not preserve fuel'
    replay = snapshot(runner, 'ThermalStudy', 480)
    assert replay['materials']['hash'] == end['hash'], 'Thermal scene is not deterministic'
    if previews:
        for label, frame in (('sealed', 29), ('breached', 240), ('quenched', 480)):
            preview = runner.call('preview_scene', True, kind='scene', name='ThermalStudy',
                                  frames=frame, width=780, height=540)
            (runner.output / f'thermal-{label}.png').write_bytes(base64.b64decode(preview['preview']['data']))
    print('PASS: occluded control, timed breach, coolant quenching, fuel retention and deterministic replay')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', default='build')
    parser.add_argument('--project', type=Path, default=PROJECT)
    parser.add_argument('--output', type=Path, default=Path('/tmp/chibi-thermal-study'))
    parser.add_argument('--author', action='store_true', help='Create/update only ThermalStudy assets through MCP')
    parser.add_argument('--skip-preview', action='store_true')
    args = parser.parse_args()
    runner = Runner(args.build, args.output, args.project)
    if args.author:
        author(runner)
    check(runner, not args.skip_preview)


if __name__ == '__main__':
    main()
