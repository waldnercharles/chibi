#!/usr/bin/env node

import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { spawnSync } from "node:child_process";
import { join, resolve } from "node:path";
import { toolCatalog } from "./tool-catalog.mjs";

const root = resolve(process.argv[2] || process.cwd());
const server = resolve(root, "tools/mcp/server.mjs");
const controller = process.env.CHIBI_CTL || resolve(root, "build/chibi_ctl");
const expectedPublicNames = [
  "buildGraph",
  "previewField",
  "buildPrototype",
  "buildScene",
  "optimizeGraph",
  "previewShader",
  "previewScene",
  "searchAssets",
  "searchNodes",
  "inspectGraph",
  "readAsset",
  "validateAsset",
  "listAssets",
  "getAssetCatalog",
  "inspectAssetDependencies",
  "findReferences",
  "callHierarchy",
  "getProjectStatus",
  "describeComponents",
  "inspectSystem",
  "getExecutionStatus",
  "runScenario",
  "captureRuntimeSnapshot",
  "profileScenario",
  "recordScenario",
  "replayScenario",
  "describeRegistry",
  "describeContract",
];

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
    throw new Error(result.stderr.trim() || "chibi_ctl returned no response");
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
  { jsonrpc: "2.0", id: 3, method: "tools/call", params: { name: "getProjectStatus", arguments: {} } },
  { jsonrpc: "2.0", id: 4, method: "tools/call", params: { name: "listAssets", arguments: {} } },
  { jsonrpc: "2.0", id: 14, method: "tools/call", params: { name: "getAssetCatalog", arguments: {} } },
  { jsonrpc: "2.0", id: 15, method: "tools/call", params: { name: "searchAssets", arguments: { query: "Health" } } },
  { jsonrpc: "2.0", id: 16, method: "tools/call", params: { name: "inspectAssetDependencies", arguments: { kind: "behavior", name: "HealthBehavior" } } },
  { jsonrpc: "2.0", id: 19, method: "tools/call", params: { name: "inspectGraph", arguments: { kind: "behavior", name: "HealthBehavior", graph_name: "" } } },
  { jsonrpc: "2.0", id: 20, method: "tools/call", params: { name: "inspectGraph", arguments: { kind: "library", name: "DirectionMath", graph_name: "Direction Orientation" } } },
  { jsonrpc: "2.0", id: 34, method: "tools/call", params: { name: "inspectGraph", arguments: { kind: "behavior", name: "HealthBehavior", graph_name: "", include_layout: true } } },
  { jsonrpc: "2.0", id: 5, method: "tools/call", params: { name: "readAsset", arguments: { kind: "behavior", name: "HealthBehavior" } } },
  { jsonrpc: "2.0", id: 6, method: "tools/call", params: { name: "validateAsset", arguments: { kind: "behavior", name: "HealthBehavior" } } },
  { jsonrpc: "2.0", id: 7, method: "tools/call", params: { name: "describeRegistry", arguments: { sections: ["nodes"] } } },
  { jsonrpc: "2.0", id: 27, method: "tools/call", params: { name: "describeContract", arguments: {} } },
  { jsonrpc: "2.0", id: 28, method: "tools/call", params: { name: "listAssets", arguments: { limit: 2 } } },
  { jsonrpc: "2.0", id: 29, method: "tools/call", params: { name: "getAssetCatalog", arguments: { limit: 1, summary: true } } },
  { jsonrpc: "2.0", id: 30, method: "tools/call", params: { name: "searchAssets", arguments: { query: "Health", limit: 1, summary: true } } },
  { jsonrpc: "2.0", id: 31, method: "tools/call", params: { name: "getAssetCatalog", arguments: { cursor: 1, limit: 1, summary: true } } },
  { jsonrpc: "2.0", id: 8, method: "resources/list", params: {} },
  { jsonrpc: "2.0", id: 12, method: "resources/templates/list", params: {} },
  { jsonrpc: "2.0", id: 9, method: "resources/read", params: { uri: "chibi://project/status" } },
  { jsonrpc: "2.0", id: 17, method: "resources/read", params: { uri: "chibi://catalog" } },
  { jsonrpc: "2.0", id: 10, method: "resources/read", params: { uri: "chibi://asset/behavior/HealthBehavior" } },
  { jsonrpc: "2.0", id: 18, method: "resources/read", params: { uri: "chibi://asset/behavior/HealthBehavior/dependencies" } },
  { jsonrpc: "2.0", id: 21, method: "resources/read", params: { uri: "chibi://asset/library/DirectionMath/graph/1" } },
  { jsonrpc: "2.0", id: 11, method: "tools/call", params: { name: "missing_tool", arguments: {} } },
  { jsonrpc: "2.0", id: 13, method: "resources/read", params: { uri: "chibi://asset/behavior/nope" } },
  { jsonrpc: "2.0", id: 22, method: "tools/call", params: { name: "readAsset", arguments: { kind: "behavior" } } },
  { jsonrpc: "2.0", id: 23, method: "tools/call", params: { name: "inspectAssetDependencies", arguments: { kind: "behavior", name: "HealthBehavior", direction: "sideways" } } },
  { jsonrpc: "2.0", id: 24, method: "resources/read", params: { uri: "chibi://asset/behavior/%ZZ" } },
  { jsonrpc: "2.0", id: 25, method: "resources/read", params: { uri: "chibi://missing" } },
  { jsonrpc: "2.0", id: 26, method: "resources/read", params: {} },
  { jsonrpc: "2.0", id: 32, method: "tools/call", params: { name: "getAssetCatalog", arguments: { limit: 0 } } },
  { jsonrpc: "2.0", id: 33, method: "tools/call", params: { name: "getAssetCatalog", arguments: { cursor: "1" } } },
  { jsonrpc: "2.0", id: 35, method: "tools/call", params: { name: "searchAssets", arguments: { query: "__mcp_no_such_asset__" } } },
  { jsonrpc: "2.0", id: 36, method: "tools/call", params: { name: "searchNodes", arguments: { query: "__mcp_no_such_node__" } } },
  { jsonrpc: "2.0", id: 38, method: "tools/call", params: { name: "searchNodes", arguments: { query: "button", include_pins: false } } },
  { jsonrpc: "2.0", id: 37, method: "tools/call", params: { name: "buildGraph", arguments: {
    kind: "behavior", name: "McpExpressionProbe", create: true, dry_run: true,
    operations: [
      { op: "add_node", ref: "expression", type: "Behavior Expression", source: "x + 1" },
      { op: "set_expression", node: "expression", source: "x * 2" },
    ],
  } } },
  { jsonrpc: "2.0", id: 60, method: "tools/call", params: { name: "findReferences", arguments: { event: "Damage" } } },
  { jsonrpc: "2.0", id: 61, method: "tools/call", params: { name: "callHierarchy", arguments: {
    asset: { kind: "behavior", name: "PlayerContactBehavior" }, graph: "Apply Contact Damage",
  } } },
  { jsonrpc: "2.0", id: 62, method: "tools/call", params: { name: "findReferences", arguments: {
    asset: { kind: "behavior", name: "HealthBehavior" }, variable: "__mcp_no_such_variable__",
  } } },
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
if (!result.stderr.includes(`chibi MCP verified project root: ${root}`)) {
  throw new Error("MCP must verify and report its native project root before authoring");
}
const responses = result.stdout.trim().split("\n").filter(Boolean).map(JSON.parse);
const expectedResponses = requests.filter((request) => Object.hasOwn(request, "id")).length;
if (responses.length !== expectedResponses) {
  throw new Error(`expected ${expectedResponses} responses, got ${responses.length}`);
}
const responseById = new Map(responses.map((response) => [response.id, response]));

