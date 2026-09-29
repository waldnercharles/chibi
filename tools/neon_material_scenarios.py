"""Authored stress fixtures applied to isolated copies of NeonSwarm through MCP."""


def sample(frame, x=0, y=0, fire=False):
    return dict(frame=frame, input=dict(movement=dict(x=x, y=y),
        aim=dict(x=x or 1, y=y, firing=fire), controls=[1] if fire else []))


def entity(name, prototype, x, y, **components):
    return dict(op='add_entity', name=name, prototype=prototype, behaviors=[],
        components=dict(GameTransform=dict(x=x, y=y), **components))


def scenario(name):
    operations = []
    inputs = [sample(0, 1, 0, True)]
    frames = 240
    if name == 'dense-fog':
        frames, inputs = 60, [sample(0)]
        operations.append(entity('Benchmark atmosphere', 'Cloud', -200, 0,
            GameFluidVolume=dict(width=1000, height=600)))
    if name == 'ignition':
        frames, inputs = 90, [sample(0, fire=True)]
        for i, x in enumerate([-180, 0, 180]):
            operations.append(entity(f'Benchmark fuel {i}', 'FuelCloud', x, 0,
                GameFluidVolume=dict(width=180, height=180)))
            operations.append(entity(f'Benchmark igniter {i}', 'Projectile', x, 0,
                GameVelocity=dict(x=0, y=0), GameCollider=dict(radius=8),
                GameIgniter=dict(heat=2.5)))
    if name == 'destruction':
        frames, inputs = 90, [sample(0, fire=True)]
        for i in range(24):
            operations.append(entity(f'Benchmark pillar {i}', 'HardRock', -250 + i * 18, 0,
                GameAppearance=dict(width=8, height=56),
                GameCollider=dict(width=8, height=56), GameOrientation=dict(radians=0),
                GameDestructible=dict(cell_size=2, min_fragment_cells=2),
                GameSubstance=dict(hardness=.5, retain_chips=True)))
    if name == 'camera-turns':
        frames = 30
        directions = [(1, 0), (0, -1), (-1, 0), (0, 1)]
        inputs = [sample(i * 30, *directions[i % 4], fire=True) for i in range(7)]
    return dict(frames=frames, input_timeline=inputs, operations=operations)


SCENARIOS = ['travel', 'dense-fog', 'ignition', 'destruction', 'camera-turns']


def check_load(name, state):
    materials = state['materials']
    if name == 'dense-fog':
        assert materials['active_particles'] >= 10000, 'Dense fog fixture lost its particle load'
    if name == 'ignition':
        assert materials['burning_particles'] >= 20, 'Ignition fixture must retain burning fuel'
    if name == 'destruction':
        assert materials['fragments'] >= 8, 'Destruction fixture must contain detached debris'
