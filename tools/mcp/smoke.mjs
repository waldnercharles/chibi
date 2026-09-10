#!/usr/bin/env node

import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { spawnSync } from "node:child_process";
import { join, resolve } from "node:path";

const root = resolve(process.argv[2] || process.cwd());
const server = resolve(root, "tools/mcp/server.mjs");
const controller = resolve(root, "build/blueprints_ctl");

function nativeRequestAt(projectRoot, operation, args = {}) {
  const result = spawnSync(controller, ["--project-root", projectRoot], {
    cwd: projectRoot,
    input: `${JSON.stringify({ contract: 1, operation, ...args })}\n`,
    encoding: "utf8",
    maxBuffer: 32 * 1024 * 1024,
  });
  if (result.error) {
    throw result.error;
  }
  const line = result.stdout.trim().split("\n").filter(Boolean).at(-1);
  if (!line) {
    throw new Error(result.stderr.trim() || "blueprints_ctl returned no response");
  }
  return { response: JSON.parse(line), status: result.status };
}

function nativeRequest(operation, args = {}) {
  return nativeRequestAt(root, operation, args);
}

const requests = [
  {
    jsonrpc: "2.0",
    id: 1,
    method: "initialize",
    params: { protocolVersion: "2025-06-18", capabilities: {}, clientInfo: { name: "smoke", version: "0.1.0" } },
  },
  { jsonrpc: "2.0", method: "notifications/initialized" },
  { jsonrpc: "2.0", id: 2, method: "tools/list", params: {} },
  { jsonrpc: "2.0", id: 3, method: "tools/call", params: { name: "project_status", arguments: {} } },
  { jsonrpc: "2.0", id: 4, method: "tools/call", params: { name: "list_assets", arguments: {} } },
  { jsonrpc: "2.0", id: 14, method: "tools/call", params: { name: "catalog", arguments: {} } },
  { jsonrpc: "2.0", id: 15, method: "tools/call", params: { name: "search_assets", arguments: { query: "Health" } } },
  { jsonrpc: "2.0", id: 16, method: "tools/call", params: { name: "asset_dependencies", arguments: { kind: "behavior", name: "HealthBehavior" } } },
  { jsonrpc: "2.0", id: 19, method: "tools/call", params: { name: "inspect_graph", arguments: { kind: "behavior", name: "HealthBehavior", graph_name: "" } } },
  { jsonrpc: "2.0", id: 20, method: "tools/call", params: { name: "inspect_graph", arguments: { kind: "library", name: "DirectionMath", graph_name: "Direction Orientation" } } },
  { jsonrpc: "2.0", id: 5, method: "tools/call", params: { name: "read_asset", arguments: { kind: "behavior", name: "HealthBehavior" } } },
  { jsonrpc: "2.0", id: 6, method: "tools/call", params: { name: "validate_asset", arguments: { kind: "behavior", name: "HealthBehavior" } } },
  { jsonrpc: "2.0", id: 7, method: "tools/call", params: { name: "describe_registry", arguments: {} } },
  { jsonrpc: "2.0", id: 27, method: "tools/call", params: { name: "describe_contract", arguments: {} } },
  { jsonrpc: "2.0", id: 28, method: "tools/call", params: { name: "list_assets", arguments: { limit: 2 } } },
  { jsonrpc: "2.0", id: 29, method: "tools/call", params: { name: "catalog", arguments: { limit: 1, summary: true } } },
  { jsonrpc: "2.0", id: 30, method: "tools/call", params: { name: "search_assets", arguments: { query: "Health", limit: 1, summary: true } } },
  { jsonrpc: "2.0", id: 31, method: "tools/call", params: { name: "catalog", arguments: { cursor: 1, limit: 1, summary: true } } },
  { jsonrpc: "2.0", id: 8, method: "resources/list", params: {} },
  { jsonrpc: "2.0", id: 12, method: "resources/templates/list", params: {} },
  { jsonrpc: "2.0", id: 9, method: "resources/read", params: { uri: "blueprints://project/status" } },
  { jsonrpc: "2.0", id: 17, method: "resources/read", params: { uri: "blueprints://catalog" } },
  { jsonrpc: "2.0", id: 10, method: "resources/read", params: { uri: "blueprints://asset/behavior/HealthBehavior" } },
  { jsonrpc: "2.0", id: 18, method: "resources/read", params: { uri: "blueprints://asset/behavior/HealthBehavior/dependencies" } },
  { jsonrpc: "2.0", id: 21, method: "resources/read", params: { uri: "blueprints://asset/library/DirectionMath/graph/1" } },
  { jsonrpc: "2.0", id: 11, method: "tools/call", params: { name: "missing_tool", arguments: {} } },
  { jsonrpc: "2.0", id: 13, method: "resources/read", params: { uri: "blueprints://asset/behavior/nope" } },
  { jsonrpc: "2.0", id: 22, method: "tools/call", params: { name: "read_asset", arguments: { kind: "behavior" } } },
  { jsonrpc: "2.0", id: 23, method: "tools/call", params: { name: "asset_dependencies", arguments: { kind: "behavior", name: "HealthBehavior", direction: "sideways" } } },
  { jsonrpc: "2.0", id: 24, method: "resources/read", params: { uri: "blueprints://asset/behavior/%ZZ" } },
  { jsonrpc: "2.0", id: 25, method: "resources/read", params: { uri: "blueprints://missing" } },
  { jsonrpc: "2.0", id: 26, method: "resources/read", params: {} },
  { jsonrpc: "2.0", id: 32, method: "tools/call", params: { name: "catalog", arguments: { limit: 0 } } },
  { jsonrpc: "2.0", id: 33, method: "tools/call", params: { name: "catalog", arguments: { cursor: "1" } } },
  { jsonrpc: "2.0", method: "ping" },
];
const result = spawnSync(process.execPath, [server], {
  cwd: root,
  input: `${requests.map((request) => JSON.stringify(request)).join("\n")}\n`,
  encoding: "utf8",
  maxBuffer: 32 * 1024 * 1024,
});
if (result.error || result.status !== 0) {
  throw result.error || new Error(result.stderr || `MCP server exited with ${result.status}`);
}
const responses = result.stdout.trim().split("\n").filter(Boolean).map(JSON.parse);
const expectedResponses = requests.filter((request) => Object.hasOwn(request, "id")).length;
if (responses.length !== expectedResponses) {
  throw new Error(`expected ${expectedResponses} responses, got ${responses.length}`);
}
const responseById = new Map(responses.map((response) => [response.id, response]));
if (responseById.get(1)?.result?.serverInfo?.name !== "c-blueprints" ||
    responseById.get(1)?.result?.protocolVersion !== "2025-06-18") {
  throw new Error("initialize did not identify c-blueprints");
}
if (!responseById.get(2)?.result?.tools?.some((tool) => tool.name === "validate_asset" && tool.outputSchema)) {
  throw new Error("tools/list did not expose validate_asset");
}
if (!responseById.get(2)?.result?.tools?.some((tool) =>
  tool.name === "describe_contract" && tool.annotations?.readOnlyHint === true)) {
  throw new Error("tools/list did not expose the reflected contract metadata");
}
if (!responseById.get(2)?.result?.tools?.some((tool) => tool.name === "catalog" && tool.outputSchema) ||
    !responseById.get(2)?.result?.tools?.some((tool) => tool.name === "search_assets" && tool.inputSchema?.required?.includes("query")) ||
    !responseById.get(2)?.result?.tools?.some((tool) => tool.name === "asset_dependencies" && tool.inputSchema?.required?.includes("kind")) ||
    !responseById.get(2)?.result?.tools?.some((tool) => tool.name === "inspect_graph" &&
      tool.inputSchema?.properties?.graph_id?.type === "integer" &&
      tool.inputSchema?.properties?.graph_name?.type === "string") ||
    !responseById.get(2)?.result?.tools?.some((tool) => tool.name === "inspect_system" &&
      tool.inputSchema?.required?.includes("kind") &&
      tool.inputSchema?.required?.includes("name")) ||
    !responseById.get(2)?.result?.tools?.some((tool) => tool.name === "run_scenario" &&
      tool.inputSchema?.required?.includes("name") &&
      tool.inputSchema?.properties?.frames?.maximum === 600 &&
      tool.inputSchema?.properties?.fixed_dt?.type === "number") ||
    !responseById.get(2)?.result?.tools?.some((tool) => tool.name === "runtime_snapshot" &&
      tool.inputSchema?.required?.includes("name") &&
      tool.inputSchema?.properties?.entity_limit?.maximum === 256 &&
      tool.inputSchema?.properties?.field_limit?.maximum === 128) ||
    !responseById.get(2)?.result?.tools?.some((tool) => tool.name === "record_scenario" &&
      tool.inputSchema?.required?.includes("name") &&
      tool.inputSchema?.properties?.inputs?.maxItems === 600) ||
    !responseById.get(2)?.result?.tools?.some((tool) => tool.name === "replay_scenario" &&
      tool.inputSchema?.oneOf?.length === 2 &&
      tool.inputSchema?.properties?.recording_path?.maxLength === 1024)) {
  throw new Error("tools/list did not expose catalog and graph tools");
}
const status = JSON.parse(responseById.get(3).result.content[0].text);
const assets = JSON.parse(responseById.get(4).result.content[0].text);
const catalog = JSON.parse(responseById.get(14).result.content[0].text);
const search = JSON.parse(responseById.get(15).result.content[0].text);
const dependencies = JSON.parse(responseById.get(16).result.content[0].text);
const healthGraph = JSON.parse(responseById.get(19).result.content[0].text);
const libraryGraph = JSON.parse(responseById.get(20).result.content[0].text);
const read = JSON.parse(responseById.get(5).result.content[0].text);
const validation = JSON.parse(responseById.get(6).result.content[0].text);
const registry = JSON.parse(responseById.get(7).result.content[0].text);
const contract = JSON.parse(responseById.get(27).result.content[0].text);
const pagedAssets = JSON.parse(responseById.get(28).result.content[0].text);
const pagedCatalog = JSON.parse(responseById.get(29).result.content[0].text);
const pagedSearch = JSON.parse(responseById.get(30).result.content[0].text);
const secondCatalogPage = JSON.parse(responseById.get(31).result.content[0].text);
if (status.ok !== true || assets.result?.assets?.length < 1 ||
    catalog.ok !== true || !catalog.result?.assets?.some((asset) => asset.name === "HealthBehavior") ||
    search.ok !== true || !search.result?.assets?.some((asset) => asset.name === "HealthBehavior") ||
    dependencies.ok !== true || dependencies.result?.asset?.name !== "HealthBehavior" ||
    catalog.result?.catalog_revision !== search.result?.catalog_revision ||
    catalog.result?.catalog_revision !== dependencies.result?.catalog_revision ||
    catalog.result?.content_hash !== search.result?.content_hash ||
    catalog.result?.content_hash !== dependencies.result?.content_hash ||
    healthGraph.ok !== true || healthGraph.result?.name !== "HealthBehavior" ||
    !healthGraph.result?.graph || !Array.isArray(healthGraph.result.graph.nodes) ||
    !healthGraph.result?.validation ||
    libraryGraph.ok !== true || libraryGraph.result?.name !== "DirectionMath" ||
    libraryGraph.result?.graph?.name !== "Direction Orientation" ||
    read.result?.name !== "HealthBehavior" || validation.result?.valid !== true ||
    registry.result?.nodes?.length < 1 ||
    contract.ok !== true ||
    !contract.result?.operations?.some((operation) => operation.name === "describe_contract") ||
    contract.result?.reflection?.nodes < 1 ||
    pagedAssets.ok !== true || pagedAssets.result?.count !== 2 ||
    pagedAssets.result?.has_more !== true || pagedAssets.result?.next_cursor !== 2 ||
    pagedCatalog.ok !== true || pagedCatalog.result?.summary !== true ||
    pagedCatalog.result?.count !== 1 || pagedCatalog.result?.has_more !== true ||
    pagedCatalog.result?.assets?.[0]?.members !== undefined ||
    pagedSearch.ok !== true || pagedSearch.result?.summary !== true ||
    pagedSearch.result?.count !== 1 || pagedSearch.result?.has_more !== false ||
    pagedSearch.result?.assets?.[0]?.members !== undefined ||
    secondCatalogPage.ok !== true || secondCatalogPage.result?.count !== 1 ||
    secondCatalogPage.result?.next_cursor !== 2) {
  throw new Error("native MCP operations failed");
}
if (!responseById.get(12)?.result?.resourceTemplates?.some((template) =>
  template.uriTemplate === "blueprints://asset/{kind}/{name}")) {
  throw new Error("resources/templates/list did not expose the asset template");
}
if (!responseById.get(12)?.result?.resourceTemplates?.some((template) =>
  template.uriTemplate === "blueprints://asset/{kind}/{name}/dependencies")) {
  throw new Error("resources/templates/list did not expose the dependency template");
}
if (!responseById.get(12)?.result?.resourceTemplates?.some((template) =>
  template.uriTemplate === "blueprints://asset/{kind}/{name}/graph/{graph_id}")) {
  throw new Error("resources/templates/list did not expose the graph template");
}
if (!responseById.get(12)?.result?.resourceTemplates?.some((template) =>
  template.uriTemplate === "blueprints://recording/{id}")) {
  throw new Error("resources/templates/list did not expose the recording template");
}
if (responseById.get(9)?.error || responseById.get(10)?.error || responseById.get(17)?.error ||
    responseById.get(18)?.error || responseById.get(21)?.error ||
    !responseById.get(10)?.result?.contents?.length || !responseById.get(17)?.result?.contents?.length ||
    !responseById.get(18)?.result?.contents?.length || !responseById.get(21)?.result?.contents?.length) {
  throw new Error("MCP status request failed");
}
if (responseById.get(11)?.error?.code !== -32602) {
  throw new Error("unknown tools return the expected JSON-RPC error");
}
if (responseById.get(13)?.error?.code !== -32002) {
  throw new Error("missing resources return the expected JSON-RPC error");
}
if (responseById.get(22)?.error?.code !== -32602 ||
    responseById.get(23)?.error?.code !== -32602 ||
    responseById.get(24)?.error?.code !== -32602 ||
    responseById.get(25)?.error?.code !== -32002 ||
    responseById.get(26)?.error?.code !== -32602 ||
    responseById.get(32)?.error?.code !== -32602 ||
    responseById.get(33)?.error?.code !== -32602) {
  throw new Error("MCP protocol boundary errors are not mapped correctly");
}

