#!/usr/bin/env node
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { call } from "./author.mjs";

const project = dirname(fileURLToPath(import.meta.url));
const evidence = resolve(project, "../../build/model-evidence");
const v = (x, y, z) => ({ x, y, z });
const connect = (from, to, to_pin = "Geometry") => ({ op: "connect", from, from_pin: "Geometry", to, to_pin, kind: "data" });
function node(ref, type, inputs = {}) {
  return [{ op: "add_node", ref, type: `Model ${type}` }, ...Object.entries(inputs).map(([pin, value]) =>
    typeof value === "string" && ["Geometry", "A", "B"].includes(pin)
      ? connect(value, ref, pin) : { op: "set_input", node: ref, pin, value })];
}
function model(name, description, operations, output) {
  if (!existsSync(resolve(project, `content/models/${name}.kdl`)))
    call("buildGraph", { kind: "model", name, create: true, description, operations: [...operations, connect(output, "output")] });
  call("validateAsset", { kind: "model", name });
  if (!existsSync(resolve(project, `content/prototypes/${name}.kdl`)))
    call("buildPrototype", { kind: "prototype", name, create: true, operations: Object.entries({
      GameTransform: {}, GameOrientation: {}, GameVisualTransform: { scale: 1, pitch: 0.38, yaw: 0.1, roll: -0.12 },
      GameGeometry3D: { width: 1, height: 1, depth: 1 }, GameSdfSurface3D: { enabled: true },
      GameShader: { shape: name, material: "StudyIvory" }, GameMaterial: { absorption: 1, edge_light: 1, emission_multiplier: 1 },
    }).map(([component, fields]) => ({ op: "add_component", component, fields })) });
  call("validateAsset", { kind: "prototype", name });
}

model("RoundedBlock", "Radius preserves the requested outer half-size.",
  node("body", "Rounded Box", { "Half Size": v(27, 22, 25), Radius: 6 }), "body");
model("CappedCylinder", "Finite cylinder along local Y.",
  node("body", "Cylinder", { Radius: 23, "Half Height": 24 }), "body");
model("Capsule", "Radius around a finite Y-axis segment.",
  node("body", "Capsule", { Radius: 16, "Half Segment": 16 }), "body");
model("Torus", "A ring around local Y with an open center.",
  node("body", "Torus", { "Major Radius": 22, "Minor Radius": 8 }), "body");
