#!/usr/bin/env node
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { call } from "./author.mjs";

const project = dirname(fileURLToPath(import.meta.url));
const v = (x, y, z) => ({ x, y, z });
const wire = (from, from_pin, to, to_pin) => ({ op: "connect", from, from_pin, to, to_pin, kind: "data" });
const geometry = (from, to, pin = "Geometry") => wire(from, "Geometry", to, pin);
function node(ref, type, inputs = {}, metadata = {}) {
  return [{ op: "add_node", ref, type: `Model ${type}`, ...metadata }, ...Object.entries(inputs).map(([pin, value]) =>
    typeof value === "string" && ["Geometry", "A", "B"].includes(pin)
      ? geometry(value, ref, pin) : { op: "set_input", node: ref, pin, value })];
}
function parameter(ref, name, value, min, max, type = "float") {
  return [
    ...node(ref, "Parameter", {}, { reference_name: name }),
    ...Object.entries({ Default: value, Minimum: min, Maximum: max }).map(([pin, value]) =>
      ({ op: "set_input", node: ref, pin, type, value })),
  ];
}
function model(name, description, operations, output) {
  if (!existsSync(resolve(project, `content/models/${name}.kdl`)))
    call("buildGraph", { kind: "model", name, create: true, description, operations: [...operations, geometry(output, "output")] });
  call("validateAsset", { kind: "model", name });
}

model("EngineModule", "A reusable engine body with a public radius and independently named shell and collar.", [
  ...parameter("radius", "Radius", 14, 9, 24),
  ...node("body", "Cylinder", { "Half Height": 16 }), wire("radius", "Value", "body", "Radius"),
  ...node("bore", "Cylinder", { Radius: 8, "Half Height": 22 }),
  ...node("cut", "Difference", { A: "body", B: "bore" }),
  ...node("shell", "Part", { Geometry: "cut" }, { reference_name: "Engine shell" }),
  ...node("ring", "Torus", { "Minor Radius": 2.5 }), wire("radius", "Value", "ring", "Major Radius"),
  ...node("rim_offset", "Translate", { Geometry: "ring", Offset: v(0, 16, 0) }),
  ...node("collar", "Part", { Geometry: "rim_offset" }, { reference_name: "Front collar" }),
  ...node("engine", "Union", { A: "shell", B: "collar" }),
], "engine");

const placementFunction = [
  { op: "add_graph", name: "Offset Part", kind: "function", pure: true, ref: "place" },
  { op: "add_pin", node: "place.entry", side: "output", name: "Geometry", kind: "data", type: "handle", type_name: "Geometry3D" },
  { op: "add_pin", node: "place.entry", side: "output", name: "Offset", kind: "data", type: "ShaderVec3" },
  { op: "add_pin", node: "place.return", side: "input", name: "Geometry", kind: "data", type: "handle", type_name: "Geometry3D" },
  ...node("placement", "Translate"),
  wire("place.entry", "Geometry", "placement", "Geometry"), wire("place.entry", "Offset", "placement", "Offset"),
  geometry("placement", "place.return"),
  { op: "select_graph", graph: 0 },
];
model("EngineVariants", "Three references retain shared defaults or use explicit local radius overrides. Placement is a geometry function.", [
  ...placementFunction,
  ...node("small", "Instance", { Radius: 10 }, { reference_asset: "EngineModule" }),
  ...node("standard", "Instance", {}, { reference_asset: "EngineModule" }),
  ...node("large", "Instance", { Radius: 20 }, { reference_asset: "EngineModule" }),
  { op: "add_node", ref: "left", type: "Call Pure Function", reference_name: "Offset Part" },
  geometry("small", "left"), { op: "set_input", node: "left", pin: "Offset", value: v(-48, 0, 0) },
  { op: "add_node", ref: "right", type: "Call Pure Function", reference_name: "Offset Part" },
  geometry("large", "right"), { op: "set_input", node: "right", pin: "Offset", value: v(54, 0, 0) },
  ...node("pair", "Union", { A: "left", B: "standard" }),
  ...node("assembly", "Union", { A: "pair", B: "right" }),
], "assembly");
model("EngineRadialArray", "Six bounded references, each with a stable array-index path, rotate around local Y.", [
  ...node("module", "Instance", { Radius: 10 }, { reference_asset: "EngineModule" }),
  ...node("position", "Translate", { Geometry: "module", Offset: v(40, 0, 0) }),
  ...node("repeat", "Radial Array", { Geometry: "position", Count: 6, "Angle Step": Math.PI / 3, Axis: "Y" }),
], "repeat");

