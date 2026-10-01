"""Repeatable authored workloads for physics and material profiling.

Fixtures are applied to an isolated Arena2D project copy through Chibi MCP.
The live Player and Weapon create every projectile used by the barrage cases.
"""

import math
from physics_material_crowd import SPAWN_BATCH, crowd_layout

WARMUP_FRAMES = 60
MEASURED_FRAMES = 240
TOTAL_FRAMES = WARMUP_FRAMES + MEASURED_FRAMES
DEFAULT_CROWD_COUNT = 20000
MAX_CROWD_COUNT = 20000
FAN_DEGREES = 120
FAN_SWEEP_FRAMES = 60
FAN_PROBE_FRAMES = 8

SCENARIOS = (
    'terrain_barrage',
    'enemy_barrage',
    'enemy_collisions',
    'fog_liquid',
)

KEEP_ENTITIES = ('Player', 'Camera', 'HRC', 'FireMouse', 'FireStick')
FIRE_COOLDOWN = 1.0 / 240.0
PROJECTILE_LIFETIME = 1.0


def sample(frame, x=1, y=0, firing=False, point=False, movement=None):
    inputs = dict(
        aim=dict(x=x, y=y, firing=firing, point=point),
        controls=[1] if firing else [],
    )
    if movement is not None:
        inputs['movement'] = dict(x=movement[0], y=movement[1])
    return dict(frame=frame, input=inputs)


def entity(name, prototype, x, y, behaviors=None, **components):
    result = dict(
        op='add_entity',
        name=name,
        prototype=prototype,
        components=dict(GameTransform=dict(x=x, y=y), **components),
    )
    if behaviors is not None:
        result['behaviors'] = behaviors
    return result


def _prototype_operations():
    return {
        'Player': [dict(op='set_field', component='GameWeapon', field='cooldown', value=FIRE_COOLDOWN)],
        'Projectile': [
            dict(op='set_field', component='GameProjectile', field='damage', value=1),
            dict(op='set_field', component='GameProjectile', field='lifetime', value=PROJECTILE_LIFETIME),
        ],
        # Its particles live at most .4 s; retire the one-shot source after
        # they finish so effect-source entities do not accumulate indefinitely.
        'HitEmitter': [dict(op='set_field', component='GameProjectile', field='lifetime', value=.5)],
    }


def _aim_timeline():
    """Repeat a broad directional sweep; the authored Weapon drains its cooldown."""
    samples = [sample(0)]
    for frame in range(WARMUP_FRAMES, TOTAL_FRAMES - 1):
        # Coprime stride visits every angle once per second while keeping the
        # visible spray broad during short intervals too.
        lane = ((frame - WARMUP_FRAMES) * 17) % FAN_SWEEP_FRAMES
        angle = math.radians(FAN_DEGREES * (lane / (FAN_SWEEP_FRAMES - 1) - .5))
        samples.append(sample(frame, math.cos(angle), math.sin(angle), True))
    samples.append(sample(TOTAL_FRAMES - 1))
    return samples


def _terrain_barrage():
    targets = [(0, y) for y in (-300, -100, 100, 300)]
    operations = [dict(op='set_entity_components', entity='Camera', components={
        'GameCamera': dict(height=1000, width=1000 * 16 / 9),
    })]
    for index, (_, y) in enumerate(targets):
        operations.append(entity(
            f'Benchmark terrain {index}', 'HardRock', 0, y,
            GameAppearance=dict(width=240, height=192),
            GameCollider=dict(category=8, height=192, mask=15, shape='Box', width=240),
            GameDestructible=dict(cell_size=2, min_fragment_cells=12),
            GameSubstance=dict(hardness=.5, retain_chips=True),
        ))
    return dict(
        operations=operations,
        input_timeline=_aim_timeline(),
        check=dict(targets=[f'Benchmark terrain {i}' for i in range(len(targets))]),
        control_input_timeline=[sample(0, targets[0][0], targets[0][1], False, True)],
    )


def _enemy_barrage():
    targets = [(40, y) for y in (-270, -90, 90, 270)]
    operations = [dict(op='set_entity_components', entity='Camera', components={
        'GameCamera': dict(height=1000, width=1000 * 16 / 9),
    })]
    for index, (x, y) in enumerate(targets):
        operations.append(entity(
            f'Benchmark enemy {index}', 'Swarmer', x, y,
            GameAppearance=dict(height=80, width=80),
            GameBody=dict(type='Dynamic'),
            GameCollider=dict(category=2, height=80, mask=15, radius=40, shape='Box', width=80),
            GameDestructible=dict(
                cell_size=2,
                min_fragment_cells=8,
                pierceable_fragments=True,
                release_fraction=.3,
            ),
            GameHealth=dict(current=500, maximum=500),
            GameMovement=dict(speed=0),
            GameVelocity=dict(x=0, y=0),
        ))
    return dict(
        operations=operations,
        input_timeline=_aim_timeline(),
        check=dict(targets=[f'Benchmark enemy {i}' for i in range(len(targets))]),
        control_input_timeline=[sample(0, targets[0][0], targets[0][1], False, True)],
    )


