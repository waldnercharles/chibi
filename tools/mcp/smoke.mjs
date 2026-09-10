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
  { jsonrpc: "2.0", id: 5, method: "tools/call", params: { name: "read_asset", arguments: { kind: "behavior", name: "HealthBehavior" } } },
  { jsonrpc: "2.0", id: 6, method: "tools/call", params: { name: "validate_asset", arguments: { kind: "behavior", name: "HealthBehavior" } } },
  { jsonrpc: "2.0", id: 7, method: "tools/call", params: { name: "describe_registry", arguments: {} } },
  { jsonrpc: "2.0", id: 8, method: "resources/list", params: {} },
  { jsonrpc: "2.0", id: 12, method: "resources/templates/list", params: {} },
  { jsonrpc: "2.0", id: 9, method: "resources/read", params: { uri: "blueprints://project/status" } },
  { jsonrpc: "2.0", id: 10, method: "resources/read", params: { uri: "blueprints://asset/behavior/HealthBehavior" } },
  { jsonrpc: "2.0", id: 11, method: "tools/call", params: { name: "missing_tool", arguments: {} } },
  { jsonrpc: "2.0", id: 13, method: "resources/read", params: { uri: "blueprints://asset/behavior/nope" } },
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
const status = JSON.parse(responseById.get(3).result.content[0].text);
const assets = JSON.parse(responseById.get(4).result.content[0].text);
const read = JSON.parse(responseById.get(5).result.content[0].text);
const validation = JSON.parse(responseById.get(6).result.content[0].text);
const registry = JSON.parse(responseById.get(7).result.content[0].text);
if (status.ok !== true || assets.result?.assets?.length < 1 ||
    read.result?.name !== "HealthBehavior" || validation.result?.valid !== true ||
    registry.result?.nodes?.length < 1) {
  throw new Error("native MCP operations failed");
}
if (!responseById.get(12)?.result?.resourceTemplates?.some((template) =>
  template.uriTemplate === "blueprints://asset/{kind}/{name}")) {
  throw new Error("resources/templates/list did not expose the asset template");
}
if (responseById.get(9)?.error || responseById.get(10)?.error || !responseById.get(10)?.result?.contents?.length) {
  throw new Error("MCP status request failed");
}
if (responseById.get(11)?.error?.code !== -32602) {
  throw new Error("unknown tools return the expected JSON-RPC error");
}
if (responseById.get(13)?.error?.code !== -32002) {
  throw new Error("missing resources return the expected JSON-RPC error");
}

const nativeStatus = nativeRequest("status");
const nativeAssets = nativeRequest("list_assets");
const nativeRead = nativeRequest("read_asset", { kind: "behavior", name: "HealthBehavior" });
const nativeValidation = nativeRequest("validate_asset", { kind: "behavior", name: "HealthBehavior" });
const nativeRegistry = nativeRequest("describe_registry");
const nativeMissing = nativeRequest("read_asset", { path: "content/nope.json" });
if (nativeStatus.status !== 0 || nativeStatus.response.ok !== true ||
    nativeAssets.response.result?.assets?.length < 1 ||
    nativeRead.response.result?.name !== "HealthBehavior" ||
    nativeValidation.response.result?.valid !== true ||
    nativeRegistry.response.result?.nodes?.length < 1 ||
    nativeMissing.status !== 6 || nativeMissing.response.error?.code !== "asset_not_found") {
  throw new Error("native controller smoke checks failed");
}
console.log("MCP protocol smoke passed");
