#!/usr/bin/env node
// Independent HRC reuse fixtures, authored through the public MCP protocol.
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, readFileSync, readdirSync, mkdirSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const project = dirname(fileURLToPath(import.meta.url));
const engine = resolve(project, '../..');
const controller = process.env.CHIBI_CTL || resolve(engine, 'build/chibi_ctl');
const evidence = resolve(engine, 'build/hrc-reuse-evidence');
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
behavior('HrcReuseLightSweep', 'Varies emission intensity while every light, receiver and blocker keeps static geometry.', [
  ['intensity = 0.35 + 0.2 * sin(elapsed * 1.1)', 'GameMaterial', 'emission_intensity', 'intensity'],
]);
behavior('HrcReuseBlockerSweep', 'Translates a raised blocker across a continuous receiver; cache must invalidate on geometry changes.', [
  ['x = 100 * sin(elapsed * 0.9)', 'GameTransform', 'x', 'x'],
]);
behavior('HrcReuseCameraPan', 'Translates the camera across static receivers, exposing geometry beyond the original viewport.', [
  ['x = 130 * sin(elapsed * 0.7)', 'GameTransform', 'x', 'x'],
]);
behavior('HrcReuseIntegerPan', 'Quantized orthographic camera motion in eight-material-pixel steps at 960x540, camera height 400.', [
  ['x = floor(22 * sin(elapsed * 0.7)) * (3200 / 540)', 'GameTransform', 'x', 'x'],
]);
function entity(name, prototype, x, y, components = {}, behaviors = []) {
  return { op: 'add_entity', name, prototype, behaviors, components: { GameTransform: { x, y }, ...components } };
}
const lights = [entity('West cyan', 'SdfLight', -240, 0, { GameMaterial: { emission_color: 0x38d9edff } }),
  entity('East amber', 'SdfLight', 240, 0, { GameMaterial: { emission_color: 0xffac54ff } })];
const receiver = entity('Continuous receiver', 'SdfHorizonCanopy', 0, 0,
  { GameVisualTransform: { scale: 1.7 }, GameShader: { material: 'SdfCyan' } });
function scene(name, entities, cameraBehavior = [], camera = {}, lighting = {}) {
  if (!existsSync(resolve(project, 'content/scenes', name + '.kdl'))) {
    call('buildScene', { kind: 'scene', name, create: true, operations: [
      entity('Camera', 'SdfOptimizationCamera', 0, 0, { GameCamera: { width: 620, height: 400, focal_distance: 0, ...camera },
        GameHRC2D: { surface_shadow_mode: 'Horizon8', ambient: .08, fog: .02, bounce_intensity: .12, ...lighting } }, cameraBehavior),
      ...entities,
    ] });
  }
  call('validateAsset', { kind: 'scene', name });
}
scene('HrcReuseChangingLights', [receiver, ...lights,
  entity('Animated source', 'SdfLight', 220, 0, { GameMaterial: { emission_color: 0xec749fff } }, ['HrcReuseLightSweep']),
  entity('Static raised blocker', 'SdfRoundedBox', 0, 0, { GameVisualTransform: { scale: .6 }, GameGeometry3D: { elevation: 45 } }),
]);
scene('HrcReuseMovingBlocker', [receiver, ...lights,
  entity('Translating blocker', 'SdfRoundedBox', 0, 0, { GameVisualTransform: { scale: .6 }, GameGeometry3D: { elevation: 45 } }, ['HrcReuseBlockerSweep']),
]);
const panorama = [...lights, ...[-360, -180, 0, 180, 360].map((x, i) =>
  entity(`Receiver ${i + 1}`, 'SdfHorizonCanopy', x, 0, { GameVisualTransform: { scale: .8 }, GameShader: { material: i % 2 ? 'SdfAmber' : 'SdfCyan' } })),
  ...[-300, -100, 100, 300].map((x, i) => entity(`Raised blocker ${i + 1}`, 'SdfRoundedBox', x, 0,
    { GameVisualTransform: { scale: .4 }, GameGeometry3D: { elevation: 35 } })),
 ];
scene('HrcReuseCameraPan', panorama, ['HrcReuseCameraPan']);
scene('HrcReuseIntegerPan', panorama, ['HrcReuseIntegerPan']);
scene('HrcReuseThinGaps', [...lights,
  entity('Continuous smooth receiver', 'SdfHorizonCanopy', -130, 15, { GameVisualTransform: { scale: .8 } }),
  entity('Thin ring opening', 'SdfQualityThinRing', -130, 15, { GameGeometry3D: { elevation: 28 }, GameVisualTransform: { scale: 1, pitch: .25 } }),
  ...[4, 10].flatMap((gap, i) => [0, 1].map(side => entity(`Gap ${gap} hill ${side + 1}`, 'SdfSphere',
    85 + side * (51 + gap), -75 + i * 135, { GameVisualTransform: { scale: .85, pitch: 0 }, GameShader: { material: 'SdfAmber' } }))),
  entity('Small narrow bore', 'SdfQualityNarrowCut', -130, 110, { GameVisualTransform: { scale: .3 } }),
]);
scene('SdfNearPlaneBounds', [
  entity('Near clipped side box', 'SdfRoundedBox', .375, 0, { GameVisualTransform: { scale: .0038, pitch: 0, roll: 0 }, GameGeometry3D: { elevation: 31 }, GameShader: { material: 'SdfCyan' } }),
  entity('Off-screen near-plane sphere', 'SdfSphere', 10, 0, { GameVisualTransform: { scale: .0026, pitch: 0, roll: 0 }, GameGeometry3D: { elevation: 31 }, GameShader: { material: 'SdfRose' } }),
  entity('Ordinary perspective sphere', 'SdfSphere', -14, 7, { GameVisualTransform: { scale: .1, pitch: 0, roll: 0 }, GameGeometry3D: { elevation: 16 }, GameShader: { material: 'SdfAmber' } }),
], [], { width: 96, height: 80, focal_distance: 32 }, { surface_shadow_mode: 'Reference', ambient: .5, fog: 0, bounce_intensity: .2 });
for (const flags of [['--format'], ['--format', '--check']]) {
  const run = spawnSync(controller, ['--project-root', project, ...flags], { cwd: engine, encoding: 'utf8' });
  if (run.status !== 0) throw Error(run.stderr || run.stdout);
}
for (const [path, hash] of Object.entries(baseline)) if (digest(path) !== hash) throw Error(`Existing asset changed: ${path}`);
console.log(`Preserved ${Object.keys(baseline).length} prior code assets.`);
