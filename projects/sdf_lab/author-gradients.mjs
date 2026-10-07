#!/usr/bin/env node
// Analytic-gradient normal fixtures, authored through the public MCP protocol.
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, readFileSync, readdirSync, mkdirSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const project = dirname(fileURLToPath(import.meta.url));
const engine = resolve(project, '../..');
const controller = process.env.CHIBI_CTL || resolve(engine, 'build/chibi_ctl');
const evidence = resolve(engine, 'build/sdf-gradient-evidence');
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

for (const name of ['SdfRoundedBox', 'SdfTorus', 'SdfBoredBox', 'SdfSmoothUnion', 'SdfBreathingSphere', 'SdfInstanceShape'])
  call('inspectGraph', { kind: 'shape', name });
call('searchNodes', { query: 'noise' });
const fallback = 'SdfGradientNoise';
if (!existsSync(resolve(project, 'content/shapes', fallback + '.kdl'))) {
  call('buildGraph', { kind: 'shape', name: fallback, create: true,
    description: 'Small spatial-noise displacement deliberately exercises tetrahedral fallback in Analytic normal mode.',
    operations: [
      { op: 'set_bounds', value: [48, 48] },
      { op: 'add_node', ref: 'position', type: 'Shader ObjectPosition' },
      { op: 'add_node', ref: 'distance', type: 'Behavior Expression', source: 'length(position) - 30 + 0.05 * noise(position, 0.15)' },
      { op: 'connect', from: 'position', from_pin: 'Value', to: 'distance', to_pin: 'position', kind: 'data' },
      { op: 'connect', from: 'distance', from_pin: 'Result', to: 'output', to_pin: 'Distance', kind: 'data' },
    ] });
}
call('validateAsset', { kind: 'shape', name: fallback });
writeFileSync(resolve(evidence, fallback + '-graph.json'), JSON.stringify(call('inspectGraph', { kind: 'shape', name: fallback }), null, 2) + '\n');
function entity(name, prototype, x, y, fields = {}) {
  return { op: 'add_entity', name, prototype, components: { GameTransform: { x, y }, ...fields } };
}
function scene(name, shapes) {
  if (!existsSync(resolve(project, 'content/scenes', name + '.kdl'))) call('buildScene', { kind: 'scene', name, create: true, operations: [
    entity('Camera', 'SdfOptimizationCamera', 0, 0, { GameCamera: { width: 500, height: 320, focal_distance: 700 },
      GameHRC2D: { surface_shadow_mode: 'Horizon8', ambient: .22, fog: .015, bounce_intensity: .12 } }),
    entity('Cyan light', 'SdfLight', -215, -45, { GameMaterial: { emission_color: 0x38d9edff } }),
    entity('Amber light', 'SdfLight', 215, 45, { GameMaterial: { emission_color: 0xffac54ff } }),
    ...shapes,
  ] });
  call('validateAsset', { kind: 'scene', name });
}
scene('SdfAnalyticGradientStudy', [
  entity('Rounded box rotated', 'SdfRoundedBox', -150, -70, { GameOrientation: { radians: .35 }, GameVisualTransform: { scale: .95, pitch: .55, roll: .25 }, GameShader: { material: 'SdfCyan' } }),
  entity('Rounded box small tilted', 'SdfRoundedBox', -50, -70, { GameOrientation: { radians: -.45 }, GameVisualTransform: { scale: .65, pitch: -.35, roll: -.2 }, GameShader: { material: 'SdfAmber' } }),
  entity('Torus opening', 'SdfTorus', 50, -70, { GameVisualTransform: { scale: .95, pitch: .65, roll: .15 } }),
  entity('Hard difference bore', 'SdfBoredBox', 150, -70, { GameVisualTransform: { scale: .95, pitch: .1, roll: -.15 }, GameShader: { material: 'SdfRose' } }),
  entity('Smooth union blend', 'SdfSmoothUnion', -150, 65, { GameVisualTransform: { scale: .95, pitch: .3, roll: .2 }, GameShader: { material: 'SdfAmber' } }),
  entity('Breathing sphere animated', 'SdfBreathingSphere', -50, 65, { GameVisualTransform: { scale: .9 }, GameShader: { material: 'SdfCyan' } }),
  entity('Parameterized CSG', 'SdfInstance', 50, 65, { GameVisualTransform: { scale: 1.05, pitch: .6, roll: -.2 }, GameShader: { material: 'SdfInstanceTint', parameters: 'Radius 25\nBore 10\n(array)Tint 0.65 0.3 0.48\n' } }),
  entity('Noise fallback', 'SdfSphere', 150, 65, { GameVisualTransform: { scale: .95 }, GameShader: { shape: fallback, material: 'SdfIvory' } }),
]);
scene('SdfAnalyticGradientFallback', [
  entity('Noise fallback', 'SdfSphere', 0, 0, { GameVisualTransform: { scale: 2, pitch: .4, roll: -.2 }, GameShader: { shape: fallback, material: 'SdfIvory' } }),
]);
const run = spawnSync(controller, ['--project-root', project, '--format'], { cwd: engine, encoding: 'utf8' });
if (run.status !== 0) throw Error(run.stderr || run.stdout);
for (const [path, hash] of Object.entries(baseline)) if (digest(path) !== hash) throw Error(`Existing asset changed: ${path}`);
console.log(`Preserved ${Object.keys(baseline).length} prior code assets.`);
