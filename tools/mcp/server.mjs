#!/usr/bin/env node

import { existsSync } from "node:fs";
import { resolve } from "node:path";
import { spawnSync } from "node:child_process";

const protocolVersion = "2025-11-25";
const supportedProtocolVersions = [protocolVersion, "2025-06-18"];
const serverVersion = "0.1.0";
const root = resolve(process.env.CBLUEPRINTS_ROOT || process.cwd());

function isNotification(message) {
  return !Object.prototype.hasOwnProperty.call(message, "id");
}

function reply(message, response) {
  return isNotification(message) ? null : response;
}

function negotiateProtocolVersion(requested) {
  return supportedProtocolVersions.includes(requested) ? requested : protocolVersion;
}

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
  const outputSchema = { type: "object", additionalProperties: true };
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
      outputSchema,
    },
    {
      name: "list_assets",
      description: "List authored JSON assets with stable paths, sizes, and content hashes.",
      inputSchema: noArguments,
      outputSchema,
    },
    {
      name: "read_asset",
      description: "Read one authored asset as parsed JSON.",
      inputSchema: assetArguments,
      outputSchema,
    },
    {
      name: "validate_asset",
      description: "Load and validate one asset through the engine's native loader/compiler.",
      inputSchema: assetArguments,
      outputSchema,
    },
    {
      name: "describe_registry",
      description: "Describe reflected graph nodes, pins, types, services, operators, and components.",
      inputSchema: noArguments,
      outputSchema,
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
    const error = new Error(`blueprints_ctl could not start: ${result.error.message}`);
    error.rpcCode = -32603;
    throw error;
  }
  const line = result.stdout.trim().split("\n").filter(Boolean).at(-1);
  if (!line) {
    const error = new Error(result.stderr.trim() || "blueprints_ctl returned no response");
    error.rpcCode = -32603;
    throw error;
  }
  try {
    return JSON.parse(line);
  } catch (error) {
    error.rpcCode = -32603;
    throw error;
  }
}

function toolCall(name, args) {
  switch (name) {
    case "project_status": return nativeRequest("status");
    case "list_assets": return nativeRequest("list_assets");
    case "read_asset": return nativeRequest("read_asset", args || {});
    case "validate_asset": return nativeRequest("validate_asset", args || {});
    case "describe_registry": return nativeRequest("describe_registry");
    default: {
      const error = new Error(`unknown tool: ${name}`);
      error.rpcCode = -32602;
      throw error;
    }
  }
}

function toolResult(response) {
  return {
    content: [{ type: "text", text: JSON.stringify(response) }],
    structuredContent: response,
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
  if (!message || typeof message !== "object" || Array.isArray(message) ||
      message.jsonrpc !== "2.0" || typeof message.method !== "string") {
    return errorResponse(message?.id, -32600, "Invalid Request");
  }
  if (message.method === "notifications/initialized") {
    return null;
  }
  if (message.method === "initialize") {
    return reply(message, {
      jsonrpc: "2.0",
      id: message.id,
      result: {
        protocolVersion: negotiateProtocolVersion(message.params?.protocolVersion),
        capabilities: { tools: {}, resources: {} },
        serverInfo: { name: "c-blueprints", version: serverVersion },
      },
    });
  }
  if (message.method === "ping") {
    return reply(message, { jsonrpc: "2.0", id: message.id, result: {} });
  }
  if (message.method === "tools/list") {
    return reply(message, { jsonrpc: "2.0", id: message.id, result: { tools: toolDefinitions() } });
  }
  if (message.method === "resources/list") {
    return reply(message, {
      jsonrpc: "2.0",
      id: message.id,
      result: {
        resources: [{
          uri: "blueprints://project/status",
          name: "c-blueprints project status",
          mimeType: "application/json",
        }],
        resourceTemplates: [{
          uriTemplate: "blueprints://asset/{kind}/{name}",
          name: "c-blueprints authored asset",
          mimeType: "application/json",
        }],
      },
    });
  }
  if (message.method === "resources/read") {
    try {
      const uri = message.params?.uri;
      const response = resourceRead(uri);
      return reply(message, {
        jsonrpc: "2.0",
        id: message.id,
        result: { contents: [{ uri, mimeType: "application/json", text: JSON.stringify(response) }] },
      });
    } catch (error) {
      return reply(message, errorResponse(message.id, -32000, error.message));
    }
  }
  if (message.method === "tools/call") {
    try {
      const response = toolCall(message.params?.name, message.params?.arguments);
      return reply(message, { jsonrpc: "2.0", id: message.id, result: toolResult(response) });
    } catch (error) {
      return reply(message, errorResponse(message.id, error.rpcCode || -32603, error.message));
    }
  }
  return reply(message, errorResponse(message.id, -32601, `Method not found: ${message.method}`));
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
