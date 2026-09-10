#!/usr/bin/env node

import { spawnSync } from "node:child_process";
import { resolve } from "node:path";

const root = resolve(process.argv[2] || process.cwd());
const server = resolve(root, "tools/mcp/server.mjs");
const requests = [
  { jsonrpc: "2.0", id: 1, method: "initialize", params: {} },
  { jsonrpc: "2.0", id: 2, method: "tools/list", params: {} },
  { jsonrpc: "2.0", id: 3, method: "tools/call", params: { name: "project_status", arguments: {} } },
  { jsonrpc: "2.0", id: 4, method: "resources/read", params: { uri: "blueprints://project/status" } },
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
if (responses.length !== requests.length) {
  throw new Error(`expected ${requests.length} responses, got ${responses.length}`);
}
if (responses[0].result?.serverInfo?.name !== "c-blueprints") {
  throw new Error("initialize did not identify c-blueprints");
}
if (!responses[1].result?.tools?.some((tool) => tool.name === "validate_asset")) {
  throw new Error("tools/list did not expose validate_asset");
}
if (responses[2].result?.isError || responses[3].error) {
  throw new Error("MCP status request failed");
}
console.log("MCP protocol smoke passed");
