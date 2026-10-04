#!/usr/bin/env node
// Publish independent optimization fixtures through the public authoring API.
import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync, readFileSync, readdirSync, mkdirSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const project = dirname(fileURLToPath(import.meta.url));
const engine = resolve(project, "../..");
const controller = process.env.CHIBI_CTL || resolve(engine, "build/chibi_ctl");
const evidence = resolve(engine, "build/sdf-optimization-evidence");
mkdirSync(evidence, { recursive: true });
function files(directory) {
  return readdirSync(directory, { withFileTypes: true }).flatMap(entry => {
    const path = resolve(directory, entry.name);
    return entry.isDirectory() ? files(path) : path.endsWith(".kdl") ? [path] : [];
  });
}
function digest(path) { return createHash("sha256").update(readFileSync(path)).digest("hex"); }
const preservation = resolve(evidence, "authored-preservation.json");
const baseline = existsSync(preservation) ? JSON.parse(readFileSync(preservation)) :
  Object.fromEntries(files(resolve(project, "content")).map(path => [path, digest(path)]));
writeFileSync(preservation, JSON.stringify(baseline, null, 2) + "\n");
function call(name, args) {
  const run = spawnSync(process.execPath, [resolve(engine, "tools/mcp/server.mjs")], {
    cwd: engine, env: { ...process.env, CHIBI_ROOT: project, CHIBI_CTL: controller },
    input: JSON.stringify({ jsonrpc: "2.0", id: 1, method: "tools/call", params: { name, arguments: args } }) + "\n",
    encoding: "utf8", maxBuffer: 32 * 1024 * 1024,
  });
  if (run.status !== 0) throw Error(run.stderr || run.stdout);
  const response = JSON.parse(run.stdout.trim());
  if (response.error || response.result?.isError) throw Error(JSON.stringify(response));
  console.log(name, args.name || "");
  return response.result.structuredContent.result;
}
const status = call("getProjectStatus", {});
if (resolve(status.project_root) !== project) throw Error("Unexpected authoring project root");
function wire(from, to, pin, output = "Result") {
  return { op: "connect", from, from_pin: output, to, to_pin: pin, kind: "data" };
}
function shape(name, description, source) {
  if (!existsSync(resolve(project, "content/shapes", name + ".kdl"))) {
    call("buildGraph", { kind: "shape", name, create: true, description, operations: [
      { op: "set_bounds", value: [140, 140] },
      { op: "add_node", ref: "position", type: "Shader ObjectPosition" },
      { op: "add_node", ref: "distance", type: "Behavior Expression", source },
      wire("position", "distance", "position", "Value"), wire("distance", "output", "Distance"),
    ] });
  }
  call("validateAsset", { kind: "shape", name });
  const graph = call("inspectGraph", { kind: "shape", name });
  writeFileSync(resolve(evidence, name + "-graph.json"), JSON.stringify(graph, null, 2) + "\n");
}
function prototype(name, shapeName, bounds) {
  if (!existsSync(resolve(project, "content/prototypes", name + ".kdl"))) {
    call("buildPrototype", { kind: "prototype", name, create: true, operations: Object.entries({
      GameTransform: {}, GameOrientation: {}, GameVisualTransform: { scale: 1 },
      GameGeometry3D: bounds, GameSdfSurface3D: { enabled: true },
      GameShader: { shape: shapeName, material: "SdfIvory" },
      GameMaterial: { absorption: 1, edge_light: 1, emission_multiplier: 1 },
    }).map(([component, fields]) => ({ op: "add_component", component, fields })) });
  }
  call("validateAsset", { kind: "prototype", name });
}
function entity(name, prototype, x, y, components = {}, behaviors = []) {
  return { op: "add_entity", name, prototype, behaviors,
    components: { GameTransform: { x, y }, ...components } };
}
const lights = [
  ["West cyan", -260, 0, 0x38d9edff], ["East amber", 260, 0, 0xffac54ff],
  ["North rose", 0, -160, 0xec749fff], ["South ivory", 0, 160, 0xcddcffff],
].map(([name, x, y, color]) => entity(name, "SdfLight", x, y, { GameMaterial: { emission_color: color } }));
function scene(name, entities, camera = {}) {
  let asset;
  if (!existsSync(resolve(project, "content/scenes", name + ".kdl"))) {
    call("buildScene", { kind: "scene", name, create: true, operations: [
      entity("Camera", "SdfCamera", 0, 0, { GameCamera: { width: 620, height: 400, ...camera },
        GameHRC2D: { ambient: 0.08, fog: 0.02, bounce_intensity: 0.12 } }),
      ...lights, ...entities.slice(0, 100),
    ] });
  }
  asset = call("readAsset", { kind: "scene", name });
  const present = new Set(asset.document.entities.map(item => item.name));
  const remaining = entities.filter(item => !present.has(item.name));
  for (let offset = 0; offset < remaining.length; offset += 120) {
    call("buildScene", { kind: "scene", name, expected_hash: asset.hash, operations: remaining.slice(offset, offset + 120) });
    asset = call("readAsset", { kind: "scene", name });
  }
  call("validateAsset", { kind: "scene", name });
}

