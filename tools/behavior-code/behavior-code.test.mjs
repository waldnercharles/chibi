import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import test from 'node:test';
import { chmodSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  buildOperations,
  expectedConnections,
  graphToSource,
  parseBehavior,
  reconcileSourceIdentities,
  resolveBehavior,
  sourceGraphShape,
  sourceGraphReferences,
  setMutationRef,
  updateGraphOperations,
  verifyTopology,
} from './behavior-code.mjs';

const registry = {
  components: [
    { key: 'GAME.GameProjectile', name: 'GameProjectile', fields: [
      { name: 'age', type: 'float', writable: true },
      { name: 'lifetime', type: 'float', writable: true },
      { name: 'speed', type: 'float', writable: true },
    ] },
    { key: 'GAME.GamePickup', name: 'GamePickup', fields: [
      { name: 'lifetime', type: 'float', writable: true },
    ] },
  ],
  events: [
    { name: 'Tick', source_name: 'Tick', payload: [{ pin: 'DeltaSeconds', alias: 'dt', type: 'float' }] },
    { name: 'Input', source_name: 'Input', payload: [{ pin: 'Direction', alias: 'direction', type: 'CF_V2' }] },
    { name: 'Contact Begin', source_name: 'ContactBegin', payload: [
      { pin: 'Other', alias: 'other', type: 'ecs.entity' },
      { pin: 'Point', alias: 'point', type: 'CF_V2' },
    ] },
  ],
};

const sample = `behavior ProjectileLifecycle {
  on Tick(GameProjectile projectile) {
    projectile.age += event.dt;
    // Keep expiry behavior readable in both code and graph views.
    if (projectile.age >= projectile.lifetime) {
      destroy(self);
    }
  }
}`;

function compileModel(source, previousMap = null) {
  const behavior = resolveBehavior(parseBehavior(source), registry);
  const state = reconcileSourceIdentities(behavior, previousMap);
  let node = 1;
  const descriptors = sourceGraphShape(behavior);
  const refs = Object.fromEntries(descriptors.map(({ ref }) => [ref, node++]));
  const types = Object.fromEntries(descriptors.map(({ ref, type }) => [ref, type]));
  return { behavior, map: { refs, types, node_references: sourceGraphReferences(behavior),
    edges: expectedConnections(behavior, refs), state_signature: JSON.stringify(behavior.stateVariables), ...state } };
}

function mappedGraph(behavior, map) {
  const expressions = new Map();
  for (const handler of behavior.handlers) {
    for (const statement of handler.body) {
      if (statement.kind === 'expression') expressions.set(statement.id, statement.resolvedSource);
      if (statement.kind === 'if' && !statement.conditionSet) expressions.set(statement.id, statement.resolvedCondition);
    }
  }
  return {
    nodes: sourceGraphShape(behavior).map((descriptor) => ({
      id: map.refs[descriptor.ref],
      type: descriptor.type,
      ...(descriptor.event ? { reference_name: descriptor.event } : {}),
      ...(descriptor.variable !== undefined ? { reference_name: descriptor.variable } : {}),
      ...(expressions.has(descriptor.ref) ? { reference_name: expressions.get(descriptor.ref) } : {}),
    })),
    edges: expectedConnections(behavior, map.refs).map((edge) => ({
      from: `${edge.from}.${edge.from_pin}`,
      to: `${edge.to}.${edge.to_pin}`,
      kind: edge.kind,
    })),
  };
}

function checkedInContactGraph(map, kdl) {
  const nodes = Object.entries(map.refs).map(([ref, id]) => {
    const multiline = new RegExp(String.raw`\bid\s+${id}\s+type\s+"([^"]+)"(?:\s+reference_name\s+"([^"]+)")?`).exec(kdl);
    const compact = new RegExp(String.raw`\bid=${id}\s+type="([^"]+)"`).exec(kdl);
    const match = multiline ?? compact;
    assert(match, `checked-in ContactRegistry graph is missing node ${id}`);
    assert.equal(match[1], map.types[ref]);
    return { id, type: match[1], ...(match[2] ? { reference_name: match[2] } : {}) };
  });
  const edges = [...kdl.matchAll(/-\s+from="([^"]+)"\s+to="([^"]+)"/g)].map(([, from, to]) => {
    const saved = map.edges.find((edge) => `${edge.from}.${edge.from_pin}` === from && `${edge.to}.${edge.to_pin}` === to);
    assert(saved, `checked-in ContactRegistry map is missing graph edge ${from} -> ${to}`);
    return { from, to, kind: saved.kind };
  });
  assert.equal(edges.length, map.edges.length);
  return { nodes, edges };
}

function statementBySource(behavior, source) {
  return behavior.handlers[0].body.find((statement) => statement.source === source);
}

function assertPlannedConnections(behavior) {
  const refs = Object.fromEntries(sourceGraphShape(behavior).map(({ ref }, index) => [ref, index + 1]));
  const actual = buildOperations(behavior, refs).filter(({ op }) => op === 'connect').map(({ from, from_pin, to, to_pin, kind }) =>
    `${from}.${from_pin}->${to}.${to_pin}:${kind}`).sort();
  const expected = expectedConnections(behavior, refs).map(({ from, from_pin, to, to_pin, kind }) =>
    `${from}.${from_pin}->${to}.${to_pin}:${kind}`).sort();
  assert.deepEqual(actual, expected);
}

test('parses named reflected aliases and resolves event payload fields', () => {
  const { behavior } = compileModel(sample);
  const handler = behavior.handlers[0];
  assert.equal(behavior.name, 'ProjectileLifecycle');
  assert.deepEqual(handler.params.map(({ name, kind, componentKey }) => [name, kind, componentKey]), [
    ['projectile', 'component', 'GAME.GameProjectile'],
  ]);
  assert.match(handler.body[0].resolvedSource, /^_bc_component_0_age \+= _bc_event_DeltaSeconds$/);
  assert.deepEqual(handler.componentFields._bc_component_0_age, {
    name: '_bc_component_0_age', alias: 'projectile', componentKey: 'GAME.GameProjectile', field: 'age', type: 'float',
  });
  assert.deepEqual(handler.eventFields._bc_event_DeltaSeconds, { alias: 'dt', pin: 'DeltaSeconds', type: 'float' });
});

