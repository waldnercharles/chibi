#!/usr/bin/env node
// Centered orbit of the original analytic-gradient study, authored through the public MCP protocol.
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, readFileSync, readdirSync, mkdirSync, writeFileSync, mkdtempSync, cpSync, copyFileSync, rmSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { isDeepStrictEqual } from 'node:util';
import { fileURLToPath } from 'node:url';

const project = dirname(fileURLToPath(import.meta.url));
const engine = resolve(project, '../..');
const controller = process.env.CHIBI_CTL || resolve(engine, 'build/chibi_ctl');
const evidence = resolve(engine, 'build/sdf-emitter-orbit-evidence');
mkdirSync(evidence, { recursive: true });
function files(directory) {
  return readdirSync(directory, { withFileTypes: true }).flatMap(entry => {
    const path = resolve(directory, entry.name);
    return entry.isDirectory() ? files(path) : (path.endsWith('.kdl') || path.endsWith('.code')) ? [path] : [];
  });
}
const digest = path => createHash('sha256').update(readFileSync(path)).digest('hex');
const preservation = resolve(evidence, 'authored-preservation.json');
const baseline = existsSync(preservation) ? JSON.parse(readFileSync(preservation)) :
  Object.fromEntries([resolve(project, 'project.kdl'), ...files(resolve(project, 'content'))].map(path => [path, digest(path)]));
writeFileSync(preservation, JSON.stringify(baseline, null, 2) + '\n');
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

function behavior(name, description, assignments) {
  if (!existsSync(resolve(project, 'content/behaviors', name + '.code'))) {
    const operations = [
      { op: 'add_variable', name: 'elapsed', type: 'float', default: 0 },
      { op: 'add_node', ref: 'tick', type: 'Event', reference_name: 'Tick' },
      { op: 'add_node', ref: 'advance', type: 'Behavior Expression', source: 'elapsed += dt' },
      { op: 'bind_input', node: 'advance', pin: 'elapsed', kind: 'variable', variable: 'elapsed' },
      { op: 'bind_input', node: 'advance', pin: 'dt', kind: 'delta_time' },
      { op: 'connect', from: 'tick', from_pin: 'Then', to: 'advance', to_pin: 'Exec', kind: 'exec' },
    ];
    let previous = 'advance';
    for (const [index, [source, component, field, pin]] of assignments.entries()) {
      const node = `motion${index}`;
      operations.push({ op: 'add_node', ref: node, type: 'Behavior Expression', source },
        { op: 'bind_input', node, pin: 'elapsed', kind: 'variable', variable: 'elapsed' },
        { op: 'bind_input', node, pin, kind: 'component', component, field },
        { op: 'connect', from: previous, from_pin: 'Then', to: node, to_pin: 'Exec', kind: 'exec' });
      previous = node;
    }
    call('buildGraph', { kind: 'behavior', name, create: true, owner_type: 'ecs.entity', description, operations });
  }
  call('validateAsset', { kind: 'behavior', name });
  writeFileSync(resolve(evidence, name + '-graph.json'), JSON.stringify(call('inspectGraph', { kind: 'behavior', name }), null, 2) + '\n');
}

const name = 'SdfAnalyticGradientOrbit';
behavior('SdfCameraCenteredOrbit', 'Sixteen-second full camera orbit around the unchanged analytic-gradient study. Constant target and distance; yaw and roll pi/2 align the orbit with the original image vertical axis.', [
  ['pitch = elapsed * (2 * pi / 16)', 'GameCamera', 'pitch', 'pitch'],
]);
const source = call('readAsset', { kind: 'scene', name: 'SdfAnalyticGradientStudy' }).document;
if (!existsSync(resolve(project, 'content/scenes', name + '.kdl'))) {
  const entities = structuredClone(source.entities);
  const camera = entities.find(entity => entity.name === 'Camera');
  Object.assign(camera.components.GameCamera, { width: 500, height: 320, focal_distance: 700,
    yaw: Math.PI / 2, roll: Math.PI / 2, pitch: 0, target_elevation: 0 });
  camera.components.GameTransform = { x: 0, y: 0 };
  camera.behaviors = ['SdfCameraCenteredOrbit'];
  call('buildScene', { kind: 'scene', name, create: true,
    operations: entities.map(entity => ({ op: 'add_entity', ...entity })) });
}
call('validateAsset', { kind: 'scene', name });
const beforeLights = call('readAsset', { kind: 'scene', name });
const emitters = [
  ['Cyan light', 0x38d9edff], ['Amber light', 0xffac54ff],
].map(([lightName, color]) => ({ name: lightName, prototype: 'SdfSphere', components: {
  GameTransform: structuredClone(source.entities.find(entity => entity.name === lightName).components.GameTransform),
  GameGeometry3D: { width: 64, height: 64, depth: 64, elevation: 0 },
  GameVisualTransform: { scale: .2, pitch: 0, yaw: 0, roll: 0 },
  GameShader: { shape: 'SdfSphere', material: '' },
  GameMaterial: { scene: 0xffffffff, albedo: 0xffffffff, emission_color: color,
    emission_intensity: .45, emission_multiplier: 1, absorption: 0 },
} }));
const lightOperations = emitters.flatMap(emitter => {
  const actual = beforeLights.document.entities.find(entity => entity.name === emitter.name);
  if (actual.prototype === emitter.prototype && isDeepStrictEqual(actual.components, emitter.components)) return [];
  return [
    { op: 'set_entity_prototype', entity: emitter.name, prototype: emitter.prototype },
    { op: 'set_entity_components', entity: emitter.name, components: emitter.components },
  ];
});
if (lightOperations.length) call('buildScene', { kind: 'scene', name, expected_hash: beforeLights.hash, operations: lightOperations });
call('validateAsset', { kind: 'scene', name });
const authored = call('readAsset', { kind: 'scene', name }).document;
const receivers = document => document.entities.filter(entity => !['Camera', 'Cyan light', 'Amber light'].includes(entity.name));
if (!isDeepStrictEqual(receivers(authored), receivers(source))) throw Error('Source receiver overrides changed');
for (const emitter of emitters) {
  const actual = authored.entities.find(entity => entity.name === emitter.name);
  if (actual.prototype !== emitter.prototype || !isDeepStrictEqual(actual.components, emitter.components)) throw Error(`Sphere emitter mismatch: ${emitter.name}`);
}
const originalCamera = source.entities.find(entity => entity.name === 'Camera');
const camera = authored.entities.find(entity => entity.name === 'Camera');
if (!isDeepStrictEqual(camera.components.GameHRC2D, originalCamera.components.GameHRC2D)) throw Error('Source lighting overrides changed');

