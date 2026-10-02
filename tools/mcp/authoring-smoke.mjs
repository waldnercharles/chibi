#!/usr/bin/env node

import {
  existsSync,
  mkdirSync,
  mkdtempSync,
  rmSync,
  writeFileSync,
} from "node:fs";
import { fixtureKdl } from "./fixture-kdl.mjs";
import { tmpdir } from "node:os";
import { spawnSync } from "node:child_process";
import { join, resolve } from "node:path";

const root = resolve(process.argv[2] || process.cwd());
const server = resolve(root, "tools/mcp/server.mjs");
const controller = process.env.CHIBI_CTL || resolve(root, "build", "chibi_ctl");
const skipGpu = process.env.CHIBI_MCP_SKIP_GPU === "1";

if (!existsSync(controller)) {
  throw new Error(`missing controller: ${controller}`);
}

function assert(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

function bytes(value) {
  return Buffer.byteLength(JSON.stringify(value), "utf8");
}

function nativeRequest(projectRoot, operation, args = {}) {
  const result = spawnSync(controller, ["--project-root", projectRoot], {
    cwd: projectRoot,
    input: `${JSON.stringify({ ...args, contract: 1, operation })}\n`,
    encoding: "utf8",
    maxBuffer: 32 * 1024 * 1024,
  });
  if (result.error) {
    throw result.error;
  }
  const line = result.stdout.trim().split("\n").filter(Boolean).at(-1);
  if (!line) {
    throw new Error(result.stderr.trim() || `${operation} returned no response`);
  }
  return JSON.parse(line);
}

function request(id, name, args) {
  return {
    jsonrpc: "2.0",
    id,
    method: "tools/call",
    params: { name, arguments: args },
  };
}

function initialize() {
  return {
    jsonrpc: "2.0",
    id: 1,
    method: "initialize",
    params: {
      protocolVersion: "2025-06-18",
      capabilities: {},
      clientInfo: { name: "authoring-smoke", version: "0.1.0" },
    },
  };
}

function runMcp(projectRoot, requests) {
  const renders = requests.some(({ params }) => params?.name === "previewShader");
  const run = spawnSync(process.execPath, [server], {
    cwd: root,
    env: {
      ...process.env,
      CHIBI_ROOT: projectRoot,
      CHIBI_CTL: controller,
      // Match the native GPU checks: Cute's shader compiler retains allocations
      // at shutdown. Keep leak checks enabled for the headless authoring runs.
      ...(renders ? { ASAN_OPTIONS: `${process.env.ASAN_OPTIONS || ""}:detect_leaks=0` } : {}),
    },
    input: `${requests.map((item) => JSON.stringify(item)).join("\n")}\n`,
    encoding: "utf8",
    maxBuffer: 64 * 1024 * 1024,
  });
  if (run.error || run.status !== 0) {
    throw run.error || new Error(run.stderr || `MCP server exited with ${run.status}`);
  }
  const lines = run.stdout.trim().split("\n").filter(Boolean);
  assert(lines.length > 0, run.stderr.trim() || "MCP server returned no responses");
  return new Map(lines.map((line) => {
    const response = JSON.parse(line);
    return [response.id, response];
  }));
}

function structured(response) {
  return response?.result?.structuredContent || {};
}

function toolText(response) {
  return (response?.result?.content || [])
    .filter((item) => item.type === "text")
    .map((item) => item.text || "")
    .join("\n");
}

function nativeAsset(projectRoot, kind, name) {
  const response = nativeRequest(projectRoot, "read_asset", { kind, name });
  assert(response.ok === true && response.result?.document, `could not read ${kind}/${name}: ${JSON.stringify(response)}`);
  return response.result;
}

function checkPngPreview(response, expectedHash, path, requestedSize) {
  const result = response?.result;
  assert(result && result.isError !== true, `preview tool failed: ${JSON.stringify(response)}`);
  const image = result.content?.find((item) => item.type === "image");
  assert(image && typeof image.data === "string" && image.data.length > 0, `preview did not return an image: ${JSON.stringify(response)}`);
  assert(image.mimeType === "image/png", `unexpected preview mime type: ${image.mimeType}`);
  const png = Buffer.from(image.data, "base64");
  assert(png.subarray(0, 8).equals(Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a])), "preview is not a PNG");
  const compact = result.structuredContent || {};
  const preview = compact.result?.preview;
  assert(preview && preview.data === undefined, "preview base64 leaked into structuredContent");
  const pngWidth = png.readUInt32BE(16);
  const pngHeight = png.readUInt32BE(20);
  assert(pngWidth === preview.width && pngHeight === preview.height, "PNG dimensions differ from preview metadata");
  if (requestedSize) {
    assert(preview.width === requestedSize.width && preview.height === requestedSize.height, "preview dimensions differ from the request");
  }
  assert(compact.result?.hash === expectedHash, `preview hash ${compact.result?.hash} does not match saved asset ${expectedHash}`);
  assert(!JSON.stringify(compact).includes(image.data), "preview base64 leaked into structuredContent text");
  assert(!toolText(response).includes(image.data), "preview base64 leaked into text content");
  writeFileSync(path, png);
  return { png, hash: compact.result.hash, bytes: png.length };
}

