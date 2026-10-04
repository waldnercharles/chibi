#!/usr/bin/env node
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { call } from "./author.mjs";

const project = dirname(fileURLToPath(import.meta.url));
const v = (x, y, z) => ({ x, y, z });
const wire = (from, from_pin, to, to_pin, kind = "data") => ({ op: "connect", from, from_pin, to, to_pin, kind });
const geometry = (from, to, pin = "Geometry") => wire(from, "Geometry", to, pin);
const node = (ref, type, inputs = {}, metadata = {}) => [
  { op: "add_node", ref, type, ...metadata },
  ...Object.entries(inputs).map(([pin, value]) => ({ op: "set_input", node: ref, pin, value })),
];
function parameter(ref, name, defaults, minimum, maximum, type = "float") {
  return [
    ...node(ref, "Model Parameter", {}, { reference_name: name }),
    ...Object.entries({ Default: defaults, Minimum: minimum, Maximum: maximum }).map(([pin, value]) =>
      ({ op: "set_input", node: ref, pin, value, type })),
  ];
}
function graph(kind, name, description, operations, metadata = {}) {
  if (!existsSync(resolve(project, `content/${kind === "behavior" ? "behaviors" : "models"}/${name}.kdl`)))
    call("buildGraph", { kind, name, create: true, description, operations, ...metadata });
  call("validateAsset", { kind, name });
}

graph("model", "AdjustableBoom", "Public uniform dimensions and transforms share one distance program. Its off-center bounds follow the same parameter snapshot.", [
  ...parameter("reach", "Reach", 24, 8, 55),
  ...parameter("bend", "Bend", 0, -0.8, 0.8),
  ...parameter("offset", "Offset", v(0, 0, 0), v(-40, -10, -25), v(40, 30, 25), "ShaderVec3"),
  ...parameter("factors", "Factors", v(1, 1, 1), v(0.65, 0.65, 0.65), v(1.4, 1.4, 1.4), "ShaderVec3"),
  ...node("size", "Shader Vector3", { Y: 8, Z: 11 }), wire("reach", "Value", "size", "X"),
  ...node("solid", "Model Rounded Box", { Radius: 3 }), wire("size", "Value", "solid", "Half Size"),
  ...node("center", "Shader Vector3"), wire("reach", "Value", "center", "X"),
  ...node("shift", "Model Translate"), geometry("solid", "shift"), wire("center", "Value", "shift", "Offset"),
  ...node("part", "Model Part", {}, { reference_name: "Extending arm" }), geometry("shift", "part"),
  ...node("angles", "Shader Vector3"), wire("bend", "Value", "angles", "Z"),
  ...node("turn", "Model Rotate"), geometry("part", "turn"), wire("angles", "Value", "turn", "Angles"),
  ...node("scale", "Model Scale"), geometry("turn", "scale"), wire("factors", "Value", "scale", "Factors"),
  ...node("move", "Model Translate"), geometry("scale", "move"), wire("offset", "Value", "move", "Offset"),
  geometry("move", "output"),
]);

const expression = (ref, source) => node(ref, "Behavior Expression", {}, { source });
const elapsed = (target) => ({ op: "bind_input", node: target, pin: "elapsed", kind: "variable", variable: "elapsed" });
const clock = [
  { op: "add_variable", ref: "elapsed", name: "elapsed", type: "float", default: 0 },
  ...node("tick", "Event", {}, { reference_name: "Tick" }),
  ...expression("advance", "elapsed += dt"), elapsed("advance"),
  { op: "bind_input", node: "advance", pin: "dt", kind: "delta_time" },
  wire("tick", "Then", "advance", "Exec", "exec"),
];
graph("behavior", "ModelBoundsMotion", "Changes declared shape uniforms while rigidly rotating the entity. No model source is rebuilt.", [
  ...clock, { op: "add_self_node", ref: "self" },
  ...expression("reach", "31.5 + 23.5 * sin(elapsed * 0.9)"), elapsed("reach"),
  ...expression("bend", "0.7 * sin(elapsed * 1.3)"), elapsed("bend"),
  ...node("set_reach", "Set Shape Parameter", { Name: "Reach" }),
  ...node("set_bend", "Set Shape Parameter", { Name: "Bend" }),
  wire("self", "Self", "set_reach", "Entity"), wire("self", "Self", "set_bend", "Entity"),
  wire("reach", "Result", "set_reach", "Value"), wire("bend", "Result", "set_bend", "Value"),
  ...expression("pitch", "pitch = 0.45 + 0.25 * sin(elapsed * 0.7)"), elapsed("pitch"),
  { op: "bind_input", node: "pitch", pin: "pitch", kind: "component", component: "GameVisualTransform", field: "pitch" },
  wire("advance", "Then", "set_reach", "Exec", "exec"),
  wire("set_reach", "Then", "set_bend", "Exec", "exec"),
  wire("set_bend", "Then", "pitch", "Exec", "exec"),
], { owner_type: "ecs.entity" });
graph("behavior", "ModelBoundsCameraMotion", "Moves the existing perspective camera while model bounds and dimensions change.", [
  ...clock, ...expression("move", "x = 25 * sin(elapsed * 0.55)"), elapsed("move"),
  { op: "bind_input", node: "move", pin: "x", kind: "component", component: "GameTransform", field: "x" },
  wire("advance", "Then", "move", "Exec", "exec"),
], { owner_type: "ecs.entity" });

