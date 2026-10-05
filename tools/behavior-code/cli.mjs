#!/usr/bin/env node
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { basename, dirname, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import {
  buildOperations,
  expectedConnections,
  eventRef,
  graphToSource,
  parseBehavior,
  resolveBehavior,
  reconcileSourceIdentities,
  sourceGraphShape,
  sourceGraphReferences,
  setConditionRef,
  setMutationRef,
  updateGraphOperations,
  verifyTopology,
} from './behavior-code.mjs';

const REPO_ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const SERVER = resolve(REPO_ROOT, 'tools/mcp/server.mjs');
const MARKER_VERSION = 1;

function usage() {
  console.error('Usage: behavior-code.mjs validate FILE [--project-root PATH] [--ctl PATH]');
  console.error('Usage: behavior-code.mjs compile FILE [--project-root PATH] [--ctl PATH]');
  console.error('       behavior-code.mjs decompile FILE [--project-root PATH] [--ctl PATH]');
}

function argsOf(argv) {
  const command = argv[0];
  const sourceFile = argv[1] ? resolve(argv[1]) : null;
  const options = { command, sourceFile, root: process.cwd(), ctl: process.env.CHIBI_CTL };
  for (let i = 2; i < argv.length; i++) {
    if (argv[i] === '--project-root' && argv[i + 1]) options.root = resolve(argv[++i]);
    else if (argv[i] === '--ctl' && argv[i + 1]) options.ctl = resolve(argv[++i]);
    else throw new Error('unknown or incomplete option ' + argv[i]);
  }
  if (!options.ctl) options.ctl = resolve(options.root, 'build/chibi_ctl');
  if (!['validate', 'compile', 'decompile'].includes(command) || !sourceFile) {
    usage();
    process.exitCode = 2;
    return null;
  }
  return options;
}

function mcp(options, name, arguments_) {
  const request = (id, method, params) => ({ jsonrpc: '2.0', id, method, params });
  const input = [
    request(1, 'initialize', {
      protocolVersion: '2025-06-18',
      capabilities: {},
      clientInfo: { name: 'behavior-code-prototype', version: '0.1.0' },
    }),
    request(2, 'tools/call', { name, arguments: arguments_ }),
  ].map((item) => JSON.stringify(item)).join('\n') + '\n';
  const run = spawnSync(process.execPath, [SERVER], {
    cwd: REPO_ROOT,
    env: { ...process.env, CHIBI_ROOT: options.root, CHIBI_CTL: options.ctl },
    input,
    encoding: 'utf8',
    maxBuffer: 32 * 1024 * 1024,
  });
  if (run.error) throw run.error;
  if (run.status !== 0) throw new Error(run.stderr.trim() || (name + ' exited with ' + run.status));
  const response = run.stdout.split('\n').filter(Boolean).map((line) => JSON.parse(line)).find((item) => item.id === 2);
  if (!response) throw new Error(run.stderr.trim() || (name + ' returned no response'));
  const result = response.result;
  const structured = result?.structuredContent;
  if (!structured || structured.ok === false || result.isError) {
    const detail = structured?.error?.message ?? result?.content?.[0]?.text ?? (name + ' failed');
    const error = new Error(detail);
    error.code = structured?.error?.code ?? 'E_GRAPH_BUILD';
    error.failedOperation = structured?.result?.failed_operation;
    error.nativeDiagnostics = structured?.result?.diagnostics ?? structured?.result?.validation?.diagnostics ?? [];
    throw error;
  }
  return structured.result;
}

function inspect(options, name) {
  const result = mcp(options, 'inspectGraph', { kind: 'behavior', name });
  if (result.validation && !result.validation.valid) {
    throw new Error('the existing behavior graph does not compile');
  }
  return result;
}

function verifyEngineCall(options) {
  const result = mcp(options, 'searchNodes', { domain: 'behavior', query: 'destroy entity', include_pins: true });
  const node = result.nodes?.find((candidate) => candidate.type === 'Destroy Entity');
  if (!node || !node.in?.includes('Exec: exec') || !node.in?.includes('Entity: ecs.entity')) {
    throw new Error('the engine registry does not expose the typed Destroy Entity call required by destroy(self)');
  }
  return node;
}

function verifyCreatedGraph(options, name) {
  const inspected = inspect(options, name);
  const validation = mcp(options, 'validateAsset', { kind: 'behavior', name });
  if (validation?.valid === false || validation?.ok === false) {
    throw new Error('the behavior asset failed validateAsset');
  }
  return inspected;
}

function componentBindings(behavior) {
  const bindings = new Map();
  for (const handler of behavior.handlers) {
    for (const param of handler.params) {
      bindings.set(handler.event + ':' + param.name, {
        kind: param.kind,
        component: param.componentKey,
      });
    }
  }
  return bindings;
}

function statementsIn(block) {
  const result = [];
  for (const statement of block) {
    result.push(statement);
    if (statement.kind === 'if') result.push(...statementsIn(statement.yes), ...statementsIn(statement.no));
    if (statement.kind === 'foreach') result.push(...statementsIn(statement.body));
  }
  return result;
}

function sourceSpanForRef(behavior, ref, map) {
  if (Number.isSafeInteger(ref) && map) {
    ref = Object.entries(map.refs ?? {}).find(([, id]) => Number(id) === ref)?.[0] ?? ref;
  }
  for (const handler of behavior.handlers) {
    if (ref === eventRef(handler.event)) return handler.eventSpan;
    for (const statement of statementsIn(handler.body)) {
      if (ref === statement.id || ref === setMutationRef(statement.id) || ref === setConditionRef(statement.id)) {
        return statement.span;
      }
    }
  }
  return null;
}

function attachSourceDiagnostics(error, behavior, operations, map = null) {
  if (!error.nativeDiagnostics?.length && !Number.isSafeInteger(error.failedOperation)) return error;
  const operation = Number.isSafeInteger(error.failedOperation) ? operations[error.failedOperation] : null;
  let primarySpan = null;
  for (const ref of [operation?.ref, operation?.node, operation?.to, operation?.from]) {
    primarySpan = sourceSpanForRef(behavior, ref, map);
    if (primarySpan) break;
  }
  if (!primarySpan) primarySpan = { start: 0, end: 0, line: 1, column: 1 };
  error.code = 'E_GRAPH_COMPILE';
  error.span = primarySpan;
  error.sourceDiagnostics = error.nativeDiagnostics?.length
    ? error.nativeDiagnostics.map((item) => ({
      code: item.rule_name ?? error.code,
      message: item.message ?? error.message,
      span: sourceSpanForRef(behavior, item.node, map) ?? primarySpan,
    }))
    : [{ code: 'E_GRAPH_BUILD', message: error.message, span: primarySpan }];
  return error;
}

function failureDiagnostics(error) {
  return error.sourceDiagnostics?.length ? error.sourceDiagnostics : [diagnostic(error)];
}

function engineRegistry(options) {
  const components = mcp(options, 'describeComponents', {}).components ?? [];
  const outline = mcp(options, 'searchNodes', { domain: 'behavior' });
  const names = outline.events ?? [];
  const events = [];
  for (const displayName of names) {
    const sourceName = displayName === 'Contact Begin' ? 'ContactBegin' : displayName;
    if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(sourceName)) continue;
    const found = mcp(options, 'searchNodes', {
      domain: 'behavior', query: displayName, include_pins: true, limit: 50,
    });
    const candidate = (found.nodes ?? []).find((node) => node.type === 'Event' && node.event_name === displayName);
    if (!candidate) continue;
    const payload = (candidate.out ?? []).map((pin) => {
      const match = /^([^:]+):\s*(.+)$/.exec(pin);
      return match && match[1] !== 'Then'
        ? { pin: match[1].trim(), type: match[2].trim(), alias: eventPayloadAlias(match[1].trim()) }
        : null;
    }).filter(Boolean);
    events.push({ name: displayName, source_name: sourceName, payload });
  }
  return { components, events };
}

