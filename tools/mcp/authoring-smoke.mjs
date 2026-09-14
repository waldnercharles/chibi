#!/usr/bin/env node

import {
  existsSync,
  mkdirSync,
  mkdtempSync,
  rmSync,
  writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { spawnSync } from "node:child_process";
import { isAbsolute, join, resolve } from "node:path";

const root = resolve(process.argv[2] || process.cwd());
const server = resolve(root, "tools/mcp/server.mjs");
const controller = process.env.CHIBI_CTL || resolve(root, "build", "chibi_ctl");
const configuredRenderController = process.env.CHIBI_RENDER_CTL;
const renderController = configuredRenderController
  ? (isAbsolute(configuredRenderController)
    ? configuredRenderController
    : resolve(root, configuredRenderController))
  : resolve(root, "build", "chibi_render_ctl");
const skipGpu = process.env.CHIBI_MCP_SKIP_GPU === "1";

if (!existsSync(controller)) {
  throw new Error(`missing controller: ${controller}`);
}
if (!skipGpu && !existsSync(renderController)) {
  throw new Error(`missing render controller: ${renderController}; set CHIBI_MCP_SKIP_GPU=1 only for an explicit headless smoke`);
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
  const run = spawnSync(process.execPath, [server], {
    cwd: root,
    env: {
      ...process.env,
      CHIBI_ROOT: projectRoot,
      CHIBI_CTL: controller,
      CHIBI_RENDER_CTL: renderController,
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
mkdirSync(join(projectRoot, "content", "behaviors"), { recursive: true });
mkdirSync(join(projectRoot, "content", "materials"), { recursive: true });
mkdirSync(join(projectRoot, "content", "shapes"), { recursive: true });
mkdirSync(join(projectRoot, "content", "shader-libraries"), { recursive: true });

try {
  const materialCreateArgs = {
    kind: "material",
    name: materialName,
    create: true,
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
    request(3, "search_nodes", { query: "Shader", limit: 3 }),
    request(4, "search_nodes", { query: "Shader Surface Output", limit: 3 }),
    request(5, "search_nodes", { query: "Shader Shape Output", limit: 3 }),
    request(6, "describe_registry", {}),
    request(7, "edit_graph", {
      kind: "behavior",
      name: behaviorName,
      create: true,
      operations: [{ op: "add_graph", name: "Smoke Function", kind: "function", ref: "function" }],
    }),
    request(8, "edit_graph", materialCreateArgs),
    request(9, "edit_graph", shapeCreateArgs),
    request(13, "edit_graph", shaderLibraryCreateArgs),
    request(10, "read_asset", { kind: "behavior", name: behaviorName }),
    request(11, "read_asset", { kind: "material", name: materialName }),
    request(12, "read_asset", { kind: "shape", name: shapeName }),
    request(14, "read_asset", { kind: "shader-library", name: shaderLibraryName }),
  ]);

  const tools = first.get(2)?.result?.tools || [];
  const previewTool = tools.find((tool) => tool.name === "preview_shader");
  const editTool = tools.find((tool) => tool.name === "edit_graph");
  const search = structured(first.get(3));
  const surfaceSearch = structured(first.get(4));
  const shapeSearch = structured(first.get(5));
  const registry = structured(first.get(6));
  const behaviorCreate = structured(first.get(7));
  const materialCreate = structured(first.get(8));
  const shapeCreate = structured(first.get(9));
  const shaderLibraryCreate = structured(first.get(13));
  const behaviorRead = structured(first.get(10));
  const materialRead = structured(first.get(11));
  const shapeRead = structured(first.get(12));
  const shaderLibraryRead = structured(first.get(14));
  const surfaceNode = surfaceSearch.result?.nodes?.find((node) => node.name === "Shader Surface Output");
  const shapeNode = shapeSearch.result?.nodes?.find((node) => node.name === "Shader Shape Output");
  const baseColorPin = surfaceNode?.inputs?.find((pin) => pin.name === "Base Color")?.name;
  const distancePin = shapeNode?.inputs?.find((pin) => pin.name === "Distance")?.name;
  const operationProperties = editTool?.inputSchema?.properties?.operations?.items?.properties || {};

  assert(previewTool?.inputSchema?.properties?.width?.maximum === 512, "preview width limit is missing");
  assert(!previewTool.inputSchema.properties.graph_id && !previewTool.inputSchema.properties.graph_name, "preview advertises unsupported graph selectors");
  assert(!editTool.inputSchema.properties.layout && !operationProperties.x && !operationProperties.y, "graph placement controls should remain native and automatic");
  assert(editTool?.annotations?.readOnlyHint === false && editTool?.annotations?.destructiveHint === true, "edit_graph mutation annotations are missing");
  assert(operationProperties.op?.enum?.includes("set_ramp") && operationProperties.op?.enum?.includes("set_bounds") && operationProperties.op?.enum?.includes("add_pin"), "shader authoring operations are missing from the MCP operation schema");
  assert(operationProperties.reference_asset && operationProperties.pure && operationProperties.side && operationProperties.stops?.maxItems === 16, "native graph edit fields are missing from the MCP operation schema");
  assert(search.ok === true && search.result?.count > 0 && search.result.count <= 3, `bounded shader search failed: ${JSON.stringify(search)}`);
  assert(search.result.nodes.every((node) => Array.isArray(node.inputs) && Array.isArray(node.outputs)), "bounded shader search omitted default pins");
  assert(baseColorPin === "Base Color" && distancePin === "Distance", "named shader output pins were not discoverable");
  assert(Array.isArray(registry.result?.nodes) && registry.result.nodes.length > 0, "registry is empty");
  assert(behaviorCreate.ok === true && typeof behaviorCreate.result?.after_hash === "string", `behavior create failed: ${JSON.stringify(behaviorCreate)}`);
  assert(materialCreate.ok === true && typeof materialCreate.result?.after_hash === "string", `material create failed: ${JSON.stringify(materialCreate)}`);
  assert(shapeCreate.ok === true && typeof shapeCreate.result?.after_hash === "string", `shape create failed: ${JSON.stringify(shapeCreate)}`);
  assert(shaderLibraryCreate.ok === true && typeof shaderLibraryCreate.result?.after_hash === "string", `shader library create failed: ${JSON.stringify(shaderLibraryCreate)}`);
  assert(materialCreate.result.nodes?.some((node) => node.ref === "output"), "material output alias was not returned");
  assert(shapeCreate.result.nodes?.some((node) => node.ref === "output") &&
    shapeCreate.result.nodes?.some((node) => node.ref === "position") &&
    shapeCreate.result.nodes?.some((node) => node.ref === "circle"), "shape aliases were not returned");
  assert(behaviorRead.ok === true && materialRead.ok === true && shapeRead.ok === true, "created asset readback failed");
  assert(shapeRead.result.document.bounds_x === 20 && shapeRead.result.document.bounds_y === 20, "shape bounds edit was not persisted");
  assert(shaderLibraryRead.ok === true && shaderLibraryRead.result?.document?.domain === "shader-library", "shader library readback failed");
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
    request(2, "edit_graph", {
      kind: "material",
      name: libraryConsumerName,
      create: true,
      operations: [{
        op: "add_node",
        ref: "math_call",
        type: "Call Pure Function",
        reference_id: mathGraphId,
        reference_name: "Math",
        reference_asset: shaderLibraryName,
      }],
    }),
    request(3, "read_asset", { kind: "material", name: libraryConsumerName }),
  ]);
  const libraryConsumerCreate = structured(libraryConsumerRun.get(2));
  const libraryConsumerRead = structured(libraryConsumerRun.get(3));
  assert(libraryConsumerCreate.ok === true && libraryConsumerCreate.result?.published === true, `shader library reference failed: ${JSON.stringify(libraryConsumerCreate)}`);
  const libraryConsumerJson = JSON.stringify(libraryConsumerRead.result?.document || {});
  assert(libraryConsumerRead.ok === true && libraryConsumerJson.includes(shaderLibraryName) &&
    libraryConsumerJson.includes("Math") && libraryConsumerJson.includes(String(mathGraphId)), "shader library reference was not persisted");

  const behaviorFollowup = runMcp(projectRoot, [
    initialize(),
    request(2, "edit_graph", {
      kind: "behavior",
      name: behaviorName,
      expected_hash: behaviorHash,
      dry_run: true,
      operations: [{ op: "add_graph", name: "Dry Run Function", kind: "function", ref: "dry" }],
    }),
    request(3, "edit_graph", {
      kind: "behavior",
      name: behaviorName,
      expected_hash: behaviorHash,
      operations: [{ op: "add_graph", name: "Applied Function", kind: "function", ref: "applied" }],
    }),
    request(4, "edit_graph", {
      kind: "behavior",
      name: behaviorName,
      expected_hash: behaviorHash,
      operations: [{ op: "add_graph", name: "Stale Function", kind: "function", ref: "stale" }],
    }),
  ]);
  const dryRun = structured(behaviorFollowup.get(2));
  const applied = structured(behaviorFollowup.get(3));
  const conflict = structured(behaviorFollowup.get(4));
  assert(dryRun.ok === true && dryRun.result?.dry_run === true && dryRun.result?.published === false, `behavior dry run failed: ${JSON.stringify(dryRun)}`);
  assert(applied.ok === true && applied.result?.published === true, `behavior apply failed: ${JSON.stringify(applied)}`);
  assert(conflict.ok === false && conflict.error?.code === "asset_conflict", `asset conflict failed: ${JSON.stringify(conflict)}`);
  const behaviorAppliedHash = applied.result.after_hash;

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
      request(2, "preview_shader", { kind: "material", name: materialName, ...previewSize, include_source: false }),
    ]);
    redPreview = checkPngPreview(redRun.get(2), materialRedHash, redPreviewPath, previewSize);
  }

  const blueEditArgs = {
    kind: "material",
    name: materialName,
    expected_hash: materialRedHash,
    operations: [{ op: "set_input", node: materialOutputId, pin: baseColorPin, value: { x: 0, y: 0, z: 1 } }],
  };
  const blueRun = runMcp(projectRoot, [initialize(), request(2, "edit_graph", blueEditArgs)]);
  const blueEdit = structured(blueRun.get(2));
  assert(blueEdit.ok === true && blueEdit.result?.published === true, `material color edit failed: ${JSON.stringify(blueEdit)}`);
  const materialBlueHash = blueEdit.result.after_hash;
  const materialBlueRead = nativeAsset(projectRoot, "material", materialName);
  assert(materialBlueRead.hash === materialBlueHash, "material color edit readback hash differs");

  if (!skipGpu) {
    const bluePreviewRun = runMcp(projectRoot, [
      initialize(),
      request(2, "preview_shader", { kind: "material", name: materialName, ...previewSize, include_source: false }),
    ]);
    bluePreview = checkPngPreview(bluePreviewRun.get(2), materialBlueHash, bluePreviewPath, previewSize);
    assert(!redPreview.png.equals(bluePreview.png), "material previews did not change after the color edit");

    const shapePreviewRun = runMcp(projectRoot, [
      initialize(),
      request(2, "preview_shader", { kind: "shape", name: shapeName, ...previewSize, include_source: false }),
    ]);
    shapePreview = checkPngPreview(shapePreviewRun.get(2), shapeRead.result.hash, shapePreviewPath, previewSize);
  }

  const headless = nativeRequest(projectRoot, "preview_shader", {
    kind: "material",
    name: "MissingPreview",
  });
  assert(headless.ok === false && headless.error?.message?.includes("separate chibi_render_ctl"), `headless preview boundary changed: ${JSON.stringify(headless)}`);

  const searchBytes = bytes(search.result);
  const registryBytes = bytes(registry.result);
  const rawAssetBytes = bytes(materialRead.result.document);
  const compactResponseBytes = Buffer.byteLength(toolText(blueRun.get(2)), "utf8");
  assert(searchBytes < registryBytes, `bounded search was not smaller than describe_registry (${searchBytes} >= ${registryBytes})`);
  assert(compactResponseBytes < rawAssetBytes, `compact edit response was not smaller than the raw asset (${compactResponseBytes} >= ${rawAssetBytes})`);
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
