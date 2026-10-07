#!/usr/bin/env node
// Static editable construction study; no gameplay systems or scene animation.
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { call } from "./author.mjs";

const project = dirname(fileURLToPath(import.meta.url));
const v = (x, y, z) => ({ x, y, z });
const wire = (from, from_pin, to, to_pin) => ({ op: "connect", from, from_pin, to, to_pin, kind: "data" });
const geo = (from, to, pin = "Geometry") => wire(from, "Geometry", to, pin);
const node = (ref, type, inputs = {}, metadata = {}) => [
  { op: "add_node", ref, type: `Model ${type}`, ...metadata },
  ...Object.entries(inputs).map(([pin, value]) => ({ op: "set_input", node: ref, pin, value })),
];
const parameter = (ref, name, value, min, max, type = "float") => [
  ...node(ref, "Parameter", {}, { reference_name: name }),
  ...Object.entries({ Default: value, Minimum: min, Maximum: max }).map(([pin, value]) =>
    ({ op: "set_input", node: ref, pin, type, value })),
];
const part = (ref, source, name, slot) => [
  ...node(ref, "Part", {}, { reference_name: name }), geo(source, ref), wire(slot, "Slot", ref, "Material"),
];
function asset(kind, name, description, operations) {
  if (!existsSync(resolve(project, `content/${kind}s/${name}.${["behavior", "library", "system"].includes(kind) ? "code" : "kdl"}`)))
    call("buildGraph", { kind, name, description, create: true, operations });
  call("validateAsset", { kind, name });
}
for (const [name, color, roughness, metallic, emission] of [
  ["ShipPaint", v(0.08, 0.24, 0.31), 0.35, 0.15, v(0, 0, 0)],
  ["ShipMetal", v(0.62, 0.68, 0.72), 0.3, 0.7, v(0, 0, 0)],
  ["ShipGlow", v(0.12, 0.46, 0.72), 0.4, 0.05, v(0.12, 0.55, 1.1)],
]) asset("material", name, "A simple existing material graph for the static ship study.", [
  ...Object.entries({ "Base Color": color, Roughness: roughness, Metallic: metallic, Emission: emission })
    .map(([pin, value]) => ({ op: "set_input", node: "output", pin, value })),
]);

asset("model", "StudyShipEngine", "Reusable hollow engine with an editable radius, named casing and glowing exhaust.", [
  ...node("metal", "Material Slot", {}, { reference_name: "Casing", reference_asset: "ShipMetal" }),
  ...node("glow", "Material Slot", {}, { reference_name: "Exhaust", reference_asset: "ShipGlow" }),
  ...parameter("radius", "Radius", 10, 8, 14),
  ...node("outer", "Cylinder", { "Half Height": 19 }), wire("radius", "Value", "outer", "Radius"),
  ...node("bore", "Cylinder", { Radius: 6, "Half Height": 22 }),
  ...node("shell", "Difference"), geo("outer", "shell", "A"), geo("bore", "shell", "B"),
  ...part("casing", "shell", "Engine casing", "metal"),
  ...node("disk", "Cylinder", { Radius: 5.8, "Half Height": 1 }),
  ...node("disk_position", "Translate", { Offset: v(0, -15, 0) }), geo("disk", "disk_position"),
  ...part("exhaust", "disk_position", "Exhaust disk", "glow"),
  ...node("assembly", "Union"), geo("casing", "assembly", "A"), geo("exhaust", "assembly", "B"),
  // Model Rotate maps the engine's local Y axis to ship Z.
  ...node("axis", "Rotate", { Angles: v(Math.PI / 2, 0, 0) }), geo("assembly", "axis"), geo("axis", "output"),
]);

