#!/usr/bin/env python3
"""Author and measure fine fluid grains crossing a destructible barrier."""
import argparse
import base64
import copy
import json
import math
import subprocess
from pathlib import Path

from check_neon_swarm import PROJECT, Runner
from material_thermal_scenario import publish, snapshot


def parcel_count(width, height, spacing):
    nx, ny = math.ceil(width / spacing), math.ceil(height / spacing)
    return sum((((x + .5 - nx * .5) * spacing / (width * .5)) ** 2 +
                ((y + .5 - ny * .5) * spacing / (height * .5)) ** 2) <= 1
               for y in range(ny) for x in range(nx))


def author(runner, spacing):
    runner.author('searchAssets', dict(query='ThermalStudy', limit=8))
    base = runner.author('readAsset', dict(kind='prototype', name='ThermalStudyWorld'))
    world = copy.deepcopy(base['document']['components'])
    count = parcel_count(40, 64, spacing)
    share = parcel_count(40, 64, 8) / count
    world['GAME.MaterialFluidWorld'].update(kernel=spacing * 3, particle_limit=count * 2 + 64)
    world['GAME.GameCombustion'].update(burn_rate=.01 * share, flame_emission=.6)
    world['GAME.GameHRC2D']['adaptive'] = False
    publish(runner, 'buildPrototype', 'prototype', 'MaterialDetailWorld',
            [dict(op='add_component', component=c, fields=v) for c, v in world.items()])
    for closed in (False, True):
        base = runner.author('readAsset', dict(kind='scene', name='ThermalStudyClosed' if closed else 'ThermalStudy'))
        entities = []
        for entity in base['document']['entities']:
            entity = copy.deepcopy(entity)
            entity['op'] = 'add_entity'
            entity.pop('retained_components', None)
            if entity['name'] == 'Materials':
                entity['prototype'] = 'MaterialDetailWorld'
            if entity['name'] in ('Coolant', 'Fuel'):
                fuel = entity['name'] == 'Fuel'
                components = entity['components']
                components['GameTransform']['y'] = 8 if fuel else -8
                components['GameFluidVolume'].update(
                    spacing=spacing, mass=.004 * share, grain_radius=spacing * .22,
                    visibility=.3, refraction=0,
                    velocity=dict(x=-20 if fuel else 20, y=-2 if fuel else 2))
                if fuel:
                    components['GameFuel']['amount'] = share
            entities.append(entity)
        publish(runner, 'buildScene', 'scene', 'MaterialDetailClosed' if closed else 'MaterialDetail', entities)
    subprocess.run([str(runner.build / 'chibi_ctl'), '--project-root', str(runner.project), '--format'],
                   check=True, capture_output=True, text=True)
    print(f'Authored {count * 2:,} simulated grains, spacing {spacing:g}, '
          f'grain diameter {spacing * .44:g} world units', flush=True)


def check(runner, previews, profile):
    for kind, name in (('prototype', 'MaterialDetailWorld'),
                       ('scene', 'MaterialDetail'), ('scene', 'MaterialDetailClosed')):
        runner.author('validateAsset', dict(kind=kind, name=name))
    report = {}
    for name in ('MaterialDetail', 'MaterialDetailClosed'):
        report[name] = {str(frame): snapshot(runner, name, frame)
                        for frame in (1, 29, 240, 480)}
    opened, closed = report['MaterialDetail'], report['MaterialDetailClosed']
    assert opened['29']['materials']['hash'] == closed['29']['materials']['hash']
    initial = opened['1']['materials']
    end, control = opened['480']['materials'], closed['480']['materials']
    assert initial['particles'] >= 4000, 'Demo must contain thousands of simulated grains'
    assert end['particles'] == control['particles'] == initial['particles']
    assert end['thermal_particles'] == initial['particles']
    assert math.isclose(initial['thermal_energy'], .128, rel_tol=1e-5), 'Resampling changed represented hot mass'
    assert math.isclose(control['thermal_energy'], initial['thermal_energy'], rel_tol=1e-6), 'Heat leaked through the sealed wall'
    assert end['cells'] < control['cells']
    assert end['burning_particles'] < control['burning_particles'], 'Contact did not cool any fuel'
    assert end['fuel_remaining'] > control['fuel_remaining'], 'Cooling did not preserve fuel'
    assert snapshot(runner, 'MaterialDetail', 480)['materials']['hash'] == end['hash']
    if previews:
        for frame in (29, 120, 240, 480):
            result = runner.call('preview_scene', True, kind='scene', name='MaterialDetail',
                                 frames=frame, width=1280, height=720)
            (runner.output / f'detail-{frame}.png').write_bytes(base64.b64decode(result['preview']['data']))
    if profile:
        report['profile'] = runner.call('profile_scenario', kind='scene', name='MaterialDetail',
                                        frames=479, warmup_frames=1, behavior_details=False)
        assert report['profile']['world_hash'] == opened['480']['world_hash']
        print('Simulation milliseconds:', report['profile']['timing']['frame_ms'], flush=True)
    (runner.output / 'material-detail.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: fine matter, conserved initial mass, sealed control, cooling, fuel retention and replay', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', default='build-release')
    parser.add_argument('--project', type=Path, default=PROJECT)
    parser.add_argument('--output', type=Path, default=Path('/tmp/chibi-material-detail'))
    parser.add_argument('--author', action='store_true')
    parser.add_argument('--spacing', type=float, default=1)
    parser.add_argument('--skip-preview', action='store_true')
    parser.add_argument('--profile', action='store_true')
    args = parser.parse_args()
    assert 2 / 3 <= args.spacing <= 1
    runner = Runner(args.build, args.output, args.project)
    if args.author:
        author(runner, args.spacing)
    check(runner, not args.skip_preview, args.profile)


if __name__ == '__main__':
    main()
