#!/usr/bin/env node
// Reproduce the study assets through the same public graph tools Studio uses.
import { spawnSync } from "node:child_process";
import { mkdirSync, existsSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const project = dirname(fileURLToPath(import.meta.url));
const engine = resolve(project, "../..");
const controller = process.env.CHIBI_CTL || resolve(engine, "build/chibi_ctl");
for (const kind of ["models", "shapes", "materials", "behaviors", "prototypes", "scenes", "ui"])
  mkdirSync(resolve(project, "content", kind), { recursive: true });

export function call(name, args) {
  // Studio's new-model template is a connected sphere (node 3). These scripts
  // supply a complete construction, so remove the starter before building it.
  if (name === "buildGraph" && args.kind === "model" && args.create)
    args = { ...args, operations: [{ op: "remove_node", node: 3 }, ...args.operations] };
  const run = spawnSync(process.execPath, [resolve(engine, "tools/mcp/server.mjs")], {
    cwd: engine, env: { ...process.env, CHIBI_ROOT: project, CHIBI_CTL: controller },
    input: JSON.stringify({ jsonrpc: "2.0", id: 1, method: "tools/call", params: { name, arguments: args } }) + "\n",
    encoding: "utf8", maxBuffer: 64 * 1024 * 1024,
  });
  if (run.status !== 0) throw Error(run.stderr);
  const response = JSON.parse(run.stdout.trim());
  if (response.error || response.result?.isError) {
    const evidence = resolve(engine, "build/model-evidence");
    mkdirSync(evidence, { recursive: true });
    writeFileSync(resolve(evidence, "failed-authoring-request.json"), JSON.stringify({ name, arguments: args }, null, 2));
    throw Error(`${JSON.stringify(response)}\n${run.stderr}`);
  }
  console.log(`${name}: ${args.name || ""}`);
  return response.result;
}

const status = call("getProjectStatus", {}).structuredContent.result;
if (resolve(status.project_root) !== project) throw Error("Authoring project root does not match Modeling Lab");

function exists(kind, name) { return existsSync(resolve(project, `content/${kind}/${name}.kdl`)); }
function graph(kind, name, description, operations) {
  if (!exists(`${kind}s`, name)) call("buildGraph", { kind, name, create: true, description, operations });
  call("validateAsset", { kind, name });
}
function prototype(name, components) {
  if (!exists("prototypes", name)) call("buildPrototype", { kind: "prototype", name, create: true,
    operations: Object.entries(components).map(([component, fields]) => ({ op: "add_component", component, fields })) });
  call("validateAsset", { kind: "prototype", name });
}
const connect = (from, from_pin, to, to_pin) => ({ op: "connect", from, from_pin, to, to_pin, kind: "data" });

graph("model", "ModelSphere", "A sphere with computed bounds; radius is measured in local project units.", [
  { op: "add_node", type: "Model Sphere", ref: "sphere" },
  { op: "set_input", node: "sphere", pin: "Radius", value: 30 },
  connect("sphere", "Geometry", "output", "Geometry"),
]);
graph("shape", "AnalyticalSphere", "Independent numerical reference, length(p) - 30, with manual rendering bounds.", [
  { op: "set_bounds", value: [30, 30] },
  { op: "add_node", type: "Shader ObjectPosition", ref: "position" },
  { op: "add_node", type: "Behavior Expression", ref: "distance", source: "length(p) - 30" },
  connect("position", "Value", "distance", "p"), connect("distance", "Result", "output", "Distance"),
]);
graph("material", "StudyIvory", "Shared neutral material for matched comparisons.", [
  { op: "set_input", node: "output", pin: "Base Color", value: { x: 0.64, y: 0.69, z: 0.74 } },
  { op: "set_input", node: "output", pin: "Roughness", value: 0.46 },
  { op: "set_input", node: "output", pin: "Metallic", value: 0.08 },
]);
prototype("StudyCamera", { GameTransform: {}, GameCamera: { width: 300, height: 210, focal_distance: 650 },
  GameHRC2D: { resolution: 0.5, min_resolution: 0.5, adaptive: false, intensity: 3, exposure: 0.6,
    ambient: 0.05, fog: 0.08, bloom: 0.03, bounce_intensity: 0.2, radiance_hysteresis: 0.65, bounce_hysteresis: 0.65 } });
prototype("StudyLight", { GameTransform: {}, GameAppearance: { shape: "Circle", width: 12, height: 12 },
  GameMaterial: { scene: 0xffffffff, albedo: 0xffffffff, emission_color: 0xffffffff, emission_intensity: 0.45, emission_multiplier: 1 } });
for (const name of ["ModelSphere", "AnalyticalSphere"])
  prototype(name, { GameTransform: {}, GameOrientation: {}, GameVisualTransform: { scale: 1, pitch: 0.45, roll: -0.12 },
    GameGeometry3D: { width: name === "ModelSphere" ? 4 : 60, height: name === "ModelSphere" ? 4 : 60, depth: name === "ModelSphere" ? 4 : 60 }, GameSdfSurface3D: { enabled: true },
    GameShader: { shape: name, material: "StudyIvory" }, GameMaterial: { absorption: 1, edge_light: 1, emission_multiplier: 1 } });
if (!exists("scenes", "ModelPrimitiveStudy")) call("buildScene", { kind: "scene", name: "ModelPrimitiveStudy", create: true, operations: [
  { op: "add_entity", name: "Camera", prototype: "StudyCamera" },
  { op: "add_entity", name: "Key light", prototype: "StudyLight", components: { GameTransform: { x: 0, y: -70 } } },
  { op: "add_entity", name: "Model sphere", prototype: "ModelSphere", components: { GameTransform: { x: -60, y: 0 } } },
  { op: "add_entity", name: "Analytical sphere", prototype: "AnalyticalSphere", components: { GameTransform: { x: 60, y: 0 } } },
] });
call("validateAsset", { kind: "scene", name: "ModelPrimitiveStudy" });

for (const name of ["ModelSphere", "AnalyticalSphere"]) {
  const scene = `${name}Control`;
  if (!exists("scenes", scene)) call("buildScene", { kind: "scene", name: scene, create: true, operations: [
    { op: "add_entity", name: "Camera", prototype: "StudyCamera" },
    { op: "add_entity", name: "Key light", prototype: "StudyLight", components: { GameTransform: { x: 0, y: -70 } } },
    { op: "add_entity", name: "Sphere", prototype: name, components: name === "ModelSphere" ? {
      GameGeometry3D: { width: 4, height: 4, depth: 4 },
    } : {} },
  ] });
  call("validateAsset", { kind: "scene", name: scene });
}

if (process.argv.includes("--preview")) {
  for (const name of ["ModelPrimitiveStudy", "ModelSphereControl", "AnalyticalSphereControl"]) {
  const result = call("previewScene", { kind: "scene", name, width: 900, height: 630,
    frames: 0, render_frames: 2, freeze_simulation: true });
  const evidence = resolve(engine, "build/model-evidence");
  mkdirSync(evidence, { recursive: true });
  for (const item of result.content || []) if (item.type === "image")
    writeFileSync(resolve(evidence, `${name}.png`), Buffer.from(item.data, "base64"));
  writeFileSync(resolve(evidence, `${name}.json`), JSON.stringify(result.structuredContent, null, 2));
  }
}