for (const [name, components] of [
  ["AdjustableBoom", {
    GameTransform: {}, GameOrientation: {}, GameVisualTransform: { scale: 1, pitch: 0.45, yaw: 0.25, roll: -0.15 },
    GameGeometry3D: { width: 1, height: 1, depth: 1 }, GameSdfSurface3D: { enabled: true },
    GameShader: { shape: "AdjustableBoom", material: "StudyIvory" },
    GameMaterial: { absorption: 1, edge_light: 1, emission_multiplier: 1 },
  }],
  ["BoundsMeshControl", {
    GameTransform: {}, GameOrientation: {}, GameVisualTransform: { scale: 1, pitch: 0.45, yaw: 0.25, roll: -0.15 },
    GameGeometry3D: { width: 36, height: 24, depth: 40, elevation: 12 }, GameMeshSurface2D: { enabled: true },
    GameShader: { material: "StudyIvory" }, GameMaterial: { absorption: 1, edge_light: 1, emission_multiplier: 1 },
  }],
]) if (!existsSync(resolve(project, `content/prototypes/${name}.kdl`)))
  call("buildPrototype", { kind: "prototype", name, create: true,
    operations: Object.entries(components).map(([component, fields]) => ({ op: "add_component", component, fields })) });

if (!existsSync(resolve(project, "content/scenes/ModelBoundsStudy.kdl")))
  call("buildScene", { kind: "scene", name: "ModelBoundsStudy", create: true, operations: [
    { op: "add_entity", name: "Camera", prototype: "StudyCamera", behaviors: ["ModelBoundsCameraMotion"],
      components: { GameCamera: { width: 610, height: 370 }, GameHRC2D: { surface_relief: 0, ambient: 0.12 } } },
    { op: "add_entity", name: "Key light", prototype: "StudyLight", components: { GameTransform: { x: -245, y: -105 } } },
    { op: "add_entity", name: "Fill light", prototype: "StudyLight", components: { GameTransform: { x: 245, y: 105 } } },
    { op: "add_entity", name: "Changing reach and bend", prototype: "AdjustableBoom", behaviors: ["ModelBoundsMotion"],
      components: { GameTransform: { x: -155, y: -60 } } },
    { op: "add_entity", name: "Minimum dimensions", prototype: "AdjustableBoom", components: {
      GameTransform: { x: -125, y: 90 },
      GameShader: { parameters: "Reach 8\nBend -0.8\n(array)Offset -40 -10 -25\n(array)Factors 0.65 0.65 0.65\n" },
    } },
    { op: "add_entity", name: "Maximum dimensions", prototype: "AdjustableBoom", components: {
      GameTransform: { x: 85, y: 45 },
      GameShader: { parameters: "Reach 55\nBend 0.8\n(array)Offset -40 30 25\n(array)Factors 1.4 1.4 1.4\n" },
    } },
    { op: "add_entity", name: "Independent mesh overlap", prototype: "BoundsMeshControl", components: { GameTransform: { x: 95, y: 55 } } },
  ] });
call("validateAsset", { kind: "scene", name: "ModelBoundsStudy" });
if (process.argv.includes("--preview")) {
  const evidence = resolve(project, "../../build/model-evidence");
  mkdirSync(evidence, { recursive: true });
  for (const frames of [0, 90, 180, 270]) {
    const result = call("previewScene", { kind: "scene", name: "ModelBoundsStudy", width: 1220, height: 740,
      frames, render_frames: 2, freeze_simulation: true });
    const stem = `ModelBoundsStudy-${String(frames).padStart(3, "0")}`;
    for (const item of result.content || []) if (item.type === "image")
      writeFileSync(resolve(evidence, `${stem}.png`), Buffer.from(item.data, "base64"));
    writeFileSync(resolve(evidence, `${stem}.json`), JSON.stringify(result.structuredContent, null, 2));
  }
}