model("SharedShelfPanel", "A reusable panel exposes typed vector dimensions within declared ranges.", [
  ...parameter("size", "Half Size", v(45, 2, 12), v(5, 1, 4), v(60, 6, 20), "ShaderVec3"),
  ...node("panel", "Box"), wire("size", "Value", "panel", "Half Size"),
  ...node("part", "Part", { Geometry: "panel" }, { reference_name: "Shelf panel" }),
], "part");
model("ShelfFixture", "An unrelated prop uses shared panels and bounded linear arrays; no gameplay systems are required.", [
  ...node("panel", "Instance", {}, { reference_asset: "SharedShelfPanel" }),
  ...node("level", "Translate", { Geometry: "panel", Offset: v(0, 12, 0) }),
  ...node("shelves", "Linear Array", { Geometry: "level", Count: 3, Step: v(0, 20, 0) }),
  ...node("post", "Box", { "Half Size": v(3, 32, 3) }),
  ...node("named_post", "Part", { Geometry: "post" }, { reference_name: "Corner post" }),
  ...node("corner", "Translate", { Geometry: "named_post", Offset: v(-45, 32, -12) }),
  ...node("row", "Linear Array", { Geometry: "corner", Count: 2, Step: v(90, 0, 0) }),
  ...node("posts", "Linear Array", { Geometry: "row", Count: 2, Step: v(0, 0, 24) }),
  ...node("shelf", "Union", { A: "shelves", B: "posts" }),
  ...node("centered", "Translate", { Geometry: "shelf", Offset: v(0, -32, 0) }),
], "centered");

for (const name of ["EngineVariants", "EngineRadialArray", "ShelfFixture"])
  if (!existsSync(resolve(project, `content/prototypes/${name}.kdl`)))
    call("buildPrototype", { kind: "prototype", name, create: true, operations: Object.entries({
      GameTransform: {}, GameOrientation: {}, GameVisualTransform: { scale: 1, pitch: name === "ShelfFixture" ? -0.95 : 0.45, yaw: 0.3, roll: -0.1 },
      GameGeometry3D: { width: 1, height: 1, depth: 1 }, GameSdfSurface3D: { enabled: true },
      GameShader: { shape: name, material: "StudyIvory" }, GameMaterial: { absorption: 1, edge_light: 1, emission_multiplier: 1 },
    }).map(([component, fields]) => ({ op: "add_component", component, fields })) });
if (!existsSync(resolve(project, "content/scenes/ModelReuseStudy.kdl")))
  call("buildScene", { kind: "scene", name: "ModelReuseStudy", create: true, operations: [
    { op: "add_entity", name: "Camera", prototype: "StudyCamera", components: { GameCamera: { width: 600, height: 410 }, GameHRC2D: { surface_relief: 0 } } },
    { op: "add_entity", name: "Key light", prototype: "StudyLight", components: { GameTransform: { x: -240, y: -90 } } },
    { op: "add_entity", name: "Fill light", prototype: "StudyLight", components: { GameTransform: { x: 250, y: 55 }, GameMaterial: { emission_color: 0x9dd4ffff } } },
    { op: "add_entity", name: "Shared module variants", prototype: "EngineVariants", components: { GameTransform: { x: -70, y: -105 } } },
    { op: "add_entity", name: "Radial module array", prototype: "EngineRadialArray", components: { GameTransform: { x: -125, y: 70 } } },
    { op: "add_entity", name: "Shelf with linear arrays", prototype: "ShelfFixture", components: { GameTransform: { x: 140, y: 40 } } },
  ] });
call("validateAsset", { kind: "scene", name: "ModelReuseStudy" });
if (process.argv.includes("--preview")) {
  const evidence = resolve(project, "../../build/model-evidence");
  mkdirSync(evidence, { recursive: true });
  const result = call("previewScene", { kind: "scene", name: "ModelReuseStudy", width: 1200, height: 820,
    frames: 0, render_frames: 2, freeze_simulation: true });
  for (const item of result.content || []) if (item.type === "image")
    writeFileSync(resolve(evidence, "ModelReuseStudy.png"), Buffer.from(item.data, "base64"));
  writeFileSync(resolve(evidence, "ModelReuseStudy.json"), JSON.stringify(result.structuredContent, null, 2));
}
