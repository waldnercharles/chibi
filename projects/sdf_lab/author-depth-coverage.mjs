#!/usr/bin/env node
// Depth lighting coverage and energy fixtures, authored through the public MCP protocol.
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, readFileSync, readdirSync, mkdirSync, writeFileSync, mkdtempSync, cpSync, copyFileSync, rmSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const project = dirname(fileURLToPath(import.meta.url));
const engine = resolve(project, '../..');
const controller = process.env.CHIBI_CTL || resolve(engine, 'build/chibi_ctl');
const evidence = resolve(engine, 'build/sdf-depth-coverage-evidence');
mkdirSync(evidence, { recursive: true });
function files(directory) {
  return readdirSync(directory, { withFileTypes: true }).flatMap(entry => {
    const path = resolve(directory, entry.name);
    return entry.isDirectory() ? files(path) : path.endsWith('.kdl') ? [path] : [];
  });
}
const digest = path => createHash('sha256').update(readFileSync(path)).digest('hex');
const preservation = resolve(evidence, 'authored-preservation.json');
const baseline = existsSync(preservation) ? JSON.parse(readFileSync(preservation)) :
  Object.fromEntries([resolve(project, 'project.kdl'), ...files(resolve(project, 'content'))].map(path => [path, digest(path)]));
writeFileSync(preservation, JSON.stringify(baseline, null, 2) + '\n');
const runBaseline = Object.fromEntries([resolve(project, 'project.kdl'), ...files(resolve(project, 'content'))].map(path => [path, digest(path)]));
function call(name, args) {
  const run = spawnSync(process.execPath, [resolve(engine, 'tools/mcp/server.mjs')], {
    cwd: engine, env: { ...process.env, CHIBI_ROOT: project, CHIBI_CTL: controller },
    input: JSON.stringify({ jsonrpc: '2.0', id: 1, method: 'tools/call', params: { name, arguments: args } }) + '\n',
    encoding: 'utf8', maxBuffer: 32 * 1024 * 1024,
  });
  if (run.status !== 0) throw Error(run.stderr || run.stdout);
  const response = JSON.parse(run.stdout.trim());
  if (response.error || response.result?.isError) throw Error(JSON.stringify(response));
  console.log(name, args.name || '');
  return response.result.structuredContent.result;
}
if (resolve(call('getProjectStatus', {}).project_root) !== project) throw Error('Unexpected project root');


call('searchAssets', { query: 'Sdf Sphere' });
call('searchNodes', { query: 'Behavior Expression' });
call('inspectGraph', { kind: 'shape', name: 'SdfSphere' });
call('readAsset', { kind: 'prototype', name: 'SdfComparisonMesh' });
const behavior = 'SdfCoverageLightCircle';
if (!existsSync(resolve(project, 'content/behaviors', behavior + '.kdl'))) {
  call('buildGraph', { kind: 'behavior', name: behavior, create: true, owner_type: 'ecs.entity',
    description: 'A small light circles a stationary receiver every eight seconds at radius 60 and fixed elevation. Separate x/y assignments are required because GameTransform exposes scalar fields.',
    operations: [
      { op: 'add_variable', name: 'elapsed', type: 'float', default: 0 },
      { op: 'add_node', ref: 'tick', type: 'Event', reference_name: 'Tick' },
      { op: 'add_node', ref: 'advance', type: 'Behavior Expression', source: 'elapsed += dt' },
      { op: 'bind_input', node: 'advance', pin: 'elapsed', kind: 'variable', variable: 'elapsed' },
      { op: 'bind_input', node: 'advance', pin: 'dt', kind: 'delta_time' },
      { op: 'connect', from: 'tick', from_pin: 'Then', to: 'advance', to_pin: 'Exec', kind: 'exec' },
      ...['x', 'y'].flatMap((axis, index) => [
        { op: 'add_node', ref: axis, type: 'Behavior Expression', source: `${axis} = 60 * ${index ? 'sin' : 'cos'}(elapsed * (2 * pi / 8))` },
        { op: 'bind_input', node: axis, pin: 'elapsed', kind: 'variable', variable: 'elapsed' },
        { op: 'bind_input', node: axis, pin: axis, kind: 'component', component: 'GameTransform', field: axis },
        { op: 'connect', from: index ? 'x' : 'advance', from_pin: 'Then', to: axis, to_pin: 'Exec', kind: 'exec' },
      ]),
    ] });
}
const slab = 'SdfCoverageSlab';
if (!existsSync(resolve(project, 'content/shapes', slab + '.kdl'))) call('buildGraph', {
  kind: 'shape', name: slab, create: true,
  description: 'Stationary broad rounded slab: a simple lighting receiver without animation or material emission.',
  operations: [
    { op: 'set_bounds', value: [90, 90] },
    { op: 'add_node', ref: 'position', type: 'Shader ObjectPosition' },
    { op: 'add_node', ref: 'offset', type: 'Behavior Expression', source: 'abs(p) - vec3(82, 6, 82)' },
    { op: 'add_node', ref: 'distance', type: 'Behavior Expression', source: 'length(max(offset, 0)) + min(max(max(offset.x, offset.y), offset.z), 0) - 4' },
    { op: 'connect', from: 'position', from_pin: 'Value', to: 'offset', to_pin: 'p', kind: 'data' },
    { op: 'connect', from: 'offset', from_pin: 'Result', to: 'distance', to_pin: 'offset', kind: 'data' },
    { op: 'connect', from: 'distance', from_pin: 'Result', to: 'output', to_pin: 'Distance', kind: 'data' },
  ] });
