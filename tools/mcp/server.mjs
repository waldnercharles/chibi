#!/usr/bin/env node

import { existsSync } from "node:fs";
import { resolve } from "node:path";
import { spawnSync } from "node:child_process";

const protocolVersion = "2025-11-25";
const serverVersion = "0.1.0";
const root = resolve(process.env.CBLUEPRINTS_ROOT || process.cwd());

function controllerPath() {
  const candidates = [
    process.env.CBLUEPRINTS_CTL,
    resolve(root, "build", "blueprints_ctl"),
    resolve(root, "build-release", "blueprints_ctl"),
  ].filter(Boolean);
  return candidates.find((candidate) => existsSync(candidate)) || "blueprints_ctl";
}

function send(message) {
  process.stdout.write(`${JSON.stringify(message)}\n`);
}

function errorResponse(id, code, message) {
  return {
    jsonrpc: "2.0",
    id: id ?? null,
    error: { code, message },
  };
}

function toolDefinitions() {
  const noArguments = { type: "object", additionalProperties: false };
  const assetArguments = {
    type: "object",
    properties: {
      kind: {
        type: "string",
        description: "Asset family, such as behavior, prototype, or scene.",
      },
      name: {
        type: "string",
        description: "Asset filename without the .json suffix.",
      },
    },
    required: ["kind", "name"],
    additionalProperties: false,
  };
  return [
    {
      name: "project_status",
      description: "Return project paths, asset counts, and reflected registry counts.",
      inputSchema: noArguments,
    },
    {
      name: "list_assets",
      description: "List authored JSON assets with stable paths, sizes, and content hashes.",
      inputSchema: noArguments,
    },
    {
      name: "read_asset",
      description: "Read one authored asset as parsed JSON.",
      inputSchema: assetArguments,
    },
    {
      name: "validate_asset",
      description: "Load and validate one asset through the engine's native loader/compiler.",
      inputSchema: assetArguments,
    },
    {
      name: "describe_registry",
      description: "Describe reflected graph nodes, pins, types, services, operators, and components.",
      inputSchema: noArguments,
    },
  ];
}

function nativeRequest(operation, args = {}) {
  const result = spawnSync(
    controllerPath(),
    ["--project-root", root],
    {
      cwd: root,
      input: `${JSON.stringify({ contract: 1, operation, ...args })}\n`,
      encoding: "utf8",
      maxBuffer: 32 * 1024 * 1024,
    },
  );
  if (result.error) {
    throw new Error(`blueprints_ctl could not start: ${result.error.message}`);
  }
  const line = result.stdout.trim().split("\n").filter(Boolean).at(-1);
  if (!line) {
    throw new Error(result.stderr.trim() || "blueprints_ctl returned no response");
  }
  return JSON.parse(line);
}

function toolCall(name, args) {
  switch (name) {
    case "project_status": return nativeRequest("status");
    case "list_assets": return nativeRequest("list_assets");
    case "read_asset": return nativeRequest("read_asset", args || {});
    case "validate_asset": return nativeRequest("validate_asset", args || {});
    case "describe_registry": return nativeRequest("describe_registry");
    default: throw new Error(`unknown tool: ${name}`);
  }
}

function toolResult(response) {
  return {
    content: [{ type: "text", text: JSON.stringify(response) }],
    isError: response.ok !== true,
  };
}

function resourceRead(uri) {
  if (uri === "blueprints://project/status") {
    return nativeRequest("status");
  }
  const match = /^blueprints:\/\/asset\/([^/]+)\/([^/]+)$/.exec(uri);
  if (match) {
    return nativeRequest("read_asset", { kind: match[1], name: match[2] });
  }
  throw new Error(`unknown resource: ${uri}`);
}

function dispatch(message) {
  if (message.jsonrpc !== "2.0" || typeof message.method !== "string") {
    return errorResponse(message.id, -32600, "Invalid Request");
  }
  if (message.method === "notifications/initialized") {
    return null;
  }
  if (message.method === "initialize") {
    return {
      jsonrpc: "2.0",
      id: message.id,
      result: {
        protocolVersion,
        capabilities: { tools: {}, resources: {} },
        serverInfo: { name: "c-blueprints", version: serverVersion },
      },
    };
  }
  if (message.method === "ping") {
    return { jsonrpc: "2.0", id: message.id, result: {} };
  }
  if (message.method === "tools/list") {
    return { jsonrpc: "2.0", id: message.id, result: { tools: toolDefinitions() } };
  }
  if (message.method === "resources/list") {
    return {
      jsonrpc: "2.0",
      id: message.id,
      result: {
        resources: [{
          uri: "blueprints://project/status",
          name: "c-blueprints project status",
          mimeType: "application/json",
        }],
      },
    };
  }
  if (message.method === "resources/read") {
    try {
      const uri = message.params?.uri;
      const response = resourceRead(uri);
      return {
        jsonrpc: "2.0",
        id: message.id,
        result: { contents: [{ uri, mimeType: "application/json", text: JSON.stringify(response) }] },
      };
    } catch (error) {
      return errorResponse(message.id, -32000, error.message);
    }
  }
  if (message.method === "tools/call") {
    try {
      const response = toolCall(message.params?.name, message.params?.arguments);
      return { jsonrpc: "2.0", id: message.id, result: toolResult(response) };
    } catch (error) {
      return {
        jsonrpc: "2.0",
        id: message.id,
        result: { content: [{ type: "text", text: error.message }], isError: true },
      };
    }
  }
  return errorResponse(message.id, -32601, `Method not found: ${message.method}`);
}

let pending = "";
process.stdin.setEncoding("utf8");
process.stdin.on("data", (chunk) => {
  pending += chunk;
  let newline;
  while ((newline = pending.indexOf("\n")) >= 0) {
    const line = pending.slice(0, newline).trim();
    pending = pending.slice(newline + 1);
    if (!line) continue;
    try {
      const message = JSON.parse(line);
      const response = dispatch(message);
      if (response) send(response);
    } catch (error) {
      send(errorResponse(null, -32700, `Parse error: ${error.message}`));
    }
  }
});