test('lowers aliases to typed component bindings and event data wires', () => {
  const { behavior } = compileModel(sample);
  const operations = buildOperations(behavior);
  assertPlannedConnections(behavior);
  assert.deepEqual(operations.filter((op) => op.op === 'add_node').map((op) => op.type), [
    'Event', 'Behavior Expression', 'Behavior Expression', 'Destroy Entity',
  ]);
  assert.deepEqual(operations.filter((op) => op.op === 'bind_input').map(({ pin, kind, component, field }) => [pin, kind, component, field]), [
    ['_bc_component_0_age', 'component', 'GAME.GameProjectile', 'age'],
    ['_bc_component_0_age', 'component', 'GAME.GameProjectile', 'age'],
    ['_bc_component_0_lifetime', 'component', 'GAME.GameProjectile', 'lifetime'],
  ]);
  assert.deepEqual(operations.filter((op) => op.op === 'set_pin' && op.display_name)
    .map(({ pin, display_name }) => [pin, display_name]), [
      ['_bc_component_0_age', 'projectile.age'],
      ['_bc_event_DeltaSeconds', 'event.dt'],
      ['_bc_component_0_age', 'projectile.age'],
      ['_bc_component_0_lifetime', 'projectile.lifetime'],
    ]);
  assert(operations.some((op) => op.op === 'connect' && op.kind === 'data' && op.from_pin === 'DeltaSeconds' && op.to_pin === '_bc_event_DeltaSeconds'));
});

test('insertion and deletion preserve IDs for unchanged statements', () => {
  const original = compileModel(sample);
  const originalIds = new Map(original.behavior.handlers[0].body.map((statement) => [statement.kind === 'if' ? 'if' : statement.source, statement.id]));
  const inserted = sample.replace('    // Keep expiry', '    projectile.speed -= event.dt;\n    // Keep expiry');
  const afterInsert = compileModel(inserted, original.map);
  assert.equal(statementBySource(afterInsert.behavior, 'projectile.age += event.dt').id, originalIds.get('projectile.age += event.dt'));
  assert.equal(afterInsert.behavior.handlers[0].body.find((statement) => statement.kind === 'if').id, originalIds.get('if'));
  assert.notEqual(statementBySource(afterInsert.behavior, 'projectile.speed -= event.dt').id, originalIds.get('projectile.age += event.dt'));

  const afterDelete = compileModel(sample, compileModel(inserted, original.map).map);
  assert.equal(statementBySource(afterDelete.behavior, 'projectile.age += event.dt').id, originalIds.get('projectile.age += event.dt'));
});

test('reorder follows unique statement structure; an edited statement retains its ID', () => {
  const twoAssignments = sample.replace('    // Keep expiry', '    projectile.speed -= event.dt;\n    // Keep expiry');
  const before = compileModel(twoAssignments);
  const ageId = statementBySource(before.behavior, 'projectile.age += event.dt').id;
  const speedId = statementBySource(before.behavior, 'projectile.speed -= event.dt').id;
  const reordered = twoAssignments.replace('    projectile.age += event.dt;\n    projectile.speed -= event.dt;', '    projectile.speed -= event.dt;\n    projectile.age += event.dt;');
  const afterReorder = compileModel(reordered, before.map);
  assert.equal(statementBySource(afterReorder.behavior, 'projectile.age += event.dt').id, ageId);
  assert.equal(statementBySource(afterReorder.behavior, 'projectile.speed -= event.dt').id, speedId);

  const edited = compileModel(sample.replace('projectile.age += event.dt;', 'projectile.age += event.dt * 2;'), compileModel(sample).map);
  assert.equal(statementBySource(edited.behavior, 'projectile.age += event.dt * 2').id, ageId);
});

test('duplicate unmarked edits fail without mutating the old sidecar', () => {
  const duplicates = sample.replace('    projectile.age += event.dt;', '    projectile.age += event.dt;\n    projectile.age += event.dt;');
  const before = compileModel(duplicates);
  const mapBefore = JSON.stringify(before.map);
  const insertedDuplicate = duplicates.replace('    // Keep expiry', '    projectile.age += event.dt;\n    // Keep expiry');
  assert.throws(() => compileModel(insertedDuplicate, before.map), /repeated unmarked statements make identity ambiguous/);
  assert.equal(JSON.stringify(before.map), mapBefore);
});

test('optional markers keep repeated statement identities through reorder', () => {
  const marked = sample.replace('    projectile.age += event.dt;', '    // @node first\n    projectile.age += event.dt;\n    // @node second\n    projectile.age += event.dt;');
  const before = compileModel(marked);
  const firstId = before.behavior.handlers[0].body[0].id;
  const secondId = before.behavior.handlers[0].body[1].id;
  const reordered = marked.replace('    // @node first\n    projectile.age += event.dt;\n    // @node second\n    projectile.age += event.dt;', '    // @node second\n    projectile.age += event.dt;\n    // @node first\n    projectile.age += event.dt;');
  const after = compileModel(reordered, before.map);
  assert.equal(after.behavior.handlers[0].body[0].id, secondId);
  assert.equal(after.behavior.handlers[0].body[1].id, firstId);
});

