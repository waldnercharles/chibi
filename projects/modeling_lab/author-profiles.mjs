#!/usr/bin/env node
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { call } from "./author.mjs";

const project = dirname(fileURLToPath(import.meta.url));
const v2 = (x, y) => ({ x, y });
const wire = (from, pin, to, input) => ({ op: "connect", from, from_pin: pin, to, to_pin: input, kind: "data" });
const geo = (from, to, input = "Geometry") => wire(from, "Geometry", to, input);
const profile = (from, to) => wire(from, "Profile", to, "Profile");
const node = (ref, type, inputs = {}, metadata = {}) => [
  { op: "add_node", ref, type, ...metadata },
  ...Object.entries(inputs).map(([pin, value]) => ({ op: "set_input", node: ref, pin, value })),
];
function model(name, description, operations) {
  if (!existsSync(resolve(project, `content/models/${name}.kdl`)))
    call("buildGraph", { kind: "model", name, create: true, description, operations });
  call("validateAsset", { kind: "model", name });
}

const wing = [[-45, -6], [30, -14], [58, 4], [-45, 12]];
model("ProfileWing", "A convex four-point wing profile, extruded symmetrically along Z. The independent mesh control uses explicit triangles.", [
  ...node("outline", "Model Profile Polygon", { Vertices: wing.map(([x, y]) => v2(x, y)) }),
  ...node("extrude", "Model Extrude", { "Half Depth": 6 }), profile("outline", "extrude"),
  ...node("part", "Model Part", {}, { reference_name: "Wing housing" }), geo("extrude", "part"), geo("part", "output"),
]);
model("ProfileBracket", "An extruded rectangle with a corner cut and a circular through-hole. Profiles remain editable construction inputs.", [
  ...node("blank", "Model Profile Rectangle", { "Half Size": v2(36, 28) }),
  ...node("extrude", "Model Extrude", { "Half Depth": 7 }), profile("blank", "extrude"),
  ...node("corner", "Model Profile Rectangle", { "Half Size": v2(28, 24), Center: v2(15, 12) }),
  ...node("corner_cut", "Model Extrude", { "Half Depth": 9 }), profile("corner", "corner_cut"),
  ...node("notch", "Model Difference"), geo("extrude", "notch", "A"), geo("corner_cut", "notch", "B"),
  ...node("hole", "Model Profile Circle", { Radius: 6, Center: v2(-24, -16) }),
  ...node("hole_cut", "Model Extrude", { "Half Depth": 9 }), profile("hole", "hole_cut"),
  ...node("bore", "Model Difference"), geo("notch", "bore", "A"), geo("hole_cut", "bore", "B"), geo("bore", "output"),
]);
model("ProfileVessel", "A hollow vessel from two convex radial profiles revolved about Y. The cut keeps the outer solid's surface ownership.", [
  ...node("outer", "Model Profile Polygon", { Vertices: [[0, -28], [17, -28], [28, -8], [28, 14], [16, 28], [0, 28]].map(([x, y]) => v2(x, y)) }),
  ...node("outside", "Model Revolve"), profile("outer", "outside"),
  ...node("inner", "Model Profile Polygon", { Vertices: [[0, -20], [12, -20], [23, -5], [23, 14], [13, 32], [0, 32]].map(([x, y]) => v2(x, y)) }),
  ...node("inside", "Model Revolve"), profile("inner", "inside"),
  ...node("shell", "Model Difference"), geo("outside", "shell", "A"), geo("inside", "shell", "B"), geo("shell", "output"),
]);
model("ProfileRing", "A circle centered at positive radial distance revolves into the same surface as an analytical torus.", [
  ...node("circle", "Model Profile Circle", { Radius: 7, Center: v2(24, 0) }),
  ...node("lathe", "Model Revolve"), profile("circle", "lathe"), geo("lathe", "output"),
]);