const cleanEnvironment = { ...process.env };
delete cleanEnvironment.CBLUEPRINTS_ROOT;
const cwdIndependent = spawnSync(process.execPath, [server], {
  cwd: "/tmp",
  env: cleanEnvironment,
  input: `${[
    {
      jsonrpc: "2.0",
      id: 30,
      method: "tools/call",
      params: { name: "project_status", arguments: {} },
    },
  ].map((request) => JSON.stringify(request)).join("\n")}\n`,
  encoding: "utf8",
  maxBuffer: 32 * 1024 * 1024,
});
if (cwdIndependent.error || cwdIndependent.status !== 0) {
  throw cwdIndependent.error || new Error(cwdIndependent.stderr || "CWD-independent server launch failed");
}
const cwdResponses = cwdIndependent.stdout.trim().split("\n").filter(Boolean).map(JSON.parse);
if (cwdResponses[0]?.result?.isError ||
    JSON.parse(cwdResponses[0]?.result?.content?.[0]?.text || "{}").ok !== true) {
  throw new Error("server did not resolve its project root independently of CWD");
}

const nativeStatus = nativeRequest("status");
const nativeAssets = nativeRequest("list_assets");
const nativeCatalog = nativeRequest("catalog");
const nativeSearch = nativeRequest("search_assets", { query: "Health" });
const nativeDependencies = nativeRequest("asset_dependencies", { kind: "behavior", name: "HealthBehavior" });
const nativeHealthGraph = nativeRequest("inspect_graph", {
  kind: "behavior",
  name: "HealthBehavior",
  graph_name: "",
});
const nativeLibraryGraph = nativeRequest("inspect_graph", {
  kind: "library",
  name: "DirectionMath",
  graph_id: 1,
});
const nativeRead = nativeRequest("read_asset", { kind: "behavior", name: "HealthBehavior" });
const nativeValidation = nativeRequest("validate_asset", { kind: "behavior", name: "HealthBehavior" });
const nativeRegistry = nativeRequest("describe_registry");
const nativeMissing = nativeRequest("read_asset", { path: "content/nope.json" });
if (nativeStatus.status !== 0 || nativeStatus.response.ok !== true ||
    nativeAssets.response.result?.assets?.length < 1 ||
    nativeCatalog.response.ok !== true || !nativeCatalog.response.result?.assets?.some((asset) => asset.name === "HealthBehavior") ||
    nativeSearch.response.ok !== true || !nativeSearch.response.result?.assets?.some((asset) => asset.name === "HealthBehavior") ||
    nativeDependencies.response.ok !== true || nativeDependencies.response.result?.asset?.name !== "HealthBehavior" ||
    nativeHealthGraph.response.ok !== true || nativeHealthGraph.response.result?.graph?.name !== "" ||
    nativeHealthGraph.response.result?.graph?.nodes?.length < 1 ||
    nativeLibraryGraph.response.ok !== true || nativeLibraryGraph.response.result?.graph?.name !== "Direction Orientation" ||
    nativeRead.response.result?.name !== "HealthBehavior" ||
    nativeValidation.response.result?.valid !== true ||
    nativeRegistry.response.result?.nodes?.length < 1 ||
    nativeMissing.status !== 6 || nativeMissing.response.error?.code !== "asset_not_found") {
  throw new Error("native controller smoke checks failed");
}

