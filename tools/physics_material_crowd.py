"""Author the bounded, incremental crowd used by physics material profiles."""

import math


SPAWN_BATCH = 200
GRID_PITCH = 18
GRID_CENTER_X = 105
GRID_CENTER_Y = 0
SPAWNER_BEHAVIOR = 'BenchmarkCrowdSpawn'
SPAWNER_PROTOTYPE = 'BenchmarkCrowdSpawner'


def _connect(source, source_pin, target, target_pin, kind='data'):
    return dict(op='connect', **{
        'from': source, 'from_pin': source_pin,
        'to': target, 'to_pin': target_pin, 'kind': kind,
    })


def crowd_layout(count):
    if isinstance(count, bool) or not isinstance(count, int) or not 1 <= count <= 20_000:
        raise ValueError('crowd_count must be between 1 and 20000')
    columns = math.ceil(math.sqrt(count))
    rows = (count + columns - 1) // columns
    return dict(
        count=count,
        columns=columns,
        rows=rows,
        pitch=GRID_PITCH,
        center_x=GRID_CENTER_X,
        center_y=GRID_CENTER_Y,
        spawn_batch=SPAWN_BATCH,
    )


def prepare_crowd(runner, count):
    """Create a private, deterministic crowd spawner through Chibi MCP."""
    layout = crowd_layout(count)
    # Every member must keep driving into the crowd, including those beyond
    # the shipping Pursuit graph's 600-unit player detection radius.
    pursuit = runner.author('readAsset', dict(kind='behavior', name='Pursuit'))
    nearest = [node['id'] for graph in pursuit['document']['graphs']
        for node in graph.get('nodes', []) if node.get('type') == 'Find Nearest Entity']
    assert len(nearest) == 1
    radius = math.hypot(layout['columns'] * GRID_PITCH + 200, layout['rows'] * GRID_PITCH)
    runner.author('buildGraph', dict(kind='behavior', name='Pursuit', expected_hash=pursuit['hash'],
        operations=[dict(op='set_input', node=nearest[0], pin='Radius', value=radius)]))
    runner.author('validateAsset', dict(kind='behavior', name='Pursuit'))
    swarmer = runner.author('readAsset', dict(kind='prototype', name='Swarmer'))
    behavior_names = swarmer['document'].get('behaviors', [])
    operations = [
        dict(op='remove_behavior', behavior=name)
        for name in behavior_names if name != 'Pursuit'
    ]
    if 'Pursuit' not in behavior_names:
        operations.append(dict(op='add_behavior', behavior='Pursuit'))
    if operations:
        runner.author('buildPrototype', dict(
            kind='prototype', name='Swarmer', expected_hash=swarmer['hash'],
            operations=operations,
        ))
    swarmer_validation = runner.author('validateAsset', dict(kind='prototype', name='Swarmer'))
    if not swarmer_validation['valid']:
        raise RuntimeError(f"Swarmer validation failed: {swarmer_validation.get('diagnostics')}")

    operations = [
        dict(op='add_variable', name='Spawned', type='int', default=0, category='Crowd', ref='spawned'),
        dict(op='add_variable', name='Count', type='int', default=count, category='Crowd', ref='count'),
        dict(op='add_variable', name='Columns', type='int', default=layout['columns'], category='Grid', ref='columns'),
        dict(op='add_variable', name='Rows', type='int', default=layout['rows'], category='Grid', ref='rows'),
        dict(op='add_variable', name='Batch', type='int', default=0, category='State', ref='batch'),
        dict(op='add_node', type='Event', event_name='Tick', ref='tick'),
        dict(op='add_node', type='Behavior Expression', source='batch = 0', ref='reset_batch'),
        dict(op='bind_input', node='reset_batch', pin='batch', kind='variable', variable='batch'),
        dict(op='add_node', type='WhileLoop', ref='loop'),
        dict(op='add_node', type='Behavior Expression',
            source=f'spawned < count && batch < {SPAWN_BATCH}', ref='keep_spawning'),
        dict(op='bind_input', node='keep_spawning', pin='spawned', kind='variable', variable='spawned'),
        dict(op='bind_input', node='keep_spawning', pin='count', kind='variable', variable='count'),
        dict(op='bind_input', node='keep_spawning', pin='batch', kind='variable', variable='batch'),
        dict(op='add_node', type='Spawn Prototype', ref='spawn', inputs={'Prototype': 'Swarmer'}),
        dict(op='add_node', type='Behavior Expression',
            source='105 + (spawned % columns - (columns - 1) / 2) * 18', ref='grid_x'),
        dict(op='bind_input', node='grid_x', pin='spawned', kind='variable', variable='spawned'),
        dict(op='bind_input', node='grid_x', pin='columns', kind='variable', variable='columns'),
        dict(op='add_node', type='Behavior Expression',
            source='(floor(spawned / columns) - (rows - 1) / 2) * 18', ref='grid_y'),
        dict(op='bind_input', node='grid_y', pin='spawned', kind='variable', variable='spawned'),
        dict(op='bind_input', node='grid_y', pin='columns', kind='variable', variable='columns'),
        dict(op='bind_input', node='grid_y', pin='rows', kind='variable', variable='rows'),
        dict(op='add_node', type='Make_GameTransform', ref='transform'),
        _connect('grid_x', 'Result', 'transform', 'x'),
        _connect('grid_y', 'Result', 'transform', 'y'),
        dict(op='add_node', type='SetEntity_GameTransform', ref='initialize'),
        _connect('tick', 'Then', 'reset_batch', 'Exec', 'exec'),
        _connect('reset_batch', 'Then', 'loop', 'Exec', 'exec'),
        _connect('keep_spawning', 'Result', 'loop', 'Condition'),
        _connect('loop', 'Body', 'spawn', 'Exec', 'exec'),
        _connect('spawn', 'Then', 'initialize', 'Exec', 'exec'),
        _connect('spawn', 'Entity', 'initialize', 'Entity'),
        _connect('transform', 'GameTransform', 'initialize', 'Value'),
        dict(op='add_node', type='Behavior Expression', source='spawned += 1', ref='advance_spawned'),
        dict(op='bind_input', node='advance_spawned', pin='spawned', kind='variable', variable='spawned'),
        dict(op='add_node', type='Behavior Expression', source='batch += 1', ref='advance_batch'),
        dict(op='bind_input', node='advance_batch', pin='batch', kind='variable', variable='batch'),
        _connect('initialize', 'Then', 'advance_spawned', 'Exec', 'exec'),
        _connect('advance_spawned', 'Then', 'advance_batch', 'Exec', 'exec'),
    ]
    runner.author('buildGraph', dict(
        kind='behavior', name=SPAWNER_BEHAVIOR, create=True,
        owner_type='ecs.entity', operations=operations,
    ))
    graph = runner.author('inspectGraph', dict(
        kind='behavior', name=SPAWNER_BEHAVIOR, graph_name='',
    ))
    dry_run = runner.author('optimizeGraph', dict(
        kind='behavior', name=SPAWNER_BEHAVIOR, dry_run=True, math_expressions=True,
    ))
    if dry_run.get('changes'):
        graph_hash = graph.get('content_hash', graph.get('hash'))
        if graph_hash:
            runner.author('optimizeGraph', dict(
                kind='behavior', name=SPAWNER_BEHAVIOR, dry_run=False,
                math_expressions=True, expected_hash=graph_hash,
            ))
    graph_validation = runner.author('validateAsset', dict(kind='behavior', name=SPAWNER_BEHAVIOR))
    if not graph_validation['valid']:
        raise RuntimeError(f"crowd behavior validation failed: {graph_validation.get('diagnostics')}")

    runner.author('buildPrototype', dict(
        kind='prototype', name=SPAWNER_PROTOTYPE, create=True,
        operations=[
            dict(op='add_component', component='GameTransform'),
            dict(op='add_behavior', behavior=SPAWNER_BEHAVIOR),
        ],
    ))
    prototype_validation = runner.author('validateAsset', dict(kind='prototype', name=SPAWNER_PROTOTYPE))
    if not prototype_validation['valid']:
        raise RuntimeError(f"crowd spawner prototype validation failed: {prototype_validation.get('diagnostics')}")
    return layout
