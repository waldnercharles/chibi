#!/usr/bin/env node
// Optional depth-thickness lighting fixtures, authored through the public MCP protocol.
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, readFileSync, readdirSync, mkdirSync, writeFileSync, mkdtempSync, cpSync, copyFileSync, rmSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const project = dirname(fileURLToPath(import.meta.url));
const engine = resolve(project, '../..');
const controller = process.env.CHIBI_CTL || resolve(engine, 'build/chibi_ctl');
const evidence = resolve(engine, 'build/sdf-depth-lighting-evidence');
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


const source = call('readAsset', { kind: 'scene', name: 'SdfAnalyticGradientOrbit' }).document;
function depthLighting(previous = {}) {
  return { ...previous, surface_mode: 'Depth thickness', surface_depth_radius: 128, surface_thickness: 12,
    ambient: 0, fog: 0, shadow_floor: 0, bloom: 0, bounce_intensity: 0, feedback: false };
}
const orbit = structuredClone(source.entities);
const orbitCamera = orbit.find(entity => entity.name === 'Camera');
orbitCamera.components.GameHRC2D = depthLighting(orbitCamera.components.GameHRC2D);
function entity(name, prototype, x, y, fields) {
  return { name, prototype, components: { GameTransform: { x, y }, ...fields } };
}
const study = [
  entity('Camera', 'SdfOptimizationCamera', 0, 0, { GameCamera: { width: 520, height: 340, focal_distance: 600,
    yaw: 0, pitch: .35, roll: 0, target_elevation: 0 },
    GameSurfaceQuality: { resolution: 'Full', sdf_normal_mode: 'Analytic', sdf_pixel_error: 0, disable_sdf_lod: true },
    GameHRC2D: depthLighting({ surface_shadow_mode: 'Horizon8', intensity: 3, exposure: .6,
      resolution: .5, min_resolution: .5, adaptive: false, lighting_margin: 125 }) }),
  entity('Cyan rounded receiver', 'SdfRoundedBox', -65, -10, { GameVisualTransform: { scale: 1.25, pitch: .2, yaw: 0, roll: -.15 },
    GameGeometry3D: { elevation: 0 }, GameShader: { material: 'SdfCyan' }, GameMaterial: { emission_intensity: 0 } }),
  entity('Amber curved receiver', 'SdfTorus', 80, 15, { GameVisualTransform: { scale: 1.25, pitch: .4, yaw: 0, roll: .15 },
    GameGeometry3D: { elevation: 15 }, GameShader: { material: 'SdfAmber' }, GameMaterial: { emission_intensity: 0 } }),
  entity('Thin elevated mesh blocker', 'SdfComparisonMesh', 5, 0, { GameVisualTransform: { scale: 1, pitch: 0, yaw: 0, roll: 0 },
    GameGeometry3D: { width: 14, height: 55, depth: 130, elevation: 30 },
    GameShader: { material: 'SdfRose' }, GameMaterial: { emission_intensity: 0 } }),
  ...[['Cyan source', -95, -25, 0x38d9edff], ['Amber source', 110, 50, 0xffac54ff]].map(([name, x, y, color]) =>
    entity(name, 'SdfSphere', x, y, { GameGeometry3D: { width: 64, height: 64, depth: 64, elevation: 100 },
      GameVisualTransform: { scale: .4, pitch: 0, yaw: 0, roll: 0 }, GameShader: { shape: 'SdfSphere', material: '' },
      GameMaterial: { scene: 0xffffffff, albedo: 0xffffffff, emission_color: color, emission_intensity: 6,
        emission_multiplier: 1, absorption: 0 } })),
];
for (const [name, entities] of [['SdfDepthThicknessOrbit', orbit], ['SdfDepthThicknessStudy', study]]) {
  if (!existsSync(resolve(project, 'content/scenes', name + '.kdl'))) call('buildScene', { kind: 'scene', name, create: true,
    operations: entities.map(entity => ({ op: 'add_entity', ...entity })) });
  const validation = call('validateAsset', { kind: 'scene', name });
  if (!validation.valid) throw Error(`Scene invalid: ${name}`);
  writeFileSync(resolve(evidence, name + '-validation.json'), JSON.stringify(validation, null, 2) + '\n');
}
const scratch = mkdtempSync(resolve(tmpdir(), 'chibi-depth-lighting-format-'));
try {
  cpSync(resolve(project, 'content'), resolve(scratch, 'content'), { recursive: true });
  copyFileSync(resolve(project, 'project.kdl'), resolve(scratch, 'project.kdl'));
  const run = spawnSync(controller, ['--project-root', scratch, '--format'], { cwd: engine, encoding: 'utf8' });
  if (run.status !== 0) throw Error(run.stderr || run.stdout);
  for (const path of files(resolve(project, 'content'))) if (!(path in baseline))
    copyFileSync(resolve(scratch, path.slice(project.length + 1)), path);
} finally { rmSync(scratch, { recursive: true }); }
for (const [path, hash] of Object.entries(baseline)) if (digest(path) !== hash) throw Error(`Existing asset changed: ${path}`);
console.log(`Preserved ${Object.keys(baseline).length} prior code assets including manifest.`);
writeFileSync(resolve(evidence, 'conditions.json'), JSON.stringify({ scenes: ['SdfDepthThicknessOrbit', 'SdfDepthThicknessStudy'],
  surface_mode: 'Depth thickness', surface_depth_radius: 128, surface_thickness: 12,
  ambient: 0, fog: 0, shadow_floor: 0, bounce_intensity: 0, feedback: false,
  orbit_source: 'SdfAnalyticGradientOrbit', source_eight_receivers_and_sphere_emitters_preserved: true,
  no_back_plane: true }, null, 2) + '\n');
if (process.argv.includes('--replay')) {
  for (const name of ['SdfDepthThicknessOrbit', 'SdfDepthThicknessStudy']) {
    const recording = call('recordScenario', { kind: 'scene', name, frames: 960 });
    const replay = call('replayScenario', { recording_path: recording.recording_path });
    if (!replay.deterministic || replay.diverged) throw Error(`Replay diverged: ${name}`);
    writeFileSync(resolve(evidence, name + '-replay.json'), JSON.stringify(replay, null, 2) + '\n');
  }
}