const projectRoot = mkdtempSync(join(tmpdir(), "chibi-mcp-authoring-"));
const behaviorName = "McpAuthoringBehavior";
const materialName = "McpAuthoringMaterial";
const shapeName = "McpAuthoringShape";
const shaderLibraryName = "McpAuthoringShaderLibrary";
const libraryConsumerName = "McpAuthoringLibraryConsumer";
const prototypeName = "McpAuthoringPrototype";
const sceneName = "McpAuthoringScene";
mkdirSync(join(projectRoot, "content", "behaviors"), { recursive: true });
mkdirSync(join(projectRoot, "content", "materials"), { recursive: true });
mkdirSync(join(projectRoot, "content", "shapes"), { recursive: true });
mkdirSync(join(projectRoot, "content", "shader-libraries"), { recursive: true });
mkdirSync(join(projectRoot, "content", "prototypes"), { recursive: true });
mkdirSync(join(projectRoot, "content", "scenes"), { recursive: true });
mkdirSync(join(projectRoot, "content", "scenarios"), { recursive: true });
// A scenario replays keyed input on the scene the batch below creates.
writeFileSync(
  join(projectRoot, "content", "scenarios", "McpAuthoringScenario.kdl"),
  fixtureKdl({scene: sceneName, frames: 3, saved_state: {best: "7"}, keys: [
    {frame: 0, movement: {x: 1, y: 0}}, {frame: 2, controls: ["Space"]},
  ]}),
);

