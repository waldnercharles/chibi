#!/usr/bin/env node
// Zero-ambient moving-camera fixtures, authored through the public MCP protocol.
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, readFileSync, readdirSync, mkdirSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const project = dirname(fileURLToPath(import.meta.url));
const engine = resolve(project, '../..');
const controller = process.env.CHIBI_CTL || resolve(engine, 'build/chibi_ctl');
const evidence = resolve(engine, 'build/sdf-camera-evidence');
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
  Object.fromEntries(files(resolve(project, 'content')).map(path => [path, digest(path)]));
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

behavior('SdfCameraEllipse', 'Moves the camera center around a sixteen-second ellipse. Geometry and emitters remain fixed.', [
  ['x = 70 * cos(elapsed * (2 * pi / 16))', 'GameTransform', 'x', 'x'],
  ['y = 40 * sin(elapsed * (2 * pi / 16))', 'GameTransform', 'y', 'y'],
]);
for (const name of ['SdfBoredBox', 'SdfTorus', 'SdfSphere', 'SdfComparisonMesh', 'SdfLight'])
  call('validateAsset', { kind: 'prototype', name });
function entity(name, prototype, x, y, fields = {}, behaviors = []) {
  return { op: 'add_entity', name, ...(prototype ? { prototype } : {}), behaviors,
    components: { GameTransform: { x, y }, ...fields } };
}
function receiver(name, prototype, x, y, height, scale, pitch, roll, material) {
  return entity(name, prototype, x, y, { GameGeometry3D: { elevation: height },
    GameVisualTransform: { scale, pitch, yaw: 0, roll }, GameShader: { material },
    GameMaterial: { emission_intensity: 0, emission_color: 0x000000ff } });
}
const flatReceiver = 'SdfCameraFlatReceiver';
if (!existsSync(resolve(project, 'content/prototypes', flatReceiver + '.kdl'))) {
  call('buildPrototype', { kind: 'prototype', name: flatReceiver, create: true, operations: [
    { op: 'add_component', component: 'GameTransform', fields: {} },
    { op: 'add_component', component: 'GameAppearance', fields: { shape: 'Box', width: 120, height: 24, color: 0x7b8894ff } },
    { op: 'add_component', component: 'GameMaterial', fields: { scene: 0x7b8894ff, albedo: 0x7b8894ff, absorption: 0, emission_intensity: 0 } },
  ] });
}
call('validateAsset', { kind: 'prototype', name: flatReceiver });
const fixed = [
  entity('Flat reference stage', 'SdfCameraFlatReceiver', 0, 0, { GameAppearance: { shape: 'Box', width: 490, height: 280, color: 0x293441ff },
    GameMaterial: { scene: 0x293441ff, albedo: 0x293441ff, absorption: 0, emission_intensity: 0 } }),
  entity('Flat reference stripe', 'SdfCameraFlatReceiver', 0, 139, { GameAppearance: { shape: 'Box', width: 230, height: 10, color: 0x7b8894ff },
    GameMaterial: { scene: 0x7b8894ff, albedo: 0x7b8894ff, absorption: 0, emission_intensity: 0 } }),
  receiver('Far amber sphere', 'SdfSphere', -65, 15, 0, 1.15, 0, 0, 'SdfAmber'),
  receiver('Mid cyan torus', 'SdfTorus', -20, -5, 60, 1.25, .32, .2, 'SdfCyan'),
  receiver('Near rose bored box', 'SdfBoredBox', 58, 18, 130, 1.05, .28, -.25, 'SdfRose'),
  receiver('Far ivory mesh', 'SdfComparisonMesh', -135, -60, 0, 1.15, .6, .2, 'SdfIvory'),
  receiver('Raised cyan mesh', 'SdfComparisonMesh', 140, -65, 60, 1, .55, -.25, 'SdfCyan'),
  receiver('Raised amber sphere', 'SdfSphere', 135, 60, 130, .8, 0, 0, 'SdfAmber'),
  receiver('Far ivory torus', 'SdfTorus', -130, 73, 0, .85, .55, -.2, 'SdfIvory'),
  ...[[-215, -85, 0x35d9edff, 'Cyan'], [215, 60, 0xffad50ff, 'Amber'], [5, -120, 0xfa68c7ff, 'Magenta']].map(([x, y, color, name]) =>
    entity(name + ' fixed emitter', 'SdfLight', x, y, { GameAppearance: { width: 20, height: 20, color },
      GameMaterial: { emission_color: color, emission_intensity: .85 } })),
];
for (const [name, focal_distance] of [['SdfZeroAmbientCamera', 500], ['SdfZeroAmbientOrtho', 0]]) {
  if (!existsSync(resolve(project, 'content/scenes', name + '.kdl'))) call('buildScene', { kind: 'scene', name, create: true,
    operations: [entity('Camera', 'SdfOptimizationCamera', 70, 0, {
      GameCamera: { width: 700, height: 460, focal_distance },
      GameSurfaceQuality: { resolution: 'Full', sdf_normal_mode: 'Analytic', sdf_pixel_error: 0, disable_sdf_lod: true },
      GameHRC2D: { surface_shadow_mode: 'Horizon8', ambient: 0, fog: 0, shadow_floor: 0, lighting_margin: 125, bloom: 0,
        bounce_intensity: .18, intensity: 4, exposure: .8, surface_radius: 128, surface_relief: 1.2 },
    }, ['SdfCameraEllipse']), ...fixed] });
  call('validateAsset', { kind: 'scene', name });
}
const run = spawnSync(controller, ['--project-root', project, '--format'], { cwd: engine, encoding: 'utf8' });
if (run.status !== 0) throw Error(run.stderr || run.stdout);
for (const [path, hash] of Object.entries(baseline)) if (digest(path) !== hash) throw Error(`Existing asset changed: ${path}`);
console.log(`Preserved ${Object.keys(baseline).length} prior code assets.`);
if (process.argv.includes('--replay')) {
  for (const name of ['SdfZeroAmbientCamera', 'SdfZeroAmbientOrtho']) {
    const recording = call('recordScenario', { kind: 'scene', name, frames: 960 });
    const replay = call('replayScenario', { recording_path: recording.recording_path });
    if (!replay.deterministic || replay.diverged) throw Error(`Replay diverged: ${name}`);
    writeFileSync(resolve(evidence, name + '-replay.json'), JSON.stringify(replay, null, 2) + '\n');
    const snapshots = [0, 240, 480, 720, 960].map(frames => call('captureRuntimeSnapshot', { kind: 'scene', name, frames,
      entity_limit: 128, fields: ['GameTransform.x', 'GameTransform.y', 'GameHRC2D.ambient', 'GameHRC2D.fog',
                                'GameCamera.focal_distance', 'GameMaterial.emission_intensity'] }));
    const poses = snapshots.map(snapshot => Object.fromEntries(snapshot.entities.map(entity => [entity.name,
      Object.fromEntries(entity.components.flatMap(component => component.fields.map(field => [component.name + '.' + field.name, field.value])))])));
    for (let i = 0; i < poses.length; i++) {
      for (const actor of Object.keys(poses[0])) if (actor !== 'Camera' && JSON.stringify(poses[0][actor]) !== JSON.stringify(poses[i][actor]))
        throw Error(`Unexpected object motion: ${name}/${actor}`);
      const camera = poses[i].Camera;
      const phase = i * Math.PI / 2;
      if (Math.abs(camera['GameTransform.x'] - 70 * Math.cos(phase)) > .02 || Math.abs(camera['GameTransform.y'] - 40 * Math.sin(phase)) > .02)
        throw Error(`Camera ellipse mismatch: ${name}/${i}`);
      if (camera['GameHRC2D.ambient'] !== 0 || camera['GameHRC2D.fog'] !== 0) throw Error('Ambient/fog must stay zero');
    }
    writeFileSync(resolve(evidence, name + '-poses.json'), JSON.stringify({ frames: [0, 240, 480, 720, 960], poses }, null, 2) + '\n');
  }
}