def _enemy_collisions(crowd_count):
    layout = crowd_layout(crowd_count)
    columns, rows = layout['columns'], layout['rows']
    operations = [
        entity('Benchmark spawner', 'BenchmarkCrowdSpawner', 105, 0),
        dict(op='set_entity_components', entity='Player', components={
            'GameTransform': dict(x=105 - (columns + 1) * 9 - 100, y=0),
        }),
        dict(op='set_entity_components', entity='Camera', components={
            'GameTransform': dict(x=105, y=0),
            'GameCamera': dict(height=max(540, rows * 18 + 150),
                width=max(960, (rows * 18 + 150) * 16 / 9)),
        }),
        dict(op='set_entity_behaviors', entity='Camera', behaviors=[]),
    ]
    return dict(
        operations=operations,
        input_timeline=[sample(0)],
        check=dict(crowd_count=crowd_count),
        control_input_timeline=[sample(0)],
    )


def _fog_liquid():
    timeline = [
        sample(0, movement=(0, 0)),
        sample(WARMUP_FRAMES, movement=(1, 0)),
        sample(WARMUP_FRAMES + MEASURED_FRAMES // 2, movement=(-1, 0)),
    ]
    return dict(
        operations=[
            entity('Benchmark fog', 'Cloud', -320, 0,
                GameFluidVolume=dict(width=320, height=200, spacing=5)),
            entity('Benchmark liquid', 'Liquid', -320, 0,
                GameFluidVolume=dict(width=160, height=128, kind=1, spacing=4)),
        ],
        input_timeline=timeline,
        check=dict(fog='Benchmark fog', liquid='Benchmark liquid'),
        control_input_timeline=[sample(0, movement=(0, 0))],
    )


def scenario(name, crowd_count=DEFAULT_CROWD_COUNT):
    if name not in SCENARIOS:
        raise ValueError(f'unknown physics/material scenario: {name}')
    if crowd_count < 1 or crowd_count > MAX_CROWD_COUNT:
        raise ValueError(f'crowd_count must be between 1 and {MAX_CROWD_COUNT}')
    fixture = {
        'terrain_barrage': _terrain_barrage,
        'enemy_barrage': _enemy_barrage,
        'enemy_collisions': lambda: _enemy_collisions(crowd_count),
        'fog_liquid': _fog_liquid,
    }[name]()
    warmup = max(WARMUP_FRAMES, math.ceil(crowd_count / SPAWN_BATCH) + 20) if name == 'enemy_collisions' else WARMUP_FRAMES
    return dict(
        name=name,
        warmup_frames=warmup,
        frames=MEASURED_FRAMES,
        total_frames=warmup + MEASURED_FRAMES,
        keep_entities=KEEP_ENTITIES,
        prototype_operations=_prototype_operations(),
        **fixture,
    )


def prepare(runner, fixture, prefix_operations=()):
    """Apply a fixture to a private project copy using hash-checked MCP edits."""
    if fixture['name'] == 'enemy_collisions':
        from physics_material_crowd import prepare_crowd
        prepare_crowd(runner, fixture['check']['crowd_count'])
    for name, operations in fixture['prototype_operations'].items():
        asset = runner.author('readAsset', dict(kind='prototype', name=name))
        runner.author('buildPrototype', dict(
            kind='prototype', name=name, expected_hash=asset['hash'], operations=operations,
        ))
        runner.author('validateAsset', dict(kind='prototype', name=name))

    asset = runner.author('readAsset', dict(kind='scene', name='Sandbox'))
    keep = set(fixture['keep_entities'])
    operations = [dict(op='remove_entity', entity=item['name'])
        for item in asset['document']['entities'] if item['name'] not in keep]
    operations.extend(prefix_operations)
    operations.extend(fixture['operations'])
    runner.author('buildScene', dict(
        kind='scene', name='Sandbox', expected_hash=asset['hash'], operations=operations,
    ))
    runner.author('validateAsset', dict(kind='scene', name='Sandbox'))
    return runner


def _entity_fields(entity_record):
    return {
        component['name'] + '.' + field['name']: field['value']
        for component in entity_record['components']
        for field in component['fields']
    }


def _named_entities(state, names):
    entities = {entity_record.get('name'): entity_record for entity_record in state['entities']}
    return {name: entities[name] for name in names if name in entities}


def check_fan(state):
    """Verify real projectile velocities cover both edges of the firing fan."""
    angles = []
    for record in state['entities']:
        values = _entity_fields(record)
        if values.get('GameCollider.category') == 4:
            angles.append(math.atan2(values['GameVelocity.y'], values['GameVelocity.x']))
    assert len(angles) >= 30, 'Fan probe does not sustain roughly four projectiles per tick'
    assert max(angles) - min(angles) >= math.radians(FAN_DEGREES - 10), 'Projectile fan is too narrow'


def check_load(name, before, mid, after, no_fire_after, crowd_count=DEFAULT_CROWD_COUNT):
    """Check fixture population and verify barrage effects during measured frames."""
    fixture = scenario(name, crowd_count)
    check = fixture['check']
    if name == 'terrain_barrage':
        names = check['targets']
        assert set(_named_entities(before, names)) == set(names), 'Terrain targets missing before fire'
        assert set(_named_entities(after, names)) == set(names), 'Terrain targets must remain as a sustained load'
        assert mid['materials']['cells'] < before['materials']['cells'], 'Terrain barrage made no measured-window cuts'
        assert after['materials']['cells'] < mid['materials']['cells'], 'Terrain barrage stopped before the sample ended'
        assert no_fire_after['materials']['cells'] == before['materials']['cells'], 'No-fire control changed terrain cells'
        return
    if name == 'enemy_barrage':
        names = check['targets']
        before_targets = _named_entities(before, names)
        mid_targets = _named_entities(mid, names)
        after_targets = _named_entities(after, names)
        control_targets = _named_entities(no_fire_after, names)
        assert set(before_targets) == set(names), 'Enemy targets missing before fire'
        assert set(mid_targets) == set(names), 'Enemy targets must remain during the sample'
        assert set(after_targets) == set(names), 'Enemy targets must remain alive through the sample'
        assert set(control_targets) == set(names), 'No-fire enemy targets missing'
        before_health = sum(_entity_fields(before_targets[target])['GameHealth.current'] for target in names)
        mid_health = sum(_entity_fields(mid_targets[target])['GameHealth.current'] for target in names)
        after_health = sum(_entity_fields(after_targets[target])['GameHealth.current'] for target in names)
        control_health = sum(_entity_fields(control_targets[target])['GameHealth.current'] for target in names)
        assert mid_health < before_health, 'Enemy barrage made no first-half health damage'
        assert after_health < mid_health, 'Enemy barrage made no second-half health damage'
        assert all(_entity_fields(after_targets[target])['GameHealth.current']
            < _entity_fields(before_targets[target])['GameHealth.current'] for target in names), \
            'Enemy barrage must damage every target'
        assert all(_entity_fields(after_targets[target])['GameHealth.current'] > 0 for target in names), \
            'Enemy targets must survive the full sample'
        assert control_health == before_health, 'No-fire control damaged enemy health'
        assert mid['materials']['cells'] < before['materials']['cells'], 'Enemy barrage made no measured-window cuts'
        assert after['materials']['cells'] < mid['materials']['cells'], 'Enemy barrage stopped before the sample ended'
        assert no_fire_after['materials']['cells'] == before['materials']['cells'], 'No-fire control changed enemy cells'
        return
    if name == 'enemy_collisions':
        # Detail rows are bounded; these material/population counts cover the
        # entire world. The profiler independently verifies all physics bodies.
        for state in (before, mid, after):
            assert state['live_entities'] == before['live_entities'], 'Crowd population changed'
            assert state['materials']['solids'] == crowd_count, 'Crowd material hulls missing'
            assert state['materials']['cells'] == crowd_count * 400, 'Crowd material occupancy changed'
        assert before['live_entities'] >= crowd_count, 'Crowd spawn did not finish during warmup'
        sampled = 0
        for entity_record in after['entities']:
            fields = _entity_fields(entity_record)
            if fields.get('GameCollider.category') == 2:
                sampled += 1
                assert fields.get('GameDestructible.cell_size', 0) > 0, 'Crowd entities lost destructibility'
        assert sampled >= min(128, crowd_count), 'Crowd detail sample missing'
        return
    if name == 'fog_liquid':
        names = [check['fog'], check['liquid']]
        volumes = _named_entities(after, names)
        assert set(volumes) == set(names), 'Fog and liquid source volumes must remain present'
        assert _entity_fields(volumes[check['liquid']])['GameFluidVolume.kind'] == 1, 'Liquid fixture changed kind'
        assert after['materials']['active_particles'] > 0, 'Fog/liquid fixture emitted no active particles'
        player = _entity_fields(_named_entities(after, ['Player'])['Player'])
        assert -400 <= player['GameTransform.x'] <= -240, 'Player must return inside both fluid volumes'
        assert after['particle_count'] > 0, 'Moving through fog and liquid must emit contact particles'
