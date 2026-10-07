#!/usr/bin/env node
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { call } from "./author.mjs";

const project = dirname(fileURLToPath(import.meta.url));
const v = (x, y, z) => ({ x, y, z });
const wire = (from, from_pin, to, to_pin) => ({ op: "connect", from, from_pin, to, to_pin, kind: "data" });
const geo = (from, to, pin = "Geometry") => wire(from, "Geometry", to, pin);
const node = (ref, type, inputs = {}, metadata = {}) => [
  { op: "add_node", ref, type, ...metadata },
  ...Object.entries(inputs).map(([pin, value]) => ({ op: "set_input", node: ref, pin, value,
    ...(type.startsWith("Shader") && value?.z !== undefined ? { type: "ShaderVec3" } : {}) })),
];
function graph(kind, name, description, operations) {
  if (!existsSync(resolve(project, `content/${kind}s/${name}.${["behavior", "library", "system"].includes(kind) ? "code" : "kdl"}`)))
    call("buildGraph", { kind, name, create: true, description, operations });
  call("validateAsset", { kind, name });
}
for (const [name, color, metallic, roughness, emission] of [
  ["ModelBluePaint", v(0.04, 0.28, 0.64), 0.05, 0.4],
  ["ModelOrangePaint", v(0.8, 0.15, 0.025), 0.05, 0.4],
  ["ModelBrass", v(0.72, 0.45, 0.12), 0.85, 0.23],
  ["ModelSignal", v(0.08, 0.7, 0.32), 0, 0.32, v(0.1, 0.8, 0.35)],
]) graph("material", name, "Part material for the shared material ownership study.", [
  { op: "set_input", node: "output", pin: "Base Color", value: color },
  { op: "set_input", node: "output", pin: "Metallic", value: metallic },
  { op: "set_input", node: "output", pin: "Roughness", value: roughness },
  ...(emission ? [{ op: "set_input", node: "output", pin: "Emission", value: emission }] : []),
]);
graph("material", "ModelPartBands", "Bands use named-Part coordinates and remain attached through parent transforms and reuse.", [
  ...node("position", "Shader PartPosition"), ...node("x", "Shader X"), wire("position", "Value", "x", "Value"),
  ...node("wave", "Behavior Expression", {}, { source: "0.5 + 0.5 * sin(x * 0.55)" }), wire("x", "Value", "wave", "x"),
  ...node("colors", "Shader Mix"),
  wire("wave", "Result", "colors", "Alpha"), wire("colors", "Value", "output", "Base Color"),
  ...node("dark", "Shader Vector3", { X: 0.06, Y: 0.16, Z: 0.36 }),
  ...node("light", "Shader Vector3", { X: 0.7, Y: 0.83, Z: 0.95 }),
  wire("dark", "Value", "colors", "A"), wire("light", "Value", "colors", "B"),
  { op: "set_input", node: "output", pin: "Roughness", value: 0.45 },
]);
const slot = (ref, name, material) => node(ref, "Model Material Slot", {}, { reference_name: name, reference_asset: material });
const part = (ref, name, geometry, material) => [
  ...node(ref, "Model Part", {}, { reference_name: name }), geo(geometry, ref), wire(material, "Slot", ref, "Material"),
];
graph("model", "PaintedModule", "Painted body, metal insert and emissive cap. A cylindrical cut inherits the painted left operand's material; a smooth join uses its dominant operand.", [
  ...slot("body_slot", "Body", "ModelBluePaint"), ...slot("insert_slot", "Insert", "ModelBrass"), ...slot("signal_slot", "Signal", "ModelSignal"),
  ...node("box", "Model Rounded Box", { "Half Size": v(26, 15, 19), Radius: 3 }),
  ...part("body", "Painted body", "box", "body_slot"),
  ...node("hole", "Model Cylinder", { Radius: 8, "Half Height": 22 }),
  ...node("hole_move", "Model Translate", { Offset: v(-10, 0, 0) }), geo("hole", "hole_move"),
  ...node("cut", "Model Difference"), geo("body", "cut", "A"), geo("hole_move", "cut", "B"),
  ...node("insert_shape", "Model Sphere", { Radius: 13 }),
  ...part("insert", "Metal insert", "insert_shape", "insert_slot"),
  ...node("insert_move", "Model Translate", { Offset: v(23, 6, 0) }), geo("insert", "insert_move"),
  ...node("join", "Model Smooth Union", { Smoothness: 5 }), geo("cut", "join", "A"), geo("insert_move", "join", "B"),
  ...node("cap_shape", "Model Cylinder", { Radius: 5, "Half Height": 4 }),
  ...part("cap", "Signal cap", "cap_shape", "signal_slot"),
  ...node("cap_move", "Model Translate", { Offset: v(5, 17, 9) }), geo("cap", "cap_move"),
  ...node("all", "Model Union"), geo("join", "all", "A"), geo("cap_move", "all", "B"), geo("all", "output"),
]);
graph("model", "PaintedVariants", "Two references reuse one model source, with independently overridden body slots.", [
  ...node("orange", "Model Instance", { Body: "ModelOrangePaint" }, { reference_asset: "PaintedModule" }),
  ...node("orange_move", "Model Translate", { Offset: v(-48, 0, 0) }), geo("orange", "orange_move"),
  ...node("bands", "Model Instance", { Body: "ModelPartBands" }, { reference_asset: "PaintedModule" }),
  ...node("bands_turn", "Model Rotate", { Angles: v(0, 0.4, 0.2) }), geo("bands", "bands_turn"),
  ...node("bands_move", "Model Translate", { Offset: v(48, 0, 0) }), geo("bands_turn", "bands_move"),
  ...node("union", "Model Union"), geo("orange_move", "union", "A"), geo("bands_move", "union", "B"), geo("union", "output"),
]);
for (const name of ["PaintedModule", "PaintedVariants"]) {
  if (!existsSync(resolve(project, `content/prototypes/${name}.kdl`)))
    call("buildPrototype", { kind: "prototype", name, create: true, operations: Object.entries({
      GameTransform: {}, GameOrientation: {}, GameVisualTransform: { scale: 1, pitch: -0.15, yaw: 0.12, roll: 0.45 },
      GameGeometry3D: { width: 1, height: 1, depth: 1 }, GameSdfSurface3D: { enabled: true },
      GameShader: { shape: name, material: "StudyIvory" }, GameMaterial: { absorption: 1, edge_light: 1, emission_multiplier: 1 },
    }).map(([component, fields]) => ({ op: "add_component", component, fields })) });
}
if (!existsSync(resolve(project, "content/scenes/ModelMaterialStudy.kdl")))
  call("buildScene", { kind: "scene", name: "ModelMaterialStudy", create: true, operations: [
    { op: "add_entity", name: "Camera", prototype: "StudyCamera", components: { GameCamera: { width: 420, height: 270 },
      GameHRC2D: { surface_relief: 0, ambient: 0.18, bloom: 0.015 } } },
    { op: "add_entity", name: "Key", prototype: "StudyLight", components: { GameTransform: { x: -170, y: -75 } } },
    { op: "add_entity", name: "Fill", prototype: "StudyLight", components: { GameTransform: { x: 170, y: 75 } } },
    { op: "add_entity", name: "Original component", prototype: "PaintedModule", components: { GameTransform: { x: 0, y: -65 } } },
    { op: "add_entity", name: "Slot overrides and transformed bands", prototype: "PaintedVariants", components: { GameTransform: { x: 0, y: 50 } } },
  ] });
call("validateAsset", { kind: "scene", name: "ModelMaterialStudy" });
if (process.argv.includes("--preview")) {
  const evidence = resolve(project, "../../build/model-evidence");
  mkdirSync(evidence, { recursive: true });
  const result = call("previewScene", { kind: "scene", name: "ModelMaterialStudy", width: 1260, height: 810, frames: 0, render_frames: 2, freeze_simulation: true });
  for (const item of result.content || []) if (item.type === "image")
    writeFileSync(resolve(evidence, "ModelMaterialStudy.png"), Buffer.from(item.data, "base64"));
  writeFileSync(resolve(evidence, "ModelMaterialStudy.json"), JSON.stringify(result.structuredContent, null, 2));
}