test('legacy source maps still require their original markers for migration', () => {
  const marked = sample.replace('    projectile.age += event.dt;', '    // @node age-step\n    projectile.age += event.dt;')
    .replace('    if (projectile.age >= projectile.lifetime)', '    // @node expiry-check\n    if (projectile.age >= projectile.lifetime)')
    .replace('      destroy(self);', '      // @node despawn\n      destroy(self);');
  const first = compileModel(marked);
  const legacyMap = { ...first.map, source_tree: undefined, next_node_id: undefined };
  assert.throws(() => compileModel(sample, legacyMap), /compile once with its existing \/\/ @node markers/);
});

test('graph expression edits export qualified source and retain event aliases', () => {
  const { behavior, map } = compileModel(sample);
  const refs = map.refs;
  const nodeIds = { event: refs['event-Tick'], step: refs['source-1'], branch: refs['source-2'], destroy: refs['source-3'], self: refs.self };
  const graph = {
    nodes: [
      { id: nodeIds.event, type: 'Event', reference_name: 'Tick', out: ['Then: exec', 'DeltaSeconds: float'] },
      { id: nodeIds.step, type: 'Behavior Expression', reference_name: '_bc_component_0_age += _bc_event_DeltaSeconds', inputs: {
        _bc_component_0_age: { display_name: 'projectile.age', binding: { kind: 'component_field', component: 'GAME.GameProjectile', field: 'age' } },
        _bc_event_DeltaSeconds: { display_name: 'event.dt' },
      }, in: ['Exec: exec'], out: ['Then: exec'] },
      { id: nodeIds.branch, type: 'Behavior Expression', reference_name: '_bc_component_0_age > _bc_component_0_lifetime', inputs: {
        _bc_component_0_age: { binding: { kind: 'component_field', component: 'GAME.GameProjectile', field: 'lifetime' } },
        _bc_component_0_lifetime: { binding: { kind: 'component_field', component: 'GAME.GameProjectile', field: 'lifetime' } },
      }, in: ['Exec: exec'], out: ['True: exec', 'False: exec'] },
      { id: nodeIds.destroy, type: 'Destroy Entity', in: ['Exec: exec', 'Entity: ecs.entity'] },
      { id: nodeIds.self, type: 'Self', out: ['Self: ecs.entity'] },
    ],
    edges: [
      { from: `${nodeIds.event}.Then`, to: `${nodeIds.step}.Exec`, kind: 'exec' },
      { from: `${nodeIds.step}.Then`, to: `${nodeIds.branch}.Exec`, kind: 'exec' },
      { from: `${nodeIds.branch}.True`, to: `${nodeIds.destroy}.Exec`, kind: 'exec' },
      { from: `${nodeIds.self}.Self`, to: `${nodeIds.destroy}.Entity`, kind: 'data' },
      { from: `${nodeIds.event}.DeltaSeconds`, to: `${nodeIds.step}._bc_event_DeltaSeconds`, kind: 'data' },
    ],
  };
  verifyTopology(graph, behavior, refs);
  const result = graphToSource(sample, behavior, graph, map, registry);
  assert.match(result, /if \(projectile\.lifetime > projectile\.lifetime\)/);
  assert.match(result, /event\.dt/);
  assert.match(result, /Keep expiry behavior readable/);
  assert.doesNotMatch(result, /_bc_(?:component|event)_/);
  assert.doesNotMatch(result, /@node/);
  const roundtrip = compileModel(result, map);
  assert.equal(roundtrip.behavior.handlers[0].body[1].id, behavior.handlers[0].body[1].id);
});

test('name resolution reports ambiguity, missing members, read-only writes and stable spans', () => {
  const source = `behavior Bad {\n  on Tick(GameProjectile projectile, GamePickup pickup) {\n    lifetime += event.missing;\n  }\n}`;
  assert.throws(() => resolveBehavior(parseBehavior(source), registry), (error) => {
    assert.equal(error.code, 'E_NAME_AMBIGUOUS_COMPONENT_FIELD');
    assert.equal(source.slice(error.span.start, error.span.end), 'lifetime');
    assert.equal(error.span.line, 3);
    return true;
  });
  const missing = `behavior Bad { on Tick(GameProjectile projectile) { projectile.missing = 1; } }`;
  assert.throws(() => resolveBehavior(parseBehavior(missing), registry), { code: 'E_COMPONENT_FIELD_NOT_FOUND' });
  const readOnlyRegistry = { ...registry, components: [{ ...registry.components[0], fields: [{ name: 'entity', type: 'ecs.entity', writable: false }] }] };
  const readOnly = `behavior Bad { on Tick(GameProjectile projectile) { projectile.entity = 1; } }`;
  assert.throws(() => resolveBehavior(parseBehavior(readOnly), readOnlyRegistry), { code: 'E_ASSIGNMENT_READ_ONLY' });

  const bareField = compileModel(`behavior BareField {
  on Tick(GameProjectile projectile) {
    age += event.dt;
  }
}`);
  assert.match(bareField.behavior.handlers[0].body[0].resolvedSource, /^_bc_component_0_age \+= _bc_event_DeltaSeconds$/);
  assert(buildOperations(bareField.behavior).some((operation) => operation.op === 'bind_input'
    && operation.pin === '_bc_component_0_age' && operation.field === 'age'));
});

test('diagnostics span the original RHS tokens with compact and multiline operators', () => {
  for (const [source, line] of [
    [`behavior Bad { on Tick(GameProjectile projectile) { projectile.age+=event.missing; } }`, 1],
    [`behavior Bad {
  on Tick(GameProjectile projectile) {
    projectile.age +=
      event.missing;
  }
}`, 4],
  ]) {
    assert.throws(() => resolveBehavior(parseBehavior(source), registry), (error) => {
      assert.equal(source.slice(error.span.start, error.span.end), 'event.missing');
      assert.equal(error.span.line, line);
      return true;
    });
  }
});

