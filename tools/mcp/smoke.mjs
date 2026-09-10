#!/usr/bin/env node

import { spawnSync } from "node:child_process";
import { resolve } from "node:path";

const root = resolve(process.argv[2] || process.cwd());
const server = resolve(root, "tools/mcp/server.mjs");
const controller = resolve(root, "build/blueprints_ctl");

function nativeRequest(operation, args = {}) {
  const result = spawnSync(controller, ["--project-root", root], {
    cwd: root,
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
      tool.inputSchema?.properties?.graph_name?.type === "string")) {
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
    contract.result?.reflection?.nodes < 1) {
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
    responseById.get(26)?.error?.code !== -32602) {
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
console.log("MCP protocol smoke passed");
