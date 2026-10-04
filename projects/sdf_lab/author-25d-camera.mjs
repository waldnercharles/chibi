#!/usr/bin/env node
// Constrained camera-sway fixtures, authored through the public MCP protocol.
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, readFileSync, readdirSync, mkdirSync, writeFileSync, mkdtempSync, cpSync, copyFileSync, rmSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const project = dirname(fileURLToPath(import.meta.url));
const engine = resolve(project, '../..');
const controller = process.env.CHIBI_CTL || resolve(engine, 'build/chibi_ctl');
const evidence = resolve(engine, 'build/sdf-25d-camera-evidence');
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
  if (!existsSync(resolve(project, 'content/behaviors', name + '.kdl'))) {
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

behavior('SdfCameraYawSway', 'Small side-to-side camera yaw at fixed tilt: plus/minus 0.3 radians over sixteen seconds. Every world object remains fixed.', [
  ['yaw = 0.3 * sin(elapsed * (2 * pi / 16))', 'GameCamera', 'yaw', 'yaw'],
]);
const original = call('readAsset', { kind: 'scene', name: 'SdfOrbitCamera' }).document;
const cases = [['Sdf25DCamera', 650, 'Gather'], ['Sdf25DOrtho', 0, 'Gather'], ['Sdf25DCameraBlur', 650, 'Blur']];
for (const [name, focal_distance, surface_mode] of cases) {
  if (!existsSync(resolve(project, 'content/scenes', name + '.kdl'))) {
    const entities = structuredClone(original.entities);
    const camera = entities.find(entity => entity.name === 'Camera');
    Object.assign(camera.components.GameCamera, { width: 780, height: 520, focal_distance, yaw: 0, pitch: .55, target_elevation: 60 });
    camera.components.GameHRC2D.surface_mode = surface_mode;
    camera.behaviors = ['SdfCameraYawSway'];
    call('buildScene', { kind: 'scene', name, create: true,
      operations: entities.map(entity => ({ op: 'add_entity', ...entity })) });
  }
  call('validateAsset', { kind: 'scene', name });
}
const scratch = mkdtempSync(resolve(tmpdir(), 'chibi-25d-format-'));
try {
  cpSync(resolve(project, 'content'), resolve(scratch, 'content'), { recursive: true });
  copyFileSync(resolve(project, 'project.kdl'), resolve(scratch, 'project.kdl'));
  const run = spawnSync(controller, ['--project-root', scratch, '--format'], { cwd: engine, encoding: 'utf8' });
  if (run.status !== 0) throw Error(run.stderr || run.stdout);
  for (const path of files(resolve(project, 'content'))) if (!(path in baseline))
    copyFileSync(resolve(scratch, path.slice(project.length + 1)), path);
} finally { rmSync(scratch, { recursive: true }); }
for (const [path, hash] of Object.entries(baseline)) if (digest(path) !== hash) throw Error(`Existing asset changed: ${path}`);
console.log(`Preserved ${Object.keys(baseline).length} prior KDL assets including manifest.`);
writeFileSync(resolve(evidence, 'conditions.json'), JSON.stringify({ period_seconds: 16, yaw_amplitude_radians: .3,
  pitch_radians: .55, target_elevation: 60, cases }, null, 2) + '\n');
if (process.argv.includes('--replay')) {
  for (const [name] of cases) {
    const recording = call('recordScenario', { kind: 'scene', name, frames: 960 });
    const replay = call('replayScenario', { recording_path: recording.recording_path });
    if (!replay.deterministic || replay.diverged) throw Error(`Replay diverged: ${name}`);
    writeFileSync(resolve(evidence, name + '-replay.json'), JSON.stringify(replay, null, 2) + '\n');
    const poses = [];
    for (const frames of [0, 240, 480, 720, 960]) poses.push(call('captureRuntimeSnapshot', { kind: 'scene', name, frames,
      entity_limit: 128, fields: ['GameTransform.x', 'GameTransform.y', 'GameCamera.yaw', 'GameCamera.pitch',
        'GameCamera.target_elevation', 'GameGeometry3D.elevation', 'GameMaterial.emission_intensity', 'GameHRC2D.ambient', 'GameHRC2D.fog'] }));
    const values = poses.map(snapshot => Object.fromEntries(snapshot.entities.map(entity => [entity.name,
      Object.fromEntries(entity.components.flatMap(component => component.fields.map(field => [component.name + '.' + field.name, field.value])))])));
    for (let i = 0; i < values.length; i++) {
      for (const actor of Object.keys(values[0])) if (actor !== 'Camera' && JSON.stringify(values[0][actor]) !== JSON.stringify(values[i][actor]))
        throw Error(`World actor moved: ${name}/${actor}`);
      const camera = values[i].Camera;
      if (camera['GameTransform.x'] !== 0 || camera['GameTransform.y'] !== 0 ||
          Math.abs(camera['GameCamera.yaw'] - .3 * Math.sin(i * Math.PI / 2)) > .0001 ||
          Math.abs(camera['GameCamera.pitch'] - .55) > .00001 || camera['GameCamera.target_elevation'] !== 60 ||
          camera['GameHRC2D.ambient'] !== 0 || camera['GameHRC2D.fog'] !== 0) throw Error(`Camera sway mismatch: ${name}/${i}`);
    }
    writeFileSync(resolve(evidence, name + '-poses.json'), JSON.stringify(poses, null, 2) + '\n');
  }
}