model("CrossBoredHousing", "Two nested differences cut orthogonal finite holes through a rounded block.", [
  ...node("body", "Rounded Box", { "Half Size": v(27, 23, 25), Radius: 4 }),
  ...node("bore", "Cylinder", { Radius: 11, "Half Height": 40 }),
  ...node("cross_bore", "Rotate", { Geometry: "bore", Angles: v(0, 0, Math.PI / 2) }),
  ...node("first_cut", "Difference", { A: "body", B: "bore" }),
  ...node("cross_cut", "Difference", { A: "first_cut", B: "cross_bore" }),
], "cross_cut");
model("ClippedSphere", "Sphere intersected with a thin finite slab.", [
  ...node("sphere", "Sphere", { Radius: 29 }),
  ...node("slab", "Box", { "Half Size": v(34, 12, 21) }),
  ...node("intersection", "Intersection", { A: "sphere", B: "slab" }),
], "intersection");
model("EmptyIntersection", "Disjoint solids produce a valid empty model with no draw submission.", [
  ...node("a", "Sphere", { Radius: 18 }),
  ...node("b", "Translate", { Geometry: "a", Offset: v(60, 0, 0) }),
  ...node("intersection", "Intersection", { A: "a", B: "b" }),
], "intersection");
model("SmoothJoin", "A polynomial smooth union joins two spheres.", [
  ...node("sphere", "Sphere", { Radius: 19 }),
  ...node("left", "Translate", { Geometry: "sphere", Offset: v(-17, 0, 0) }),
  ...node("right", "Translate", { Geometry: "sphere", Offset: v(17, 0, 0) }),
  ...node("join", "Smooth Union", { A: "left", B: "right", Smoothness: 10 }),
], "join");
model("OffsetSphere", "Off-center model bounds stay authoritative with manual proxy dimensions of one.", [
  ...node("sphere", "Sphere", { Radius: 18 }),
  ...node("offset", "Translate", { Geometry: "sphere", Offset: v(26, 8, -10) }),
], "offset");
const asymmetric = [
  ...node("stem", "Box", { "Half Size": v(22, 7, 9) }),
  ...node("head", "Box", { "Half Size": v(9, 18, 12) }),
  ...node("head_position", "Translate", { Geometry: "head", Offset: v(24, 8, -3) }),
  ...node("part", "Union", { A: "stem", B: "head_position" }),
];
model("AsymmetricRotation", "Forward rotations apply X, then Y, then Z in radians.", [
  ...asymmetric, ...node("rotated", "Rotate", { Geometry: "part", Angles: v(0.32, 0.65, 0.21) }),
], "rotated");
model("MirroredPart", "Reflection across the local X=0 plane keeps asymmetric detail distinct.", [
  ...asymmetric, ...node("reflected", "Reflect", { Geometry: "part", Axis: "X" }),
], "reflected");
model("ScaledPart", "Positive nonuniform scale uses a conservative step bound.", [
  ...asymmetric, ...node("scaled", "Scale", { Geometry: "part", Factors: v(0.7, 1.4, 1.1) }),
], "scaled");
model("ScaledSmoothJoin", "Smooth bounds account for the conservative distance ratio under nonuniform scale.", [
  ...node("sphere", "Sphere", { Radius: 16 }),
  ...node("scaled", "Scale", { Geometry: "sphere", Factors: v(1.7, 0.65, 0.9) }),
  ...node("left", "Translate", { Geometry: "scaled", Offset: v(-18, 0, 0) }),
  ...node("right", "Translate", { Geometry: "scaled", Offset: v(18, 0, 0) }),
  ...node("join", "Smooth Union", { A: "left", B: "right", Smoothness: 7 }),
], "join");

function scene(name, models, camera = {}) {
  if (!existsSync(resolve(project, `content/scenes/${name}.kdl`)))
    call("buildScene", { kind: "scene", name, create: true, operations: [
      { op: "add_entity", name: "Camera", prototype: "StudyCamera", components: { GameCamera: { width: 540, height: 370 }, ...camera } },
      { op: "add_entity", name: "Key light", prototype: "StudyLight", components: { GameTransform: { x: -200, y: -100 } } },
      { op: "add_entity", name: "Fill light", prototype: "StudyLight", components: { GameTransform: { x: 200, y: 30 }, GameMaterial: { emission_color: 0x9dd4ffff } } },
      ...models.map((prototype, i) => ({ op: "add_entity", name: prototype, prototype,
        components: { GameTransform: { x: (i % 3 - 1) * 145, y: Math.floor(i / 3) * 110 - 60 } } })),
    ] });
  call("validateAsset", { kind: "scene", name });
}
scene("ModelBooleanStudy", ["CrossBoredHousing", "ClippedSphere", "SmoothJoin", "RoundedBlock", "Torus", "EmptyIntersection"]);
scene("ModelTransformStudy", ["AsymmetricRotation", "MirroredPart", "ScaledPart", "OffsetSphere", "ScaledSmoothJoin", "Capsule"]);
scene("ModelTransformNoRelief", ["AsymmetricRotation", "MirroredPart", "ScaledPart", "OffsetSphere", "ScaledSmoothJoin", "Capsule"], { GameHRC2D: { surface_relief: 0 } });

if (process.argv.includes("--preview")) {
  mkdirSync(evidence, { recursive: true });
  for (const name of ["ModelBooleanStudy", "ModelTransformStudy", "ModelTransformNoRelief"]) {
    const result = call("previewScene", { kind: "scene", name, width: 1080, height: 740,
      frames: 0, render_frames: 2, freeze_simulation: true });
    for (const item of result.content || []) if (item.type === "image")
      writeFileSync(resolve(evidence, `${name}.png`), Buffer.from(item.data, "base64"));
    writeFileSync(resolve(evidence, `${name}.json`), JSON.stringify(result.structuredContent, null, 2));
  }
}