shape("SdfQualityThinRing", "Thin torus: resolves a real opening at several projected sizes.",
  "length(vec2(length(position.xz) - 27, position.y)) - 2");
shape("SdfQualityNarrowCut", "Sphere minus a narrow through-cylinder: silhouette and depth test.",
  "max(length(position) - 30, -(length(position.xz) - 3.5))");
shape("SdfHorizonCanopy", "Conservative ellipsoid distance for a smooth continuous height canopy.",
  "(length(position / vec3(110, 28, 65)) - 1) * 28");
prototype("SdfQualityThinRing", "SdfQualityThinRing", { width: 64, height: 12, depth: 64 });
prototype("SdfQualityNarrowCut", "SdfQualityNarrowCut", { width: 64, height: 64, depth: 64 });
prototype("SdfHorizonCanopy", "SdfHorizonCanopy", { width: 224, height: 60, depth: 134 });
if (!existsSync(resolve(project, "content/behaviors/SdfQualityScaleSweep.kdl"))) {
  call("buildGraph", { kind: "behavior", name: "SdfQualityScaleSweep", create: true, owner_type: "ecs.entity",
    description: "Changes projected bounds across a graph LOD cutoff; geometry remains deterministic.", operations: [
      { op: "add_variable", ref: "elapsed", name: "elapsed", type: "float", default: 0 },
      { op: "add_node", ref: "tick", type: "Event", reference_name: "Tick" },
      { op: "add_node", ref: "advance", type: "Behavior Expression", source: "elapsed += dt" },
      { op: "bind_input", node: "advance", pin: "elapsed", kind: "variable", variable: "elapsed" },
      { op: "bind_input", node: "advance", pin: "dt", kind: "delta_time" },
      { op: "add_node", ref: "scale", type: "Behavior Expression", source: "scale = 0.23 + 0.2 * sin(elapsed * 1.4)" },
      { op: "bind_input", node: "scale", pin: "elapsed", kind: "variable", variable: "elapsed" },
      { op: "bind_input", node: "scale", pin: "scale", kind: "component", component: "GameVisualTransform", field: "scale" },
      { op: "connect", from: "tick", from_pin: "Then", to: "advance", to_pin: "Exec", kind: "exec" },
      { op: "connect", from: "advance", from_pin: "Then", to: "scale", to_pin: "Exec", kind: "exec" },
    ] });
}
call("validateAsset", { kind: "behavior", name: "SdfQualityScaleSweep" });
scene("SdfQualityStudy", [
  entity("Large near sphere", "SdfSphere", -165, -85, { GameVisualTransform: { scale: 1.3, pitch: 0 }, GameGeometry3D: { elevation: 180 }, GameShader: { material: "SdfCyan" } }),
  entity("Large far sphere", "SdfSphere", -55, -85, { GameVisualTransform: { scale: 1.3, pitch: 0 }, GameGeometry3D: { elevation: -220 }, GameShader: { material: "SdfAmber" } }),
  ...[.5, .15, .045, .015].map((scale, i) => entity(`Small sphere ${i + 1}`, "SdfSphere", 45 + i * 45, -85,
    { GameVisualTransform: { scale, pitch: 0 }, GameShader: { material: "SdfRose" } })),
  entity("Thin ring", "SdfQualityThinRing", -160, 25, { GameVisualTransform: { scale: 1, pitch: .3 }, GameShader: { material: "SdfAmber" } }),
  entity("Small thin ring", "SdfQualityThinRing", -80, 25, { GameVisualTransform: { scale: .3, pitch: .3 } }),
  entity("Narrow cut", "SdfQualityNarrowCut", 15, 25, { GameVisualTransform: { pitch: .45 }, GameShader: { material: "SdfCyan" } }),
  entity("Moving cut", "SdfQualityNarrowCut", 130, 25, {}, ["SdfRigidSweep"]),
  entity("Rear triangle", "SdfComparisonMesh", -25, 115, { GameGeometry3D: { width: 90, height: 8, depth: 55, elevation: -8 }, GameVisualTransform: { pitch: 0, roll: 0 } }),
  entity("Front distance sphere", "SdfSphere", 0, 115, { GameGeometry3D: { elevation: 8 }, GameVisualTransform: { scale: .7, pitch: 0 }, GameShader: { material: "SdfRose" } }),
], { focal_distance: 650 });
scene("SdfHorizonStudy", [
  entity("Continuous canopy", "SdfHorizonCanopy", -105, 0, { GameGeometry3D: { elevation: 0 }, GameShader: { material: "SdfCyan" } }),
  entity("Moving blocker", "SdfRoundedBox", -105, 0, { GameVisualTransform: { scale: .45 }, GameGeometry3D: { elevation: 30 }, GameShader: { material: "SdfRose" } }, ["SdfRigidSweep"]),
  entity("Separated west hill", "SdfSphere", 100, -55, { GameVisualTransform: { scale: .85, pitch: 0 }, GameShader: { material: "SdfAmber" } }),
  entity("Separated east hill", "SdfSphere", 155, -55, { GameVisualTransform: { scale: .85, pitch: 0 }, GameShader: { material: "SdfAmber" } }),
  entity("Low continuous receiver", "SdfComparisonMesh", 130, 65, { GameGeometry3D: { width: 115, height: 6, depth: 70, elevation: -20 }, GameVisualTransform: { pitch: 0, roll: 0 } }),
  entity("Tilted ridge", "SdfRoundedBox", 130, 65, { GameVisualTransform: { scale: .65, pitch: .4, roll: .35 }, GameGeometry3D: { elevation: 10 } }),
], { focal_distance: 0 });
scene("SdfStaticInstances1000", Array.from({ length: 1000 }, (_, i) => entity(`Static compatible ${i + 1}`, "SdfInstance",
  (i % 40 - 19.5) * 13, (Math.floor(i / 40) - 12) * 13, {
    GameVisualTransform: { scale: .12 }, GameOrientation: { radians: (i % 12) * Math.PI / 6 },
    GameMaterial: { scene: [0x5baab5ff, 0xd59c68ff, 0xbe758aff][i % 3], albedo: [0x5baab5ff, 0xd59c68ff, 0xbe758aff][i % 3] },
  })), { width: 590, height: 400, focal_distance: 0 });

