#!/usr/bin/env node
import assert from "node:assert/strict";
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { call } from "./author.mjs";

const project = dirname(fileURLToPath(import.meta.url));
const ringCount = 24, ringRadius = 10, partRadius = 3;
const v = (x, y, z) => ({ x, y, z });
const wire = (from, from_pin, to, to_pin) => ({ op: "connect", from, from_pin, to, to_pin, kind: "data" });
const node = (ref, type, inputs = {}, metadata = {}) => [
  { op: "add_node", ref, type, ...metadata },
  ...Object.entries(inputs).map(([pin, value]) => ({ op: "set_input", node: ref, pin, value })),
];
function graph(kind, name, description, operations) {
  if (!existsSync(resolve(project, `content/${kind}s/${name}.${["behavior", "library", "system"].includes(kind) ? "code" : "kdl"}`)))
    call("buildGraph", { kind, name, create: true, description, operations });
  call("validateAsset", { kind, name });
}
graph("model", "CostSphere", "A single public uniform dimension, shared by every instance in the cost study.", [
  ...node("radius", "Model Parameter", { Default: 1, Minimum: 0.5, Maximum: 2 }, { reference_name: "Radius" }),
  ...node("sphere", "Model Sphere"), wire("radius", "Value", "sphere", "Radius"), wire("sphere", "Geometry", "output", "Geometry"),
]);
graph("shape", "CostRawSphere", "Independent numerical equivalent of CostSphere. Its unit-radius distance is length(p)-1.", [
  { op: "set_bounds", value: [1, 1] },
  ...node("position", "Shader ObjectPosition"),
  ...node("distance", "Behavior Expression", {}, { source: "length(p) - 1" }),
  wire("position", "Value", "distance", "p"), wire("distance", "Result", "output", "Distance"),
]);
graph("model", "CostConstruction", "A bounded 24-part radial construction deliberately repeats field work while retaining a small authored graph.", [
  ...node("part", "Model Sphere", { Radius: partRadius }),
  ...node("offset", "Model Translate", { Offset: v(ringRadius, 0, 0) }), wire("part", "Geometry", "offset", "Geometry"),
  ...node("ring", "Model Radial Array", { Count: ringCount, "Angle Step": 2 * Math.PI / ringCount, Axis: 1 }),
  wire("offset", "Geometry", "ring", "Geometry"), wire("ring", "Geometry", "output", "Geometry"),
]);
// Both variants use exactly the same primitive values, CSG and transforms.
// Part wrappers and one shared neutral slot isolate hit resolution/binding work.
function hitControl(named) {
  const operations = [];
  const geo = (from, to, pin = "Geometry") => wire(from, "Geometry", to, pin);
  if (named) operations.push(...node("ivory", "Model Material Slot", {}, {
    reference_name: "Neutral", reference_asset: "StudyIvory",
  }));
  function part(source, ref, name) {
    if (!named) return source;
    operations.push(...node(ref, "Model Part", {}, { reference_name: name }), geo(source, ref),
      wire("ivory", "Slot", ref, "Material"));
    return ref;
  }
  operations.push(
    ...node("body", "Model Rounded Box", { "Half Size": v(26, 15, 19), Radius: 3 }),
    ...node("bore", "Model Cylinder", { Radius: 8, "Half Height": 22 }),
    ...node("bore_position", "Model Translate", { Offset: v(-10, 0, 0) }), geo("bore", "bore_position"),
    ...node("cut", "Model Difference"), geo("body", "cut", "A"), geo("bore_position", "cut", "B"),
  );
  const body = part("cut", "painted_body", "Body and cut");
  operations.push(...node("sphere", "Model Sphere", { Radius: 13 }),
    ...node("sphere_position", "Model Translate", { Offset: v(23, 6, 0) }), geo("sphere", "sphere_position"));
  const sphere = part("sphere_position", "smooth_insert", "Smooth insert");
  operations.push(...node("smooth", "Model Smooth Union", { Smoothness: 5 }), geo(body, "smooth", "A"), geo(sphere, "smooth", "B"),
    ...node("cap", "Model Cylinder", { Radius: 5, "Half Height": 4 }),
    ...node("cap_position", "Model Translate", { Offset: v(5, 17, 9) }), geo("cap", "cap_position"));
  const cap = part("cap_position", "top_cap", "Top cap");
  operations.push(...node("assembly", "Model Union"), geo("smooth", "assembly", "A"), geo(cap, "assembly", "B"), geo("assembly", "output"));
  return operations;
}
graph("model", "CostHitEntityMaterial", "PaintedModule geometry with no named parts, using only the neutral entity-wide material.", hitControl(false));
graph("model", "CostHitPartMaterial", "Identical PaintedModule geometry with three named parts bound to one neutral material slot; isolates hit-resolution and binding overhead.", hitControl(true));
for (const name of ["CostSphere", "CostRawSphere", "CostConstruction", "CostHitEntityMaterial", "CostHitPartMaterial"]) {
  if (!existsSync(resolve(project, `content/prototypes/${name}.kdl`)))
    call("buildPrototype", { kind: "prototype", name, create: true, operations: Object.entries({
      GameTransform: {}, GameOrientation: {}, GameVisualTransform: { scale: 1 },
      GameGeometry3D: { width: 2, height: 2, depth: 2 }, GameSdfSurface3D: { enabled: true },
      GameShader: { shape: name, material: "StudyIvory" },
      GameMaterial: { absorption: 1, edge_light: 1, emission_multiplier: 1 },
    }).map(([component, fields]) => ({ op: "add_component", component, fields })) });
}
function scene(name, description, entities) {
  if (!existsSync(resolve(project, `content/scenes/${name}.kdl`))) {
    const operations = [
      { op: "add_entity", name: "Camera", prototype: "StudyCamera", components: {
        GameCamera: { width: 600, height: 400, focal_distance: 0 },
        GameHRC2D: { adaptive: false, resolution: 0.5, min_resolution: 0.5, surface_relief: 0, ambient: 0.12 },
      } },
      { op: "add_entity", name: "Light", prototype: "StudyLight", components: { GameTransform: { x: -250, y: -145 } } },
      ...entities,
    ];
    for (let i = 0; i < operations.length; i += 64) {
      const state = i ? call("readAsset", { kind: "scene", name }).structuredContent.result : null;
      call("buildScene", { kind: "scene", name, ...(i ? { expected_hash: state.hash } : { create: true }),
        operations: operations.slice(i, i + 64) });
    }
  }
  call("validateAsset", { kind: "scene", name });
}
function entity(name, prototype, x, y, scale, parameters) {
  return { op: "add_entity", name, prototype, components: { GameTransform: { x, y }, GameVisualTransform: { scale },
    ...(parameters ? { GameShader: { parameters } } : {}) } };
}
scene("ModelCostSingle", "One sphere covers the same analytical projected area as 256 radius-six spheres.", [entity("One model", "CostSphere", 0, 0, 96)]);
scene("ModelCostRawControl", "Raw field equivalent at the identical pose and projected area.", [entity("One raw field", "CostRawSphere", 0, 0, 96)]);
// Orthographic projection along Y gives equal disks centered on an X/Z ring.
// In each angular Voronoi sector, its nearest disk supplies both radial edges.
// Integrate 1/2*(outer^2-inner^2) with u=R*sin(theta), |theta|<=pi/N.
// This includes all multiple overlaps; summing disk areas would overcount them.
const a = ringRadius * Math.sin(Math.PI / ringCount);
assert(ringRadius > partRadius && a < partRadius, "Closed ring-area formula requires R>r and R*sin(pi/N)<r");
const unionArea = 2 * ringCount * (a * Math.sqrt(partRadius ** 2 - a ** 2) + partRadius ** 2 * Math.asin(a / partRadius));
const targetArea = Math.PI * 96 ** 2;
const constructionScale = Math.sqrt(targetArea / unionArea);
scene("ModelCostComplex", "An overlapping 24-sphere ring matches ModelCostSingle's analytical projected union area under the same orthographic camera.", [
  entity("Complex construction", "CostConstruction", 0, 0, constructionScale),
]);
const evidence = resolve(project, "../../build/model-evidence");
mkdirSync(evidence, { recursive: true });
writeFileSync(resolve(evidence, "model-cost-coverage.json"), JSON.stringify({
  method: "Exact angular-sector integration of equal projected disks on a regular ring; all overlaps included",
  formula: "A=2*N*(a*sqrt(r*r-a*a)+r*r*asin(a/r)), a=R*sin(pi/N)",
  ring_count: ringCount, ring_radius: ringRadius, sphere_radius: partRadius,
  unit_projected_union_area: unionArea, target_projected_area: targetArea, instance_scale: constructionScale,
  camera: { width: 600, height: 400, focal_distance: 0 },
  assumptions: ["Orthographic Y-axis projection", "Equal spheres with centers on one regular X/Z ring", "No entity rotation", "R>r and R*sin(pi/N)<r"],
  limits: "Matches continuous projected geometric area, not pixel counts, proxy coverage, ray steps, silhouette perimeter or shaded cost. Float rotations and raster edges can differ slightly.",
  hit_resolution_control: {
    scenes: ["ModelCostHitEntityMaterial", "ModelCostHitPartMaterial"],
    geometry: "Same RoundedBox/bore Difference, offset Sphere SmoothUnion, and offset cap Cylinder Union",
    material: "StudyIvory in both cases; entity-wide versus three named parts with one shared slot",
    pose: { scale: 3, roll: 0.75, yaw: 0.2, pitch: -0.1 },
    scope: "Paired release frame/pass timing measures extra hit-resolution and material-binding overhead; it is not a GPU instruction count.",
  },
}, null, 2) + "\n");
for (const name of ["CostHitEntityMaterial", "CostHitPartMaterial"]) {
  const control = entity("Matched material control", name, 0, 0, 3);
  control.components.GameVisualTransform = { scale: 3, roll: 0.75, yaw: 0.2, pitch: -0.1 };
  scene(`Model${name}`, "Paired identical geometry, pose, camera, lighting and StudyIvory material; compare named-part hit resolution against entity-wide shading.", [control]);
}
for (const variants of [false, true]) {
  const entities = [];
  for (let i = 0; i < 256; i++) {
    const radius = i % 2 ? 1.2 : 0.8;
    entities.push(entity(`Sphere ${i + 1}`, "CostSphere", (i % 16 - 7.5) * 20, (Math.floor(i / 16) - 7.5) * 20,
      variants ? 6 / Math.sqrt(1.04) : 6, variants ? `Radius ${radius}\n` : undefined));
  }
  scene(variants ? "ModelCostVariants" : "ModelCostInstances", "256 instances have equal total analytical projected area; variants normalize the mean squared radius.", entities);
}
scene("ModelCostStudy", "All workload categories together for visual inspection; use the isolated scenes for matched timing.", [
  entity("One model", "CostSphere", -210, -65, 38), entity("Raw equivalent", "CostRawSphere", -90, -65, 38),
  entity("Complex construction", "CostConstruction", 115, -50, 4),
  ...Array.from({ length: 64 }, (_, i) => entity(`Shared instance ${i + 1}`, "CostSphere", (i % 16 - 7.5) * 24,
    70 + Math.floor(i / 16) * 20, 6, i % 2 ? "Radius 1.2\n" : undefined)),
]);