const systemRoot = mkdtempSync(join(tmpdir(), "c-blueprints-system-"));
try {
  const systemDirectory = join(systemRoot, "content", "systems");
  mkdirSync(systemDirectory, { recursive: true });
  const systemPath = join(systemDirectory, "MovementSystem.json");
  const systemDocument = JSON.parse(readFileSync(
    resolve(root, "test/fixtures/systems/MovementSystem.json"),
    "utf8"
  ));
  systemDocument.query = { op: "has", component: "GameTransform" };
  writeFileSync(systemPath, JSON.stringify(systemDocument));
  const inspectedSystem = nativeRequestAt(systemRoot, "inspect_system", {
    kind: "system",
    name: "MovementSystem",
  });
  const validatedSystem = nativeRequestAt(systemRoot, "validate_asset", {
    kind: "system",
    name: "MovementSystem",
  });
  if (inspectedSystem.status !== 0 || inspectedSystem.response.ok !== true ||
      inspectedSystem.response.result?.name !== "MovementSystem" ||
      inspectedSystem.response.result?.query?.op !== "has" ||
      inspectedSystem.response.result?.query?.component !== "game.GameTransform" ||
      !Array.isArray(inspectedSystem.response.result?.validation?.diagnostics) ||
      validatedSystem.response.ok !== true ||
      validatedSystem.response.result?.valid !== true) {
    throw new Error("system inspection smoke checks failed");
  }
} finally {
  rmSync(systemRoot, { recursive: true, force: true });
}