const slabGraph = call('inspectGraph', { kind: 'shape', name: slab });
const offsetNode = slabGraph.graph.nodes.find(node => node.type === 'Behavior Expression' && node.reference_name?.startsWith('abs(p)'));
if (offsetNode?.reference_name !== 'abs(p) - vec3(82, 6, 82)') call('buildGraph', { kind: 'shape', name: slab, expected_hash: slabGraph.hash,
  operations: [{ op: 'set_expression', node: offsetNode.id, source: 'abs(p) - vec3(82, 6, 82)' }] });
for (const [kind, name] of [['behavior', behavior], ['shape', slab]]) {
  const validation = call('validateAsset', { kind, name });
  if (!validation.valid) throw Error(`Invalid ${name}`);
  writeFileSync(resolve(evidence, name + '-graph.json'), JSON.stringify(call('inspectGraph', { kind, name }), null, 2) + '\n');
}
function entity(name, prototype, x, y, components, behaviors = []) {
  return { name, prototype, components: { GameTransform: { x, y }, ...components }, behaviors };
}
function camera(width = 240, height = 240) {
  return entity('Camera', 'SdfOptimizationCamera', 0, 0, {
    GameCamera: { width, height, focal_distance: 650, yaw: 0, pitch: 0, roll: 0, target_elevation: 0 },
    GameSurfaceQuality: { resolution: 'Full', sdf_normal_mode: 'Analytic', sdf_pixel_error: 0, disable_sdf_lod: true },
    GameHRC2D: { surface_mode: 'Depth thickness', surface_depth_radius: 128, surface_thickness: 12,
      surface_shadow_mode: 'Horizon8', surface_radius: 24, intensity: 3, exposure: .6,
      resolution: .5, min_resolution: .5, adaptive: false, lighting_margin: 125,
      ambient: 0, fog: 0, shadow_floor: 0, bloom: 0, bounce_intensity: 0, feedback: false },
  });
}
function receiver(name = 'Broad stationary receiver', x = 0, y = 0, scale = 1) {
  return entity(name, 'SdfRoundedBox', x, y, {
    GameGeometry3D: { width: 180, height: 24, depth: 180, elevation: 0 },
    GameVisualTransform: { scale, pitch: 0, yaw: 0, roll: 0 },
    GameShader: { shape: slab, material: 'SdfIvory' }, GameMaterial: { emission_intensity: 0 },
  });
}
function light(name, x, y, elevation, radius, intensity, behaviors = []) {
  return entity(name, 'SdfSphere', x, y, {
    GameGeometry3D: { width: 64, height: 64, depth: 64, elevation },
    GameVisualTransform: { scale: radius / 30, pitch: 0, yaw: 0, roll: 0 },
    GameShader: { shape: 'SdfSphere', material: '' },
    GameMaterial: { scene: 0xffffffff, albedo: 0xffffffff, emission_color: 0x38d9edff,
      emission_intensity: intensity, emission_multiplier: 1, absorption: 0 },
  }, behaviors);
}
const tiltedReceiver = receiver();
tiltedReceiver.components.GameVisualTransform.pitch = .45;
const meshReceiver = receiver();
meshReceiver.prototype = 'SdfComparisonMesh';
meshReceiver.components.GameShader = { shape: '', material: 'SdfIvory' };
const scenes = [
  ['SdfDepthCoverageMeshCircle', [camera(), meshReceiver, light('Small orbiting source', 60, 0, 65, 3, 6, [behavior])]],
  ['SdfDepthCoverageCircle', [camera(), receiver(), light('Small orbiting source', 60, 0, 65, 3, 6, [behavior])]],
  ['SdfDepthEnergySmall', [camera(), receiver(), light('Small fixed source', 45, 0, 65, 3, 6)]],
  ['SdfDepthEnergyLarge', [camera(), receiver(), light('Large equal energy source', 45, 0, 65, 6, 1.5)]],
  ['SdfDepthEnergyTilted', [camera(), tiltedReceiver, light('Fixed source above tilted receiver', 45, 0, 65, 3, 6)]],
  ['SdfDepthEnergyFar', [camera(), receiver(), light('Far fixed source', 45, 0, 95, 3, 6)]],
];
for (const [name, entities] of scenes) {
  if (!existsSync(resolve(project, 'content/scenes', name + '.kdl'))) call('buildScene', { kind: 'scene', name, create: true,
    operations: entities.map(entity => ({ op: 'add_entity', ...entity })) });
  const actual = call('readAsset', { kind: 'scene', name });
  const actualReceiver = actual.document.entities.find(entity => entity.name === 'Broad stationary receiver');
  if (actualReceiver.components.GameGeometry3D.height !== 24) call('buildScene', { kind: 'scene', name, expected_hash: actual.hash,
    operations: [{ op: 'set_entity_components', entity: actualReceiver.name, components: entities.find(entity => entity.name === actualReceiver.name).components }] });
  const validation = call('validateAsset', { kind: 'scene', name });
  if (!validation.valid) throw Error(`Invalid scene ${name}`);
  writeFileSync(resolve(evidence, name + '-validation.json'), JSON.stringify(validation, null, 2) + '\n');
}
const scratch = mkdtempSync(resolve(tmpdir(), 'chibi-depth-coverage-format-'));
try {
  cpSync(resolve(project, 'content'), resolve(scratch, 'content'), { recursive: true });
  copyFileSync(resolve(project, 'project.kdl'), resolve(scratch, 'project.kdl'));
  const run = spawnSync(controller, ['--project-root', scratch, '--format'], { cwd: engine, encoding: 'utf8' });
  if (run.status !== 0) throw Error(run.stderr || run.stdout);
  for (const path of files(resolve(project, 'content'))) if (!(path in runBaseline))
    copyFileSync(resolve(scratch, path.slice(project.length + 1)), path);
} finally { rmSync(scratch, { recursive: true }); }
for (const [path, hash] of Object.entries(runBaseline)) if (digest(path) !== hash) throw Error(`Existing asset changed: ${path}`);
writeFileSync(resolve(evidence, 'conditions.json'), JSON.stringify({ scenes: scenes.map(([name]) => name),
  period_seconds: 8, circle_radius: 60, circle_elevation: 65, light_radius: 3, ambient: 0,
  mode: 'Depth thickness', depth_radius: 128, thickness: 12, preserved_assets: Object.keys(runBaseline).length, original_preserved_assets: Object.keys(baseline).length,
  energy_pair: 'radius 3 intensity 6 versus radius 6 intensity 1.5; equal projected area times intensity at equal depth',
}, null, 2) + '\n');
console.log(`Preserved ${Object.keys(runBaseline).length} prior KDL assets including manifest.`);
const snapshots = [];
for (const frames of [0, 120, 240, 360, 480]) {
  const snapshot = call('captureRuntimeSnapshot', { kind: 'scene', name: 'SdfDepthCoverageCircle', frames,
    fields: ['GameTransform.x', 'GameTransform.y', 'GameGeometry3D.elevation', 'GameMaterial.emission_intensity'], entity_limit: 8 });
  const source = snapshot.entities.find(entity => entity.name === 'Small orbiting source');
  const fields = Object.fromEntries(source.components.flatMap(component => component.fields.map(field => [component.name + '.' + field.name, field.value])));
  const angle = frames / 60 * (2 * Math.PI / 8);
  if (Math.abs(fields['GameTransform.x'] - 60 * Math.cos(angle)) > .02 ||
      Math.abs(fields['GameTransform.y'] - 60 * Math.sin(angle)) > .02 ||
      fields['GameGeometry3D.elevation'] !== 65 || fields['GameMaterial.emission_intensity'] !== 6)
    throw Error(`Orbit pose mismatch at frame ${frames}`);
  snapshots.push(snapshot);
}
writeFileSync(resolve(evidence, 'circle-poses.json'), JSON.stringify(snapshots, null, 2) + '\n');
console.log('Eight-second circle, constant distance/elevation/emission verified at five poses.');