function toolPayload(response) {
  const structured = response?.result?.structuredContent;
  if (structured !== undefined) {
    return structured;
  }
  const text = response?.result?.content?.[0]?.text;
  if (typeof text !== "string" || !text) {
    return {};
  }
  try {
    return JSON.parse(text);
  } catch {
    return {};
  }
}

if (responseById.get(1)?.result?.serverInfo?.name !== "chibi" ||
    responseById.get(1)?.result?.protocolVersion !== "2025-06-18") {
  throw new Error("initialize did not identify chibi");
}
if (typeof responseById.get(1)?.result?.instructions !== "string" ||
    !responseById.get(1).result.instructions.includes("buildGraph") ||
    !responseById.get(1).result.instructions.includes("validateAsset")) {
  throw new Error("initialize did not expose server instructions");
}
const listedTools = responseById.get(2)?.result?.tools || [];
if (!toolPayload(responseById.get(37))?.ok) {
  throw new Error("MCP rejected Behavior Expression creation or source editing");
}
const listedNames = listedTools.map((tool) => tool.name);
if (JSON.stringify(toolCatalog.map((entry) => entry.name)) !== JSON.stringify(expectedPublicNames) ||
    JSON.stringify(listedNames) !== JSON.stringify(expectedPublicNames) ||
    new Set(listedNames).size !== listedNames.length ||
    listedTools.some((tool) => !/^[a-z][A-Za-z0-9]{0,127}$/.test(tool.name) ||
      typeof tool.title !== "string" || !tool.title ||
      typeof tool.description !== "string" || !tool.description ||
      !tool.inputSchema || !tool.outputSchema || !tool.annotations ||
      Object.hasOwn(tool, "operation"))) {
  throw new Error("tools/list did not expose the deterministic public catalog");
}
if (listedTools.slice(0, 4).map((tool) => tool.name).join(",") !==
    "buildGraph,previewField,buildPrototype,buildScene") {
  throw new Error("tools/list did not put authoring tools first");
}
if (listedTools.some((tool) => tool.name.includes("_") || tool.name.includes("-") ||
    ["project_status", "edit_graph", "validate_asset", "run_scenario"].includes(tool.name))) {
  throw new Error("tools/list exposed a legacy public tool name");
}
if (!listedTools.some((tool) => tool.name === "validateAsset" &&
    tool.inputSchema?.required?.includes("kind") && tool.outputSchema) ||
    !listedTools.some((tool) => tool.name === "getAssetCatalog" && tool.outputSchema) ||
    !listedTools.some((tool) => tool.name === "searchAssets" && tool.inputSchema?.required?.includes("query")) ||
    !listedTools.some((tool) => tool.name === "inspectAssetDependencies" && tool.inputSchema?.required?.includes("kind")) ||
    !listedTools.some((tool) => tool.name === "inspectGraph" &&
      tool.inputSchema?.properties?.graph_id?.type === "integer" &&
      tool.inputSchema?.properties?.graph_name?.type === "string") ||
    !listedTools.some((tool) => tool.name === "inspectSystem" &&
      tool.inputSchema?.required?.includes("kind") &&
      tool.inputSchema?.required?.includes("name")) ||
    !listedTools.some((tool) => tool.name === "runScenario" &&
      tool.inputSchema?.required?.includes("name") &&
      tool.inputSchema?.properties?.frames?.maximum === 108000 &&
      tool.inputSchema?.properties?.fixed_dt?.type === "number") ||
    !listedTools.some((tool) => tool.name === "captureRuntimeSnapshot" &&
      tool.inputSchema?.required?.includes("name") &&
      tool.inputSchema?.properties?.entity_limit?.maximum === 256 &&
      tool.inputSchema?.properties?.field_limit?.maximum === 128) ||
    !listedTools.some((tool) => tool.name === "recordScenario" &&
      tool.inputSchema?.required?.includes("name") &&
      tool.inputSchema?.properties?.inputs?.maxItems === 1800) ||
    !listedTools.some((tool) => tool.name === "replayScenario" &&
      tool.inputSchema?.oneOf?.length === 2 &&
      tool.inputSchema?.properties?.recording_path?.type === "string")) {
  throw new Error("tools/list did not expose catalog and graph tools");
}
const status = toolPayload(responseById.get(3));
const assets = toolPayload(responseById.get(4));
const catalog = toolPayload(responseById.get(14));
const search = toolPayload(responseById.get(15));
const dependencies = toolPayload(responseById.get(16));
const healthGraph = toolPayload(responseById.get(19));
const libraryGraph = toolPayload(responseById.get(20));
const healthGraphLayout = toolPayload(responseById.get(34));
const read = toolPayload(responseById.get(5));
const validation = toolPayload(responseById.get(6));
const registry = toolPayload(responseById.get(7));
const contract = toolPayload(responseById.get(27));
const pagedAssets = toolPayload(responseById.get(28));
const pagedCatalog = toolPayload(responseById.get(29));
const pagedSearch = toolPayload(responseById.get(30));
const secondCatalogPage = toolPayload(responseById.get(31));
const missingAssets = toolPayload(responseById.get(35));
const missingNodes = toolPayload(responseById.get(36));
const buttonNodes = toolPayload(responseById.get(38));
if (status.ok !== true || assets.result?.assets?.length < 1 ||
    !status.next_steps?.length || !assets.next_steps?.length ||
    catalog.ok !== true || !catalog.result?.assets?.some((asset) => asset.name === "HealthBehavior") ||
    search.ok !== true || !search.result?.assets?.some((asset) => asset.name === "HealthBehavior") ||
    dependencies.ok !== true || dependencies.result?.asset?.name !== "HealthBehavior" ||
    catalog.result?.catalog_revision !== search.result?.catalog_revision ||
    catalog.result?.catalog_revision !== dependencies.result?.catalog_revision ||
    catalog.result?.content_hash !== search.result?.content_hash ||
    catalog.result?.content_hash !== dependencies.result?.content_hash ||
    healthGraph.ok !== true || healthGraph.result?.name !== "HealthBehavior" ||
    !healthGraph.result?.graph || !Array.isArray(healthGraph.result.graph.nodes) ||
    healthGraph.result?.layout_included !== false ||
    healthGraph.result.graph.nodes.some((node) => Object.hasOwn(node, "x") || Object.hasOwn(node, "y")) ||
    !Array.isArray(healthGraph.result?.graphs) ||
    healthGraph.result.graphs.some((graph) => Object.hasOwn(graph, "nodes") || Object.hasOwn(graph, "edges")) ||
    healthGraphLayout.ok !== true || healthGraphLayout.result?.layout_included !== true ||
    !healthGraphLayout.result?.graph?.nodes?.some((node) => Object.hasOwn(node, "x") && Object.hasOwn(node, "y")) ||
    !healthGraph.result?.validation ||
    libraryGraph.ok !== true || libraryGraph.result?.name !== "DirectionMath" ||
    libraryGraph.result?.graph?.name !== "Direction Orientation" ||
    read.result?.name !== "HealthBehavior" || validation.result?.valid !== true ||
    registry.result?.nodes?.length < 1 ||
    contract.ok !== true ||
    !contract.result?.operations?.some((operation) => operation.name === "describe_contract" &&
      operation.public_tools?.includes("describeContract")) ||
    !contract.result?.operations?.some((operation) => operation.name === "edit_graph" &&
      operation.public_tools?.includes("buildGraph")) ||
    contract.result?.reflection?.nodes < 1 ||
    pagedAssets.ok !== true || pagedAssets.result?.count !== 2 ||
    pagedAssets.result?.has_more !== true || pagedAssets.result?.next_cursor !== 2 ||
    pagedCatalog.ok !== true || pagedCatalog.result?.summary !== true ||
    pagedCatalog.result?.count !== 1 || pagedCatalog.result?.has_more !== true ||
    pagedCatalog.result?.assets?.[0]?.members !== undefined ||
    pagedSearch.ok !== true || pagedSearch.result?.summary !== true ||
    pagedSearch.result?.count !== 1 ||
    pagedSearch.result?.has_more !== (search.result?.assets?.length > 1) ||
    pagedSearch.result?.assets?.[0]?.members !== undefined ||
    secondCatalogPage.ok !== true || secondCatalogPage.result?.count !== 1 ||
    secondCatalogPage.result?.next_cursor !== 2 ||
    missingAssets.ok !== true || missingAssets.result?.assets?.length !== 0 ||
    !missingAssets.next_steps?.length ||
    missingNodes.ok !== true || missingNodes.result?.nodes?.length !== 0 ||
    !missingNodes.next_steps?.length ||
    !buttonNodes.result?.nodes?.some((node) => node.type === "Click UI Element") ||
    !buttonNodes.next_steps?.some((step) => step.includes("content/ui/")) ||
    (responseById.get(14)?.result?.content?.[0]?.text || "").length > 2048) {
  throw new Error("native MCP operations failed");
}
// Navigation: an event's definition, senders and handlers by node and graph;
// a function's callers through the handler whose run reaches the call, and
// its callees through the events it sends; use counts in inspectGraph.
const references = toolPayload(responseById.get(60));
const hierarchy = toolPayload(responseById.get(61));
const referenceSummary = JSON.parse(responseById.get(60)?.result?.content?.[0]?.text || "{}");
const contactSend = references.result?.uses?.find((use) =>
  use.role === "send" && use.asset?.name === "PlayerContactBehavior");