// Explicit polygon faces and side quads provide a mesh reference without sampling
// or evaluating the model compiler. Triangle normals come from their winding.
const vertices = [];
function triangle(a, b, c) {
  const u = b.map((n, i) => n - a[i]), v = c.map((n, i) => n - a[i]);
  const n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]];
  const length = Math.hypot(...n);
  for (const p of [a, b, c]) vertices.push([...p, ...n.map(x => x / length), 0, 0]);
}
for (let i = 1; i + 1 < wing.length; i++) {
  triangle([...wing[0], 6], [...wing[i], 6], [...wing[i + 1], 6]);
  triangle([...wing[0], -6], [...wing[i + 1], -6], [...wing[i], -6]);
}
for (let i = 0; i < wing.length; i++) {
  const a = wing[i], b = wing[(i + 1) % wing.length];
  triangle([...a, -6], [...b, -6], [...b, 6]);
  triangle([...a, -6], [...b, 6], [...a, 6]);
}
mkdirSync(resolve(project, "content/meshes"), { recursive: true });
const mesh = resolve(project, "content/meshes/ProfileWing.mesh.json");
if (!existsSync(mesh)) writeFileSync(mesh, JSON.stringify({ version: 1, vertices }, null, 2) + "\n");
for (const name of ["ProfileWing", "ProfileBracket", "ProfileVessel", "ProfileRing", "ProfileWingMesh"]) {
  const imported = name === "ProfileWingMesh";
  const components = {
    GameTransform: {}, GameOrientation: {},
    GameVisualTransform: { scale: 1, roll: name === "ProfileVessel" || name === "ProfileRing" ? 0.65 : 1.15, yaw: 0.15, pitch: -0.12 },
    GameGeometry3D: { width: 1, height: 1, depth: 1 },
    [imported ? "GameMeshSurface2D" : "GameSdfSurface3D"]: { enabled: true },
    GameShader: { material: "StudyIvory", ...(!imported ? { shape: name } : {}) },
    GameMaterial: { absorption: 1, edge_light: 1, emission_multiplier: 1 },
    ...(imported ? { GameMeshAsset: { source: "content/meshes/ProfileWing.mesh.json" } } : {}),
  };
  if (!existsSync(resolve(project, `content/prototypes/${name}.kdl`)))
    call("buildPrototype", { kind: "prototype", name, create: true,
      operations: Object.entries(components).map(([component, fields]) => ({ op: "add_component", component, fields })) });
}
function scene(name, entries, width = 600, height = 380) {
  if (!existsSync(resolve(project, `content/scenes/${name}.kdl`)))
    call("buildScene", { kind: "scene", name, create: true, operations: [
      { op: "add_entity", name: "Camera", prototype: "StudyCamera", components: {
        GameCamera: { width, height }, GameHRC2D: { surface_relief: 0, ambient: 0.12 } } },
      { op: "add_entity", name: "Key", prototype: "StudyLight", components: { GameTransform: { x: -width * 0.4, y: -height * 0.3 } } },
      { op: "add_entity", name: "Fill", prototype: "StudyLight", components: { GameTransform: { x: width * 0.4, y: height * 0.3 } } },
      ...entries.map(([prototype, x, y, overrides = {}], i) => ({ op: "add_entity", name: `${prototype} ${i + 1}`, prototype,
        components: { GameTransform: { x, y }, ...overrides } })),
    ] });
  call("validateAsset", { kind: "scene", name });
}
scene("ModelProfileStudy", [["ProfileWing", -135, -55], ["ProfileWingMesh", -135, 60], ["ProfileBracket", 10, -50], ["ProfileVessel", 135, -55], ["ProfileRing", 130, 65],
  ["ProfileWing", 0, 85, { GameVisualTransform: { roll: 0.08, yaw: 0.15, pitch: -0.12 } }]]);
scene("ModelProfileControl", [["ProfileWing", 0, 0]], 170, 115);
scene("MeshProfileControl", [["ProfileWingMesh", 0, 0]], 170, 115);
if (process.argv.includes("--preview")) {
  const evidence = resolve(project, "../../build/model-evidence");
  mkdirSync(evidence, { recursive: true });
  for (const name of ["ModelProfileStudy", "ModelProfileControl", "MeshProfileControl"]) {
    const result = call("previewScene", { kind: "scene", name, width: name === "ModelProfileStudy" ? 1200 : 1020,
      height: name === "ModelProfileStudy" ? 760 : 690, frames: 0, render_frames: 2, freeze_simulation: true });
    for (const item of result.content || []) if (item.type === "image")
      writeFileSync(resolve(evidence, `${name}.png`), Buffer.from(item.data, "base64"));
    writeFileSync(resolve(evidence, `${name}.json`), JSON.stringify(result.structuredContent, null, 2));
  }
}
