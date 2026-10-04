#!/usr/bin/env node
// Publish the lab through the public authoring protocol. Run once in a new project.
import { spawnSync } from "node:child_process";
import { mkdirSync, existsSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const project = dirname(fileURLToPath(import.meta.url));
const engine = resolve(project, "../..");
const controller = process.env.CHIBI_CTL || resolve(engine, "build/chibi_ctl");
const graphsOnly = process.argv.includes("--graphs-only");
const fixturesOnly = process.argv.includes("--fixtures-only");
const instancesOnly = process.argv.includes("--instances-only");
const instanceSwarmOnly = process.argv.includes("--instance-swarm-only");
for (const kind of ["shapes", "materials", "behaviors", "prototypes", "scenes", "ui"])
  mkdirSync(resolve(project, "content", kind), { recursive: true });

function call(name, args) {
  const run = spawnSync(process.execPath, [resolve(engine, "tools/mcp/server.mjs")], {
    cwd: engine, env: { ...process.env, CHIBI_ROOT: project, CHIBI_CTL: controller },
    input: JSON.stringify({ jsonrpc: "2.0", id: 1, method: "tools/call", params: { name, arguments: args } }) + "\n",
    encoding: "utf8", maxBuffer: 32 * 1024 * 1024,
  });
  if (run.status !== 0) throw Error(run.stderr);
  const response = JSON.parse(run.stdout.trim());
  if (response.error || response.result?.isError)
    throw Error(JSON.stringify(response));
  const result = response.result.structuredContent;
  console.log(`${name}: ${args.name || ""}`);
  return result.result;
}

function expr(ref, source) { return { op: "add_node", type: "Behavior Expression", ref, source }; }
function wire(from, to, to_pin, from_pin = "Result") {
  return { op: "connect", from, from_pin, to, to_pin, kind: "data" };
}
function shape(name, description, stages) {
  const timed = stages.some(([, , inputs]) => inputs.some(([, from]) => from === "time"));
  if (existsSync(resolve(project, "content/shapes", name + ".kdl"))) {
    const graph = call("inspectGraph", { kind: "shape", name });
    const unused = graph.graph.nodes.filter(node => !timed && node.type === "Shader Time");
    if (unused.length) call("buildGraph", { kind: "shape", name, expected_hash: graph.hash,
      operations: unused.map(node => ({ op: "remove_node", node: node.id })) });
    return;
  }
  const operations = [{ op: "set_bounds", value: [48, 48] },
    { op: "add_node", ref: "position", type: "Shader ObjectPosition" }];
  if (timed) operations.push({ op: "add_node", ref: "time", type: "Shader Time" });
  for (const [ref, source, inputs] of stages) {
    operations.push(expr(ref, source));
    for (const [pin, from, output] of inputs) operations.push(wire(from, ref, pin, output));
  }
  operations.push(wire(stages.at(-1)[0], "output", "Distance"));
  call("buildGraph", { kind: "shape", name, create: true, description, operations });
  call("validateAsset", { kind: "shape", name });
}
const position = ["p", "position", "Value"];
const boxStages = [
  ["box_offset", "abs(p) - vec3(27, 23, 27)", [position]],
  ["box_distance", "length(max(offset, 0)) + min(max(max(offset.x, offset.y), offset.z), 0) - 6", [["offset", "box_offset", "Result"]]],
];
function material(name, color) {
  if (existsSync(resolve(project, "content/materials", name + ".kdl"))) return;
  call("buildGraph", { kind: "material", name, create: true,
    description: "Neutral surface uses the renderer's field normal. Color and roughness are authored here.",
    operations: [
      { op: "set_input", node: "output", pin: "Base Color", value: { x: color[0], y: color[1], z: color[2] } },
      { op: "set_input", node: "output", pin: "Roughness", value: 0.46 },
      { op: "set_input", node: "output", pin: "Metallic", value: 0.08 },
    ] });
  call("validateAsset", { kind: "material", name });
}

if (!fixturesOnly && !instancesOnly && !instanceSwarmOnly) {
  shape("SdfSphere", "Sphere: signed distance in local authored units.", [
    ["sphere_distance", "length(p) - 30", [position]],
  ]);
  shape("SdfBox", "Exact box for a matched comparison with the engine's triangle box.", [
    ["box_offset", "abs(p) - vec3(30, 22, 30)", [position]],
    ["box_distance", "length(max(offset, 0)) + min(max(max(offset.x, offset.y), offset.z), 0)", [["offset", "box_offset", "Result"]]],
  ]);
  shape("SdfRoundedBox", "Rounded box: name the offset before computing its distance.", boxStages);
  shape("SdfTorus", "Ring around the local Y axis. The opening is real geometry.", [
    ["torus_distance", "length(vec2(length(p.xz) - 27, p.y)) - 10", [position]],
  ]);
  shape("SdfBoredBox", "Difference: rounded box minus a through-cylinder along local Y.", [
    ...boxStages,
    ["bore_distance", "length(p.xz) - 14", [position]],
    ["cut_distance", "max(body, -bore)", [["body", "box_distance", "Result"], ["bore", "bore_distance", "Result"]]],
  ]);
  shape("SdfIntersection", "Intersection of a sphere and a thin slab.", [
    ["sphere_distance", "length(p) - 33", [position]],
    ["slab_offset", "abs(p) - vec3(36, 13, 24)", [position]],
    ["slab_distance", "length(max(offset, 0)) + min(max(max(offset.x, offset.y), offset.z), 0)", [["offset", "slab_offset", "Result"]]],
    ["intersection_distance", "max(sphere, slab)", [["sphere", "sphere_distance", "Result"], ["slab", "slab_distance", "Result"]]],
  ]);
  shape("SdfSmoothUnion", "Smooth union of two offset spheres with a 12-unit blend.", [
    ["left_distance", "length(p - vec3(-15, 0, 0)) - 23", [position]],
    ["right_distance", "length(p - vec3(15, 0, 0)) - 23", [position]],
    ["blend", "clamp(0.5 + 0.5 * (right - left) / 12, 0, 1)", [["left", "left_distance", "Result"], ["right", "right_distance", "Result"]]],
    ["smooth_distance", "mix(right, left, blend) - 12 * blend * (1 - blend)", [["left", "left_distance", "Result"], ["right", "right_distance", "Result"], ["blend", "blend", "Result"]]],
  ]);
  shape("SdfBreathingSphere", "The field's radius changes with simulation time; it is not a rigid transform or a rebuilt mesh.", [
    ["radius", "27 + 8 * sin(time * 1.5)", [["time", "time", "Value"]]],
    ["sphere_distance", "length(p) - radius", [position, ["radius", "radius", "Result"]]],
  ]);
  for (const [name, color] of [
    ["SdfIvory", [0.64, 0.69, 0.74]], ["SdfCyan", [0.14, 0.56, 0.62]],
    ["SdfAmber", [0.7, 0.39, 0.16]], ["SdfRose", [0.58, 0.23, 0.34]],
  ]) material(name, color);

  const operations = [
    { op: "add_variable", ref: "elapsed", name: "elapsed", type: "float", default: 0 },
    { op: "add_node", ref: "tick", type: "Event", reference_name: "Tick" },
    expr("advance", "elapsed += dt"),
    { op: "bind_input", node: "advance", pin: "elapsed", kind: "variable", variable: "elapsed" },
    { op: "bind_input", node: "advance", pin: "dt", kind: "delta_time" },
    expr("heading", "heading = elapsed * 0.5"),
    { op: "bind_input", node: "heading", pin: "elapsed", kind: "variable", variable: "elapsed" },
    { op: "bind_input", node: "heading", pin: "heading", kind: "component", component: "GameOrientation", field: "radians" },
    expr("pitch", "pitch = 0.6 * sin(elapsed * 0.8)"),
    { op: "bind_input", node: "pitch", pin: "elapsed", kind: "variable", variable: "elapsed" },
    { op: "bind_input", node: "pitch", pin: "pitch", kind: "component", component: "GameVisualTransform", field: "pitch" },
    { op: "connect", from: "tick", from_pin: "Then", to: "advance", to_pin: "Exec", kind: "exec" },
    { op: "connect", from: "advance", from_pin: "Then", to: "heading", to_pin: "Exec", kind: "exec" },
    { op: "connect", from: "heading", from_pin: "Then", to: "pitch", to_pin: "Exec", kind: "exec" },
  ];
  if (!existsSync(resolve(project, "content/behaviors/SdfRigidSweep.kdl")))
    call("buildGraph", { kind: "behavior", name: "SdfRigidSweep", create: true,
      description: "Rigid rotation and banking only. The object's distance field remains unchanged.", owner_type: "ecs.entity", operations });
  call("validateAsset", { kind: "behavior", name: "SdfRigidSweep" });
}

if (!graphsOnly && !instancesOnly && !instanceSwarmOnly) {
  function prototype(name, components, behaviors = []) {
    if (existsSync(resolve(project, "content/prototypes", name + ".kdl"))) return;
    call("buildPrototype", { kind: "prototype", name, create: true, operations: [
      ...Object.entries(components).map(([component, fields]) => ({ op: "add_component", component, fields })),
      ...behaviors.map(behavior => ({ op: "add_behavior", behavior })),
    ] });
    call("validateAsset", { kind: "prototype", name });
  }
  prototype("SdfCamera", { GameTransform: {}, GameCamera: { width: 540, height: 330, focal_distance: 650 },
    GameHRC2D: { resolution: 0.5, min_resolution: 0.5, adaptive: false, intensity: 3, exposure: 0.6,
      ambient: 0.05, fog: 0.08, bloom: 0.03, bounce_intensity: 0.2, radiance_hysteresis: 0.65, bounce_hysteresis: 0.65 } });
  for (const shapeName of ["SdfSphere", "SdfRoundedBox", "SdfTorus", "SdfBoredBox", "SdfIntersection", "SdfSmoothUnion", "SdfBreathingSphere"])
    prototype(shapeName, { GameTransform: {}, GameOrientation: {}, GameVisualTransform: { scale: 1, pitch: 0.45, roll: -0.12 },
      GameGeometry3D: { width: 96, height: 96, depth: 96 }, GameSdfSurface3D: { enabled: true },
      GameShader: { shape: shapeName, material: "SdfIvory" }, GameMaterial: { absorption: 1, edge_light: 1, emission_multiplier: 1 } });
  prototype("SdfLight", { GameTransform: {}, GameAppearance: { shape: "Circle", width: 12, height: 12 },
    GameMaterial: { scene: 0xffffffff, albedo: 0xffffffff, emission_color: 0xffffffff, emission_intensity: 0.45, emission_multiplier: 1 } });
  const comparison = { GameTransform: {}, GameOrientation: {}, GameVisualTransform: { scale: 1, pitch: 0.5, roll: -0.15 },
    GameGeometry3D: { primitive: 0, width: 60, height: 44, depth: 60 },
    GameShader: { material: "SdfIvory" }, GameMaterial: { absorption: 1, edge_light: 1, emission_multiplier: 1 } };
  prototype("SdfComparisonMesh", { ...comparison, GameMeshSurface2D: { enabled: true } });
  prototype("SdfComparisonField", { ...comparison, GameSdfSurface3D: { enabled: true }, GameShader: { shape: "SdfBox", material: "SdfIvory" } });

  const lights = [
    { op: "add_entity", name: "Cyan light", prototype: "SdfLight", components: { GameTransform: { x: -195, y: 0 }, GameMaterial: { emission_color: 0x38d9edff } } },
    { op: "add_entity", name: "Amber light", prototype: "SdfLight", components: { GameTransform: { x: 195, y: 0 }, GameMaterial: { emission_color: 0xffac54ff } } },
    { op: "add_entity", name: "Rose light", prototype: "SdfLight", components: { GameTransform: { x: 0, y: 118 }, GameMaterial: { emission_color: 0xec749fff } } },
  ];
  function scene(name, entities, camera = {}) {
    if (existsSync(resolve(project, "content/scenes", name + ".kdl"))) return;
    call("buildScene", { kind: "scene", name, create: true, operations: [
      { op: "add_entity", name: "Camera", prototype: "SdfCamera", components: { GameCamera: camera } },
      ...lights, ...entities,
    ] });
    call("validateAsset", { kind: "scene", name });
  }
  scene("SdfShapes", ["SdfSphere", "SdfRoundedBox", "SdfTorus", "SdfBoredBox", "SdfIntersection", "SdfSmoothUnion"].map((name, i) => ({
    op: "add_entity", name, prototype: name,
    components: { GameTransform: { x: (i % 3 - 1) * 125, y: i < 3 ? -65 : 65 },
      GameShader: { material: ["SdfCyan", "SdfIvory", "SdfAmber", "SdfRose", "SdfIvory", "SdfCyan"][i] } },
  })));
  scene("SdfMotion", [
    { op: "add_entity", name: "Rigid torus", prototype: "SdfTorus", behaviors: ["SdfRigidSweep"], components: { GameTransform: { x: -110, y: 0 }, GameShader: { material: "SdfCyan" } } },
    { op: "add_entity", name: "Changing radius", prototype: "SdfBreathingSphere", components: { GameTransform: { x: 0, y: 0 }, GameShader: { material: "SdfRose" } } },
    { op: "add_entity", name: "Static radius control", prototype: "SdfSphere", components: { GameTransform: { x: 110, y: 0 }, GameShader: { material: "SdfAmber" } } },
  ]);
  scene("SdfHrcComparison", [
    { op: "add_entity", name: "Triangle box", prototype: "SdfComparisonMesh", components: { GameTransform: { x: -95, y: -60 } } },
    { op: "add_entity", name: "Distance box", prototype: "SdfComparisonField", components: { GameTransform: { x: 95, y: -60 } } },
    { op: "add_entity", name: "Rear triangle box", prototype: "SdfComparisonMesh", components: { GameTransform: { x: -12, y: 70 }, GameGeometry3D: { elevation: -12 }, GameShader: { material: "SdfAmber" } } },
    { op: "add_entity", name: "Front distance box", prototype: "SdfComparisonField", components: { GameTransform: { x: 12, y: 70 }, GameGeometry3D: { elevation: 12 }, GameShader: { material: "SdfCyan" } } },
  ]);
  for (const count of [256, 1000]) {
    const columns = count === 256 ? 16 : 40;
    const rows = Math.ceil(count / columns);
    const entities = Array.from({ length: count }, (_, i) => ({ op: "add_entity", name: `Instance ${i + 1}`, prototype: i % 3 === 0 ? "SdfTorus" : "SdfSphere", behaviors: ["SdfRigidSweep"],
      components: { GameTransform: { x: (i % columns - (columns - 1) / 2) * 13, y: (Math.floor(i / columns) - (rows - 1) / 2) * 13 },
        GameVisualTransform: { scale: 0.12 }, GameShader: { material: i % 2 ? "SdfCyan" : "SdfAmber" } } }));
    // Scene publication is bounded to 128 operations; append using its current hash.
    scene(`SdfSwarm${count}`, entities.slice(0, 100), { width: 590, height: 400, focal_distance: 0 });
    let asset = call("readAsset", { kind: "scene", name: `SdfSwarm${count}` });
    const existing = new Set(asset.document.entities.map(entity => entity.name));
    const edits = [
      ...asset.document.entities.filter(entity => entity.name.startsWith("Instance ") && !entity.behaviors?.includes("SdfRigidSweep"))
        .map(entity => ({ op: "set_entity_behaviors", entity: entity.name, behaviors: ["SdfRigidSweep"] })),
      ...entities.filter(entity => !existing.has(entity.name)),
    ];
    for (let offset = 0; offset < edits.length; offset += 120) {
      call("buildScene", { kind: "scene", name: `SdfSwarm${count}`, expected_hash: asset.hash, operations: edits.slice(offset, offset + 120) });
      asset = call("readAsset", { kind: "scene", name: `SdfSwarm${count}` });
    }
    call("validateAsset", { kind: "scene", name: `SdfSwarm${count}` });
  }
}

if ((!graphsOnly && !fixturesOnly && !instanceSwarmOnly) || instancesOnly) {
  if (!existsSync(resolve(project, "content/shapes/SdfInstanceShape.kdl"))) {
    call("buildGraph", { kind: "shape", name: "SdfInstanceShape", create: true,
      description: "Rounded body with a through-bore. Radius and Bore are named shape parameters; the crowd shares defaults, while four controls override them.",
      operations: [
        { op: "set_bounds", value: [48, 48] },
        { op: "add_node", ref: "position", type: "Shader ObjectPosition" },
        { op: "add_node", ref: "radius", type: "Shader Parameter", reference_id: 0, reference_name: "Radius" },
        { op: "set_output", node: "radius", pin: "Value", value: 21 },
        { op: "add_node", ref: "bore", type: "Shader Parameter", reference_id: 1, reference_name: "Bore" },
        { op: "set_output", node: "bore", pin: "Value", value: 7 },
        expr("offset", "abs(p) - vec3(radius * 1.1, radius * 0.6, radius * 0.75)"),
        wire("position", "offset", "p", "Value"), wire("radius", "offset", "radius", "Value"),
        expr("body", "length(max(offset, 0)) + min(max(max(offset.x, offset.y), offset.z), 0) - 3"),
        wire("offset", "body", "offset"),
        expr("opening", "length(p.xz) - bore"),
        wire("position", "opening", "p", "Value"), wire("bore", "opening", "bore", "Value"),
        expr("distance", "max(body, -opening)"),
        wire("body", "distance", "body"), wire("opening", "distance", "opening"),
        wire("distance", "output", "Distance"),
      ] });
  }
  call("validateAsset", { kind: "shape", name: "SdfInstanceShape" });
  if (!existsSync(resolve(project, "content/materials/SdfInstanceTint.kdl"))) {
    call("buildGraph", { kind: "material", name: "SdfInstanceTint", create: true,
      description: "Tint is an independent per-entity vector parameter. Only the four parameter controls use this material; the crowd uses GameMaterial fallback colors.",
      operations: [
        { op: "add_node", ref: "tint", type: "Shader Parameter", reference_name: "Tint" },
        { op: "set_output", node: "tint", pin: "Value", type: "ShaderVec3", value: { x: 0.64, y: 0.69, z: 0.74 } },
        wire("tint", "output", "Base Color", "Value"),
        { op: "set_input", node: "output", pin: "Roughness", value: 0.5 },
        { op: "set_input", node: "output", pin: "Metallic", value: 0.12 },
      ] });
  }
  call("validateAsset", { kind: "material", name: "SdfInstanceTint" });
  if (!existsSync(resolve(project, "content/prototypes/SdfInstance.kdl"))) {
    call("buildPrototype", { kind: "prototype", name: "SdfInstance", create: true, operations: [
      { op: "add_component", component: "GameTransform" },
      { op: "add_component", component: "GameOrientation" },
      { op: "add_component", component: "GameVisualTransform", fields: { scale: 0.7, pitch: 0.45, roll: -0.12 } },
      { op: "add_component", component: "GameGeometry3D", fields: { width: 84, height: 56, depth: 76 } },
      { op: "add_component", component: "GameSdfSurface3D", fields: { enabled: true } },
      { op: "add_component", component: "GameShader", fields: { shape: "SdfInstanceShape", material: "", parameters: "" } },
      { op: "add_component", component: "GameMaterial", fields: { scene: 0xa3b0bdff, albedo: 0xa3b0bdff, absorption: 1, edge_light: 1, emission_multiplier: 1 } },
    ] });
  }
  call("validateAsset", { kind: "prototype", name: "SdfInstance" });
  if (!existsSync(resolve(project, "content/scenes/SdfInstances.kdl"))) {
    const colors = [0x5baab5ff, 0xd59c68ff, 0xbe758aff, 0x89aaa1ff, 0xa3b0bdff, 0x8797c0ff];
    const crowd = Array.from({ length: 48 }, (_, i) => ({ op: "add_entity", name: `Shared instance ${i + 1}`, prototype: "SdfInstance",
      components: {
        GameTransform: { x: (i % 8 - 3.5) * 53 + Math.sin(i * 1.7) * 4, y: (Math.floor(i / 8) - 2.5) * 45 + Math.cos(i * 1.1) * 3 },
        GameOrientation: { radians: (i % 7 - 3) * 0.13 },
        GameVisualTransform: { scale: 0.5 + (i % 5) * 0.08, pitch: 0.3 + (i % 4) * 0.15, roll: (i % 3 - 1) * 0.16 },
        GameGeometry3D: { width: 72 + (i % 3) * 8, height: 48 + (i % 3) * 8, depth: 64 + (i % 3) * 8, elevation: (i % 3 - 1) * 4 },
        GameMaterial: { scene: colors[i % colors.length], albedo: colors[i % colors.length], absorption: 0.76 + (i % 5) * 0.06, edge_light: 0.8 + (i % 3) * 0.1,
          emission_color: colors[i % colors.length], emission_intensity: 0.008 + (i % 3) * 0.003 },
      } }));
    const controls = Array.from({ length: 4 }, (_, i) => ({ op: "add_entity", name: `Parameter control ${i + 1}`, prototype: "SdfInstance",
      components: { GameTransform: { x: (i - 1.5) * 72, y: 166 },
        GameVisualTransform: { scale: 0.72, pitch: 0.65, roll: 0 },
        GameShader: { material: "SdfInstanceTint", parameters: `Radius ${15 + i * 3}\nBore ${4 + i * 2}\n(array)Tint ${[0.28 + i * 0.14, 0.58 - i * 0.08, 0.66 - i * 0.11].join(" ")}\n` } } }));
    call("buildScene", { kind: "scene", name: "SdfInstances", create: true, operations: [
      { op: "add_entity", name: "Camera", prototype: "SdfCamera", components: { GameCamera: { width: 600, height: 420, focal_distance: 0 }, GameHRC2D: { ambient: 0.1, fog: 0.025, bounce_intensity: 0.16 } } },
      { op: "add_entity", name: "Cyan light", prototype: "SdfLight", components: { GameTransform: { x: -252, y: -25 }, GameMaterial: { emission_color: 0x38d9edff } } },
      { op: "add_entity", name: "Amber light", prototype: "SdfLight", components: { GameTransform: { x: 252, y: 10 }, GameMaterial: { emission_color: 0xffac54ff } } },
      { op: "add_entity", name: "Rose light", prototype: "SdfLight", components: { GameTransform: { x: 0, y: -167 }, GameMaterial: { emission_color: 0xec749fff } } },
      ...crowd.slice(0, 24),
      { op: "add_entity", name: "Interleaved triangle plinth", prototype: "SdfComparisonMesh", components: {
        GameTransform: { x: 0, y: 0 }, GameGeometry3D: { width: 110, height: 12, depth: 55, elevation: -18 },
        GameVisualTransform: { scale: 1, pitch: 0, roll: 0 }, GameShader: { material: "" },
        GameMaterial: { scene: 0x738293ff, albedo: 0x738293ff } } },
      ...crowd.slice(24), ...controls,
    ] });
  }
  call("validateAsset", { kind: "scene", name: "SdfInstances" });
}

if ((!graphsOnly && !fixturesOnly && !instancesOnly) || instanceSwarmOnly) {
  const name = "SdfInstanceSwarm1000";
  const colors = [0x5baab5ff, 0xd59c68ff, 0xbe758aff, 0x89aaa1ff, 0xa3b0bdff, 0x8797c0ff];
  const instances = Array.from({ length: 1000 }, (_, i) => ({
    op: "add_entity", name: `Compatible instance ${i + 1}`, prototype: "SdfInstance", behaviors: ["SdfRigidSweep"],
    components: {
      GameTransform: { x: (i % 40 - 19.5) * 13, y: (Math.floor(i / 40) - 12) * 13 },
      GameOrientation: { radians: (i % 12) * Math.PI / 6 },
      GameVisualTransform: { scale: 0.12 },
      GameMaterial: { scene: colors[i % colors.length], albedo: colors[i % colors.length] },
    },
  }));
  if (!existsSync(resolve(project, "content/scenes", name + ".kdl"))) {
    call("buildScene", { kind: "scene", name, create: true, operations: [
      { op: "add_entity", name: "Camera", prototype: "SdfCamera", components: { GameCamera: { width: 590, height: 400, focal_distance: 0 } } },
      { op: "add_entity", name: "Cyan light", prototype: "SdfLight", components: { GameTransform: { x: -195, y: 0 }, GameMaterial: { emission_color: 0x38d9edff } } },
      { op: "add_entity", name: "Amber light", prototype: "SdfLight", components: { GameTransform: { x: 195, y: 0 }, GameMaterial: { emission_color: 0xffac54ff } } },
      { op: "add_entity", name: "Rose light", prototype: "SdfLight", components: { GameTransform: { x: 0, y: 118 }, GameMaterial: { emission_color: 0xec749fff } } },
      ...instances.slice(0, 100),
    ] });
  }
  let asset = call("readAsset", { kind: "scene", name });
  const existing = new Set(asset.document.entities.map(entity => entity.name));
  const remaining = instances.filter(entity => !existing.has(entity.name));
  for (let offset = 0; offset < remaining.length; offset += 120) {
    call("buildScene", { kind: "scene", name, expected_hash: asset.hash, operations: remaining.slice(offset, offset + 120) });
    asset = call("readAsset", { kind: "scene", name });
  }
  call("validateAsset", { kind: "scene", name });
}