try {
  const materialCreateArgs = {
    kind: "material",
    name: materialName,
    create: true,
    description: "A smoke test material",
    graph_description: "A deterministic authoring fixture",
    operations: [
      { op: "set_input", node: "output", pin: "Base Color", value: { x: 1, y: 0, z: 0 } },
      { op: "set_input", node: "output", pin: "Unlit", value: true },
    ],
  };
  const shapeCreateArgs = {
    kind: "shape",
    name: shapeName,
    create: true,
    operations: [
      { op: "set_bounds", value: [20, 20] },
      { op: "add_node", type: "Shader Position", ref: "position" },
      { op: "add_node", type: "Shader Circle", ref: "circle", inputs: { Radius: 16 } },
      { op: "connect", from: "position", from_pin: "Position", to: "circle", to_pin: "Position", kind: "data" },
      { op: "connect", from: "circle", from_pin: "Value", to: "output", to_pin: "Distance", kind: "data" },
    ],
  };
  const shaderLibraryCreateArgs = {
    kind: "shader-library",
    name: shaderLibraryName,
    create: true,
    operations: [
      { op: "add_graph", name: "Math", kind: "function", pure: true, ref: "math" },
      { op: "add_pin", node: "math.entry", side: "output", name: "Value", kind: "data", type: "float" },
      { op: "add_pin", node: "math.return", side: "input", name: "Result", kind: "data", type: "float" },
      { op: "connect", from: "math.entry", from_pin: "Value", to: "math.return", to_pin: "Result", kind: "data" },
    ],
  };

  const first = runMcp(projectRoot, [
    initialize(),
    { jsonrpc: "2.0", id: 2, method: "tools/list", params: {} },
    request(3, "searchNodes", { query: "Shader", limit: 3 }),
    request(4, "searchNodes", { query: "Shader Surface Output", limit: 3 }),
    request(5, "searchNodes", { query: "Shader Shape Output", limit: 3 }),
    request(27, "searchNodes", { query: "Base Color", pin_type: "ShaderVec3", domain: "shader", limit: 3 }),
    request(6, "describeRegistry", { sections: ["nodes"] }),
    request(7, "buildGraph", {
      kind: "behavior",
      name: behaviorName,
      create: true,
      operations: [{ op: "add_graph", name: "Smoke Function", kind: "function", ref: "function" }],
    }),
    request(8, "buildGraph", materialCreateArgs),
    request(9, "buildGraph", shapeCreateArgs),
    request(13, "buildGraph", shaderLibraryCreateArgs),
    request(10, "readAsset", { kind: "behavior", name: behaviorName }),
    request(11, "readAsset", { kind: "material", name: materialName }),
    request(12, "readAsset", { kind: "shape", name: shapeName }),
    request(14, "readAsset", { kind: "shader-library", name: shaderLibraryName }),
    request(15, "buildPrototype", {
      kind: "prototype",
      name: prototypeName,
      create: true,
      dry_run: true,
      operations: [
        { op: "add_component", component: "GameTransform", fields: { x: 12, y: -8 } },
        { op: "add_component", component: "GameMovement", fields: { speed: 55 } },
        { op: "add_behavior", behavior: behaviorName },
      ],
    }),
    request(16, "buildPrototype", {
      kind: "prototype",
      name: prototypeName,
      create: true,
      operations: [
        { op: "add_component", component: "GameTransform", fields: { x: 12, y: -8 } },
        { op: "add_component", component: "GameMovement", fields: { speed: 55 } },
        { op: "add_behavior", behavior: behaviorName },
      ],
    }),
    request(17, "readAsset", { kind: "prototype", name: prototypeName }),
    request(18, "describeComponents", { names: ["GameMovement"] }),
    request(19, "buildScene", {
      kind: "scene",
      name: sceneName,
      create: true,
      dry_run: true,
      operations: [
        { op: "set_presentation", presentation: "3d" },
        {
          op: "add_entity",
          name: "Warden",
          prototype: prototypeName,
          components: { "GAME.GameTransform": { x: 24, y: -12 } },
          retained_components: { "GAME.GameTransform": { x: 28, y: -16 } },
          behaviors: [behaviorName],
        },
      ],
    }),
    request(20, "buildScene", {
      kind: "scene",
      name: sceneName,
      create: true,
      operations: [
        { op: "set_presentation", presentation: "3d" },
        {
          op: "add_entity",
          name: "Warden",
          prototype: prototypeName,
          components: { "GAME.GameTransform": { x: 24, y: -12 } },
          retained_components: { "GAME.GameTransform": { x: 28, y: -16 } },
          behaviors: [behaviorName],
        },
      ],
    }),
    request(21, "readAsset", { kind: "scene", name: sceneName }),
    request(22, "getExecutionStatus", { kind: "behavior", name: behaviorName }),
    request(23, "optimizeGraph", { kind: "behavior", name: behaviorName, dry_run: true }),
    request(24, "profileScenario", { kind: "prototype", name: prototypeName, frames: 2, warmup_frames: 1 }),
    request(25, "runScenario", { kind: "scene", name: sceneName, frames: 1 }),
    request(26, "captureRuntimeSnapshot", { kind: "scene", name: sceneName, frames: 1, entity_limit: 4 }),
    request(28, "profileScenario", { scenario: "McpAuthoringScenario" }),
    request(29, "runScenario", { scenario: "McpAuthoringScenario", kind: "scene" }),
    request(30, "validateAsset", { kind: "scenario", name: "McpAuthoringScenario" }),
    request(31, "runScenario", { scenario: "McpAuthoringScenario" }),
    request(32, "profileScenario", { kind: "prototype", name: prototypeName, frames: 2, warmup_frames: 1, behavior_details: false }),
  ]);

  const tools = first.get(2)?.result?.tools || [];
  const previewTool = tools.find((tool) => tool.name === "previewShader");
  const graphTool = tools.find((tool) => tool.name === "buildGraph");
  const editPrototypeTool = tools.find((tool) => tool.name === "buildPrototype");
  const editSceneTool = tools.find((tool) => tool.name === "buildScene");
  const optimizeTool = tools.find((tool) => tool.name === "optimizeGraph");
  const profileTool = tools.find((tool) => tool.name === "profileScenario");
  const executionTool = tools.find((tool) => tool.name === "getExecutionStatus");
  const search = structured(first.get(3));
  const surfaceSearch = structured(first.get(4));
  const shapeSearch = structured(first.get(5));
  const typedSearch = structured(first.get(27));
  const registry = structured(first.get(6));
  const behaviorCreate = structured(first.get(7));
  const materialCreate = structured(first.get(8));
  const shapeCreate = structured(first.get(9));
  const shaderLibraryCreate = structured(first.get(13));
  const behaviorRead = structured(first.get(10));
  const materialRead = structured(first.get(11));
  const shapeRead = structured(first.get(12));
  const shaderLibraryRead = structured(first.get(14));
  const prototypeDryRun = structured(first.get(15));
  const prototypeCreate = structured(first.get(16));
  const prototypeRead = structured(first.get(17));
  const componentDescription = structured(first.get(18));
  const sceneDryRun = structured(first.get(19));
  const sceneCreate = structured(first.get(20));
  const sceneRead = structured(first.get(21));
  const executionStatus = structured(first.get(22));
  const optimize = structured(first.get(23));
  const profile = structured(first.get(24));
  const ordinaryProfile = structured(first.get(32));
  const sceneRun = structured(first.get(25));
  const sceneSnapshot = structured(first.get(26));
  const scenarioProfile = structured(first.get(28));
  const scenarioConflict = structured(first.get(29));
  const scenarioValidation = structured(first.get(30));
  const scenarioRun = structured(first.get(31));
  const surfaceNode = surfaceSearch.result?.nodes?.find((node) => node.type === "Shader Surface Output");
  const shapeNode = shapeSearch.result?.nodes?.find((node) => node.type === "Shader Shape Output");
  // Search lists pins as "name: type".
  const inputName = (node, name) => node?.in?.map((pin) => pin.slice(0, pin.indexOf(": "))).find((pin) => pin === name);
  const baseColorPin = inputName(surfaceNode, "Base Color");
  const distancePin = inputName(shapeNode, "Distance");
  const operationProperties = graphTool?.inputSchema?.properties?.operations?.items?.properties || {};
  const prototypeOperationProperties = editPrototypeTool?.inputSchema?.properties?.operations?.items?.properties || {};

  assert(tools.length === 31, `public tool catalog has ${tools.length} tools instead of 31`);
  assert(!tools.some((tool) => tool.name === "buildUi"), "ui documents are written as files, not built");
  assert(tools.find((tool) => tool.name === "inspectUi")?.annotations?.readOnlyHint === true, "ui inspection tool is missing");
  assert(graphTool?.inputSchema?.properties?.kind?.enum?.join(",") === "behavior,event,library,material,shape,shader-library,field,system", "graph kind selector is incomplete");
  assert(graphTool?.title && ["material", "shader", "behavior", "event", "field", "system"].every((term) => graphTool.description.includes(term)), "authoring description does not expose its intent");
  assert(previewTool?.inputSchema?.properties?.width?.maximum === 512, "preview width limit is missing");
  assert(previewTool?.inputSchema?.properties?.camera?.properties?.center && previewTool.inputSchema.properties.sample_grid?.properties?.uv_min, "preview camera or sample grid controls are missing");
  assert(!previewTool.inputSchema.properties.graph_id && !previewTool.inputSchema.properties.graph_name, "preview advertises unsupported graph selectors");
  assert(!graphTool.inputSchema.properties.layout && !operationProperties.x && !operationProperties.y, "graph placement controls should remain native and automatic");
  assert(editPrototypeTool?.inputSchema?.properties?.kind?.enum?.length === 1 && editPrototypeTool.inputSchema.properties.kind.enum[0] === "prototype", "prototype selector is missing");
  const sceneOperationProperties = editSceneTool?.inputSchema?.properties?.operations?.items?.properties || {};
  assert(editSceneTool?.inputSchema?.properties?.kind?.enum?.[0] === "scene" && editSceneTool?.annotations?.destructiveHint === true, "scene authoring tool is missing");
  assert(sceneOperationProperties.op?.enum?.includes("add_entity") && sceneOperationProperties.op.enum.includes("rename_entity") && sceneOperationProperties.components && sceneOperationProperties.behaviors && !sceneOperationProperties.x && !sceneOperationProperties.y, "scene authoring operations are missing semantic fields or expose placement controls");
  assert(optimizeTool?.inputSchema?.properties?.fanout_threshold?.maximum === 64 && optimizeTool?.annotations?.readOnlyHint === false, "graph optimizer schema or mutation annotations are missing");
  assert(profileTool?.inputSchema?.properties?.warmup_frames && executionTool?.inputSchema?.properties?.kind?.enum?.includes("system"), "profiling or execution status tools are missing");
  assert(profileTool?.inputSchema?.properties?.behavior_details?.type === "boolean" && profileTool.inputSchema.properties.behavior_details.default === true, "behavior profiling granularity schema is missing");
  assert(editPrototypeTool?.annotations?.readOnlyHint === false && editPrototypeTool?.annotations?.destructiveHint === true, "buildPrototype mutation annotations are missing");
  assert(prototypeOperationProperties.op?.enum?.includes("add_component") && prototypeOperationProperties.op?.enum?.includes("set_field") && prototypeOperationProperties.fields && prototypeOperationProperties.value, "prototype authoring operations are missing from the MCP operation schema");
  const movementDescription = componentDescription.result?.components?.[0];
  assert(movementDescription?.key === "GAME.GameMovement" && movementDescription.fields?.some((field) => field.name === "speed" && field.type === "float") && movementDescription.defaults?.speed === 0, "compact component schema omitted reflected field defaults");
  assert(graphTool?.annotations?.readOnlyHint === false && graphTool?.annotations?.destructiveHint === true, "graph mutation annotations are missing");
  assert(operationProperties.op?.enum?.includes("set_ramp") && operationProperties.op?.enum?.includes("set_bounds") && operationProperties.op?.enum?.includes("add_pin"), "shader authoring operations are missing from the MCP operation schema");
  assert(operationProperties.reference_asset && operationProperties.pure && operationProperties.side && operationProperties.stops?.maxItems === 16, "native graph edit fields are missing from the MCP operation schema");
  assert(search.ok === true && search.result?.count > 0 && search.result.count <= 3, `bounded shader search failed: ${JSON.stringify(search)}`);
  assert(typedSearch.ok === true && typedSearch.result?.nodes?.some((node) => node.type === "Shader Surface Output"), `typed pin search failed: ${JSON.stringify(typedSearch)}`);
  assert(search.result.nodes.every((node) => Array.isArray(node.in) || Array.isArray(node.out)), "bounded shader search omitted default pins");
  assert(baseColorPin === "Base Color" && distancePin === "Distance", "named shader output pins were not discoverable");
  assert(Array.isArray(registry.result?.nodes) && registry.result.nodes.length > 0, "registry is empty");
  assert(behaviorCreate.ok === true && typeof behaviorCreate.result?.after_hash === "string", `behavior create failed: ${JSON.stringify(behaviorCreate)}`);
  assert(materialCreate.ok === true && typeof materialCreate.result?.after_hash === "string", `material create failed: ${JSON.stringify(materialCreate)}`);
  assert(shapeCreate.ok === true && typeof shapeCreate.result?.after_hash === "string", `shape create failed: ${JSON.stringify(shapeCreate)}`);
  assert(shaderLibraryCreate.ok === true && typeof shaderLibraryCreate.result?.after_hash === "string", `shader library create failed: ${JSON.stringify(shaderLibraryCreate)}`);
  assert(prototypeDryRun.ok === true && prototypeDryRun.result?.dry_run === true && prototypeDryRun.result?.published === false, `prototype dry run failed: ${JSON.stringify(prototypeDryRun)}`);
  assert(prototypeDryRun.next_steps?.length > 0 && sceneDryRun.next_steps?.length > 0, "authoring results omitted next_steps");
  assert(prototypeCreate.ok === true && prototypeCreate.result?.published === true && prototypeCreate.result?.after_hash === prototypeDryRun.result?.after_hash, `prototype create failed or dry-run hash was unstable: ${JSON.stringify(prototypeCreate)}`);
  assert(materialCreate.result.nodes?.some((node) => node.ref === "output"), "material output alias was not returned");
  assert(shapeCreate.result.nodes?.some((node) => node.ref === "output") &&
    shapeCreate.result.nodes?.some((node) => node.ref === "position") &&
    shapeCreate.result.nodes?.some((node) => node.ref === "circle"), "shape aliases were not returned");
  assert(behaviorRead.ok === true && materialRead.ok === true && shapeRead.ok === true, "created asset readback failed");
  assert(materialRead.result.document.description === "A smoke test material" && materialRead.result.document.graphs?.[0]?.description === "A deterministic authoring fixture", "graph metadata was not persisted");
  assert(shapeRead.result.document.bounds_x === 20 && shapeRead.result.document.bounds_y === 20, "shape bounds edit was not persisted");
  assert(shaderLibraryRead.ok === true && shaderLibraryRead.result?.document?.domain === "shader-library", "shader library readback failed");
  assert(prototypeRead.ok === true && prototypeRead.result?.document?.components?.["GAME.GameMovement"]?.speed === 55 && prototypeRead.result.document.behaviors?.includes(behaviorName), "prototype component and behavior edits were not persisted");
  assert(sceneDryRun.ok === true && sceneDryRun.result?.dry_run === true && sceneDryRun.result?.published === false, `scene dry run failed: ${JSON.stringify(sceneDryRun)}`);
  assert(sceneCreate.ok === true && sceneCreate.result?.published === true && sceneCreate.result?.after_hash === sceneDryRun.result?.after_hash, `scene create failed or dry-run hash was unstable: ${JSON.stringify(sceneCreate)}`);
  assert(sceneRead.ok === true && sceneRead.result?.document?.presentation === "3d" && sceneRead.result.document.entities?.[0]?.name === "Warden" && sceneRead.result.document.entities[0].components?.["GAME.GameTransform"]?.x === 24, `scene readback failed: ${JSON.stringify(sceneRead)}`);
  const execution = executionStatus.result?.execution;
  assert(executionStatus.ok === true && execution?.backend === (execution.jit_available ? "sljit" : "unavailable") && execution.jit === execution.jit_available, `execution status failed: ${JSON.stringify(executionStatus)}`);
  assert(execution.scope === "behavior_events" && execution.fully_jit === (execution.ready && execution.event_entries > 0 && execution.native_event_entries === execution.event_entries), "execution status omitted verified whole-event coverage");
  assert(optimize.ok === true && optimize.result?.dry_run === true && optimize.result?.changed === false, `graph optimization dry run failed: ${JSON.stringify(optimize)}`);
  assert(profile.ok === true && profile.result?.timing?.sampled_frames === 2 && profile.result?.warmup_frames === 1 && profile.result?.allocations?.scope === "xalloc wrappers" && profile.result?.timing?.frame_ms?.max >= profile.result?.timing?.frame_ms?.p50 && profile.result?.timing?.worst_frames?.length === 2, `scenario profiling failed: ${JSON.stringify(profile)}`);
  assert(profile.result.behavior_details === true && ordinaryProfile.ok === true && ordinaryProfile.result?.behavior_details === false && ordinaryProfile.result?.behaviors?.length === 0 && ordinaryProfile.result?.world_hash === profile.result.world_hash && ordinaryProfile.result?.timing?.sampled_frames === 2 && ordinaryProfile.result?.allocations?.scope === "xalloc wrappers" && ordinaryProfile.result?.behavior_work?.sampled_updates === 2, "ordinary profile mode lost frame or endpoint evidence");
  assert(profile.result.execution?.scope === "behavior_events" && profile.result.execution.backend === execution.backend && profile.result.execution.instances === 1, `profile reported the wrong live behavior backend: ${JSON.stringify(profile.result.execution)}`);
  assert(sceneRun.ok === true && sceneRun.result?.authored_entities === 1, `scene scenario failed: ${JSON.stringify(sceneRun)}`);
  assert(scenarioProfile.ok === true && scenarioProfile.result?.frames === 3 && scenarioProfile.result?.timing?.worst_frames?.length === 3, `scenario profile failed: ${JSON.stringify(scenarioProfile)}`);
  assert(scenarioConflict.ok === false && /omit name, kind and inputs/.test(scenarioConflict.error?.message || JSON.stringify(scenarioConflict)), `scenario with a kind was not rejected: ${JSON.stringify(scenarioConflict)}`);
  assert(scenarioValidation.ok === true, `scenario validation failed: ${JSON.stringify(scenarioValidation)}`);
  assert(scenarioRun.ok === true && scenarioRun.result?.frames === 3 && scenarioRun.result?.saved_state?.best === "7", `scenario run did not start from its saved progress: ${JSON.stringify(scenarioRun)}`);
  assert(sceneSnapshot.ok === true && sceneSnapshot.result?.entity_count === 1 && sceneSnapshot.result?.entities?.length === 1, `scene runtime snapshot failed: ${JSON.stringify(sceneSnapshot)}`);
  const sceneRenameRun = runMcp(projectRoot, [
    initialize(),
    request(2, "buildScene", {
      kind: "scene",
      name: sceneName,
      expected_hash: sceneCreate.result.after_hash,
      dry_run: true,
      operations: [
        { op: "rename_entity", entity: "Warden", name: "Sentinel" },
        { op: "set_entity_components", entity: "Sentinel", components: { "GAME.GameTransform": { x: 31, y: -18 } } },
      ],
    }),
    request(3, "readAsset", { kind: "scene", name: sceneName }),
    request(4, "buildScene", {
      kind: "scene",
      name: sceneName,
      expected_hash: sceneCreate.result.after_hash,
      operations: [
        { op: "rename_entity", entity: "Warden", name: "Sentinel" },
        { op: "set_entity_components", entity: "Sentinel", components: { "GAME.GameTransform": { x: 31, y: -18 } } },
      ],
    }),
    request(5, "readAsset", { kind: "scene", name: sceneName }),
  ]);
  const sceneRenameDryRun = structured(sceneRenameRun.get(2));
  const sceneBeforeRename = structured(sceneRenameRun.get(3));
  const sceneRename = structured(sceneRenameRun.get(4));
  const sceneAfterRename = structured(sceneRenameRun.get(5));
  assert(sceneRenameDryRun.ok === true && sceneRenameDryRun.result?.dry_run === true && sceneRenameDryRun.result?.published === false && sceneRenameDryRun.result?.after_hash !== sceneCreate.result.after_hash, `scene rename dry run failed: ${JSON.stringify(sceneRenameDryRun)}`);
  assert(sceneBeforeRename.ok === true && sceneBeforeRename.result?.hash === sceneCreate.result.after_hash && sceneBeforeRename.result.document.entities?.[0]?.name === "Warden", "scene rename dry run published a change");
  assert(sceneRename.ok === true && sceneRename.result?.published === true && sceneRename.result?.after_hash === sceneRenameDryRun.result.after_hash, `scene rename publish failed or dry-run hash was unstable: ${JSON.stringify(sceneRename)}`);
  assert(!Array.isArray(sceneRename.result?.warnings), "ordinary scene object rename returned Studio-only warnings");
  assert(sceneAfterRename.ok === true && sceneAfterRename.result.document.entities?.[0]?.name === "Sentinel" && sceneAfterRename.result.document.entities[0].components?.["GAME.GameTransform"]?.x === 31, "later scene operations did not address the renamed entity");

  const sceneInvalidRenameRun = runMcp(projectRoot, [
    initialize(),
    request(2, "buildScene", {
      kind: "scene",
      name: sceneName,
      expected_hash: sceneRename.result.after_hash,
      dry_run: false,
      operations: [{ op: "rename_entity", entity: "Sentinel", name: "" }],
    }),
    request(3, "buildScene", {
      kind: "scene",
      name: sceneName,
      expected_hash: sceneRename.result.after_hash,
      dry_run: false,
      operations: [
        { op: "add_entity", name: "Replacement", prototype: prototypeName },
        { op: "rename_entity", entity: "Sentinel", name: "Replacement" },
      ],
    }),
    request(4, "buildScene", {
      kind: "scene",
      name: sceneName,
      expected_hash: sceneRename.result.after_hash,
      dry_run: true,
      operations: [{ op: "rename_entity", entity: "Sentinel", name: "Sentinel" }],
    }),
  ]);
  const emptySceneRename = structured(sceneInvalidRenameRun.get(2));
  const duplicateSceneRename = structured(sceneInvalidRenameRun.get(3));
  const sameSceneRename = structured(sceneInvalidRenameRun.get(4));
  assert(emptySceneRename.ok === false && emptySceneRename.error?.code === "validation_failed", `scene rename accepted an empty name: ${JSON.stringify(emptySceneRename)}`);
  assert(duplicateSceneRename.ok === false && duplicateSceneRename.error?.code === "validation_failed", `scene rename accepted a duplicate name: ${JSON.stringify(duplicateSceneRename)}`);
  assert(sameSceneRename.ok === true && sameSceneRename.result?.dry_run === true && sameSceneRename.result?.changed === false, `same-name scene rename failed: ${JSON.stringify(sameSceneRename)}`);
  const sceneAfterFailedRenames = structured(runMcp(projectRoot, [
    initialize(),
    request(2, "readAsset", { kind: "scene", name: sceneName }),
  ]).get(2));
  assert(sceneAfterFailedRenames.ok === true && sceneAfterFailedRenames.result?.hash === sceneRename.result.after_hash && sceneAfterFailedRenames.result.document.entities?.length === 1, "invalid scene rename batches changed the published scene");

  writeFileSync(join(projectRoot, "project.kdl"), "version 4\nname \"Studio\"\ncontent \"content\"\nstart_scene \"scenes/Studio.kdl\"\n");
  const studioSceneCreateRun = runMcp(projectRoot, [
    initialize(),
    request(2, "buildScene", {
      kind: "scene",
      name: "Studio",
      create: true,
      operations: [{ op: "add_entity", name: "TopBar", prototype: prototypeName }],
    }),
  ]);
  const studioSceneCreate = structured(studioSceneCreateRun.get(2));
  assert(studioSceneCreate.ok === true, `Studio scene fixture failed to create: ${JSON.stringify(studioSceneCreate)}`);
  const studioRenameRun = runMcp(projectRoot, [
    initialize(),
    request(2, "buildScene", {
      kind: "scene",
      name: "Studio",
      expected_hash: studioSceneCreate.result.after_hash,
      dry_run: true,
      operations: [{ op: "rename_entity", entity: "TopBar", name: "RenamedTopBar" }],
    }),
    request(3, "buildScene", {
      kind: "scene",
      name: "Studio",
      expected_hash: studioSceneCreate.result.after_hash,
      operations: [{ op: "rename_entity", entity: "TopBar", name: "RenamedTopBar" }],
    }),
  ]);
  const studioRenameDryRun = structured(studioRenameRun.get(2));
  const studioRename = structured(studioRenameRun.get(3));
  assert(studioRenameDryRun.ok === true && Array.isArray(studioRenameDryRun.result?.warnings) && studioRenameDryRun.result.warnings.length > 0, `Studio scene rename dry run omitted its warning: ${JSON.stringify(studioRenameDryRun)}`);
  assert(studioRename.ok === true && Array.isArray(studioRename.result?.warnings) && studioRename.result.warnings.length > 0, `Studio scene rename omitted its warning: ${JSON.stringify(studioRename)}`);
  rmSync(join(projectRoot, "project.kdl"), { force: true });

  const missingBehaviorRun = runMcp(projectRoot, [
    initialize(),
    request(2, "buildPrototype", {
      kind: "prototype",
      name: prototypeName,
      expected_hash: prototypeCreate.result.after_hash,
      dry_run: true,
      operations: [{ op: "add_behavior", behavior: "MissingBehavior" }],
    }),
  ]);
  const missingBehavior = structured(missingBehaviorRun.get(2));
  assert(missingBehavior.next_steps?.some((step) => step.includes("searchAssets")), "asset-not-found response omitted searchAssets guidance");
  assert(missingBehavior.ok === false && missingBehavior.error?.code === "asset_not_found" && nativeAsset(projectRoot, "prototype", prototypeName).hash === prototypeCreate.result.after_hash, "prototype edits accepted a missing behavior or changed the asset on failure");
  assert(JSON.stringify(shaderLibraryRead.result.document).includes("Math") &&
    JSON.stringify(shaderLibraryRead.result.document).includes("Value") &&
    JSON.stringify(shaderLibraryRead.result.document).includes("Result"), "shader library pins were not authored");
  const materialOutputId = materialCreate.result.nodes.find((node) => node.ref === "output")?.id;
  assert(Number.isInteger(materialOutputId), "material output node ID was not returned");

  const behaviorHash = behaviorRead.result.hash;
  const materialRedHash = materialRead.result.hash;
  assert(behaviorHash === behaviorCreate.result.after_hash, "behavior readback hash differs from create result");
  assert(materialRedHash === materialCreate.result.after_hash, "material readback hash differs from create result");
  const mathGraphId = shaderLibraryCreate.result.graphs?.find((graph) => graph.ref === "math")?.id;
  assert(Number.isInteger(mathGraphId), "shader library graph reference was not returned");

  const libraryConsumerRun = runMcp(projectRoot, [
    initialize(),
    request(2, "buildGraph", {
      kind: "material",
      name: libraryConsumerName,
      create: true,
      operations: [{
        op: "add_node",
        ref: "math_call",
        type: "Call Pure Function",
        reference_name: "Math",
        reference_asset: shaderLibraryName,
      }],
    }),
    request(3, "readAsset", { kind: "material", name: libraryConsumerName }),
  ]);
  const libraryConsumerCreate = structured(libraryConsumerRun.get(2));
  const libraryConsumerRead = structured(libraryConsumerRun.get(3));
  assert(libraryConsumerCreate.ok === true && libraryConsumerCreate.result?.published === true, `shader library reference failed: ${JSON.stringify(libraryConsumerCreate)}`);
  const libraryConsumerJson = JSON.stringify(libraryConsumerRead.result?.document || {});
  assert(libraryConsumerRead.ok === true && libraryConsumerJson.includes(shaderLibraryName) &&
    libraryConsumerJson.includes("Math") && libraryConsumerJson.includes(String(mathGraphId)), "shader library reference was not persisted");

  const behaviorFollowup = runMcp(projectRoot, [
    initialize(),
    request(2, "buildGraph", {
      kind: "behavior",
      name: behaviorName,
      expected_hash: behaviorHash,
      dry_run: true,
      operations: [{ op: "add_graph", name: "Dry Run Function", kind: "function", ref: "dry" }],
    }),
    request(3, "buildGraph", {
      kind: "behavior",
      name: behaviorName,
      expected_hash: behaviorHash,
      operations: [{ op: "add_graph", name: "Applied Function", kind: "function", ref: "applied" }],
    }),
    request(4, "buildGraph", {
      kind: "behavior",
      name: behaviorName,
      expected_hash: behaviorHash,
      operations: [{ op: "add_graph", name: "Stale Function", kind: "function", ref: "stale" }],
    }),
  ]);
  const dryRun = structured(behaviorFollowup.get(2));
  const applied = structured(behaviorFollowup.get(3));
  const conflict = structured(behaviorFollowup.get(4));
  assert(conflict.next_steps?.some((step) => step.includes("inspectGraph")) && conflict.next_steps?.some((step) => step.includes("retry")), "asset-conflict response omitted recovery guidance");
  assert(dryRun.ok === true && dryRun.result?.dry_run === true && dryRun.result?.published === false, `behavior dry run failed: ${JSON.stringify(dryRun)}`);
  assert(applied.ok === true && applied.result?.published === true, `behavior apply failed: ${JSON.stringify(applied)}`);
  assert(conflict.ok === false && conflict.error?.code === "asset_conflict", `asset conflict failed: ${JSON.stringify(conflict)}`);
  const behaviorAppliedHash = applied.result.after_hash;

  const invalidMaterialName = "McpAuthoringInvalidMaterial";
  const invalidMaterial = JSON.parse(JSON.stringify(materialRead.result.document));
  invalidMaterial.graphs[0].nodes[0].type = "Shader Node That Does Not Exist";
  writeFileSync(join(projectRoot, "content", "materials", invalidMaterialName + ".kdl"), fixtureKdl(invalidMaterial));
  const publicValidationRun = runMcp(projectRoot, [
    initialize(),
    request(2, "validateAsset", { kind: "material", name: invalidMaterialName }),
  ]);
  const publicValidation = structured(publicValidationRun.get(2));
  assert(publicValidation.ok === false && publicValidation.error?.code === "validation_failed" &&
    publicValidation.next_steps?.some((step) => step.includes("validateAsset")), "validation failure omitted validateAsset guidance");
  const publicUnresolvedRun = runMcp(projectRoot, [
    initialize(),
    request(2, "buildGraph", {
      kind: "material",
      name: materialName,
      expected_hash: materialRedHash,
      operations: [{ op: "add_node", type: "Shader Node That Does Not Exist", ref: "bad" }],
    }),
  ]);
  const publicUnresolved = structured(publicUnresolvedRun.get(2));
  assert(publicUnresolved.ok === false && publicUnresolved.error?.code === "invalid_request" &&
    publicUnresolved.next_steps?.some((step) => step.includes("searchNodes")), "unresolved graph reference omitted searchNodes guidance");

  const badShader = nativeRequest(projectRoot, "edit_graph", {
    kind: "material",
    name: materialName,
    expected_hash: materialRedHash,
    operations: [{ op: "add_node", type: "Shader Node That Does Not Exist", ref: "bad" }],
  });
  assert(badShader.ok === false && badShader.error, `invalid shader batch unexpectedly succeeded: ${JSON.stringify(badShader)}`);
  const materialAfterBad = nativeAsset(projectRoot, "material", materialName);
  assert(materialAfterBad.hash === materialRedHash, "invalid shader batch changed the saved asset");

  const badBehavior = nativeRequest(projectRoot, "edit_graph", {
    kind: "behavior",
    name: behaviorName,
    expected_hash: behaviorAppliedHash,
    operations: [{ op: "add_node", type: "Behavior Node That Does Not Exist", ref: "bad" }],
  });
  assert(badBehavior.ok === false && badBehavior.error, `invalid behavior batch unexpectedly succeeded: ${JSON.stringify(badBehavior)}`);
  const behaviorAfterBad = nativeAsset(projectRoot, "behavior", behaviorName);
  assert(behaviorAfterBad.hash === behaviorAppliedHash, "invalid behavior batch changed the saved asset");

  const redPreviewPath = "/tmp/chibi-mcp-authoring-material-red.png";
  const bluePreviewPath = "/tmp/chibi-mcp-authoring-material-blue.png";
  const shapePreviewPath = "/tmp/chibi-mcp-authoring-shape.png";
  const previewSize = { width: 64, height: 64 };
  let redPreview;
  let bluePreview;
  let shapePreview;
  if (!skipGpu) {
    const redRun = runMcp(projectRoot, [
      initialize(),
      request(2, "previewShader", { kind: "material", name: materialName, ...previewSize, include_source: false }),
    ]);
    redPreview = checkPngPreview(redRun.get(2), materialRedHash, redPreviewPath, previewSize);
  }

  const blueEditArgs = {
    kind: "material",
    name: materialName,
    expected_hash: materialRedHash,
    operations: [{ op: "set_input", node: materialOutputId, pin: baseColorPin, value: { x: 0, y: 0, z: 1 } }],
  };
  const blueRun = runMcp(projectRoot, [initialize(), request(2, "buildGraph", blueEditArgs)]);
  const blueEdit = structured(blueRun.get(2));
  assert(blueEdit.ok === true && blueEdit.result?.published === true, `material color edit failed: ${JSON.stringify(blueEdit)}`);
  const materialBlueHash = blueEdit.result.after_hash;
  const materialBlueRead = nativeAsset(projectRoot, "material", materialName);
  assert(materialBlueRead.hash === materialBlueHash, "material color edit readback hash differs");

  if (!skipGpu) {
    const bluePreviewRun = runMcp(projectRoot, [
      initialize(),
      request(2, "previewShader", { kind: "material", name: materialName, ...previewSize, include_source: false }),
    ]);
    bluePreview = checkPngPreview(bluePreviewRun.get(2), materialBlueHash, bluePreviewPath, previewSize);
    assert(!redPreview.png.equals(bluePreview.png), "material previews did not change after the color edit");

    const shapePreviewRun = runMcp(projectRoot, [
      initialize(),
      request(2, "previewShader", { kind: "shape", name: shapeName, ...previewSize, include_source: false }),
    ]);
    shapePreview = checkPngPreview(shapePreviewRun.get(2), shapeRead.result.hash, shapePreviewPath, previewSize);
  }

  const headless = nativeRequest(projectRoot, "preview_shader", {
    kind: "material",
    name: "MissingPreview",
  });
  assert(headless.ok === false && headless.error?.message?.includes("chibi_ctl --render"), `headless preview boundary changed: ${JSON.stringify(headless)}`);

  const searchBytes = bytes(search.result);
  const registryBytes = bytes(registry.result);
  const rawAssetBytes = bytes(materialRead.result.document);
  const compactResponseBytes = Buffer.byteLength(toolText(blueRun.get(2)), "utf8");
  assert(searchBytes < registryBytes, `bounded search was not smaller than describe_registry (${searchBytes} >= ${registryBytes})`);
  assert(compactResponseBytes < 1024, `edit response echoed more than a summary (${compactResponseBytes} bytes)`);
  console.log(JSON.stringify({
    authoring_sizes: {
      search_nodes_bytes: searchBytes,
      describe_registry_bytes: registryBytes,
      semantic_create_request_bytes: bytes(materialCreateArgs),
      semantic_edit_request_bytes: bytes(blueEditArgs),
      resulting_raw_asset_bytes: rawAssetBytes,
      compact_success_response_bytes: compactResponseBytes,
    },
    gpu_preview: skipGpu
      ? { skipped: true }
      : {
        red_png_bytes: redPreview.bytes,
        blue_png_bytes: bluePreview.bytes,
        shape_png_bytes: shapePreview.bytes,
        red_path: redPreviewPath,
        blue_path: bluePreviewPath,
        shape_path: shapePreviewPath,
      },
  }));
} finally {
  rmSync(projectRoot, { recursive: true, force: true });
}
