#!/usr/bin/env python3
"""Author and replay a real ignition-driven material explosion showcase."""
import argparse
import base64
import json
import hashlib
import math
import subprocess
from pathlib import Path

from check_neon_swarm import PROJECT, Runner, fields
from material_showcase_scenario import BLUE, CYAN, STONE, entity, publish, volume, wall

NAME = 'MaterialShowcaseExplosion'


def count_parcels(width, height, spacing, gas):
    nx, ny = math.ceil(width / spacing), math.ceil(height / spacing)
    count = 0
    for y in range(ny):
        for x in range(nx):
            px = 2 * (x + .5 - nx * .5) * spacing / width
            py = 2 * (y + .5 - ny * .5) * spacing / height
            edge = 1
            if gas:
                angle = math.atan2(py, px)
                edge = (.76 + .12 * math.sin(3 * angle) + .07 * math.cos(5 * angle + 1)
                        + .05 * math.sin(7 * angle + 2))
            count += math.hypot(px, py) <= edge
    return count


def dense_volume(name, x, y, width, height, color, gas=False, fuel=False):
    result = volume(name, x, y, width, height, color, gas=gas, fuel=fuel)
    fluid = result['components']['GameFluidVolume']
    old_spacing = 1.5 if gas else 1
    spacing = old_spacing / 4
    share = count_parcels(width, height, old_spacing, gas) / count_parcels(width, height, spacing, gas)
    fluid.update(spacing=spacing, mass=.001 * old_spacing**2 * share)
    if fuel:
        result['components']['GameFuel']['amount'] = .05 * share
    return result


def author(runner):
    runner.author('searchNodes', dict(domain='behavior', query='Set GameIgniter', limit=4))
    graph = NAME + 'Ignition'
    publish(runner, 'behavior', graph, [
        dict(op='add_node', type='Event', event_name='Begin', ref='begin'),
        dict(op='add_node', type='Delay', ref='wait', inputs={'Duration': 1}),
        dict(op='add_node', type='Set Component', ref='ignite',
             inputs={'Component': 'GameIgniter', 'heat': 2}),
        dict(op='connect', kind='exec', **{'from': 'begin', 'from_pin': 'Then', 'to': 'wait', 'to_pin': 'Exec'}),
        dict(op='connect', kind='exec', **{'from': 'wait', 'from_pin': 'Completed', 'to': 'ignite', 'to_pin': 'Exec'}),
    ])
    world = dict(MaterialFluidWorld=dict(chunk_size=64, buffer=32, kernel=.75,
                 particle_limit=131072, solver='Mpm', mpm_cell_size=3, mpm_max_substeps=32,
                 gravity=dict(x=0, y=0), gas_cell_size=4, gas_vorticity=1, gas_drag=.2),
                 MaterialDestructionWorld=dict(fragment_limit=96, debris_limit=512),
                 GameHRC2D=dict(resolution=.5, min_resolution=.5, target_fps=60,
                               adaptive=False, ambient=.2, trace_levels=1),
                 GameCombustion=dict(ignition=.9, extinction=.4, burn_rate=.1 * 32 / 448,
                      heat_release=2, cooling=.01, flame_color=0xffa135ff,
                      flame_emission=.8, smoke_lifetime=6, smoke_color=0x9a657aff,
                      smoke_absorption=.015, blast_radius=80, blast_speed=3, blast_damage=.75))
    publish(runner, 'prototype', NAME + 'World',
            [dict(op='add_component', component=c, fields=v) for c, v in world.items()])
    slab = wall('Crater slab', 0, -20, 130, 24)
    slab['components'].update(GameDestructible=dict(cell_size=2, min_fragment_cells=6),
         GameSubstance=dict(hardness=1, retain_chips=False, blast_multiplier=1),
         MaterialBreakProfile=dict(chip_speed=8, fragment_speed=4, release_speed=4,
                                   dust_limit=0, chunk_limit=0, smoke_count=0))
    fuel = dense_volume('Charge', 6, 6, 6, 6, 0xf3b24aff, fuel=True)
    igniter = entity('Timed igniter', dict(GameTransform=dict(x=6, y=-6),
         GameBody=dict(type='Static'), GameCollider=dict(shape='Circle', radius=.1, category=8, mask=15),
         GameIgniter=dict(heat=0)), [graph])
    entities = [dict(op='add_entity', name='Materials', prototype=NAME + 'World'),
        entity('Camera', dict(GameCamera=dict(width=260, height=180))),
        wall('Floor', 0, -64, 204, 8), wall('Ceiling', 0, 64, 204, 8),
        wall('Left wall', -100, 0, 8, 136), wall('Right wall', 100, 0, 8, 136),
        slab, fuel, igniter,
        dense_volume('Water', -26, 4, 24, 24, BLUE),
        dense_volume('Gas', 38, 12, 30, 28, CYAN, gas=True)]
    for i, (x, y) in enumerate(((-35, 32), (-8, 38), (32, 38))):
        entities.append(entity(f'Loose block {i + 1}', dict(
            GameTransform=dict(x=x, y=-y), GameVelocity=dict(x=0, y=0),
            GameBody=dict(type='Dynamic', allow_rotation=True),
            GameCollider=dict(shape='Box', width=10, height=10, category=8, mask=15),
            GameAppearance=dict(width=10, height=10, color=STONE),
            GameMaterial=dict(scene=STONE, albedo=STONE, absorption=1))))
    publish(runner, 'scene', NAME, entities)
    for suffix in ('rml', 'rcss'):
        path = runner.project / 'content/ui' / f'{NAME}.{suffix}'
        if path.exists():
            path.unlink()
    subprocess.run([str(runner.build / 'chibi_ctl'), '--project-root', str(runner.project), '--format'],
                   check=True, capture_output=True, text=True)