const contactCaller = hierarchy.result?.root?.callers?.find((item) => item.name === "Check Enemy Contact");
const damageCallee = hierarchy.result?.root?.callees?.find((item) => item.kind === "event" && item.name === "Damage");
const applyDamage = healthGraph.result?.graphs?.find((graph) => graph.name === "Apply Damage");
if (references.ok !== true || references.result?.definition?.asset?.path !== "content/events/Damage.yaml" ||
    !(references.result?.counts?.send >= 2) || references.result?.counts?.handle !== 1 ||
    contactSend?.graph !== "Apply Contact Damage" || !Number.isInteger(contactSend?.node) ||
    referenceSummary.counts?.send !== references.result.counts.send ||
    hierarchy.ok !== true || hierarchy.result?.root?.name !== "Apply Contact Damage" ||
    contactCaller?.callers?.[0]?.kind !== "handler" || contactCaller.callers[0].event !== "Contact Begin" ||
    !damageCallee?.callees?.some((item) => item.kind === "handler" && item.asset?.name === "HealthBehavior") ||
    !(applyDamage?.uses?.call >= 1) || !(healthGraph.result?.uses?.attach >= 1) ||
    responseById.get(62)?.result?.isError !== true ||
    toolPayload(responseById.get(62))?.error?.code !== "asset_not_found") {
  throw new Error("findReferences, callHierarchy and inspectGraph use counts failed");
}
if (!responseById.get(12)?.result?.resourceTemplates?.some((template) =>
  template.uriTemplate === "chibi://asset/{kind}/{name}")) {
  throw new Error("resources/templates/list did not expose the asset template");
}
if (!responseById.get(12)?.result?.resourceTemplates?.some((template) =>
  template.uriTemplate === "chibi://asset/{kind}/{name}/dependencies")) {
  throw new Error("resources/templates/list did not expose the dependency template");
}
if (!responseById.get(12)?.result?.resourceTemplates?.some((template) =>
  template.uriTemplate === "chibi://asset/{kind}/{name}/graph/{graph_id}")) {
  throw new Error("resources/templates/list did not expose the graph template");
}
if (!responseById.get(12)?.result?.resourceTemplates?.some((template) =>
  template.uriTemplate === "chibi://recording/{id}")) {
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
delete cleanEnvironment.CHIBI_ROOT;
const cwdIndependent = spawnSync(process.execPath, [server], {
  cwd: "/tmp",
  env: cleanEnvironment,
  input: `${[
    {
      jsonrpc: "2.0",
      id: 30,
      method: "tools/call",
      params: { name: "getProjectStatus", arguments: {} },
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
    toolPayload(cwdResponses[0]).ok !== true) {
  throw new Error("server did not resolve its project root independently of CWD");
}

const launcher = resolve(root, "tools/mcp/launch.sh");
const defaultProjectRoot = resolve(root, "projects/arena2d");
const launcherRequests = [
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
    params: { name: "getProjectStatus", arguments: {} },
  },
];
const launcherProbe = spawnSync("/bin/sh", [launcher], {
  cwd: "/tmp",
  env: cleanEnvironment,
  input: `${launcherRequests.map((request) => JSON.stringify(request)).join("\n")}\n`,
  encoding: "utf8",
  maxBuffer: 32 * 1024 * 1024,
});
if (launcherProbe.error || launcherProbe.status !== 0) {
  throw launcherProbe.error || new Error(launcherProbe.stderr || "MCP launcher failed");
}
const launcherResponses = launcherProbe.stdout.trim().split(/\r?\n/).filter(Boolean).map(JSON.parse);
const launcherTools = launcherResponses.find((response) => response.id === 1);
const launcherStatus = launcherResponses.find((response) => response.id === 2);
if (launcherResponses.length !== launcherRequests.length ||
    launcherTools?.result?.tools?.length !== expectedPublicNames.length) {
  throw new Error("MCP launcher did not keep stdout protocol-only from another CWD");
}
if (launcherStatus?.result?.structuredContent?.result?.project_root !== defaultProjectRoot) {
  throw new Error(`MCP launcher defaulted to the wrong project root: ${launcherStatus?.result?.structuredContent?.result?.project_root}`);
}

const explicitRootProbe = spawnSync("/bin/sh", [launcher], {
  cwd: "/tmp",
  env: { ...cleanEnvironment, CHIBI_ROOT: root },
  input: `${JSON.stringify(launcherRequests[1])}\n`,
  encoding: "utf8",
  maxBuffer: 32 * 1024 * 1024,
});
if (explicitRootProbe.error || explicitRootProbe.status !== 0) {
  throw explicitRootProbe.error || new Error(explicitRootProbe.stderr || "MCP launcher explicit-root probe failed");
}
const explicitRootResponse = explicitRootProbe.stdout.trim().split(/\r?\n/).filter(Boolean).map(JSON.parse)[0];
if (explicitRootResponse?.result?.structuredContent?.result?.project_root !== root) {
  throw new Error("MCP launcher did not honor an explicit CHIBI_ROOT");
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
const nativeHealthGraphLayout = nativeRequest("inspect_graph", {
  kind: "behavior",
  name: "HealthBehavior",
  graph_name: "",
  include_layout: true,
});
const nativeLibraryGraph = nativeRequest("inspect_graph", {
  kind: "library",
  name: "DirectionMath",
  graph_id: 1,
});
const nativeRead = nativeRequest("read_asset", { kind: "behavior", name: "HealthBehavior" });
const nativeValidation = nativeRequest("validate_asset", { kind: "behavior", name: "HealthBehavior" });
const linkedValidation = nativeRequest("validate_asset", { kind: "behavior", name: "TwinStickWeaponBehavior" });
if (linkedValidation.response.ok !== true || !linkedValidation.response.result?.valid) {
  throw new Error("validateAsset must link external library functions before compilation");
}

const nativeRegistry = nativeRequest("describe_registry", { sections: ["nodes"] });
const defaultRegistry = nativeRequest("describe_registry");
const nativeMissing = nativeRequest("read_asset", { path: "content/nope.yaml" });
if (nativeStatus.status !== 0 || nativeStatus.response.ok !== true ||
    nativeAssets.response.result?.assets?.length < 1 ||
    nativeCatalog.response.ok !== true || !nativeCatalog.response.result?.assets?.some((asset) => asset.name === "HealthBehavior") ||
    nativeSearch.response.ok !== true || !nativeSearch.response.result?.assets?.some((asset) => asset.name === "HealthBehavior") ||
    nativeDependencies.response.ok !== true || nativeDependencies.response.result?.asset?.name !== "HealthBehavior" ||
    nativeHealthGraph.response.ok !== true || nativeHealthGraph.response.result?.graph?.name !== "" ||
    nativeHealthGraph.response.result?.graph?.nodes?.length < 1 ||
    nativeHealthGraph.response.result?.layout_included !== false ||
    nativeHealthGraph.response.result.graph.nodes.some((node) => Object.hasOwn(node, "x") || Object.hasOwn(node, "y")) ||
    !Array.isArray(nativeHealthGraph.response.result?.graphs) ||
    nativeHealthGraph.response.result.graphs.some((graph) => Object.hasOwn(graph, "nodes") || Object.hasOwn(graph, "edges")) ||
    nativeHealthGraphLayout.response.ok !== true ||
    nativeHealthGraphLayout.response.result?.layout_included !== true ||
    !nativeHealthGraphLayout.response.result?.graph?.nodes?.some((node) => Object.hasOwn(node, "x") && Object.hasOwn(node, "y")) ||
    nativeLibraryGraph.response.ok !== true || nativeLibraryGraph.response.result?.graph?.name !== "Direction Orientation" ||
    nativeRead.response.result?.name !== "HealthBehavior" ||
    nativeValidation.response.result?.valid !== true ||
    nativeRegistry.response.result?.nodes?.length < 1 ||
    defaultRegistry.response.result?.nodes !== undefined ||
    !(defaultRegistry.response.result?.components?.length > 0) ||
    nativeMissing.status !== 6 || nativeMissing.response.error?.code !== "asset_not_found") {
  throw new Error("native controller smoke checks failed");
}

// Dependencies come from the shared reference index: events a behavior
// sends or handles, and prototypes a graph spawns.
const edgeTo = (response, kind, name) =>
  response.response.result?.edges?.some((edge) => edge.to.kind === kind && edge.to.name === name && edge.to.resolved);
const edgeFrom = (response, kind, name) =>
  response.response.result?.edges?.some((edge) => edge.from.kind === kind && edge.from.name === name);
const spawners = nativeRequest("asset_dependencies", { kind: "prototype", name: "ArenaHitPulse", direction: "incoming" });
const senders = nativeRequest("asset_dependencies", { kind: "event", name: "Damage", direction: "incoming" });
if (!edgeTo(nativeDependencies, "event", "Damage") || !edgeTo(nativeDependencies, "event", "Died") ||
    !edgeFrom(spawners, "behavior", "ArenaImpactFeedback") ||
    !edgeFrom(senders, "behavior", "PlayerContactBehavior") || !edgeFrom(senders, "behavior", "HealthBehavior")) {
  throw new Error("asset dependencies must include event and Spawn Prototype links");
}

const systemRoot = mkdtempSync(join(tmpdir(), "chibi-system-"));
try {
  const systemDirectory = join(systemRoot, "content", "systems");
  mkdirSync(systemDirectory, { recursive: true });
  // Asset files are YAML, and loaders also accept JSON documents.
  const systemPath = join(systemDirectory, "MovementSystem.yaml");
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
      inspectedSystem.response.result?.query?.component !== "GAME.GameTransform" ||
      !Array.isArray(inspectedSystem.response.result?.validation?.diagnostics) ||
      validatedSystem.response.ok !== true ||
      validatedSystem.response.result?.valid !== true) {
    throw new Error("system inspection smoke checks failed");
  }
} finally {
  rmSync(systemRoot, { recursive: true, force: true });
}

const queryRoot = mkdtempSync(join(tmpdir(), "chibi-query-fusion-"));
try {
  const document = nativeRequest("read_asset", { kind: "behavior", name: "ArenaWaveBehavior" }).response.result.document;
  const query = document.graphs.find((graph) => graph.name === "Count Members Snapshot");
  if (!query) throw new Error("query fusion fixture is missing");
  document.name = "QueryFusionCheck";
  document.variables = [];
  document.graphs = [{ ...document.graphs[0], nodes: [], edges: [] }, query];
  const directory = join(queryRoot, "content", "behaviors");
  mkdirSync(directory, { recursive: true });
  writeFileSync(join(directory, "QueryFusionCheck.yaml"), JSON.stringify(document));
  const inspected = nativeRequestAt(queryRoot, "inspect_graph", {
    kind: "behavior", name: "QueryFusionCheck", graph_name: query.name,
  });
  const validated = nativeRequestAt(queryRoot, "validate_asset", {
    kind: "behavior", name: "QueryFusionCheck",
  });
  const nodes = query.nodes.filter((node) => node.type === "Count Matching Entities");
  for (const result of [inspected.response.result?.validation, validated.response.result]) {
    const notes = result?.diagnostics?.filter((item) => item.rule_name === "PERF-QUERY-FUSION");
    if (!result?.valid || result.notes !== 4 || result.warnings !== 0 || notes?.length !== 4 ||
        notes.some((item) => item.severity !== "note" || item.graph !== query.id ||
          !nodes.some((node) => node.id === item.node) || !String(item.pin).startsWith(`${item.node}.`))) {
      throw new Error(`query fusion diagnostics lost severity or source identity: ${JSON.stringify(result)}`);
    }
  }
} finally {
  rmSync(queryRoot, { recursive: true, force: true });
}

const scenarioRoot = mkdtempSync(join(tmpdir(), "chibi-scenario-"));
try {
  const prototypeDirectory = join(scenarioRoot, "content", "prototypes");
  mkdirSync(prototypeDirectory, { recursive: true });
  const prototype = nativeRequestAt(resolve(root, "test/fixtures"), "read_asset", {
    kind: "prototype", name: "Editor2DPreview",
  }).response.result.document;
  prototype.components.GameInput = { move: { x: 0, y: 0 } };
  prototype.components.GameVelocity = { x: 0, y: 0 };
  prototype.behaviors = ["ReplayInput"];
  writeFileSync(join(prototypeDirectory, "Editor2DPreview.yaml"), JSON.stringify(prototype));
  const behaviorDirectory = join(scenarioRoot, "content", "behaviors");
  mkdirSync(behaviorDirectory, { recursive: true });
  writeFileSync(
    join(behaviorDirectory, "ReplayInput.yaml"),
    readFileSync(resolve(root, "test/fixtures/content/behaviors/ReplayInput.yaml"))
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
        name: "runScenario",
        arguments: { kind: "prototype", name: "Editor2DPreview", frames: 2, fixed_dt: 1 / 60 },
      },
    },
    {
      jsonrpc: "2.0",
      id: 3,
      method: "tools/call",
      params: {
        name: "runScenario",
        arguments: { kind: "prototype", name: "Editor2DPreview", frames: 2, fixed_dt: 1 / 60 },
      },
    },
    {
      jsonrpc: "2.0",
      id: 4,
      method: "tools/call",
      params: {
        name: "runScenario",
        arguments: { name: "Editor2DPreview", frames: 108001 },
      },
    },
    {
      jsonrpc: "2.0",
      id: 5,
      method: "tools/call",
      params: {
        name: "captureRuntimeSnapshot",
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
        name: "captureRuntimeSnapshot",
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
        name: "captureRuntimeSnapshot",
        arguments: { name: "Editor2DPreview", entity_limit: 0 },
      },
    },
    {
      jsonrpc: "2.0",
      id: 8,
      method: "tools/call",
      params: {
        name: "recordScenario",
        arguments: {
          kind: "prototype",
          name: "Editor2DPreview",
          frames: 3,
          fixed_dt: 1 / 60,
          inputs: [
            {
              movement: { x: 0.5, y: -0.25 },
              aim: { x: 1, y: 0 },
              actions: [{ name: "Fire", pressed: true }],
              events: [{ name: "Trace" }],
            },
            {
              movement: { x: 0, y: 0 },
              aim: { x: 0, y: 1 },
              actions: [{ name: "Fire", pressed: false }],
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
        name: "recordScenario",
        arguments: {
          kind: "prototype",
          name: "Editor2DPreview",
          frames: 3,
          fixed_dt: 1 / 60,
          inputs: [
            {
              movement: { x: 0.5, y: -0.25 },
              aim: { x: 1, y: 0 },
              actions: [{ name: "Fire", pressed: true }],
              events: [{ name: "Trace" }],
            },
            {
              movement: { x: 0, y: 0 },
              aim: { x: 0, y: 1 },
              actions: [{ name: "Fire", pressed: false }],
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
        name: "replayScenario",
        arguments: { recording: null },
      },
    },
    {
      jsonrpc: "2.0",
      id: 11,
      method: "tools/call",
      params: {
        name: "replayScenario",
        arguments: { recording_path: `${scenarioRoot}/missing-recording.json` },
      },
    },
    {
      jsonrpc: "2.0",
      id: 12,
      method: "tools/call",
      params: {
        name: "replayScenario",
        arguments: { recording: {}, recording_path: "recording.json" },
      },
    },
  ];
  const scenarioProcess = spawnSync(process.execPath, [server], {
    cwd: root,
    env: { ...process.env, CHIBI_ROOT: scenarioRoot, CHIBI_CTL: controller },
    input: `${scenarioRequests.map((request) => JSON.stringify(request)).join("\n")}\n`,
    encoding: "utf8",
    maxBuffer: 32 * 1024 * 1024,
  });
  if (scenarioProcess.error || scenarioProcess.status !== 0) {
    throw scenarioProcess.error || new Error(scenarioProcess.stderr || "scenario MCP server failed");
  }
  const scenarioResponses = scenarioProcess.stdout.trim().split("\n").filter(Boolean).map(JSON.parse);
  const scenarioById = new Map(scenarioResponses.map((response) => [response.id, response]));
  const firstScenario = toolPayload(scenarioById.get(2));
  const secondScenario = toolPayload(scenarioById.get(3));
  const firstSnapshot = toolPayload(scenarioById.get(5));
  const secondSnapshot = toolPayload(scenarioById.get(6));
  const firstRecord = toolPayload(scenarioById.get(8));
  const secondRecord = toolPayload(scenarioById.get(9));
  if (firstRecord.ok !== true || secondRecord.ok !== true) {
    throw new Error(`scenario recording failed: ${JSON.stringify([firstRecord.error, secondRecord.error])}`);
  }
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
    id: undefined,
    recording_id: undefined,
  };
  const secondRecordingWithoutPath = secondRecording && {
    ...secondRecording,
    artifact_path: undefined,
    recording_path: undefined,
    id: undefined,
    recording_id: undefined,
  };
  const replayInlineRequest = {
    jsonrpc: "2.0",
    id: 13,
    method: "tools/call",
    params: { name: "replayScenario", arguments: { recording: firstRecording } },
  };
  const replayProcess = spawnSync(process.execPath, [server], {
    cwd: root,
    env: { ...process.env, CHIBI_ROOT: scenarioRoot, CHIBI_CTL: controller },
    input: `${JSON.stringify(replayInlineRequest)}\n`,
    encoding: "utf8",
    maxBuffer: 32 * 1024 * 1024,
  });
  if (replayProcess.error || replayProcess.status !== 0) {
    throw replayProcess.error || new Error(replayProcess.stderr || "replay MCP server failed");
  }
  const replayResponse = JSON.parse(replayProcess.stdout.trim().split("\n").filter(Boolean).at(-1));
  const replayResult = toolPayload(replayResponse);
  const replayPathRequest = {
    jsonrpc: "2.0",
    id: 15,
    method: "tools/call",
    params: { name: "replayScenario", arguments: { recording_path: firstArtifactPath } },
  };
  const replayPathProcess = spawnSync(process.execPath, [server], {
    cwd: root,
    env: { ...process.env, CHIBI_ROOT: scenarioRoot, CHIBI_CTL: controller },
    input: `${JSON.stringify(replayPathRequest)}\n`,
    encoding: "utf8",
    maxBuffer: 32 * 1024 * 1024,
  });
  if (replayPathProcess.error || replayPathProcess.status !== 0) {
    throw replayPathProcess.error || new Error(replayPathProcess.stderr || "recording path replay MCP server failed");
  }
  const replayPathResponse = JSON.parse(replayPathProcess.stdout.trim().split("\n").filter(Boolean).at(-1));
  const replayPathResult = toolPayload(replayPathResponse);
  const recordingId = firstRecord.result?.recording_id || firstRecording?.recording_id || firstRecording?.id;
  const recordingResourceRequest = {
    jsonrpc: "2.0",
    id: 16,
    method: "resources/read",
    params: { uri: `chibi://recording/${recordingId}` },
  };
  const recordingResourceProcess = spawnSync(process.execPath, [server], {
    cwd: root,
    env: { ...process.env, CHIBI_ROOT: scenarioRoot, CHIBI_CTL: controller },
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
    params: { name: "replayScenario", arguments: { recording: mutatedRecording } },
  };
  const divergenceProcess = spawnSync(process.execPath, [server], {
    cwd: root,
    env: { ...process.env, CHIBI_ROOT: scenarioRoot, CHIBI_CTL: controller },
    input: `${JSON.stringify(divergenceRequest)}\n`,
    encoding: "utf8",
    maxBuffer: 32 * 1024 * 1024,
  });
  if (divergenceProcess.error || divergenceProcess.status !== 0) {
    throw divergenceProcess.error || new Error(divergenceProcess.stderr || "divergence MCP server failed");
  }
  const divergenceResponse = JSON.parse(divergenceProcess.stdout.trim().split("\n").filter(Boolean).at(-1));
  const divergenceResult = toolPayload(divergenceResponse);
  const firstDivergenceFrame = divergenceResult.result?.first_divergence_frame ??
    divergenceResult.result?.first_divergence?.frame ??
    divergenceResult.result?.divergence?.frame;
  if (scenarioById.get(1)?.result?.tools?.some((tool) => tool.name === "runScenario") !== true ||
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
