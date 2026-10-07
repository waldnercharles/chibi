#!/usr/bin/env node
// Reference construction, authored through Studio's public graph operations.
// Source: reference/README.md. Geometry uses X right, Y up, Z toward the lens.
import { cpSync, existsSync, mkdirSync, readFileSync, renameSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { createHash } from "node:crypto";
import { fileURLToPath } from "node:url";
import { call } from "./author.mjs";

const project = dirname(fileURLToPath(import.meta.url));
const v = (x, y, z) => ({ x, y, z });
const wire = (from, pin, to, input) => ({ op: "connect", from, from_pin: pin, to, to_pin: input, kind: "data" });
const geo = (from, to, pin = "Geometry") => wire(from, "Geometry", to, pin);
const ops = [];
const checkpoints = [];
function node(ref, type, inputs = {}, metadata = {}) {
  ops.push({ op: "add_node", ref, type: `Model ${type}`, ...metadata });
  for (const [pin, value] of Object.entries(inputs))
    ops.push(typeof value === "string" && ["Geometry", "A", "B"].includes(pin)
      ? geo(value, ref, pin) : { op: "set_input", node: ref, pin, value });
  return ref;
}
function parameter(ref, name, initial, low, high, type) {
  node(ref, "Parameter", {}, { reference_name: name });
  for (const [pin, value] of [["Default", initial], ["Minimum", low], ["Maximum", high]])
    ops.push({ op: "set_input", node: ref, pin, value, type });
}
function build(kind, name, description, operations) {
  if (!existsSync(resolve(project, `content/${kind}s/${name}.${["behavior", "library", "system"].includes(kind) ? "code" : "kdl"}`)))
    call("buildGraph", { kind, name, create: true, description, operations });
  call("validateAsset", { kind, name });
}
// Each public edit is atomic and limited to 128 operations. Completed named
// parts are safe boundaries: no half-built geometry is published between calls.
function buildLargeModel(name, description, operations) {
  const asset = resolve(project, `content/models/${name}.kdl`);
  const evidence = resolve(project, "../../build/model-evidence");
  const progressFile = resolve(evidence, `${name}-authoring-progress.json`);
  const digest = createHash("sha256").update(JSON.stringify({ description, operations, checkpoints })).digest("hex");
  let state = existsSync(progressFile) ? JSON.parse(readFileSync(progressFile, "utf8")) : null;
  if (existsSync(asset) && (!state || state.complete)) {
    call("validateAsset", { kind: "model", name });
    return;
  }
  if (state && (state.digest !== digest || state.name !== name || state.project !== project))
    throw Error(`Refusing divergent authoring resume: ${progressFile}; existing draft is preserved`);
  if (state && (!Number.isInteger(state.cursor) || state.cursor < 0 || state.cursor > operations.length || !state.aliases))
    throw Error(`Invalid authoring progress record: ${progressFile}`);
  state ||= { name, project, digest, cursor: 0, aliases: {}, hash: null, complete: false };
  if (state.cursor && !existsSync(asset)) throw Error("Authoring draft disappeared; refusing to recreate its stable IDs");
  if (existsSync(asset)) {
    const current = call("readAsset", { kind: "model", name }).structuredContent.result;
    if (current.hash !== state.hash) throw Error("Authoring draft changed outside the recorded edit; refusing divergent resume");
  }
  const batches = [];
  let cursor = state.cursor;
  while (cursor < operations.length) {
    // First create adds starter-sphere removal in call(), plus a temporary base
    // output connection here. Later batches need no extra operations.
    const limit = cursor === 0 ? 126 : 128;
    const end = checkpoints.filter(at => at > cursor && at - cursor <= limit).at(-1);
    if (!end) throw Error(`No complete-part boundary fits the 128-operation limit at operation ${cursor}`);
    batches.push([cursor, end]);
    cursor = end;
  }
  mkdirSync(evidence, { recursive: true });
  function save() {
    const temporary = `${progressFile}.tmp`;
    writeFileSync(temporary, JSON.stringify(state, null, 2) + "\n");
    renameSync(temporary, progressFile);
  }
  save();
  for (const [start, end] of batches) {
    const batch = operations.slice(start, end).map(operation => {
      const translated = { ...operation };
      // Refs created in this request remain aliases; aliases from earlier
      // requests are now exact stable node IDs returned by the public tool.
      for (const key of ["node", "from", "to"])
        if (typeof translated[key] === "string" && Object.hasOwn(state.aliases, translated[key]))
          translated[key] = state.aliases[translated[key]];
      return translated;
    });
    const create = start === 0;
    if (create) batch.push(geo("base", "output"));
    if (batch.length + (create ? 1 : 0) > 128) throw Error("Authoring batch exceeds public operation limit");
    const response = call("buildGraph", { kind: "model", name, description, operations: batch,
      ...(create ? { create: true } : { expected_hash: state.hash }) }).structuredContent.result;
    if (!/^[0-9a-f]{16}$/.test(response.after_hash || ""))
      throw Error("Authoring response omitted the committed hash; draft preserved for manual inspection");
    for (const { ref, id } of response.nodes || []) {
      if (!Number.isSafeInteger(id) || id <= 0) throw Error("Authoring returned an invalid stable node ID");
      state.aliases[ref] = id;
    }
    for (const operation of operations.slice(start, end))
      if (operation.op === "add_node" && !Object.hasOwn(state.aliases, operation.ref))
        throw Error(`Authoring response omitted node alias ${operation.ref}; draft preserved`);
    state.cursor = end;
    state.hash = response.after_hash;
    state.complete = end === operations.length;
    save();
  }
  call("validateAsset", { kind: "model", name });
}
const colors = {
  Ivory: [0.80, 0.77, 0.66], Black: [0.025, 0.026, 0.022], Lens: [0.012, 0.015, 0.02],
  Glass: [0.009, 0.018, 0.026], Red: [0.92, 0.055, 0.008], Cream: [0.78, 0.72, 0.58],
  Metal: [0.34, 0.33, 0.27], Label: [0.015, 0.014, 0.011], White: [0.75, 0.72, 0.61],
};
for (const [name, color] of Object.entries(colors).filter(([name]) => !["Label", "Lens"].includes(name))) build("material", `Polaroid${name}`,
  `${name} surface for the photograph reconstruction; editable material constants.`, [
    { op: "set_input", node: "output", pin: "Base Color", value: v(...color) },
    { op: "set_input", node: "output", pin: "Roughness", value: name === "Glass" ? 0.09 : name === "Red" ? 0.26 : name === "Lens" ? 0.29 : 0.38 },
    { op: "set_input", node: "output", pin: "Metallic", value: name === "Metal" ? 0.7 : 0 },
  ]);
build("material", "PolaroidLens", "Fine concentric lens grooves vary color and roughness in the named lens frame; the conical recess remains real geometry.", [
  { op: "add_node", ref: "position", type: "Shader PartPosition" },
  { op: "add_node", ref: "wave", type: "Behavior Expression", source: "0.5 + 0.5 * sin(sqrt((p.x + 0.05) * (p.x + 0.05) + (p.y - 5.7) * (p.y - 5.7)) * 150)" },
  wire("position", "Value", "wave", "p"),
  { op: "add_node", ref: "grooves", type: "Behavior Expression", source: "0.65 + 0.35 * g * g * g * g" },
  wire("wave", "Result", "grooves", "g"),
  { op: "add_node", ref: "color", type: "Shader Multiply" },
  wire("grooves", "Result", "color", "B"), wire("color", "Value", "output", "Base Color"),
  { op: "add_node", ref: "tint", type: "Shader Vector3" },
  ...[0.03, 0.029, 0.026].map((value, i) => ({ op: "set_input", node: "tint", pin: ["X", "Y", "Z"][i], value })),
  wire("tint", "Value", "color", "A"),
  { op: "add_node", ref: "roughness", type: "Behavior Expression", source: "0.2 + 0.25 * g" },
  wire("grooves", "Result", "roughness", "g"), wire("roughness", "Result", "output", "Roughness"),
]);
// Only the two small printed badges sample the reference image. The housing,
// optics, controls and silhouette remain construction geometry. Projective UVs
// remove the reference camera's perspective from each photographed badge.
mkdirSync(resolve(project, "content/images"), { recursive: true });
const labelImage = resolve(project, "content/images/PolaroidReference.jpg");
if (!existsSync(labelImage)) cpSync(resolve(project, "reference/polaroid-onestep-1280.jpg"), labelImage);
function homography(corners) {
  const rows = [];
  for (const [[u, w], [x, y]] of [[0, 0], [1, 0], [1, 1], [0, 1]].map((uv, i) => [uv, corners[i]])) {
    rows.push([u, w, 1, 0, 0, 0, -x * u, -x * w, x]);
    rows.push([0, 0, 0, u, w, 1, -y * u, -y * w, y]);
  }
  for (let col = 0; col < 8; col++) {
    let pivot = col;
    for (let row = col + 1; row < 8; row++) if (Math.abs(rows[row][col]) > Math.abs(rows[pivot][col])) pivot = row;
    [rows[col], rows[pivot]] = [rows[pivot], rows[col]];
    const scale = rows[col][col];
    for (let j = col; j <= 8; j++) rows[col][j] /= scale;
    for (let row = 0; row < 8; row++) if (row !== col) {
      const factor = rows[row][col];
      for (let j = col; j <= 8; j++) rows[row][j] -= factor * rows[col][j];
    }
  }
  return rows.map(row => row[8]);
}
const badgeMaps = [
  { uv: homography([[.31579, .38889], [.40241, .40058], [.39748, .45249], [.31689, .44079]]), u: "clamp((p.x + 3.95) / 2, 0, 1)", v: "clamp((6.72 - p.y) / 0.84, 0, 1)" },
  { uv: homography([[.23575, .71637], [.62774, .80556], [.62500, .86330], [.24178, .77632]]), u: "clamp((p.x + 4.45) / 8.9, 0, 1)", v: "clamp((1.96 - p.y) / 0.92, 0, 1)" },
];
const labelOps = [{ op: "add_node", ref: "position", type: "Shader PartPosition" },
  { op: "add_node", ref: "uv", type: "Shader Vector2" }];
for (let axis = 0; axis < 2; axis++) {
  const maps = badgeMaps.map(({ uv: h, u, v: w }) =>
    `((${h[axis * 3]} * (${u}) + ${h[axis * 3 + 1]} * (${w}) + ${h[axis * 3 + 2]}) / (${h[6]} * (${u}) + ${h[7]} * (${w}) + 1))`);
  labelOps.push({ op: "add_node", ref: `coordinate${axis}`, type: "Behavior Expression", source: `mix(${maps[1]}, ${maps[0]}, step(3.2, p.y))` },
    wire("position", "Value", `coordinate${axis}`, "p"), wire(`coordinate${axis}`, "Result", "uv", axis === 0 ? "X" : "Y"));
}
labelOps.push({ op: "add_node", ref: "photo", type: "Shader Image Texture" },
  { op: "set_input", node: "photo", pin: "Image", value: "content/images/PolaroidReference.jpg" },
  { op: "set_input", node: "photo", pin: "Wrap", value: 1 },
  wire("uv", "Value", "photo", "UV"), wire("photo", "Color", "output", "Base Color"),
  { op: "set_input", node: "output", pin: "Roughness", value: 0.45 });
build("material", "PolaroidLabel", "Printed badge details sampled from the attributed photograph only on the two small modeled nameplates.", labelOps);
// Six bands on the actual apron. Part wraps its placement, so PartPosition
// is in the shared uncentered construction frame here; its X origin is zero.
// Assembly centering and public Scale are outside Part and do not move stripes.
const rainbow = [[0.85, 0.055, 0.24], [0.92, 0.12, 0.012], [1, 0.49, 0.008],
  [0.86, 0.75, 0.014], [0.26, 0.46, 0.075], [0.008, 0.24, 0.36]];
const rainbowOps = [{ op: "add_node", ref: "position", type: "Shader PartPosition" },
  { op: "add_node", ref: "color", type: "Shader Vector3" }];
for (let c = 0; c < 3; c++) {
  let value = `${rainbow[0][c]}`;
  for (let i = 1; i < 6; i++) value = `mix(${value}, ${rainbow[i][c]}, step(${-0.45 + i * 0.15}, p.x))`;
  const source = `mix(${colors.Ivory[c]}, ${value}, step(abs(p.x), 0.45))`;
  rainbowOps.push({ op: "add_node", ref: `channel${c}`, type: "Behavior Expression", source },
    wire("position", "Value", `channel${c}`, "p"), wire(`channel${c}`, "Result", "color", ["X", "Y", "Z"][c]));
}
rainbowOps.push(wire("color", "Value", "output", "Base Color"),
  { op: "set_input", node: "output", pin: "Roughness", value: 0.35 });
build("material", "PolaroidRainbow", "Six rainbow bands on real ivory housing surfaces, using the winning part coordinate frame before assembly centering and public scale.", rainbowOps);

for (const name of [...Object.keys(colors), "Rainbow"])
  node(`slot${name}`, "Material Slot", {}, { reference_name: name, reference_asset: `Polaroid${name}` });
parameter("size", "Scale", v(1, 1, 1), v(0.7, 0.7, 0.7), v(1.4, 1.4, 1.4), "ShaderVec3");
parameter("lensRadius", "Lens Radius", 1.8, 1.5, 2.05, "float");
const parts = [];
function part(ref, label, shape, material, offset = null, angles = null) {
  if (angles) shape = node(`${ref}rotate`, "Rotate", { Geometry: shape, Angles: angles });
  if (offset) shape = node(`${ref}place`, "Translate", { Geometry: shape, Offset: offset });
  node(ref, "Part", { Geometry: shape }, { reference_name: label });
  ops.push(wire(`slot${material}`, "Slot", ref, "Material"));
  parts.push(ref);
  checkpoints.push(ops.length);
  return ref;
}
function box(ref, label, half, offset, material, radius = 0.08) {
  return part(ref, label, node(`${ref}shape`, "Rounded Box", { "Half Size": v(...half), Radius: radius }), material, v(...offset));
}
function side(ref, label, points, halfWidth, material, offset = v(0, 0, 0)) {
  // Profile XY = (-Z,Y); Ry(pi/2) maps its extrusion Z to world X.
  node(`${ref}profile`, "Profile Polygon", { Vertices: points.map(([y, z]) => ({ x: -z, y })) });
  node(`${ref}extrude`, "Extrude", { "Half Depth": halfWidth });
  ops.push(wire(`${ref}profile`, "Profile", `${ref}extrude`, "Profile"));
  return part(ref, label, `${ref}extrude`, material, offset, v(0, Math.PI / 2, 0));
}
function cylinderShape(ref, radius, depth) {
  node(ref, "Cylinder", { Radius: radius, "Half Height": depth });
  return node(`${ref}axis`, "Rotate", { Geometry: ref, Angles: v(Math.PI / 2, 0, 0) });
}
function cylinder(ref, label, radius, depth, offset, material) {
  return part(ref, label, cylinderShape(`${ref}shape`, radius, depth), material, v(...offset));
}
function lathe(ref, label, points, offset, material) {
  node(`${ref}profile`, "Profile Polygon", { Vertices: points.map(([x, y]) => ({ x, y })) });
  node(`${ref}shape`, "Revolve");
  ops.push(wire(`${ref}profile`, "Profile", `${ref}shape`, "Profile"));
  return part(ref, label, `${ref}shape`, material, v(...offset), v(Math.PI / 2, 0, 0));
}
// Hollow front slot belongs to the black base and keeps the base's cut material.
node("baseProfile", "Profile Polygon", { Vertices: [[0.15, -3.8], [3.3, -3.8], [2.25, 4.9], [0.15, 4.9]].map(([y, z]) => ({ x: -z, y })) });
node("baseExtrude", "Extrude", { "Half Depth": 5 }); ops.push(wire("baseProfile", "Profile", "baseExtrude", "Profile"));
node("baseAxis", "Rotate", { Geometry: "baseExtrude", Angles: v(0, Math.PI / 2, 0) });
node("slotCut", "Rounded Box", { "Half Size": v(4.35, 0.28, 0.8), Radius: 0.08 });
node("slotPlace", "Translate", { Geometry: "slotCut", Offset: v(0, 0.66, 4.8) });
node("baseCut", "Difference", { A: "baseAxis", B: "slotPlace" });
part("base", "Film housing with recessed ejection slot", "baseCut", "Black");
box("rearShell", "Rear main shell", [4.75, 2.8, 1.94], [0, 5.1, -0.75], "Black", 0.2);
side("apron", "Sloped ivory apron", [[2.35, 4.8], [3.5, 1.5], [3.8, 1.5], [2.65, 4.8]], 4.8, "Rainbow");
box("apronLip", "Ivory front lip", [4.8, 0.2, 0.13], [0, 2.44, 4.8], "Rainbow", 0.06);
box("frontPanel", "Ivory upright face", [4.8, 2.25, 0.18], [0, 5.65, 1.35], "Ivory", 0.16);
box("rightSeam", "Right shell reveal", [0.065, 2.1, 0.23], [4.83, 5.65, 1.12], "Black", 0.05);
box("leftSeam", "Left shell reveal", [0.065, 2.1, 0.23], [-4.83, 5.65, 1.12], "Black", 0.05);
side("stalk", "Rear viewfinder stalk", [[6.3, -1.7], [7.05, -4.7], [8.25, -4.7], [7.1, -1.7]], 1.2, "Black", v(3.6, 0, 0));
node("eyeBlank", "Rounded Box", { "Half Size": v(1.45, 1.03, 0.22), Radius: 0.2 });
node("eyeHole", "Rounded Box", { "Half Size": v(1.08, 0.7, 0.3), Radius: 0.12 });
node("eyeInset", "Translate", { Geometry: "eyeHole", Offset: v(0, 0, -0.23) });
node("eyeCut", "Difference", { A: "eyeBlank", B: "eyeInset" });
part("eyecup", "Raised rear eyecup", "eyeCut", "Black", v(4.2, 7.65, -4.8));
box("eyeGlass", "Recessed eyecup window", [1.04, 0.65, 0.02], [4.2, 7.65, -4.75], "Glass", 0.015);
// The public radius scales all optical parts radially about the lens center.
// Depth stays fixed, so adjusting radius keeps the bore, trim, glass and flare coherent.
const lensPartStart = parts.length;
node("barrel", "Cylinder", { Radius: 1.8, "Half Height": 0.48 });
node("barrelAxis", "Rotate", { Geometry: "barrel", Angles: v(Math.PI / 2, 0, 0) });
node("lensBore", "Profile Polygon", { Vertices: [[0, 0], [1.12, 0], [1.62, 0.62], [0, 0.62]].map(([x, y]) => ({ x, y })) });
node("lensBoreLathe", "Revolve"); ops.push(wire("lensBore", "Profile", "lensBoreLathe", "Profile"));
node("lensBoreAxis", "Rotate", { Geometry: "lensBoreLathe", Angles: v(Math.PI / 2, 0, 0) });
node("lensBoreFront", "Translate", { Geometry: "lensBoreAxis", Offset: v(0, 0, 0.05) });
node("barrelCut", "Difference", { A: "barrelAxis", B: "lensBoreFront" });
part("lensBarrel", "Recessed lens barrel", "barrelCut", "Lens", v(-0.05, 5.7, 2.04));
lathe("lensRearFlare", "Rear lens flare", [[0, -0.21], [1.5, -0.21], [1.87, 0.16], [0, 0.16]], [-0.05, 5.7, 1.76], "Black");
lathe("lensTrim", "Cream annular lens rim", [[1.69, -0.04], [1.8, -0.04], [1.8, 0.04], [1.69, 0.04]], [-0.05, 5.7, 2.52], "Cream");
cylinder("lensGlass", "Dark optical glass", 1.11, 0.04, [-0.05, 5.7, 2.13], "Glass");
const lensParts = parts.splice(lensPartStart);
let lensAssembly = lensParts[0];
for (let i = 1; i < lensParts.length; i++)
  lensAssembly = node(`lensAssembly${i}`, "Union", { A: lensAssembly, B: lensParts[i] });
ops.push({ op: "add_node", ref: "lensFactor", type: "Behavior Expression", source: "r / 1.8" },
  wire("lensRadius", "Value", "lensFactor", "r"),
  { op: "add_node", ref: "lensFactors", type: "Shader Vector3" },
  { op: "set_input", node: "lensFactors", pin: "Z", value: 1 },
  wire("lensFactor", "Result", "lensFactors", "X"), wire("lensFactor", "Result", "lensFactors", "Y"));
node("lensOrigin", "Translate", { Geometry: lensAssembly, Offset: v(0.05, -5.7, -2.04) });
node("lensRadialScale", "Scale", { Geometry: "lensOrigin" });
ops.push(wire("lensFactors", "Value", "lensRadialScale", "Factors"));
node("lensRestored", "Translate", { Geometry: "lensRadialScale", Offset: v(-0.05, 5.7, 2.04) });
parts.push("lensRestored");
checkpoints.push(ops.length);
cylinder("shutterCollar", "Cream shutter collar", 0.64, 0.08, [-2.9, 4.75, 1.61], "Cream");
cylinder("shutter", "Red shutter button", 0.53, 0.12, [-2.9, 4.75, 1.75], "Red");
box("finderBezel", "Front viewfinder bevel", [1.02, 0.9, 0.09], [3.05, 6.55, 1.61], "Cream", 0.09);
box("finderGlass", "Dark front viewfinder window", [0.93, 0.8, 0.035], [3.05, 6.55, 1.735], "Glass", 0.025);
cylinder("finderOptic", "Viewfinder internal optical surface", 0.57, 0.017, [3.05, 6.55, 1.785], "Lens");
lathe("knob", "Exposure adjustment knob", [[0, -0.16], [0.52, -0.16], [0.63, 0.13], [0.5, 0.36], [0, 0.36]], [3, 4.55, 1.78], "Black");
box("knobIndex", "Exposure index dash", [0.045, 0.14, 0.018], [3, 5.3, 1.56], "White", 0.015);
box("oneStepPlate", "Raised OneStep badge", [1.0, 0.42, 0.026], [-2.95, 6.3, 1.56], "Label", 0.02);
box("filmDoorPlate", "Polaroid film door nameplate", [4.45, 0.46, 0.035], [0, 1.5, 4.94], "Label", 0.025);
box("flashShoe", "Top accessory shoe", [1.8, 0.08, 0.48], [0, 7.98, 0.8], "Metal", 0.06);
box("flashShoeInset", "Accessory shoe central slot", [1.1, 0.013, 0.12], [0, 8.074, 0.8], "Black", 0.01);
box("slotRoller", "Visible film slot roller", [4.16, 0.035, 0.13], [0, 0.41, 4.55], "Cream", 0.025);
let result = parts[0];
for (let i = 1; i < parts.length; i++) result = node(`assembly${i}`, "Union", { A: result, B: parts[i] });
node("centered", "Translate", { Geometry: result, Offset: v(0, -4.2, 0) });
node("scaled", "Scale", { Geometry: "centered" }); ops.push(wire("size", "Value", "scaled", "Factors"));
ops.push(geo("scaled", "output"));
checkpoints.push(ops.length);
buildLargeModel("PolaroidOneStep", "Single-image reconstruction in editable named 3D parts: convex housing profiles, recessed film slot, lathed lens and controls, material slots, and public scale/lens radius. Hidden surfaces are inferred.", ops);

function prototype(name, components, behaviors = []) {
  if (!existsSync(resolve(project, `content/prototypes/${name}.kdl`)))
    call("buildPrototype", { kind: "prototype", name, create: true, operations: [
      ...Object.entries(components).map(([component, fields]) => ({ op: "add_component", component, fields })),
      ...behaviors.map(behavior => ({ op: "add_behavior", behavior })),
    ] });
  call("validateAsset", { kind: "prototype", name });
}
prototype("PolaroidOneStep", {
  GameTransform: {}, GameOrientation: {}, GameVisualTransform: { scale: 22, pitch: 0.301598, yaw: 0.124322, roll: -1.277962 },
  GameGeometry3D: { width: 1, height: 1, depth: 1 }, GameSdfSurface3D: { enabled: true },
  GameShader: { shape: "PolaroidOneStep", material: "PolaroidIvory" },
  GameMaterial: { absorption: 1, edge_light: 1, emission_multiplier: 1 },
});
prototype("PolaroidBackdrop", {
  GameTransform: {},
  GameAppearance: { shape: "Box", width: 800, height: 600, color: 0xd4bd7aff, layer: -100 },
  GameMaterial: { scene: 0xd4bd7aff, albedo: 0xd4bd7aff, absorption: 0, emission_multiplier: 1 },
});
// Orbit is authored behavior; model construction and material coordinates stay fixed.
build("behavior", "PolaroidOrbit", "Slow orbit and slight banking reveal the full inferred reconstruction volume.", [
  { op: "add_variable", name: "elapsed", ref: "elapsed", type: "float", default: 0 },
  { op: "add_node", ref: "tick", type: "Event", reference_name: "Tick" },
  { op: "add_node", ref: "advance", type: "Behavior Expression", source: "elapsed += dt" },
  { op: "bind_input", node: "advance", pin: "elapsed", kind: "variable", variable: "elapsed" },
  { op: "bind_input", node: "advance", pin: "dt", kind: "delta_time" },
  { op: "add_node", ref: "orbit", type: "Behavior Expression", source: "pitch = 0.301598 + elapsed * 0.35" },
  { op: "bind_input", node: "orbit", pin: "elapsed", kind: "variable", variable: "elapsed" },
  { op: "bind_input", node: "orbit", pin: "pitch", kind: "component", component: "GameVisualTransform", field: "pitch" },
  { op: "connect", from: "tick", from_pin: "Then", to: "advance", to_pin: "Exec", kind: "exec" },
  { op: "connect", from: "advance", from_pin: "Then", to: "orbit", to_pin: "Exec", kind: "exec" },
]);
function scene(name, visual, orbit = false) {
  if (!existsSync(resolve(project, `content/scenes/${name}.kdl`)))
    call("buildScene", { kind: "scene", name, create: true, operations: [
      { op: "add_entity", name: "Camera", prototype: "StudyCamera", components: {
        GameCamera: { width: 520, height: 390, focal_distance: 650 },
        GameHRC2D: { surface_relief: 1.5, ambient: 0.8, fog: 0, bloom: 0, bounce_intensity: 0.2, lighting_margin: 110, falloff: 0.4 },
      } },
      { op: "add_entity", name: "Warm upper-left key", prototype: "StudyLight", components: {
        GameTransform: { x: -315, y: -235 }, GameAppearance: { width: 90, height: 90 },
        GameMaterial: { emission_color: 0xffe0adff, emission_intensity: 0.06 },
      } },
      { op: "add_entity", name: "Lower-right fill", prototype: "StudyLight", components: {
        GameTransform: { x: 325, y: 230 }, GameAppearance: { width: 100, height: 100 },
        GameMaterial: { emission_color: 0xa9b9d4ff, emission_intensity: 0.025 },
      } },
      { op: "add_entity", name: "Reconstructed Polaroid", prototype: "PolaroidOneStep", behaviors: orbit ? ["PolaroidOrbit"] : [],
        components: { GameTransform: { x: 6, y: 22 }, GameVisualTransform: visual } },
      { op: "add_entity", name: "Warm photographic backdrop", prototype: "PolaroidBackdrop" },
    ] });
  call("validateAsset", { kind: "scene", name });
}
scene("ModelPropStudy", { scale: 22, pitch: 0.301598, yaw: 0.124322, roll: -1.277962 });
scene("PolaroidFrontStudy", { scale: 22, pitch: 0, yaw: 0, roll: -Math.PI / 2 });
scene("PolaroidOrbitStudy", { scale: 22, pitch: 0.301598, yaw: 0.124322, roll: -1.277962 }, true);
if (process.argv.includes("--preview")) {
  const evidence = resolve(project, "../../build/model-evidence"); mkdirSync(evidence, { recursive: true });
  for (const name of ["ModelPropStudy", "PolaroidFrontStudy", "PolaroidOrbitStudy"]) {
    const preview = call("previewScene", { kind: "scene", name, width: 1040, height: 780,
      frames: 0, render_frames: 2, freeze_simulation: true });
    for (const item of preview.content || []) if (item.type === "image")
      writeFileSync(resolve(evidence, `${name}.png`), Buffer.from(item.data, "base64"));
    writeFileSync(resolve(evidence, `${name}.json`), JSON.stringify(preview.structuredContent, null, 2));
  }
}