def check(runner, previews, profile=False):
    report = {}
    for frame in (1, 59, 63, 90, 180, 360):
        value = runner.call('runtime_snapshot', kind='scene', name=NAME, frames=frame,
                            entity_limit=64, component_limit=32, field_limit=32)
        assert all(not b['last_failure'] for e in value['entities'] for b in e['behaviors'])
        bounds = value['materials']['particle_bounds']
        assert bounds['x'] >= -96 and bounds['x'] + bounds['width'] <= 96
        assert bounds['y'] >= -60 and bounds['y'] + bounds['height'] <= 60
        assert not any(fields(e).get('GameUi.document') for e in value['entities'])
        report[str(frame)] = value
        if previews and frame in (59, 63, 90, 180, 360):
            result = runner.call('preview_scene', True, kind='scene', name=NAME,
                                 frames=frame, width=1280, height=720)
            (runner.output / f'explosion-{frame}.png').write_bytes(base64.b64decode(result['preview']['data']))
    (runner.output / 'material-explosion.json').write_text(json.dumps(report, indent=2) + '\n')
    before, after = report['59']['materials'], report['180']['materials']
    assert after['cells'] < before['cells'], 'Combustion blast did not cut terrain'
    assert after['fuel_remaining'] < before['fuel_remaining'], 'Fuel did not burn'
    assert before['burning_particles'] == 0, 'Timed ignition occurred early'
    assert report['63']['materials']['burning_particles'] > 0, 'Timed ignition did not light fuel'
    assert after['particles'] == before['particles'] == 10442, 'Explosion lost simulated matter'
    assert all(not sample['materials']['mpm']['limited'] for sample in report.values())
    for frame, moving in (('59', False), ('63', True)):
        blocks = [fields(e) for e in report[frame]['entities'] if e.get('name', '').startswith('Loose block')]
        assert len(blocks) == 3
        for block in blocks:
            speed_squared = block['GameVelocity.x']**2 + block['GameVelocity.y']**2
            assert speed_squared > 1 if moving else speed_squared == 0
    replay = runner.call('runtime_snapshot', kind='scene', name=NAME, frames=180,
                         entity_limit=64, component_limit=32, field_limit=32)
    assert replay['world_hash'] == report['180']['world_hash'], 'Explosion replay diverged'
    if profile:
        measured = runner.call('profile_scenario', kind='scene', name=NAME,
                              frames=360, warmup_frames=0, behavior_details=False)
        assert measured['timing']['sampled_frames'] == 360
        assert measured['timing']['material_fluid_work']['mpm_limited_steps'] == 0, \
            'Explosion did not advance a complete fixed step'
        assert measured['world_hash'] == report['360']['world_hash'], \
            'Full-interval profile changed deterministic state'
        report['cpu_profile'] = measured
        report['full_interval_verified'] = True
    controller = (runner.build / 'chibi_ctl').resolve()
    asset_paths = [runner.project / 'content/scenes' / f'{NAME}.kdl',
                   runner.project / 'content/prototypes' / f'{NAME}World.kdl',
                   runner.project / 'content/prototypes/MaterialShowcaseEntity.kdl',
                   runner.project / 'content/behaviors' / f'{NAME}Ignition.kdl']
    report['provenance'] = dict(controller=str(controller),
        controller_sha256=hashlib.sha256(controller.read_bytes()).hexdigest(),
        fixed_step_seconds=1 / 60,
        assets={str(path.relative_to(runner.project)): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in asset_paths})
    (runner.output / 'material-explosion.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: timed ignition, fuel consumption and blast-created crater', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', default='build-release')
    parser.add_argument('--project', type=Path, default=PROJECT)
    parser.add_argument('--output', type=Path, default=Path('/tmp/chibi-material-explosion'))
    parser.add_argument('--author', action='store_true')
    parser.add_argument('--skip-preview', action='store_true')
    parser.add_argument('--profile', action='store_true')
    args = parser.parse_args()
    runner = Runner(args.build, args.output, args.project)
    if args.author:
        author(runner)
    check(runner, not args.skip_preview, args.profile)


if __name__ == '__main__':
    main()