test('integer array and set defaults enforce signed 32-bit boundaries', () => {
  for (const declaration of ['int[] values', 'set<int> values']) {
    const valid = `behavior Bounds { state ${declaration} = [-2147483648, 2147483647]; on Tick() {} }`;
    assert.deepEqual(parseBehavior(valid).stateVariables[0].default, [-2147483648, 2147483647]);
    assert.deepEqual(parseBehavior(`behavior Bounds { state ${declaration} = []; on Tick() {} }`).stateVariables[0].default, []);

    for (const literal of ['-2147483649', '2147483648', '1.5', '-1.5']) {
      const invalid = `behavior Bounds {\n  state ${declaration} = [${literal}];\n  on Tick() {}\n}`;
      assert.throws(() => parseBehavior(invalid), (error) => {
        assert.match(error.message, /signed 32-bit integers/);
        assert.equal(invalid.slice(error.span.start, error.span.end), literal);
        assert.equal(error.span.line, 2);
        return true;
      });
    }
  }
});

test('float array and set defaults fit finite binary32 values', () => {
  for (const declaration of ['float[] values', 'set<float> values']) {
    const valid = `behavior Bounds { state ${declaration} = [3.4028234663852886e38, -3.4028234663852886e38]; on Tick() {} }`;
    assert.deepEqual(
      parseBehavior(valid).stateVariables[0].default,
      [3.4028234663852886e38, -3.4028234663852886e38]
    );
    assert.deepEqual(parseBehavior(`behavior Bounds { state ${declaration} = []; on Tick() {} }`).stateVariables[0].default, []);

    for (const literal of ['1e309', '-1e309', '1e39', '-1e39']) {
      const invalid = `behavior Bounds {\n  state ${declaration} = [${literal}];\n  on Tick() {}\n}`;
      assert.throws(() => parseBehavior(invalid), (error) => {
        assert.match(error.message, /finite float32 literals/);
        assert.equal(invalid.slice(error.span.start, error.span.end), literal);
        assert.equal(error.span.line, 2);
        return true;
      });
    }
  }
});

test('set defaults reject duplicate native values while arrays keep duplicates', () => {
  const duplicates = [
    { type: 'int', literals: '[-1, -1]', duplicate: '-1', array: [-1, -1] },
    { type: 'float', literals: '[16777216, 16777217]', duplicate: '16777217', array: [16777216, 16777217] },
    { type: 'float', literals: '[0, 1e-50]', duplicate: '1e-50', array: [0, 1e-50] },
    { type: 'float', literals: '[0, -0]', duplicate: '-0', array: [0, -0] },
    { type: 'bool', literals: '[true, false, true]', duplicate: 'true', array: [true, false, true] },
    { type: 'string', literals: String.raw`["a", "\u{61}"]`, duplicate: String.raw`"\u{61}"`, array: ['a', 'a'] },
  ];
  for (const { type, literals, duplicate, array } of duplicates) {
    const setSource = `behavior Duplicate { state set<${type}> values = ${literals}; on Tick() {} }`;
    assert.throws(() => parseBehavior(setSource), (error) => {
      assert.equal(error.code, 'E_SET_DEFAULT_DUPLICATE');
      assert.equal(setSource.slice(error.span.start, error.span.end), duplicate);
      return true;
    });

    const arraySource = `behavior Duplicate { state ${type}[] values = ${literals}; on Tick() {} }`;
    assert.deepEqual(parseBehavior(arraySource).stateVariables[0].default, array);
  }

  for (const type of ['int', 'float', 'bool', 'string', 'entity']) {
    const empty = `behavior Empty { state set<${type}> values = []; on Tick() {} }`;
    assert.deepEqual(parseBehavior(empty).stateVariables[0].default, []);
  }
  const uniqueFloat = 'behavior Unique { state set<float> values = [16777216, 16777218]; on Tick() {} }';
  assert.deepEqual(parseBehavior(uniqueFloat).stateVariables[0].default, [16777216, 16777218]);
});