function eventPayloadAlias(pin) {
  return ({ DeltaSeconds: 'dt', Direction: 'direction', Other: 'other', Point: 'point' })[pin]
    ?? pin[0].toLowerCase() + pin.slice(1);
}

function resolveSource(options, source) {
  return resolveBehavior(parseBehavior(source), engineRegistry(options));
}

function diagnostic(error) {
  return {
    code: error.code ?? 'E_BEHAVIOR_CODE',
    message: error.message,
    span: error.span ?? { start: 0, end: 0, line: 1, column: 1 },
  };
}

function validate(options) {
  try {
    const behavior = resolveSource(options, readFileSync(options.sourceFile, 'utf8'));
    process.stdout.write(JSON.stringify({ ok: true, asset: behavior.name, diagnostics: [] }) + '\n');
  } catch (error) {
    process.stdout.write(JSON.stringify({ ok: false, diagnostics: [diagnostic(error)] }) + '\n');
    process.exitCode = 1;
  }
}

function compile(options) {
  const source = readFileSync(options.sourceFile, 'utf8');
  const behavior = resolveBehavior(parseBehavior(source), engineRegistry(options));
  const assetPath = resolve(options.root, 'content/behaviors', behavior.name + '.kdl');
  const mapPath = options.sourceFile + '.map.json';
  const hasAsset = existsSync(assetPath);
  const hasMap = existsSync(mapPath);
  let previousMap = null;
  if (hasMap) {
    previousMap = JSON.parse(readFileSync(mapPath, 'utf8'));
    if (previousMap.version !== MARKER_VERSION || previousMap.asset !== behavior.name) {
      throw new Error('source map version or asset name does not match');
    }
  }
  const sourceState = reconcileSourceIdentities(behavior, previousMap);
  verifyEngineCall(options);

  const stateSignature = JSON.stringify((behavior.stateVariables ?? []).map(({ name, type, container, default: value }) => ({ name, type, container, default: value })));
  if (previousMap && ((behavior.stateVariables?.length ?? 0) > 0 || previousMap.state_signature !== undefined) && previousMap.state_signature !== stateSignature) {
    throw new Error('state variable declarations or defaults changed after graph creation; this bridge does not migrate persistent state shape, so the graph and source were left unchanged');
  }

  if (!hasAsset && hasMap) throw new Error('the source map exists but its compiled behavior asset is missing');
  if (hasAsset && !hasMap) throw new Error('behavior asset already exists without a source map; refusing to overwrite an independently authored graph');
  if (!hasAsset) {
    const operations = buildOperations(behavior);
    let published;
    try {
      published = mcp(options, 'buildGraph', {
        kind: 'behavior',
        name: behavior.name,
        create: true,
        owner_type: 'ecs.entity',
        description: 'Compiled from the bounded behavior-code prototype subset.',
        operations,
      });
    } catch (error) {
      throw attachSourceDiagnostics(error, behavior, operations);
    }
    if (!published.published) throw new Error('buildGraph did not publish the behavior asset');
    const inspected = verifyCreatedGraph(options, behavior.name);
    const created = new Map((published.nodes ?? []).filter((node) => node.ref).map((node) => [node.ref, node]));
    const refs = {};
    const types = {};
    for (const descriptor of sourceGraphShape(behavior)) {
      const node = created.get(descriptor.ref);
      if (!node) throw new Error('buildGraph did not return the created node ID for ' + descriptor.ref);
      refs[descriptor.ref] = node.id;
      types[descriptor.ref] = node.type;
    }
    verifyTopology(inspected.graph, behavior, refs);
    const map = {
      version: MARKER_VERSION,
      asset: behavior.name,
      graph_id: inspected.graph.id,
      signature: behavior.signature,
      refs,
      types,
      edges: expectedConnections(behavior, refs),
      node_references: sourceGraphReferences(behavior),
      bindings: Object.fromEntries(componentBindings(behavior)),
      state_signature: stateSignature,
      ...sourceState,
    };
    writeFileSync(mapPath, JSON.stringify(map, null, 2) + '\n');
    console.log('compiled ' + basename(options.sourceFile) + ' to ' + assetPath);
    console.log('created ' + Object.keys(refs).length + ' source-owned graph nodes; validated by the native graph compiler');
    return;
  }

  const map = previousMap;
  const before = inspect(options, behavior.name);
  const oldShape = map.signature === behavior.signature ? behavior : null;
  const plan = updateGraphOperations(behavior, map, before.graph, oldShape);
  let published;
  try {
    published = mcp(options, 'buildGraph', {
      kind: 'behavior',
      name: behavior.name,
      graph_id: map.graph_id,
      expected_hash: before.hash,
      operations: plan.operations,
    });
  } catch (error) {
    throw attachSourceDiagnostics(error, behavior, plan.operations, map);
  }
  if (!published.published) throw new Error('buildGraph did not publish the edited behavior asset');
  const created = new Map((published.nodes ?? []).filter((node) => node.ref).map((node) => [node.ref, node]));
  const refs = {};
  const types = {};
  for (const descriptor of sourceGraphShape(behavior)) {
    const selector = plan.selectors[descriptor.ref];
    const node = typeof selector === 'number'
      ? { id: selector }
      : created.get(descriptor.ref);
    if (!node) throw new Error('buildGraph did not return the created node ID for ' + descriptor.ref);
    refs[descriptor.ref] = node.id;
    types[descriptor.ref] = descriptor.type;
  }
  const after = verifyCreatedGraph(options, behavior.name);
  verifyTopology(after.graph, behavior, refs);
  const nextMap = {
    version: MARKER_VERSION, asset: behavior.name, graph_id: after.graph.id,
    signature: behavior.signature, refs, types,
    edges: expectedConnections(behavior, refs),
    node_references: sourceGraphReferences(behavior),
    bindings: Object.fromEntries(componentBindings(behavior)),
    state_signature: stateSignature,
    ...sourceState,
  };
  writeFileSync(mapPath, JSON.stringify(nextMap, null, 2) + '\n');
  const kept = [...plan.added].length;
  console.log('updated ' + assetPath + '; preserved ' + Object.keys(refs).length + ' mapped node IDs and added ' + kept + ' nodes');
}