asset("model", "StudyShip", "A static hull, convex extruded mirrored wings and two references to an editable engine. Local Y is up and the nose points toward +Z.", [
  ...node("paint", "Material Slot", {}, { reference_name: "Paint", reference_asset: "ShipPaint" }),
  ...node("metal", "Material Slot", {}, { reference_name: "Wing finish", reference_asset: "ShipMetal" }),
  ...parameter("radius", "Hull Radius", 16, 12, 20),
  ...parameter("length", "Hull Half Segment", 40, 30, 50),
  ...parameter("wing_scale", "Wing Scale", v(1, 1, 1), v(0.7, 0.8, 0.7), v(1.25, 1.2, 1.25), "ShaderVec3"),
  ...node("hull_capsule", "Capsule"), wire("radius", "Value", "hull_capsule", "Radius"), wire("length", "Value", "hull_capsule", "Half Segment"),
  ...node("hull_axis", "Rotate", { Angles: v(Math.PI / 2, 0, 0) }), geo("hull_capsule", "hull_axis"),
  ...part("hull", "hull_axis", "Main hull", "paint"),
  ...node("canopy_sphere", "Sphere", { Radius: 1 }),
  ...node("canopy_scale", "Scale", { Factors: v(10, 6, 19) }), geo("canopy_sphere", "canopy_scale"),
  ...node("canopy_position", "Translate", { Offset: v(0, 14, 14) }), geo("canopy_scale", "canopy_position"),
  ...part("canopy", "canopy_position", "Raised cockpit", "metal"),
  ...node("outline", "Profile Polygon", { Vertices: [[8, -28], [72, -36], [60, 5], [8, 22]].map(([x, y]) => ({ x, y })) }),
  ...node("wing_extrude", "Extrude", { "Half Depth": 2.5 }), wire("outline", "Profile", "wing_extrude", "Profile"),
  // Profile X/Y becomes ship X/Z; extrusion thickness becomes vertical Y.
  ...node("wing_axis", "Rotate", { Angles: v(Math.PI / 2, 0, 0) }), geo("wing_extrude", "wing_axis"),
  ...node("wing_size", "Scale"), geo("wing_axis", "wing_size"), wire("wing_scale", "Value", "wing_size", "Factors"),
  ...node("wing_position", "Translate", { Offset: v(0, -5, 0) }), geo("wing_size", "wing_position"),
  ...part("right_wing", "wing_position", "Starboard wing", "metal"),
  ...node("wing_reflection", "Reflect", { Axis: "X" }), geo("wing_position", "wing_reflection"),
  ...part("left_wing", "wing_reflection", "Port wing", "metal"),
  ...node("left_engine", "Instance", { Radius: 9 }, { reference_asset: "StudyShipEngine" }),
  ...node("left_engine_position", "Translate", { Offset: v(-33, -3, -30) }), geo("left_engine", "left_engine_position"),
  ...node("right_engine", "Instance", { Radius: 11 }, { reference_asset: "StudyShipEngine" }),
  ...node("right_engine_position", "Translate", { Offset: v(33, -3, -30) }), geo("right_engine", "right_engine_position"),
  ...["hull", "canopy", "right_wing", "left_wing", "left_engine_position", "right_engine_position"].flatMap((source, i, all) => {
    if (!i) return [];
    const ref = `join_${i}`;
    return [...node(ref, "Union"), geo(i === 1 ? all[0] : `join_${i - 1}`, ref, "A"), geo(source, ref, "B")];
  }), geo("join_5", "output"),
]);

const prototype = "StudyShip";
if (!existsSync(resolve(project, `content/prototypes/${prototype}.kdl`)))
  call("buildPrototype", { kind: "prototype", name: prototype, create: true, operations: Object.entries({
    GameTransform: {}, GameOrientation: {},
    // Runtime mapping is yaw about negative Y, roll about X, pitch about Z.
    // This pose is separate from Model Rotate's forward X/Y/Z construction order.
    GameVisualTransform: { scale: 1, yaw: 0.18, roll: -0.48, pitch: -0.1 },
    GameGeometry3D: { width: 1, height: 1, depth: 1 }, GameSdfSurface3D: { enabled: true },
    GameShader: { shape: "StudyShip", material: "ShipPaint" },
    GameMaterial: { absorption: 1, edge_light: 1, emission_multiplier: 1 },
  }).map(([component, fields]) => ({ op: "add_component", component, fields })) });
call("validateAsset", { kind: "prototype", name: prototype });
if (!existsSync(resolve(project, "content/scenes/ModelShipStudy.kdl")))
  call("buildScene", { kind: "scene", name: "ModelShipStudy", create: true, operations: [
    { op: "add_entity", name: "Camera", prototype: "StudyCamera", components: {
      GameCamera: { width: 270, height: 220, focal_distance: 700 }, GameHRC2D: { surface_relief: 0, ambient: 0.12 } } },
    { op: "add_entity", name: "Key", prototype: "StudyLight", components: { GameTransform: { x: -100, y: -70 } } },
    { op: "add_entity", name: "Fill", prototype: "StudyLight", components: { GameTransform: { x: 110, y: 65 }, GameMaterial: { emission_color: 0x83c9ffff } } },
    { op: "add_entity", name: "Editable static ship", prototype },
  ] });
call("validateAsset", { kind: "scene", name: "ModelShipStudy" });
const inspection = call("inspectGraph", { kind: "model", name: "StudyShip" });
const evidence = resolve(project, "../../build/model-evidence");
mkdirSync(evidence, { recursive: true });
writeFileSync(resolve(evidence, "ModelShipStudy-inspection.json"), JSON.stringify(inspection.structuredContent, null, 2));
if (process.argv.includes("--preview")) {
  const result = call("previewScene", { kind: "scene", name: "ModelShipStudy", width: 1080, height: 880,
    frames: 0, render_frames: 2, freeze_simulation: true });
  for (const item of result.content || []) if (item.type === "image")
    writeFileSync(resolve(evidence, "ModelShipStudy.png"), Buffer.from(item.data, "base64"));
  writeFileSync(resolve(evidence, "ModelShipStudy.json"), JSON.stringify(result.structuredContent, null, 2));
}