test('duplicate set defaults fail before publishing or rewriting assets and maps', () => {
  const root = mkdtempSync(join(tmpdir(), 'behavior-code-duplicate-set-'));
  try {
    const sourceDirectory = join(root, 'authoring');
    const assetDirectory = join(root, 'content', 'behaviors');
    mkdirSync(sourceDirectory, { recursive: true });
    mkdirSync(assetDirectory, { recursive: true });
    const sourceFile = join(sourceDirectory, 'DuplicateSet.code');
    const mapFile = sourceFile + '.map.json';
    const assetFile = join(assetDirectory, 'DuplicateSet.kdl');
    const source = String.raw`behavior DuplicateSet { state set<string> values = ["a", "\u{61}"]; on Tick() {} }`;
    const previousMap = '{"sentinel":"previous source map"}\n';
    const previousAsset = 'sentinel existing asset\n';
    writeFileSync(sourceFile, source);
    writeFileSync(mapFile, previousMap);
    writeFileSync(assetFile, previousAsset);

    const markerFile = join(root, 'ctl-called');
    const ctlFile = join(root, 'fake-ctl');
    writeFileSync(ctlFile, '#!/bin/sh\n: > "$CHIBI_TEST_MARKER"\nexit 1\n');
    chmodSync(ctlFile, 0o755);
    const cliFile = fileURLToPath(new URL('./cli.mjs', import.meta.url));
    const result = spawnSync(process.execPath, [
      cliFile, 'compile', sourceFile, '--project-root', root, '--ctl', ctlFile,
    ], {
      encoding: 'utf8',
      env: { ...process.env, CHIBI_TEST_MARKER: markerFile },
    });

    assert.equal(result.status, 1, result.stderr);
    const failure = JSON.parse(result.stderr);
    const diagnostic = failure.diagnostics[0];
    assert.equal(failure.ok, false);
    assert.equal(diagnostic.code, 'E_SET_DEFAULT_DUPLICATE');
    assert.equal(source.slice(diagnostic.span.start, diagnostic.span.end), String.raw`"\u{61}"`);
    assert.equal(existsSync(markerFile), false);
    assert.equal(readFileSync(mapFile, 'utf8'), previousMap);
    assert.equal(readFileSync(assetFile, 'utf8'), previousAsset);
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('string collection defaults decode escapes and reject unsupported escapes and NUL', () => {
  const source = String.raw`behavior Strings { state string[] values = ["a\nb", "a\"b", "c\\d", "\u{1f642}"]; on Tick() {} }`;
  assert.deepEqual(parseBehavior(source).stateVariables[0].default, ['a\nb', 'a"b', 'c\\d', '🙂']);
  const invalid = String.raw`behavior Strings { state string[] values = ["bad\q"]; on Tick() {} }`;
  assert.throws(() => parseBehavior(invalid), (error) => {
    assert.equal(error.code, 'E_STRING_ESCAPE');
    assert.equal(invalid.slice(error.span.start, error.span.end), '"bad\\q"');
    return true;
  });
  for (const literal of [String.raw`"a\0b"`, String.raw`"a\u0000b"`, String.raw`"a\u{0}b"`, `"a${'\0'}b"`]) {
    const invalidNul = `behavior Strings { state string[] values = [${literal}]; on Tick() {} }`;
    assert.throws(() => parseBehavior(invalidNul), (error) => {
      assert.equal(error.code, 'E_STRING_NUL');
      assert.equal(invalidNul.slice(error.span.start, error.span.end), literal);
      return true;
    });
  }
});

test('event names and payload members resolve explicitly; old field selectors are rejected', () => {
  const contact = `behavior Contact { on ContactBegin() { if (event.other == event.other) { destroy(self); } } }`;
  const behavior = resolveBehavior(parseBehavior(contact), registry);
  assert.equal(behavior.handlers[0].eventSourceName, 'Contact Begin');
  assert.deepEqual(behavior.handlers[0].eventOutputs, {
    _bc_event_Other: 'Other',
  });
  assert.throws(() => parseBehavior(sample.replace('GameProjectile projectile', 'age: field(GameProjectile.age)')), { code: 'E_PARSE' });
  assert.throws(() => resolveBehavior(parseBehavior(`behavior Bad { on Tick() { dt += 1; } }`), registry), { code: 'E_NAME_UNRESOLVED' });
});

test('typed state arrays lower to the native ForEach and preserve array element wiring', () => {
  const source = `behavior ProjectilePulse {
  state float[] increments = [0.01, 0.02, 0.03];
  on Tick(GameProjectile projectile) {
    for (increment in increments) {
      projectile.age += increment;
    }
  }
}`;
  const { behavior } = compileModel(source);
  const operations = buildOperations(behavior);
  assertPlannedConnections(behavior);
  assert.deepEqual(operations.slice(0, 2), [
    { op: 'add_variable', ref: 'state-variable-increments', name: 'increments', type: 'float', container: 'array', default: [0.01, 0.02, 0.03] },
    { op: 'add_variable_node', ref: 'state-get-increments', variable: 'increments', kind: 'get' },
  ]);
  assert(operations.some((op) => op.op === 'add_node' && op.type === 'ForEach'));
  assert(operations.some((op) => op.op === 'connect' && op.from === 'state-get-increments' && op.from_pin === 'Value' && op.to_pin === 'Array'));
  assert(operations.some((op) => op.op === 'connect' && op.from_pin === 'Array Element' && op.to_pin === 'increment' && op.kind === 'data'));
  assert.deepEqual(sourceGraphShape(behavior).map(({ type }) => type), ['Get Variable', 'Event', 'ForEach', 'Behavior Expression']);
});

test('array iteration bindings are scoped and read-only', () => {
  const source = `behavior Scope {
  state int[] values = [1, 2];
  on Tick() {
    for (value in values) {
      if (value > 0) { value = 3; }
    }
  }
}`;
  assert.throws(() => resolveBehavior(parseBehavior(source), registry), { code: 'E_LOOP_BINDING_READ_ONLY' });
  const unresolved = source.replace('for (value in values)', 'for (value in missing)');
  assert.throws(() => resolveBehavior(parseBehavior(unresolved), registry), { code: 'E_ARRAY_UNRESOLVED' });
});

test('typed sets lower membership, insertion and removal through core graph nodes', () => {
  const source = `behavior SeenItems {
  state int[] ids = [1, 2, 3];
  state set<int> seen = [];
  on Tick() {
    for (id in ids) {
      seen.add(id);
      if (seen.contains(id)) {
        seen.remove(id);
      }
    }
  }
}`;
  const { behavior } = compileModel(source);
  const operations = buildOperations(behavior);
  assertPlannedConnections(behavior);
  assert(operations.some((op) => op.op === 'add_variable' && op.name === 'seen' && op.container === 'set'));
  assert(operations.some((op) => op.op === 'add_node' && op.type === 'Set Add'));
  assert(operations.some((op) => op.op === 'add_variable_node' && op.kind === 'get' && op.variable === 'seen'));
  assert(operations.some((op) => op.op === 'add_node' && op.type === 'Set Remove'));
  assert(operations.some((op) => op.op === 'add_node' && op.type === 'Set Contains'));
  assert(operations.some((op) => op.op === 'add_node' && op.type === 'Branch'));
  assert(operations.some((op) => op.op === 'connect' && op.from_pin === 'Set' && op.to_pin === 'Value'));
  assert(operations.some((op) => op.op === 'connect' && op.from_pin === 'Return Value' && op.to_pin === 'Condition'));
  assert.deepEqual(sourceGraphShape(behavior).map(({ type }) => type), [
    'Get Variable', 'Get Variable', 'Event', 'ForEach', 'Set Add', 'Get Variable', 'Set Variable',
    'Set Contains', 'Branch', 'Set Remove', 'Set Variable',
  ]);
  const unsupported = `behavior Bad { state set<int> seen = []; on Tick() { seen.add(1); } }`;
  assert.throws(() => resolveBehavior(parseBehavior(unsupported), registry), { code: 'E_SET_ARGUMENT_UNSUPPORTED' });
});

test('entity sets accept registered event handles and preserve typed payload wires', () => {
  const source = `behavior ContactSet {
  state set<entity> contacts = [];
  on ContactBegin() {
    contacts.add(event.other);
    if (contacts.contains(event.other)) {
      contacts.remove(event.other);
    }
  }
}`;
  const { behavior } = compileModel(source);
  const operations = buildOperations(behavior);
  assertPlannedConnections(behavior);
  const event = operations.find((operation) => operation.op === 'add_node' && operation.type === 'Event');
  assert.equal(event.event_name, 'Contact Begin');
  assert(operations.some((operation) => operation.op === 'add_variable' && operation.name === 'contacts'
    && operation.type === 'handle' && operation.type_name === 'ecs.entity' && operation.container === 'set'));
  assert(operations.some((operation) => operation.op === 'connect' && operation.from === event.ref && operation.from_pin === 'Other' && operation.to_pin === 'Item'));
  assert.deepEqual(behavior.handlers[0].body[0].eventInputs, ['_bc_event_Other']);
  assert.deepEqual(behavior.handlers[0].body[1].eventInputs, ['_bc_event_Other']);
  assert.deepEqual(behavior.handlers[0].body[1].yes[0].eventInputs, ['_bc_event_Other']);
  const mismatched = source.replace('set<entity>', 'set<int>');
  assert.throws(() => resolveBehavior(parseBehavior(mismatched), registry), { code: 'E_SET_ELEMENT_TYPE' });
});

test('collection diagnostics point at the operand and malformed member paths stay structured', () => {
  const mutation = `behavior BadSet {
  state set<int> contacts = [];
  on ContactBegin() {
    contacts.add(event.other);
  }
}`;
  assert.throws(() => resolveBehavior(parseBehavior(mutation), registry), (error) => {
    assert.equal(error.code, 'E_SET_ELEMENT_TYPE');
    assert.equal(mutation.slice(error.span.start, error.span.end), 'event.other');
    assert.equal(error.span.line, 4);
    return true;
  });

  const condition = mutation.replace('contacts.add(event.other);', 'if (contacts.contains(event.other)) {}');
  assert.throws(() => resolveBehavior(parseBehavior(condition), registry), (error) => {
    assert.equal(error.code, 'E_SET_ELEMENT_TYPE');
    assert.equal(condition.slice(error.span.start, error.span.end), 'event.other');
    assert.equal(error.span.line, 4);
    return true;
  });

  const malformed = `behavior BadEvent {
  on Tick(GameProjectile projectile) {
    projectile.age = event.dt.;
  }
}`;
  assert.throws(() => resolveBehavior(parseBehavior(malformed), registry), (error) => {
    assert.equal(error.code, 'E_EVENT_NESTED_MEMBER_UNSUPPORTED');
    assert.equal(malformed.slice(error.span.start, error.span.end), 'event.dt.');
    return true;
  });
});

test('array and set source plans verify as complete graph topology and reject foreign wires', () => {
  const source = `behavior SeenItems {
  state int[] ids = [1, 2, 3];
  state set<int> seen = [];
  on Tick() {
    for (id in ids) {
      seen.add(id);
      if (seen.contains(id)) {
        seen.remove(id);
      }
    }
  }
}`;
  const { behavior } = compileModel(source);
  const descriptors = sourceGraphShape(behavior);
  const refs = Object.fromEntries(descriptors.map(({ ref }, index) => [ref, index + 1]));
  const graph = {
    nodes: descriptors.map(({ ref, type, event, variable }) => ({
      id: refs[ref],
      type,
      ...(type === 'Event' ? { reference_name: event } : {}),
      ...(variable !== undefined ? { reference_name: variable } : {}),
    })),
    edges: expectedConnections(behavior, refs).map((edge) => ({
      from: `${edge.from}.${edge.from_pin}`,
      to: `${edge.to}.${edge.to_pin}`,
      kind: edge.kind,
    })),
  };
  verifyTopology(graph, behavior, refs);
  const intact = structuredClone(graph);
  graph.edges.push({ from: `${refs['event-Tick']}.Then`, to: `${refs['source-1']}.Foreign`, kind: 'data' });
  assert.throws(() => verifyTopology(graph, behavior, refs), /source-owned graph wiring changed/);
  assert.deepEqual(intact.nodes, graph.nodes);
  assert.equal(intact.edges.length + 1, graph.edges.length);
});

test('graph expression edits roundtrip through scoped array and event aliases', () => {
  const source = `behavior ArrayRoundtrip {
  state float[] increments = [0.01, 0.02];
  on Tick(GameProjectile projectile) {
    for (increment in increments) {
      projectile.age += event.dt;
    }
  }
  }`;
  const { behavior, map } = compileModel(source);
  const refs = map.refs;
  const expressionRef = behavior.handlers[0].body[0].body[0].id;
  const graph = {
    nodes: sourceGraphShape(behavior).map(({ ref, type, event, variable }) => ({
      id: refs[ref],
      type,
      ...(type === 'Event' ? { reference_name: event } : {}),
      ...(variable !== undefined ? { reference_name: variable } : {}),
      ...(ref === expressionRef ? {
        reference_name: '_bc_component_0_age += _bc_event_DeltaSeconds * 2',
        inputs: { _bc_component_0_age: { binding: { kind: 'component_field', component: 'GAME.GameProjectile', field: 'age' } } },
      } : {}),
    })),
    edges: expectedConnections(behavior, refs).map((edge) => ({
      from: `${edge.from}.${edge.from_pin}`,
      to: `${edge.to}.${edge.to_pin}`,
      kind: edge.kind,
    })),
  };
  const result = graphToSource(source, behavior, graph, map, registry);
  assert.match(result, /for \(increment in increments\)/);
  assert.match(result, /projectile\.age \+= event\.dt \* 2/);
  const roundtrip = compileModel(result, map);
  assert.equal(roundtrip.behavior.handlers[0].body[0].body[0].id, expressionRef);
  assert.deepEqual(
    sourceGraphShape(roundtrip.behavior).map(({ ref, type }) => [ref, type]),
    sourceGraphShape(behavior).map(({ ref, type }) => [ref, type]),
  );
  assertPlannedConnections(roundtrip.behavior);
});

test('graph exports newly bound component inputs as source aliases and validates the result', () => {
  const source = `behavior NewBindings {
  on Tick(GameProjectile projectile) {
    projectile.age += event.dt;
  }
}`;
  const { behavior, map } = compileModel(source);
  const expression = behavior.handlers[0].body[0];
  const graph = mappedGraph(behavior, map);
  const node = graph.nodes.find((candidate) => candidate.id === map.refs[expression.id]);
  node.reference_name = '_bc_component_0_age += _bc_component_0_speed + _bc_event_DeltaSeconds';
  node.inputs = {
    _bc_component_0_age: { binding: { kind: 'component_field', component: 'GAME.GameProjectile', field: 'age' } },
    _bc_component_0_speed: { binding: { kind: 'component_field', component: 'GAME.GameProjectile', field: 'speed' } },
  };
  const exported = graphToSource(source, behavior, graph, map, registry);
  assert.match(exported, /projectile\.age \+= projectile\.speed \+ event\.dt/);
  assert.doesNotMatch(exported, /_bc_component_/);
  assert.doesNotThrow(() => resolveBehavior(parseBehavior(exported), registry));

  node.reference_name = '_bc_component_0_age += _bc_component_0_speed';
  delete node.inputs._bc_component_0_speed;
  assert.throws(() => graphToSource(source, behavior, graph, map, registry), /_bc_component_0_speed has no verified source binding or supported default/);
});

test('graph export preserves a colliding component field name used as a constant input', () => {
  const source = `behavior GraphConstant {
  on Tick(GameProjectile projectile) {
    projectile.age += event.dt;
  }
}`;
  const { behavior, map } = compileModel(source);
  const expression = behavior.handlers[0].body[0];
  const graph = mappedGraph(behavior, map);
  const node = graph.nodes.find((candidate) => candidate.id === map.refs[expression.id]);
  node.reference_name = '_bc_component_0_age += speed + _bc_event_DeltaSeconds';
  node.inputs = {
    _bc_component_0_age: { binding: { kind: 'component_field', component: 'GAME.GameProjectile', field: 'age' } },
    speed: { default: 2 },
  };

  const exported = graphToSource(source, behavior, graph, map, registry);
  assert.match(exported, /projectile\.age \+= \(2\) \+ event\.dt/);
  assert.doesNotMatch(exported, /projectile\.speed/);
  assertPlannedConnections(compileModel(exported, map).behavior);

  delete node.inputs.speed.default;
  assert.throws(() => graphToSource(source, behavior, graph, map, registry), /speed has no verified source binding or supported default/);
});

test('graph export parenthesizes negative defaults before exponentiation', () => {
  const source = `behavior GraphPower {
  on Tick(GameProjectile projectile) {
    projectile.age += event.dt;
  }
}`;
  const { behavior, map } = compileModel(source);
  const expression = behavior.handlers[0].body[0];
  const graph = mappedGraph(behavior, map);
  const node = graph.nodes.find((candidate) => candidate.id === map.refs[expression.id]);
  node.reference_name = '_bc_component_0_age += speed ** 2 + _bc_event_DeltaSeconds';
  node.inputs = {
    _bc_component_0_age: { binding: { kind: 'component_field', component: 'GAME.GameProjectile', field: 'age' } },
    speed: { default: -2 },
  };

  const exported = graphToSource(source, behavior, graph, map, registry);
  assert.match(exported, /projectile\.age \+= \(-2\) \*\* 2 \+ event\.dt/);
  assertPlannedConnections(compileModel(exported, map).behavior);
});

test('graph export keeps a wired loop binding when its pin also has a default', () => {
  const source = `behavior LoopBinding {
  state float[] increments = [2];
  on Tick(GameProjectile projectile) {
    for (increment in increments) {
      projectile.age += increment;
    }
  }
}`;
  const { behavior, map } = compileModel(source);
  const expression = behavior.handlers[0].body[0].body[0];
  const graph = mappedGraph(behavior, map);
  const node = graph.nodes.find((candidate) => candidate.id === map.refs[expression.id]);
  node.reference_name = expression.resolvedSource;
  node.inputs = {
    _bc_component_0_age: { binding: { kind: 'component_field', component: 'GAME.GameProjectile', field: 'age' } },
    increment: { default: 2 },
  };

  const exported = graphToSource(source, behavior, graph, map, registry);
  assert.match(exported, /for \(increment in increments\)/);
  assert.match(exported, /projectile\.age \+= increment/);
  assert.doesNotMatch(exported, /projectile\.age \+= 2/);
});

test('graph export uses the default when a loop-name input has no Array Element wire', () => {
  const source = `behavior UnwiredLoopName {
  state float[] increments = [10];
  on Tick(GameProjectile projectile) {
    for (increment in increments) {
      projectile.age += event.dt;
    }
  }
}`;
  const { behavior, map } = compileModel(source);
  const expression = behavior.handlers[0].body[0].body[0];
  const graph = mappedGraph(behavior, map);
  const node = graph.nodes.find((candidate) => candidate.id === map.refs[expression.id]);
  node.reference_name = '_bc_component_0_age += increment + _bc_event_DeltaSeconds';
  node.inputs = {
    _bc_component_0_age: { binding: { kind: 'component_field', component: 'GAME.GameProjectile', field: 'age' } },
    _bc_event_DeltaSeconds: {},
    increment: { default: 2 },
  };
  assert(!graph.edges.some((edge) => edge.to === `${node.id}.increment`));

  const exported = graphToSource(source, behavior, graph, map, registry);
  assert.match(exported, /projectile\.age \+= \(2\) \+ event\.dt/);
  assertPlannedConnections(compileModel(exported, map).behavior);

  delete node.inputs.increment.default;
  assert.throws(() => graphToSource(source, behavior, graph, map, registry), /increment has no verified source binding or supported default/);
});

test('source edits that retarget a set rebuild both semantic variable nodes', () => {
  const source = `behavior Retarget {
  state int[] ids = [1];
  state set<int> a = [];
  state set<int> b = [];
  on Tick() { for (id in ids) { if (a.contains(id)) { a.remove(id); } else { a.add(id); } } }
}`;
  const old = compileModel(source);
  const changed = compileModel(source.replace('a.remove(id)', 'b.remove(id)').replace('a.add(id)', 'b.add(id)'), old.map);
  const oldBranch = old.behavior.handlers[0].body[0].body[0];
  const branch = changed.behavior.handlers[0].body[0].body[0];
  const oldRemove = oldBranch.yes[0];
  const oldAdd = oldBranch.no[0];
  const remove = branch.yes[0];
  const add = branch.no[0];
  const sourceRef = add.id + '-set-source';
  const setterId = old.map.refs[add.id];
  const sourceId = old.map.refs[sourceRef];
  const plan = updateGraphOperations(changed.behavior, old.map, mappedGraph(old.behavior, old.map));
  assert(plan.added.has(remove.id));
  assert(plan.added.has(add.id));
  assert(plan.added.has(sourceRef));
  assert.notEqual(plan.selectors[remove.id], old.map.refs[oldRemove.id]);
  assert.notEqual(plan.selectors[add.id], setterId);
  assert.notEqual(plan.selectors[sourceRef], sourceId);
  assert(plan.operations.some((operation) => operation.op === 'remove_node' && operation.node === old.map.refs[oldRemove.id]));
  assert(plan.operations.some((operation) => operation.op === 'remove_node' && operation.node === setterId));
  assert(plan.operations.some((operation) => operation.op === 'remove_node' && operation.node === sourceId));
  assert(plan.operations.some((operation) => operation.op === 'add_variable_node' && operation.ref === remove.id && operation.variable === 'b'));
  assert(plan.operations.some((operation) => operation.op === 'add_variable_node' && operation.ref === add.id && operation.variable === 'b'));
  assert(plan.operations.some((operation) => operation.op === 'add_variable_node' && operation.ref === sourceRef && operation.variable === 'b'));
  assert(plan.operations.some((operation) => operation.op === 'connect'
    && operation.from === old.map.refs['state-get-b'] && operation.to === old.map.refs[remove.id + '-set-op']
    && operation.to_pin === 'Set'));

  const olderMap = { ...old.map };
  delete olderMap.node_references;
  assert.doesNotThrow(() => updateGraphOperations(
    changed.behavior, olderMap, mappedGraph(old.behavior, old.map)
  ));

  const changedGraph = mappedGraph(old.behavior, old.map);
  changedGraph.nodes.find((candidate) => candidate.id === setterId).reference_name = 'b';
  assert.throws(() => updateGraphOperations(changed.behavior, old.map, changedGraph), /no longer matches its source binding/);
});

test('legacy ContactRegistry map adds the new Set Add getter using the checked-in graph', () => {
  const contactSource = readFileSync(new URL('./examples/ContactRegistry.code', import.meta.url), 'utf8');
  const map = JSON.parse(readFileSync(new URL('./examples/ContactRegistry.code.map.json', import.meta.url), 'utf8'));
  const kdl = readFileSync(new URL('../../content/behaviors/ContactRegistry.kdl', import.meta.url), 'utf8');
  const { behavior } = compileModel(contactSource);
  const graph = checkedInContactGraph(map, kdl);
  const getterRef = 'source-2-set-source';

  assert.equal(map.refs[getterRef], undefined);
  const plan = updateGraphOperations(behavior, map, graph);
  assert(plan.added.has(getterRef));
  assert(plan.operations.some((operation) => operation.op === 'add_variable_node'
    && operation.ref === getterRef && operation.variable === 'contacted'));
});

test('the published array and set example resolves through the same source path', () => {
  const source = readFileSync(new URL('./examples/ProjectilePulse.code', import.meta.url), 'utf8');
  const { behavior } = compileModel(source);
  const operations = buildOperations(behavior);
  assertPlannedConnections(behavior);
  assert.equal(behavior.name, 'ProjectilePulse');
  assert(operations.some((operation) => operation.op === 'add_node' && operation.type === 'ForEach'));
  assert(operations.some((operation) => operation.op === 'add_node' && operation.type === 'Set Contains'));
  assert(operations.some((operation) => operation.op === 'add_node' && operation.type === 'Set Add'));
  const nestedMutation = behavior.handlers[0].body[0].body[0].no[0];
  assert.equal(nestedMutation.kind, 'set_mutation');
  assert(!operations.some((operation) => operation.op === 'connect'
    && operation.to === nestedMutation.id && operation.to_pin === 'mark'));

  const contactSource = readFileSync(new URL('./examples/ContactRegistry.code', import.meta.url), 'utf8');
  const { behavior: contact } = compileModel(contactSource);
  assert.equal(contact.name, 'ContactRegistry');
  assert(buildOperations(contact).some((operation) => operation.op === 'add_node' && operation.type === 'Set Add'));
});

test('unsupported calls fail with a structured parser diagnostic', () => {
  assert.throws(() => parseBehavior(sample.replace('destroy(self);', 'spawn(Other);')), (error) => {
    assert.equal(error.code, 'E_PARSE');
    assert.equal(error.span.line, 6);
    return true;
  });
});
