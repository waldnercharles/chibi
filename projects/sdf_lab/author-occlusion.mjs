#!/usr/bin/env node
// Hidden-surface and sparse-scratch fixtures, authored through the public MCP protocol.
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, readFileSync, readdirSync, mkdirSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const project = dirname(fileURLToPath(import.meta.url));
const engine = resolve(project, '../..');
const controller = process.env.CHIBI_CTL || resolve(engine, 'build/chibi_ctl');
const evidence = resolve(engine, 'build/sdf-occlusion-evidence');
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
behavior('SdfOcclusionCameraSweep', 'Small camera translation keeps near-plane blockers in view while projection changes.', [['x = 2 * sin(elapsed * 0.7)', 'GameTransform', 'x', 'x']]);
for (const name of ['SdfComparisonMesh', 'SdfBoredBox', 'SdfSmoothUnion', 'SdfInstance'])
  call('validateAsset', { kind: 'prototype', name });
function entity(name, prototype, x, y, fields = {}, behaviors = []) {
  return { op: 'add_entity', name, prototype, behaviors, components: { GameTransform: { x, y }, ...fields } };
}
function scene(name, shapes, camera = {}, behaviors = []) {
  if (!existsSync(resolve(project, 'content/scenes', name + '.kdl'))) call('buildScene', { kind: 'scene', name, create: true, operations: [
    entity('Camera', 'SdfOptimizationCamera', 0, 0, { GameCamera: { width: 640, height: 400, focal_distance: 0, ...camera },
      GameHRC2D: { surface_shadow_mode: 'Horizon8', ambient: .25, fog: .01, bounce_intensity: .1 } }, behaviors),
    entity('Cyan light', 'SdfLight', -280, -150, { GameMaterial: { emission_color: 0x38d9edff } }),
    entity('Amber light', 'SdfLight', 280, 150, { GameMaterial: { emission_color: 0xffac54ff } }),
    ...shapes,
  ] });
  call('validateAsset', { kind: 'scene', name });
}
const flat = { scale: 1, pitch: 0, yaw: 0, roll: 0 };
scene('SdfHiddenSurfaceStudy', [
  entity('Hidden complex under mesh', 'SdfInstance', -160, -70, { GameVisualTransform: flat, GameShader: { material: 'SdfRose', parameters: 'Radius 25\nBore 8\n' } }),
  entity('Near opaque mesh', 'SdfComparisonMesh', -160, -70, { GameVisualTransform: flat, GameGeometry3D: { width: 82, height: 20, depth: 82, elevation: 80 }, GameShader: { material: 'SdfCyan' } }),
  entity('Visible through hole', 'SdfSmoothUnion', 0, -70, { GameVisualTransform: { ...flat, scale: .8 }, GameShader: { material: 'SdfAmber' } }),
  entity('Near SDF with hole', 'SdfBoredBox', 0, -70, { GameVisualTransform: { ...flat, scale: 1.5 }, GameGeometry3D: { elevation: 160 }, GameShader: { material: 'SdfCyan' } }),
  entity('Partially hidden torus', 'SdfTorus', 160, -70, { GameVisualTransform: flat }),
  entity('Partial opaque mesh', 'SdfComparisonMesh', 180, -70, { GameVisualTransform: flat, GameGeometry3D: { width: 36, height: 20, depth: 84, elevation: 80 }, GameShader: { material: 'SdfRose' } }),
  entity('Hidden under solid SDF', 'SdfSphere', -120, 95, { GameVisualTransform: flat, GameShader: { shape: 'SdfGradientNoise' } }),
  entity('Solid SDF cover', 'SdfRoundedBox', -120, 95, { GameVisualTransform: { ...flat, scale: 1.5 }, GameGeometry3D: { elevation: 160 }, GameShader: { material: 'SdfAmber' } }),
  entity('Moving-hidden complex', 'SdfInstance', 120, 95, { GameVisualTransform: flat }),
  entity('Moving blocker', 'SdfComparisonMesh', 0, 95, { GameVisualTransform: flat, GameGeometry3D: { width: 76, height: 20, depth: 78, elevation: 80 }, GameShader: { material: 'SdfCyan' } }, ['HrcReuseBlockerSweep']),
]);
scene('SdfSparseScratchStudy', [-1, 1].flatMap(xsign => [-1, 1].flatMap(ysign => {
  const x = xsign * 230, y = ysign * 135;
  return [
    entity(`Group ${xsign} ${ysign} sphere`, 'SdfSphere', x - 14, y, { GameVisualTransform: { ...flat, scale: .7 }, GameShader: { material: 'SdfAmber' } }),
    entity(`Group ${xsign} ${ysign} mesh`, 'SdfComparisonMesh', x + 14, y + 5, { GameVisualTransform: { ...flat, scale: .7 }, GameGeometry3D: { elevation: 20 }, GameShader: { material: 'SdfCyan' } }),
    entity(`Group ${xsign} ${ysign} torus`, 'SdfTorus', x + 32, y - 8, { GameVisualTransform: { ...flat, scale: .6 }, GameGeometry3D: { elevation: 50 }, GameShader: { material: 'SdfRose' } }),
  ];
})), { width: 640, height: 400 });
scene('SdfOcclusionNearPlane', [
  entity('Behind clipped blocker', 'SdfTorus', 0, 0, { GameVisualTransform: { ...flat, scale: .8 }, GameGeometry3D: { elevation: 0 } }),
  entity('Near-plane crossing mesh', 'SdfComparisonMesh', 2, 0, { GameVisualTransform: { ...flat, scale: .05 }, GameGeometry3D: { width: 60, height: 100, depth: 60, elevation: 119 }, GameShader: { material: 'SdfCyan' } }),
  entity('Near-plane crossing SDF', 'SdfRoundedBox', -2, 0, { GameVisualTransform: { ...flat, scale: .05 }, GameGeometry3D: { elevation: 119 }, GameShader: { material: 'SdfRose' } }),
  entity('Ordinary receiver', 'SdfSmoothUnion', -35, 30, { GameVisualTransform: { ...flat, scale: .5 } }),
], { width: 180, height: 120, focal_distance: 120 }, ['SdfOcclusionCameraSweep']);
const run = spawnSync(controller, ['--project-root', project, '--format'], { cwd: engine, encoding: 'utf8' });
if (run.status !== 0) throw Error(run.stderr || run.stdout);
for (const [path, hash] of Object.entries(baseline)) if (digest(path) !== hash) throw Error(`Existing asset changed: ${path}`);
console.log(`Preserved ${Object.keys(baseline).length} prior code assets.`);
if (process.argv.includes('--replay')) {
  for (const name of ['SdfHiddenSurfaceStudy', 'SdfSparseScratchStudy', 'SdfOcclusionNearPlane']) {
    const recording = call('recordScenario', { kind: 'scene', name, frames: 360 });
    const replay = call('replayScenario', { recording_path: recording.recording_path });
    if (!replay.deterministic || replay.diverged) throw Error(`Replay diverged: ${name}`);
    writeFileSync(resolve(evidence, name + '-replay.json'), JSON.stringify(replay, null, 2) + '\n');
    const snapshots = [0, 90].map(frames => call('captureRuntimeSnapshot', { kind: 'scene', name,
      frames, entity_limit: 128, fields: ['GameTransform.x', 'GameTransform.y'] }));
    const poses = snapshots.map(snapshot => Object.fromEntries(snapshot.entities.map(entity => [entity.name,
      entity.components.flatMap(component => component.fields).map(field => field.value)])));
    const changing = name === 'SdfHiddenSurfaceStudy' ? 'Moving blocker' : name === 'SdfOcclusionNearPlane' ? 'Camera' : null;
    for (const actor of Object.keys(poses[0])) {
      const same = JSON.stringify(poses[0][actor]) === JSON.stringify(poses[1][actor]);
      if ((actor === changing && same) || (actor !== changing && !same)) throw Error(`Unexpected motion: ${name}/${actor}`);
    }
    writeFileSync(resolve(evidence, name + '-poses.json'), JSON.stringify({ frames: [0, 90], changing, poses }, null, 2) + '\n');
  }
}