function decompile(options) {
  const source = readFileSync(options.sourceFile, 'utf8');
  const registry = engineRegistry(options);
  const behavior = resolveBehavior(parseBehavior(source), registry);
  const mapPath = options.sourceFile + '.map.json';
  if (!existsSync(mapPath)) throw new Error('no source map; this graph was not created by the behavior-code prototype');
  const map = JSON.parse(readFileSync(mapPath, 'utf8'));
  if (map.version !== MARKER_VERSION || map.asset !== behavior.name) throw new Error('source map version or asset name does not match');
  reconcileSourceIdentities(behavior, map);
  if (map.signature !== behavior.signature) throw new Error('source has structural edits not compiled yet; run compile before graph-to-source export');
  const inspected = inspect(options, behavior.name);
  if (inspected.graph.id !== map.graph_id) throw new Error('mapped graph ID no longer exists');
  const result = graphToSource(source, behavior, inspected.graph, map, registry);
  process.stdout.write(result);
}

try {
  const options = argsOf(process.argv.slice(2));
  if (!options) process.exit(2);
  if (options.command === 'validate') validate(options);
  if (options.command === 'compile') compile(options);
  if (options.command === 'decompile') decompile(options);
} catch (error) {
  process.stderr.write(JSON.stringify({ ok: false, diagnostics: failureDiagnostics(error) }) + '\n');
  process.exitCode = 1;
}
