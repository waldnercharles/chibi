#!/usr/bin/env node
// Surface detail examples authored only through the public MCP stdio adapter.
import { spawnSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, writeFileSync, renameSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { deflateSync } from "node:zlib";
import { createHash } from "node:crypto";

const project = dirname(fileURLToPath(import.meta.url));
const engine = resolve(project, "../..");
const controller = process.env.CHIBI_CTL || resolve(engine, "build/chibi_ctl");
const evidence = resolve(engine, "build/model-evidence");
function call(name, args = {}) {
  if (name === "buildGraph" && args.kind === "model" && args.create)
    args = { ...args, operations: [{ op: "remove_node", node: 3 }, ...args.operations] };
  const run = spawnSync(process.execPath, [resolve(engine, "tools/mcp/server.mjs")], {
    cwd: engine, env: { ...process.env, CHIBI_ROOT: project, CHIBI_CTL: controller },
    input: JSON.stringify({ jsonrpc: "2.0", id: 1, method: "tools/call", params: { name, arguments: args } }) + "\n",
    encoding: "utf8", maxBuffer: 64 * 1024 * 1024,
  });
  if (run.status !== 0) throw Error(run.stderr);
  const response = JSON.parse(run.stdout.trim());
  if (response.error || response.result?.isError) throw Error(JSON.stringify(response));
  if (name === "previewScene") {
    mkdirSync(evidence, { recursive: true });
    for (const item of response.result.content || []) if (item.type === "image")
      writeFileSync(resolve(evidence, `${args.name}.png`), Buffer.from(item.data, "base64"));
  }
  console.log(`${name}: ${args.name || args.query || ""}`);
  return response.result.structuredContent.result;
}
const status = call("getProjectStatus");
if (resolve(status.project_root) !== project) throw Error("Wrong project root; no asset writes performed");
for (const query of ["Shader Bump", "Shader Normal Map", "Shader Triplanar Texture", "Shader Decal Mask"])
  if (!call("searchNodes", { query, limit: 10 }).nodes.length) throw Error(`Rebuild the controller before authoring ${query}`);
const v = (x, y, z) => z === undefined ? { x, y } : { x, y, z };
const wire = (from, from_pin, to, to_pin) => ({ op: "connect", from, from_pin, to, to_pin, kind: "data" });
const geo = (from, to, pin = "Geometry") => wire(from, "Geometry", to, pin);
function node(ref, type, inputs = {}, metadata = {}) {
  return [{ op: "add_node", ref, type, ...metadata }, ...Object.entries(inputs).map(([pin, value]) => ({
    op: "set_input", node: ref, pin, value,
    ...(type.startsWith("Shader") && value?.x !== undefined ? { type: value.z === undefined ? "ShaderVec2" : "ShaderVec3" } : {}),
  }))];
}
const input = (pin, value) => ({ op: "set_input", node: "output", pin, value });
function graph(kind, name, description, operations) {
  if (!existsSync(resolve(project, `content/${kind}s/${name}.kdl`)))
    call("buildGraph", { kind, name, create: true, description, operations });
  else if (kind === "material" && name.startsWith("DetailPolaroid") && process.argv.includes("--refresh-new-materials")) {
    const current = call("readAsset", { kind, name });
    const removed = current.document.graphs[0].nodes.filter(n => n.type !== "Shader Surface Output").map(n => ({ op: "remove_node", node: n.id }));
    const outputID = current.document.graphs[0].nodes.find(n => n.type === "Shader Surface Output").id;
    const rewritten = operations.map(op => {
      const result = { ...op };
      for (const key of ["node", "from", "to"]) if (result[key] === "output") result[key] = outputID;
      return result;
    });
    call("buildGraph", { kind, name, expected_hash: current.hash, description, operations: [...removed, ...rewritten] });
  }
  call("validateAsset", { kind, name });
  call("inspectGraph", { kind, name });
}
function prototype(name, components) {
  if (!existsSync(resolve(project, `content/prototypes/${name}.kdl`)))
    call("buildPrototype", { kind: "prototype", name, create: true, operations:
      Object.entries(components).map(([component, fields]) => ({ op: "add_component", component, fields })) });
  call("validateAsset", { kind: "prototype", name });
}
function scene(name, description, entities) {
  if (!existsSync(resolve(project, `content/scenes/${name}.kdl`)))
    call("buildScene", { kind: "scene", name, create: true, operations:
      entities.map(entity => ({ op: "add_entity", ...entity })) });
  call("validateAsset", { kind: "scene", name });
}
// These are generated data, never edits to a reference photograph.
function crc(bytes) {
  let value = 0xffffffff;
  for (const byte of bytes) {
    value ^= byte;
    for (let bit = 0; bit < 8; bit++) value = (value >>> 1) ^ ((value & 1) ? 0xedb88320 : 0);
  }
  return (value ^ 0xffffffff) >>> 0;
}
function chunk(name, bytes) {
  const type = Buffer.from(name), length = Buffer.alloc(4), checksum = Buffer.alloc(4);
  length.writeUInt32BE(bytes.length); checksum.writeUInt32BE(crc(Buffer.concat([type, bytes])));
  return Buffer.concat([length, type, bytes, checksum]);
}
function png(name, pixel) {
  const side = 64, header = Buffer.alloc(13), rows = Buffer.alloc(side * (1 + 4 * side));
  header.writeUInt32BE(side, 0); header.writeUInt32BE(side, 4); header[8] = 8; header[9] = 6;
  for (let y = 0; y < side; y++) for (let x = 0; x < side; x++) {
    const color = pixel(x / side, y / side, x, y);
    for (let c = 0; c < 4; c++) rows[y * (1 + side * 4) + 1 + x * 4 + c] = Math.round(color[c] * 255);
  }
  const bytes = Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]), chunk("IHDR", header), chunk("IDAT", deflateSync(rows)), chunk("IEND", Buffer.alloc(0))]);
  const file = resolve(project, `content/images/${name}.png`);
  mkdirSync(dirname(file), { recursive: true });
  if (existsSync(file) && !readFileSync(file).equals(bytes)) throw Error(`Generated image changed outside this script: ${file}`);
  if (!existsSync(file)) writeFileSync(file, bytes);
  return `content/images/${name}.png`;
}
const heightImage = png("DetailHeight", (u, w) => {
  const h = .5 + .35 * Math.sin(u * 2 * Math.PI) + .1 * Math.sin(w * 4 * Math.PI);
  return [h, h, h, 1];
});
const normalImage = png("DetailNormal", (u, w) => {
  const x = -.5 * Math.cos(u * 2 * Math.PI), y = -.15 * Math.cos(w * 4 * Math.PI), length = Math.hypot(x, y, 1);
  return [x / length * .5 + .5, y / length * .5 + .5, .5 / length + .5, 1];
});
const colorImage = png("DetailChecker", (u, w, x, y) => {
  const warm = ((x >> 3) + (y >> 3)) % 2;
  return warm ? [.72, .25, .04, 1] : [.035, .16, .35, 1];
});
const labelImage = png("DetailLabel", (u, w, x, y) => {
  const band = x > 5 && x < 58 && y > 8 && y < 55;
  const mark = band && ((x >= 10 && x < 17 && y > 17 && y < 45) ||
    (x >= 26 && x < 33 && y > 17 && y < 45) || (x >= 42 && x < 49 && y > 17 && y < 45));
  return mark ? [.02, .035, .045, 1] : band ? [.92, .76, .32, 1] : [.03, .04, .05, 1];
});
const base = [input("Base Color", v(.11, .28, .49)), input("Roughness", .48)];
graph("material", "DetailNeutral", "Neutral smooth control for surface detail comparisons.", base);
graph("material", "DetailBump", "World-height procedural ridges perturb normals only; the tile's silhouette and distance graph are unchanged.", [
  ...base, ...node("partPoint", "Shader PartPosition"),
  ...node("ridgeHeight", "Behavior Expression", {}, { source: "0.8 * sin(p.x * 0.6)" }),
  wire("partPoint", "Value", "ridgeHeight", "p"), ...node("ridgeNormal", "Shader Bump", { Distance: .01 }),
  wire("ridgeHeight", "Result", "ridgeNormal", "Height"), wire("ridgeNormal", "Value", "output", "Normal"),
]);
graph("material", "DetailNormalMapped", "Linear generated tangent normal data follows planar part UVs through object and nonuniform part transforms.", [
  ...base, ...node("partUV", "Shader Planar Mapping", { Scale: v(.08, .08) }),
  ...node("normalData", "Shader Image Texture", { Image: normalImage, "Color Space": 1 }),
  wire("partUV", "UV", "normalData", "UV"), ...node("detailNormal", "Shader Normal Map", { Distance: .01 }),
  wire("normalData", "Color", "detailNormal", "Color"), wire("partUV", "UV", "detailNormal", "UV"),
  wire("detailNormal", "Value", "output", "Normal"),
]);
graph("material", "DetailHeightMapped", "Linear generated height texture perturbs normals with world-unit relief and explicit derivative spacing.", [
  ...base, ...node("partUV", "Shader Planar Mapping", { Scale: v(.08, .08) }),
  ...node("heightData", "Shader Image Texture", { Image: heightImage, "Color Space": 1 }),
  wire("partUV", "UV", "heightData", "UV"), ...node("height", "Behavior Expression", {}, { source: "(color.x - 0.5) * 1.5" }),
  wire("heightData", "Color", "height", "color"), ...node("detailNormal", "Shader Bump", { Distance: .01 }),
  wire("height", "Result", "detailNormal", "Height"), wire("detailNormal", "Value", "output", "Normal"),
]);
graph("material", "DetailTriplanar", "One image resource blends three planar samples using the mapped-space geometric normal. Material sampling does not add SDF march primitives.", [
  input("Roughness", .6), ...node("partPoint", "Shader PartPosition"),
  ...node("texturePoint", "Shader Mapping3D", { Scale: v(.065, .065, .065) }),
  wire("partPoint", "Value", "texturePoint", "Position"),
  ...node("paint", "Shader Triplanar Texture", { Image: colorImage, Sharpness: 4, Distance: .01 }),
  wire("texturePoint", "Value", "paint", "Position"), wire("paint", "Color", "output", "Base Color"),
]);
graph("material", "DetailCylindrical", "Repeating checker wrap around the named part's Y axis with two texture repeats along its length.", [
  input("Roughness", .5), ...node("barrelUV", "Shader Cylindrical Mapping", { Axis: 1, Scale: v(4, .05) }),
  ...node("paint", "Shader Image Texture", { Image: colorImage }),
  wire("barrelUV", "UV", "paint", "UV"), wire("paint", "Color", "output", "Base Color"),
]);
function decalOperations(image, center, half, baseColor) {
  return [input("Roughness", .5), ...node("partPoint", "Shader PartPosition"),
    ...node("decalPoint", "Shader Mapping3D", { Offset: center }), wire("partPoint", "Value", "decalPoint", "Position"),
    ...node("decalUV", "Shader Planar Mapping", { Axis: 2, Scale: v(1 / (2 * half.x), 1 / (2 * half.y)), Offset: v(.5, .5) }),
    wire("decalPoint", "Value", "decalUV", "Position"),
    ...node("decalMask", "Shader Decal Mask", { "Half Size": half, Facing: .05, Feather: .08 }), wire("decalPoint", "Value", "decalMask", "Position"),
    ...node("labelColor", "Shader Image Texture", { Image: image, Wrap: 1 }), wire("decalUV", "UV", "labelColor", "UV"),
    ...node("paint", "Behavior Expression", {}, { source: `mix(vec3(${baseColor.x}, ${baseColor.y}, ${baseColor.z}), color, mask)` }),
    wire("labelColor", "Color", "paint", "color"), wire("decalMask", "Value", "paint", "mask"), wire("paint", "Result", "output", "Base Color")];
}
function homography(corners) {
  const rows = [];
  for (const [[u, w], [x, y]] of [[0, 0], [1, 0], [1, 1], [0, 1]].map((uv, i) => [uv, corners[i]])) {
    rows.push([u, w, 1, 0, 0, 0, -x * u, -x * w, x], [0, 0, 0, u, w, 1, -y * u, -y * w, y]);
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
function photographedDecal(corners, center, half, baseColor) {
  const h = homography(corners), operations = decalOperations("content/images/PolaroidReference.jpg", center, half, baseColor);
  // Replace the planar UV output wire with the same reference homography used
  // by the preserved baseline. The finite mask is independent of that mapping.
  const old = operations.findIndex(op => op.op === "connect" && op.to === "labelColor" && op.to_pin === "UV");
  operations.splice(old, 1);
  operations.push(...node("photoUV", "Shader Vector2"));
  for (let axis = 0; axis < 2; axis++) {
    const u = `(p.x + ${half.x}) / ${2 * half.x}`, w = `(${half.y} - p.y) / ${2 * half.y}`;
    operations.push(...node(`photoCoordinate${axis}`, "Behavior Expression", {}, {
      source: `(${h[axis * 3]} * (${u}) + ${h[axis * 3 + 1]} * (${w}) + ${h[axis * 3 + 2]}) / (${h[6]} * (${u}) + ${h[7]} * (${w}) + 1)`,
    }), wire("decalPoint", "Value", `photoCoordinate${axis}`, "p"), wire(`photoCoordinate${axis}`, "Result", "photoUV", axis ? "Y" : "X"));
  }
  operations.push(wire("photoUV", "Value", "labelColor", "UV"));
  return operations;
}
graph("material", "DetailDecal", "Front-facing finite label mask: no extra badge geometry and no change to the model silhouette.",
  decalOperations(labelImage, v(0, 0, -20), v(18, 3.5, .25), v(.11, .28, .49)));
graph("model", "DetailTile", "One rounded box shared by smooth, bump, normal and label controls.", [
  ...node("tile", "Model Rounded Box", { "Half Size": v(28, 6, 20), Radius: 2 }),
  ...node("part", "Model Part", {}, { reference_name: "Tile" }), geo("tile", "part"), geo("part", "output"),
]);
const ridgeOps = [...node("tile", "Model Rounded Box", { "Half Size": v(28, 6, 20), Radius: 2 })];
let ridgeUnion = "tile";
for (let i = 0; i < 9; i++) {
  ridgeOps.push(...node(`ridge${i}`, "Model Rounded Box", { "Half Size": v(.6, .7, 18), Radius: .3 }),
    ...node(`place${i}`, "Model Translate", { Offset: v(-24 + i * 6, 6.4, 0) }), geo(`ridge${i}`, `place${i}`),
    ...node(`join${i}`, "Model Union"), geo(ridgeUnion, `join${i}`, "A"), geo(`place${i}`, `join${i}`, "B"));
  ridgeUnion = `join${i}`;
}
ridgeOps.push(geo(ridgeUnion, "output"));
graph("model", "DetailModeledRidges", "Geometric ridge control adds nine rounded boxes, real surface relief and silhouette changes.", ridgeOps);
graph("model", "DetailBarrel", "Named cylindrical part for the shared cylindrical image mapping example.", [
  ...node("barrel", "Model Cylinder", { Radius: 19, "Half Height": 24 }), ...node("part", "Model Part", {}, { reference_name: "Barrel" }),
  geo("barrel", "part"), geo("part", "output"),
]);
function solid(model, material, visual = {}) {
  return { GameTransform: {}, GameOrientation: {}, GameVisualTransform: { scale: 1, pitch: .3, yaw: .16, roll: -.18, ...visual },
    GameGeometry3D: { width: 1, height: 1, depth: 1 }, GameSdfSurface3D: { enabled: true },
    GameShader: { shape: model, material }, GameMaterial: { absorption: 1, edge_light: 1, emission_multiplier: 1 } };
}
for (const [name, model, material, visual] of [
  ["DetailSmoothTile", "DetailTile", "DetailNeutral"], ["DetailBumpTile", "DetailTile", "DetailBump"],
  ["DetailNormalTile", "DetailTile", "DetailNormalMapped"], ["DetailHeightTile", "DetailTile", "DetailHeightMapped"],
  ["DetailReferenceTile", "DetailModeledRidges", "DetailNeutral"], ["DetailPaintTile", "DetailTile", "DetailTriplanar"],
  ["DetailLabelTile", "DetailTile", "DetailDecal", { pitch: .1, roll: -1.1 }],
  ["DetailWrappedBarrel", "DetailBarrel", "DetailCylindrical", { pitch: .3, yaw: .25, roll: -.2 }],
]) prototype(name, solid(model, material, visual));
const studyLights = [
  { name: "Camera", prototype: "StudyCamera", components: { GameCamera: { width: 380, height: 240, focal_distance: 800 }, GameHRC2D: { surface_relief: 0, ambient: .14, bloom: .01 } } },
  { name: "Key", prototype: "StudyLight", components: { GameTransform: { x: -150, y: -90 } } },
  { name: "Fill", prototype: "StudyLight", components: { GameTransform: { x: 145, y: 80 } } },
];
scene("ModelSurfaceDetailStudy", "Top row: smooth, procedural bump, modeled ridges. Bottom row: tangent normal image, linear height image and triplanar checker. Smooth and normal-only variants share the exact distance graph and bounds.", [
  ...studyLights, ...["DetailSmoothTile", "DetailBumpTile", "DetailReferenceTile", "DetailNormalTile", "DetailHeightTile", "DetailPaintTile"].map((prototype, i) => ({
    name: prototype, prototype, components: { GameTransform: { x: -100 + (i % 3) * 100, y: i < 3 ? -55 : 55 } },
  })),
]);
scene("ModelMappingDetailStudy", "Finite label and cylindrical image wrap under actor transforms.", [
  ...studyLights, { name: "Bounded front label", prototype: "DetailLabelTile", components: { GameTransform: { x: -65, y: 0 } } },
  { name: "Cylindrical checker", prototype: "DetailWrappedBarrel", components: { GameTransform: { x: 65, y: 0 } } },
]);

// Replay the existing source through ordinary graph operations. Removed marks
// become materials; retained housing, optics and controls keep their geometry.
const baseline = call("readAsset", { kind: "model", name: "PolaroidOneStep" });
const sourceGraph = baseline.document.graphs[0];
const sourceNodes = new Map(sourceGraph.nodes.map(n => [n.id, n]));
const edges = sourceGraph.edges.map(edge => {
  const [from, from_pin] = edge.from.split("."), [to, to_pin] = edge.to.split(".");
  return { from: +from, from_pin, to: +to, to_pin };
});
const dropped = new Set(["Exposure index dash", "Raised OneStep badge", "Polaroid film door nameplate", "Accessory shoe central slot"]);
const materialReplacements = { PolaroidBlack: "DetailPolaroidGrip", PolaroidIvory: "DetailPolaroidFace", PolaroidLens: "DetailPolaroidLens", PolaroidMetal: "DetailPolaroidMetal" };
graph("material", "DetailPolaroidGrip", "Finite film-door decal and subtle molded-plastic bump replace small label geometry. Part coordinates stay in the baseline construction frame.", [
  ...photographedDecal([[.23575, .71637], [.62774, .80556], [.62500, .86330], [.24178, .77632]], v(0, -1.5, -4.9), v(4.45, .46, .16), v(.025, .026, .022)),
  ...node("grainHeight", "Behavior Expression", {}, { source: "0.025 * sin(p.x * 12) * sin(p.y * 11)" }),
  wire("partPoint", "Value", "grainHeight", "p"), ...node("grainNormal", "Shader Bump", { Distance: .01 }),
  wire("grainHeight", "Result", "grainNormal", "Height"), wire("grainNormal", "Value", "output", "Normal"),
]);
graph("material", "DetailPolaroidFace", "Attributed OneStep photograph badge projected onto the retained ivory front, plus a painted exposure dash; no raised badge geometry.", [
  ...photographedDecal([[.31579, .38889], [.40241, .40058], [.39748, .45249], [.31689, .44079]], v(2.95, -6.3, -1.53), v(1, .42, .14), v(.8, .77, .66)),
  ...node("indexPoint", "Shader Mapping3D", { Offset: v(-3, -5.3, -1.53) }), wire("partPoint", "Value", "indexPoint", "Position"),
  ...node("indexMask", "Shader Decal Mask", { "Half Size": v(.045, .14, .14), Feather: .008, Facing: .05 }),
  wire("indexPoint", "Value", "indexMask", "Position"),
  ...node("indexedPaint", "Behavior Expression", {}, { source: "mix(color, vec3(0.75, 0.72, 0.61), indexMask)" }),
  wire("paint", "Result", "indexedPaint", "color"), wire("indexMask", "Value", "indexedPaint", "indexMask"), wire("indexedPaint", "Result", "output", "Base Color"),
]);
graph("material", "DetailPolaroidMetal", "Dark accessory-shoe recess represented by a bounded material mask on retained metal geometry.", [
  input("Roughness", .38), input("Metallic", .7), ...node("partPoint", "Shader PartPosition"),
  ...node("slotPoint", "Behavior Expression", {}, { source: "vec3(p.x, p.z - 0.8, p.y - 8.06)" }), wire("partPoint", "Value", "slotPoint", "p"),
  ...node("slotMask", "Shader Decal Mask", { "Half Size": v(1.1, .12, .04), Feather: .01, Facing: .05 }),
  wire("slotPoint", "Result", "slotMask", "Position"),
  ...node("paint", "Behavior Expression", {}, { source: "mix(vec3(0.34, 0.33, 0.27), vec3(0.025, 0.026, 0.022), slotMask)" }),
  wire("slotMask", "Value", "paint", "slotMask"), wire("paint", "Result", "output", "Base Color"),
]);
graph("material", "DetailPolaroidLens", "Concentric lens relief perturbs normals while the bore and rim remain real geometry.", [
  input("Base Color", v(.025, .026, .023)), input("Roughness", .32), ...node("partPoint", "Shader PartPosition"),
  ...node("grooveHeight", "Behavior Expression", {}, { source: "0.016 * sin(sqrt((p.x + 0.05) * (p.x + 0.05) + (p.y - 5.7) * (p.y - 5.7)) * 150)" }),
  wire("partPoint", "Value", "grooveHeight", "p"), ...node("grooveNormal", "Shader Bump", { Distance: .008 }),
  wire("grooveHeight", "Result", "grooveNormal", "Height"), wire("grooveNormal", "Value", "output", "Normal"),
]);
const cloneOps = [], aliases = new Map(), checkpoints = [];
let firstPartRef;
function clone(id) {
  if (aliases.has(id)) return aliases.get(id);
  const n = sourceNodes.get(id);
  if (!n) throw Error(`Missing baseline node ${id}`);
  if (n.type === "Model Part" && dropped.has(n.reference_name)) return null;
  const incoming = edges.filter(e => e.to === id), dependencies = [];
  for (const edge of incoming) dependencies.push({ ...edge, alias: clone(edge.from) });
  if (n.type === "Model Union") {
    const a = dependencies.find(e => e.to_pin === "A")?.alias, b = dependencies.find(e => e.to_pin === "B")?.alias;
    if (!a || !b) { const alias = a || b; aliases.set(id, alias); return alias; }
  }
  const ref = `baseline${id}`, metadata = {};
  for (const key of ["reference_name", "reference_asset", "reference_id"])
    if (n[key] !== undefined) metadata[key] = key === "reference_asset" ? materialReplacements[n[key]] || n[key] : n[key];
  if (n.type === "Behavior Expression") { metadata.source = n.reference_name; delete metadata.reference_name; }
  cloneOps.push({ op: "add_node", ref, type: n.type, ...metadata });
  aliases.set(id, ref);
  for (const [pin, setting] of Object.entries(n.inputs || {})) {
    const value = setting && typeof setting === "object" && Object.hasOwn(setting, "default") ? setting.default : setting;
    if (setting && typeof setting === "object" && !Object.hasOwn(setting, "default")) continue;
    cloneOps.push({ op: "set_input", node: ref, pin, value, ...(setting?.type_name ? { type: setting.type_name } : {}) });
  }
  for (const edge of dependencies) if (edge.alias) cloneOps.push(wire(edge.alias, edge.from_pin, ref, edge.to_pin));
  if (n.type === "Model Part") { firstPartRef ||= ref; checkpoints.push(cloneOps.length); }
  return ref;
}
const out = sourceGraph.nodes.find(n => n.type === "Model Output");
const rootEdge = edges.find(e => e.to === out.id && e.to_pin === "Geometry");
const clonedRoot = clone(rootEdge.from);
cloneOps.push(geo(clonedRoot, "output")); checkpoints.push(cloneOps.length);
const variant = "PolaroidSurfaceDetail", variantPath = resolve(project, `content/models/${variant}.kdl`);
const progressPath = resolve(evidence, `${variant}-detail-authoring.json`);
const digest = createHash("sha256").update(JSON.stringify({ baselineHash: baseline.hash, cloneOps })).digest("hex");
let state = existsSync(progressPath) ? JSON.parse(readFileSync(progressPath, "utf8")) : null;
if (!existsSync(variantPath) || state && !state.complete) {
  if (state && state.digest !== digest) throw Error("Baseline or author script changed during an unfinished replay; existing draft preserved");
  state ||= { digest, cursor: 0, aliases: {}, hash: null, complete: false };
  if (state.cursor && call("readAsset", { kind: "model", name: variant }).hash !== state.hash) throw Error("Draft changed outside this authoring replay");
  mkdirSync(evidence, { recursive: true });
  while (state.cursor < cloneOps.length) {
    const start = state.cursor, end = checkpoints.filter(at => at > start && at - start <= (start ? 128 : 126)).at(-1);
    if (!end) throw Error(`No complete-part boundary fits at ${start}`);
    const operations = cloneOps.slice(start, end).map(op => {
      const result = { ...op };
      for (const key of ["node", "from", "to"]) if (state.aliases[result[key]]) result[key] = state.aliases[result[key]];
      return result;
    });
    if (!start) operations.push(geo(firstPartRef, "output"));
    const result = call("buildGraph", { kind: "model", name: variant, description: "Reduced baseline Polaroid: four small marks and plates become surface detail; housing, recessed optics, controls, profiles and public parameters remain modeled.",
      operations, ...(start ? { expected_hash: state.hash } : { create: true }) });
    for (const item of result.nodes || []) state.aliases[item.ref] = item.id;
    state.cursor = end; state.hash = result.after_hash; state.complete = end === cloneOps.length;
    writeFileSync(`${progressPath}.tmp`, JSON.stringify(state, null, 2) + "\n"); renameSync(`${progressPath}.tmp`, progressPath);
  }
}
const appliedVariant = call("readAsset", { kind: "model", name: variant });
const metalSlot = appliedVariant.document.graphs[0].nodes.find(n => n.type === "Model Material Slot" && n.reference_name === "Metal");
if (metalSlot && metalSlot.reference_asset !== "DetailPolaroidMetal")
  call("buildGraph", { kind: "model", name: variant, expected_hash: appliedVariant.hash,
    operations: [{ op: "set_node", node: metalSlot.id, reference_name: metalSlot.reference_name, reference_asset: "DetailPolaroidMetal" }] });
call("validateAsset", { kind: "model", name: variant });
prototype(variant, solid(variant, "PolaroidIvory", { scale: 19, pitch: .301598, yaw: .124322, roll: -1.277962 }));
scene("PolaroidSurfaceDetailStudy", "Existing image reconstruction at left, reduced-geometry surface-detail variant at right. Reference badge crops move onto bounded material masks; the photograph baseline is preserved.", [
  { name: "Camera", prototype: "StudyCamera", components: { GameCamera: { width: 650, height: 380, focal_distance: 1100 }, GameHRC2D: { surface_relief: 0, ambient: .2, bloom: .01, intensity: 3, exposure: .6 } } },
  { name: "Key", prototype: "StudyLight", components: { GameTransform: { x: -250, y: -150 } } },
  { name: "Fill", prototype: "StudyLight", components: { GameTransform: { x: 250, y: 140 } } },
  { name: "Baseline photograph reconstruction", prototype: "PolaroidOneStep", components: { GameTransform: { x: -150, y: 0 }, GameVisualTransform: { scale: 19, pitch: .301598, yaw: .124322, roll: -1.277962 } } },
  { name: "Surface detail reconstruction", prototype: variant, components: { GameTransform: { x: 150, y: 0 } } },
]);
const reports = {};
for (const name of ["DetailTile", "DetailModeledRidges", "PolaroidOneStep", variant]) reports[name] = call("inspectGraph", { kind: "model", name });
for (const name of ["DetailBump", "DetailNormalMapped", "DetailHeightMapped", "DetailTriplanar", "DetailDecal"])
  reports[name] = call("inspectGraph", { kind: "material", name });
mkdirSync(evidence, { recursive: true });
writeFileSync(resolve(evidence, "surface-detail-authoring.json"), JSON.stringify({ baselineHash: baseline.hash, reports }, null, 2));
if (process.argv.includes("--preview")) {
  for (const name of ["ModelSurfaceDetailStudy", "ModelMappingDetailStudy", "PolaroidSurfaceDetailStudy"]) {
    const result = call("previewScene", { kind: "scene", name, width: 1200, height: 760, frames: 0, render_frames: 2, freeze_simulation: true });
    writeFileSync(resolve(evidence, `${name}.json`), JSON.stringify(result, null, 2));
    // call() also retains the public PNG content block alongside this report.
  }
}