const scenarioRoot = mkdtempSync(join(tmpdir(), "c-blueprints-scenario-"));
try {
  const prototypeDirectory = join(scenarioRoot, "content", "prototypes");
  mkdirSync(prototypeDirectory, { recursive: true });
  writeFileSync(
    join(prototypeDirectory, "Editor2DPreview.json"),
    readFileSync(resolve(root, "test/fixtures/content/prototypes/Editor2DPreview.json"))
  );
  const scenarioRequests = [
    {
      jsonrpc: "2.0",
      id: 1,
      method: "tools/list",
      params: {},
    },
    {
      jsonrpc: "2.0",
      id: 2,
      method: "tools/call",
      params: {
        name: "run_scenario",
        arguments: { kind: "prototype", name: "Editor2DPreview", frames: 2, fixed_dt: 1 / 60 },
      },
    },
    {
      jsonrpc: "2.0",
      id: 3,
      method: "tools/call",
      params: {
        name: "run_scenario",
        arguments: { kind: "prototype", name: "Editor2DPreview", frames: 2, fixed_dt: 1 / 60 },
      },
    },
    {
      jsonrpc: "2.0",
      id: 4,
      method: "tools/call",
      params: {
        name: "run_scenario",
        arguments: { name: "Editor2DPreview", frames: 601 },
      },
    },
    {
      jsonrpc: "2.0",
      id: 5,
      method: "tools/call",
      params: {
        name: "runtime_snapshot",
        arguments: {
          kind: "prototype",
          name: "Editor2DPreview",
          frames: 2,
          fixed_dt: 1 / 60,
          entity_limit: 4,
          component_limit: 16,
          field_limit: 1,
          behavior_limit: 8,
          fields: ["GameTransform.x", "GameTransform.y"],
        },
      },
    },
    {
      jsonrpc: "2.0",
      id: 6,
      method: "tools/call",
      params: {
        name: "runtime_snapshot",
        arguments: {
          kind: "prototype",
          name: "Editor2DPreview",
          frames: 2,
          fixed_dt: 1 / 60,
          entity_limit: 4,
          component_limit: 16,
          field_limit: 1,
          behavior_limit: 8,
          fields: ["GameTransform.x", "GameTransform.y"],
        },
      },
    },
    {
      jsonrpc: "2.0",
      id: 7,
      method: "tools/call",
      params: {
        name: "runtime_snapshot",
        arguments: { name: "Editor2DPreview", entity_limit: 0 },
      },
    },
    {
      jsonrpc: "2.0",
      id: 8,
      method: "tools/call",
      params: {
        name: "record_scenario",
        arguments: {
          kind: "prototype",
          name: "Editor2DPreview",
          frames: 3,
          fixed_dt: 1 / 60,
          inputs: [
            {
              movement: { x: 0.5, y: -0.25 },
              aim: { x: 1, y: 0 },
              actions: [{ name: "Fire", pressed: true, value: 1 }],
            },
            {
              movement: { x: 0, y: 0 },
              aim: { x: 0, y: 1 },
              actions: [{ name: "Fire", pressed: false, value: 0 }],
            },
            { movement: { x: -1, y: 0 }, aim: { x: -1, y: 0 }, actions: [] },
          ],
        },
      },
    },
    {
      jsonrpc: "2.0",
      id: 9,
      method: "tools/call",
      params: {
        name: "record_scenario",
        arguments: {
          kind: "prototype",
          name: "Editor2DPreview",
          frames: 3,
          fixed_dt: 1 / 60,
          inputs: [
            {
              movement: { x: 0.5, y: -0.25 },
              aim: { x: 1, y: 0 },
              actions: [{ name: "Fire", pressed: true, value: 1 }],
            },
            {
              movement: { x: 0, y: 0 },
              aim: { x: 0, y: 1 },
              actions: [{ name: "Fire", pressed: false, value: 0 }],
            },
            { movement: { x: -1, y: 0 }, aim: { x: -1, y: 0 }, actions: [] },
          ],
        },
      },
    },
    {
      jsonrpc: "2.0",
      id: 10,
      method: "tools/call",
      params: {
        name: "replay_scenario",
        arguments: { recording: null },
      },
    },
    {
      jsonrpc: "2.0",
      id: 11,
      method: "tools/call",
      params: {
        name: "replay_scenario",
        arguments: { recording_path: `${scenarioRoot}/missing-recording.json` },
      },
    },
    {
      jsonrpc: "2.0",
      id: 12,
      method: "tools/call",
      params: {
        name: "replay_scenario",
        arguments: { recording: {}, recording_path: "recording.json" },
      },
    },
  ];
  const scenarioProcess = spawnSync(process.execPath, [server], {
    cwd: root,
    env: { ...process.env, CBLUEPRINTS_ROOT: scenarioRoot, CBLUEPRINTS_CTL: controller },
    input: `${scenarioRequests.map((request) => JSON.stringify(request)).join("\n")}\n`,
    encoding: "utf8",
    maxBuffer: 32 * 1024 * 1024,
  });
  if (scenarioProcess.error || scenarioProcess.status !== 0) {
    throw scenarioProcess.error || new Error(scenarioProcess.stderr || "scenario MCP server failed");
  }
  const scenarioResponses = scenarioProcess.stdout.trim().split("\n").filter(Boolean).map(JSON.parse);
  const scenarioById = new Map(scenarioResponses.map((response) => [response.id, response]));
  const firstScenario = JSON.parse(scenarioById.get(2)?.result?.content?.[0]?.text || "{}");
  const secondScenario = JSON.parse(scenarioById.get(3)?.result?.content?.[0]?.text || "{}");
  const firstSnapshot = JSON.parse(scenarioById.get(5)?.result?.content?.[0]?.text || "{}");
  const secondSnapshot = JSON.parse(scenarioById.get(6)?.result?.content?.[0]?.text || "{}");
  const firstRecord = JSON.parse(scenarioById.get(8)?.result?.content?.[0]?.text || "{}");
  const secondRecord = JSON.parse(scenarioById.get(9)?.result?.content?.[0]?.text || "{}");
  const entities = firstSnapshot.result?.entities;
  const components = entities?.[0]?.components;
  const fields = components?.[0]?.fields;
  const transform = components?.find((component) => component.name === "GameTransform");
  const transformFields = transform?.fields;
  const xField = transformFields?.find((field) => field.name === "x");
  const firstRecording = firstRecord.result?.recording;
  const secondRecording = secondRecord.result?.recording;
  const firstRecordFrames = firstRecord.result?.frames ?? firstRecording?.frames;
  const secondRecordFrames = secondRecord.result?.frames ?? secondRecording?.frames;
  const firstFrameHashes = firstRecording?.frame_hashes ||
    firstRecording?.observations?.map((observation) => observation.world_hash);
  const secondFrameHashes = secondRecording?.frame_hashes ||
    secondRecording?.observations?.map((observation) => observation.world_hash);
  const firstArtifactPath = firstRecord.result?.recording_path ||
    firstRecord.result?.artifact_path || firstRecording?.artifact_path;
  const secondArtifactPath = secondRecord.result?.recording_path ||
    secondRecord.result?.artifact_path || secondRecording?.artifact_path;
  const firstRecordingWithoutPath = firstRecording && {
    ...firstRecording,
    artifact_path: undefined,
    recording_path: undefined,
  };
  const secondRecordingWithoutPath = secondRecording && {
    ...secondRecording,
    artifact_path: undefined,
    recording_path: undefined,
  };
  const replayInlineRequest = {
    jsonrpc: "2.0",
    id: 13,
    method: "tools/call",
    params: { name: "replay_scenario", arguments: { recording: firstRecording } },
  };
  const replayProcess = spawnSync(process.execPath, [server], {
    cwd: root,
    env: { ...process.env, CBLUEPRINTS_ROOT: scenarioRoot, CBLUEPRINTS_CTL: controller },
    input: `${JSON.stringify(replayInlineRequest)}\n`,
    encoding: "utf8",
    maxBuffer: 32 * 1024 * 1024,
  });
  if (replayProcess.error || replayProcess.status !== 0) {
    throw replayProcess.error || new Error(replayProcess.stderr || "replay MCP server failed");
  }
  const replayResponse = JSON.parse(replayProcess.stdout.trim().split("\n").filter(Boolean).at(-1));
  const replayResult = JSON.parse(replayResponse.result?.content?.[0]?.text || "{}");
  const replayPathRequest = {
    jsonrpc: "2.0",
    id: 15,
    method: "tools/call",
    params: { name: "replay_scenario", arguments: { recording_path: firstArtifactPath } },
  };
  const replayPathProcess = spawnSync(process.execPath, [server], {
    cwd: root,
    env: { ...process.env, CBLUEPRINTS_ROOT: scenarioRoot, CBLUEPRINTS_CTL: controller },
    input: `${JSON.stringify(replayPathRequest)}\n`,
    encoding: "utf8",
    maxBuffer: 32 * 1024 * 1024,
  });
  if (replayPathProcess.error || replayPathProcess.status !== 0) {
    throw replayPathProcess.error || new Error(replayPathProcess.stderr || "recording path replay MCP server failed");
  }
  const replayPathResponse = JSON.parse(replayPathProcess.stdout.trim().split("\n").filter(Boolean).at(-1));
  const replayPathResult = JSON.parse(replayPathResponse.result?.content?.[0]?.text || "{}");
  const recordingId = firstRecord.result?.recording_id || firstRecording?.recording_id || firstRecording?.id;
  const recordingResourceRequest = {
    jsonrpc: "2.0",
    id: 16,
    method: "resources/read",
    params: { uri: `blueprints://recording/${recordingId}` },
  };
  const recordingResourceProcess = spawnSync(process.execPath, [server], {
    cwd: root,
    env: { ...process.env, CBLUEPRINTS_ROOT: scenarioRoot, CBLUEPRINTS_CTL: controller },
    input: `${JSON.stringify(recordingResourceRequest)}\n`,
    encoding: "utf8",
    maxBuffer: 32 * 1024 * 1024,
  });
  if (recordingResourceProcess.error || recordingResourceProcess.status !== 0) {
    throw recordingResourceProcess.error || new Error(recordingResourceProcess.stderr || "recording resource MCP server failed");
  }
  const recordingResourceResponse = JSON.parse(recordingResourceProcess.stdout.trim().split("\n").filter(Boolean).at(-1));
  const recordingResourceText = recordingResourceResponse.result?.contents?.[0]?.text || "{}";
  const recordingResource = JSON.parse(recordingResourceText);
  const recordingResourceValue = recordingResource.result?.recording || recordingResource.result;
  const mutatedRecording = firstRecording && JSON.parse(JSON.stringify(firstRecording));
  if (mutatedRecording?.frame_hashes?.length) {
    mutatedRecording.frame_hashes[1] = "0000000000000000";
  } else if (mutatedRecording?.observations?.length) {
    mutatedRecording.observations[1].world_hash = "0000000000000000";
  }
  const divergenceRequest = {
    jsonrpc: "2.0",
    id: 14,
    method: "tools/call",
    params: { name: "replay_scenario", arguments: { recording: mutatedRecording } },
  };
  const divergenceProcess = spawnSync(process.execPath, [server], {
    cwd: root,
    env: { ...process.env, CBLUEPRINTS_ROOT: scenarioRoot, CBLUEPRINTS_CTL: controller },
    input: `${JSON.stringify(divergenceRequest)}\n`,
    encoding: "utf8",
    maxBuffer: 32 * 1024 * 1024,
  });
  if (divergenceProcess.error || divergenceProcess.status !== 0) {
    throw divergenceProcess.error || new Error(divergenceProcess.stderr || "divergence MCP server failed");
  }
  const divergenceResponse = JSON.parse(divergenceProcess.stdout.trim().split("\n").filter(Boolean).at(-1));
  const divergenceResult = JSON.parse(divergenceResponse.result?.content?.[0]?.text || "{}");
  const firstDivergenceFrame = divergenceResult.result?.first_divergence_frame ??
    divergenceResult.result?.first_divergence?.frame ??
    divergenceResult.result?.divergence?.frame;
  if (scenarioById.get(1)?.result?.tools?.some((tool) => tool.name === "run_scenario") !== true ||
      firstScenario.ok !== true || secondScenario.ok !== true ||
      firstScenario.result?.kind !== "prototype" ||
      firstScenario.result?.name !== "Editor2DPreview" ||
      firstScenario.result?.frames !== 2 ||
      firstScenario.result?.fixed_dt !== 1 / 60 ||
      typeof firstScenario.result?.world_hash !== "string" ||
      !/^[0-9a-f]+$/i.test(firstScenario.result.world_hash) ||
      typeof firstScenario.result?.live_entities !== "number" ||
      !firstScenario.result?.timing ||
      firstScenario.result?.world_hash !== secondScenario.result?.world_hash ||
      scenarioById.get(4)?.error?.code !== -32602 ||
      firstSnapshot.ok !== true || secondSnapshot.ok !== true ||
      firstSnapshot.result?.kind !== "prototype" ||
      firstSnapshot.result?.name !== "Editor2DPreview" ||
      firstSnapshot.result?.frames !== 2 ||
      firstSnapshot.result?.fixed_dt !== 1 / 60 ||
      typeof firstSnapshot.result?.world_hash !== "string" ||
      firstSnapshot.result?.world_hash !== secondSnapshot.result?.world_hash ||
      !Array.isArray(entities) ||
      !Array.isArray(components) ||
      !Array.isArray(fields) ||
      !transform ||
      !Array.isArray(transformFields) ||
      transform.field_count !== 2 ||
      transform.fields_truncated !== true ||
      transformFields.length !== 1 ||
      xField?.value !== 320 ||
      components.some((component) => component.fields.length > 1) ||
      entities.some((entity, index) => index > 0 && entity.id <= entities[index - 1].id) ||
      components.some((component, index) => index > 0 &&
        component.name.localeCompare(components[index - 1].name) < 0) ||
      fields.some((field, index) => index > 0 &&
        field.name.localeCompare(fields[index - 1].name) < 0) ||
      scenarioById.get(7)?.error?.code !== -32602 ||
      firstRecord.ok !== true || secondRecord.ok !== true ||
      firstRecordFrames !== 3 || secondRecordFrames !== 3 ||
      !firstRecording || !secondRecording ||
      JSON.stringify(firstRecordingWithoutPath) !== JSON.stringify(secondRecordingWithoutPath) ||
      JSON.stringify(firstFrameHashes) !== JSON.stringify(secondFrameHashes) ||
      typeof firstArtifactPath !== "string" || typeof secondArtifactPath !== "string" ||
      firstArtifactPath.startsWith(scenarioRoot) || secondArtifactPath.startsWith(scenarioRoot) ||
      replayResponse.result?.isError === true || replayResult.ok !== true ||
      replayResult.result?.deterministic !== true ||
      replayPathResponse.result?.isError === true || replayPathResult.ok !== true ||
      replayPathResult.result?.deterministic !== true ||
      recordingResourceResponse.error || recordingResource.ok !== true ||
      (recordingResourceValue?.recording_id || recordingResourceValue?.id) !== recordingId ||
      divergenceResponse.result?.isError === true || divergenceResult.ok !== true ||
      firstDivergenceFrame !== 1 ||
      scenarioById.get(10)?.error?.code !== -32602 ||
      scenarioById.get(11)?.result?.isError !== true ||
      scenarioById.get(12)?.error?.code !== -32602) {
    throw new Error("deterministic scenario smoke checks failed");
  }
} finally {
  rmSync(scenarioRoot, { recursive: true, force: true });
}
console.log("MCP protocol smoke passed");