// Run only after the controller includes the reflected quality component.
// Keep the static CPU-control scene loadable by the archived controller.
if (process.argv.includes("--quality-fields")) {
  const name = "SdfOptimizationCamera";
  if (!existsSync(resolve(project, "content/prototypes", name + ".kdl"))) {
    call("buildPrototype", { kind: "prototype", name, create: true, operations: Object.entries({
      GameTransform: {}, GameCamera: { width: 620, height: 400, focal_distance: 650 },
      GameHRC2D: { resolution: .5, min_resolution: .5, adaptive: false, intensity: 3, exposure: .6,
        ambient: .05, fog: .08, bloom: .03, bounce_intensity: .2, radiance_hysteresis: .65, bounce_hysteresis: .65 },
      GameSurfaceQuality: { resolution: "Supersampled", sdf_pixel_error: 0, sdf_normal_mode: "Central" },
    }).map(([component, fields]) => ({ op: "add_component", component, fields })) });
  }
  call("validateAsset", { kind: "prototype", name });
  for (const sceneName of ["SdfQualityStudy", "SdfHorizonStudy"]) {
    const asset = call("readAsset", { kind: "scene", name: sceneName });
    const camera = asset.document.entities.find(item => item.name === "Camera");
    if (camera.prototype !== name) {
      call("buildScene", { kind: "scene", name: sceneName, expected_hash: asset.hash,
        operations: [{ op: "set_entity_prototype", entity: "Camera", prototype: name }] });
    }
    call("validateAsset", { kind: "scene", name: sceneName });
  }
  const qualityName = "SdfQualityStudy";
  let asset = call("readAsset", { kind: "scene", name: qualityName });
  const lodEntities = [
    entity("Large detailed LOD control", "SdfQualityNarrowCut", -130, 165, { GameVisualTransform: { scale: .6 } }),
    entity("Small simplified LOD control", "SdfQualityNarrowCut", -40, 165, { GameVisualTransform: { scale: .08 } }),
    entity("Moving LOD threshold", "SdfQualityNarrowCut", 55, 165, { GameVisualTransform: { scale: .23 } }, ["SdfQualityScaleSweep"]),
  ].map(item => ({ ...item, components: { ...item.components,
    GameSdfSurface3D: { enabled: true, low_detail_shape: "SdfSphere", low_detail_pixels: 24 } } }));
  const present = new Set(asset.document.entities.map(item => item.name));
  const remaining = lodEntities.filter(item => !present.has(item.name));
  if (remaining.length) call("buildScene", { kind: "scene", name: qualityName, expected_hash: asset.hash, operations: remaining });
  call("validateAsset", { kind: "scene", name: qualityName });
}

for (const flags of [["--format"], ["--format", "--check"]]) {
  const run = spawnSync(controller, ["--project-root", project, ...flags], { cwd: engine, encoding: "utf8" });
  if (run.status !== 0) throw Error(run.stderr || run.stdout);
}
for (const [path, hash] of Object.entries(baseline)) {
  if (digest(path) !== hash) throw Error(`Existing asset changed: ${path}`);
}
console.log(`Preserved ${Object.keys(baseline).length} existing KDL assets.`);