const scratch = mkdtempSync(resolve(tmpdir(), 'chibi-centered-orbit-format-'));
try {
  cpSync(resolve(project, 'content'), resolve(scratch, 'content'), { recursive: true });
  copyFileSync(resolve(project, 'project.kdl'), resolve(scratch, 'project.kdl'));
  const run = spawnSync(controller, ['--project-root', scratch, '--format'], { cwd: engine, encoding: 'utf8' });
  if (run.status !== 0) throw Error(run.stderr || run.stdout);
  for (const path of files(resolve(project, 'content'))) if (!(path in baseline) || path === resolve(project, 'content/scenes', name + '.kdl'))
    copyFileSync(resolve(scratch, path.slice(project.length + 1)), path);
} finally { rmSync(scratch, { recursive: true }); }
for (const [path, hash] of Object.entries(baseline)) if (path !== resolve(project, 'content/scenes', name + '.kdl') && digest(path) !== hash) throw Error(`Existing asset changed: ${path}`);
const targetPath = resolve(project, 'content/scenes', name + '.kdl');
const preservedCount = Object.keys(baseline).filter(path => path !== targetPath).length;
console.log(`Preserved ${preservedCount} other code assets including manifest; target scene ${lightOperations.length ? 'updated' : 'already configured'}.`);
writeFileSync(resolve(evidence, 'conditions.json'), JSON.stringify({ scene: name, source: 'SdfAnalyticGradientStudy',
  period_seconds: 16, center: [0, 0], target_elevation: 0, focal_distance: 700, width: 500, height: 320,
  yaw_radians: Math.PI / 2, roll_radians: Math.PI / 2, pitch_formula: 'elapsed * (2*pi/16)',
  source_lighting_preserved: true, source_eight_receivers_preserved: true, emitter_conversion: emitters, emitter_radius: 6, no_back_plane: true,
  surface_quality: 'Authored source inherited quality; capture may override Full/Analytic for screenshot parity.' }, null, 2) + '\n');
if (process.argv.includes('--replay')) {
  const recording = call('recordScenario', { kind: 'scene', name, frames: 960 });
  const replay = call('replayScenario', { recording_path: recording.recording_path });
  if (!replay.deterministic || replay.diverged) throw Error(`Replay diverged: ${name}`);
  writeFileSync(resolve(evidence, name + '-replay.json'), JSON.stringify(replay, null, 2) + '\n');
  const poses = [];
  for (const frames of [0, 240, 480, 720, 960]) poses.push(call('captureRuntimeSnapshot', { kind: 'scene', name, frames,
    entity_limit: 128, fields: ['GameTransform.x', 'GameTransform.y', 'GameCamera.yaw', 'GameCamera.pitch',
      'GameCamera.roll', 'GameCamera.target_elevation', 'GameCamera.focal_distance', 'GameCamera.width', 'GameCamera.height',
      'GameVisualTransform.pitch', 'GameVisualTransform.yaw', 'GameVisualTransform.roll', 'GameVisualTransform.scale',
      'GameOrientation.radians', 'GameGeometry3D.elevation', 'GameGeometry3D.width', 'GameGeometry3D.height', 'GameGeometry3D.depth',
      'GameShader.shape', 'GameShader.material', 'GameMaterial.emission_intensity', 'GameMaterial.emission_color',
      'GameMaterial.emission_multiplier', 'GameMaterial.absorption'] }));
  const values = poses.map(snapshot => Object.fromEntries(snapshot.entities.map(entity => [entity.name,
    Object.fromEntries(entity.components.flatMap(component => component.fields.map(field => [component.name + '.' + field.name, field.value])))])));
  for (let i = 0; i < values.length; i++) {
    for (const actor of Object.keys(values[0])) if (actor !== 'Camera' && JSON.stringify(values[0][actor]) !== JSON.stringify(values[i][actor]))
      throw Error(`World actor moved: ${name}/${actor}`);
    const camera = values[i].Camera;
    if (camera['GameTransform.x'] !== 0 || camera['GameTransform.y'] !== 0 ||
        Math.abs(camera['GameCamera.pitch'] - i * Math.PI / 2) > .002 ||
        Math.abs(camera['GameCamera.yaw'] - Math.PI / 2) > .00001 ||
        Math.abs(camera['GameCamera.roll'] - Math.PI / 2) > .00001 ||
        camera['GameCamera.target_elevation'] !== 0 || camera['GameCamera.focal_distance'] !== 700 ||
        camera['GameCamera.width'] !== 500 || camera['GameCamera.height'] !== 320)
      throw Error(`Centered orbit mismatch: ${name}/${i}`);
  }
  writeFileSync(resolve(evidence, name + '-poses.json'), JSON.stringify(poses, null, 2) + '\n');
  console.log('Full-turn replay and fixed world/target/distance checks passed.');
}
