#!/usr/bin/env node

import { existsSync } from "node:fs";
import { dirname, isAbsolute, resolve } from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const protocolVersion = "2025-11-25";
const supportedProtocolVersions = [protocolVersion, "2025-06-18"];
const serverVersion = "0.1.0";
const serverRoot = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
const configuredRoot = process.env.CBLUEPRINTS_ROOT;
const root = configuredRoot
  ? resolve(isAbsolute(configuredRoot) ? configuredRoot : resolve(serverRoot, configuredRoot))
  : serverRoot;

function protocolError(code, message) {
  const error = new Error(message);
  error.rpcCode = code;
  return error;
}

function isObject(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function isNotification(message) {
  return !Object.prototype.hasOwnProperty.call(message, "id");
}

function reply(message, response) {
  return isNotification(message) ? null : response;
}

function negotiateProtocolVersion(requested) {
  return supportedProtocolVersions.includes(requested) ? requested : protocolVersion;
}

function resourceTemplates() {
  return [{
    uriTemplate: "blueprints://asset/{kind}/{name}",
    name: "c-blueprints authored asset",
    mimeType: "application/json",
  }, {
    uriTemplate: "blueprints://asset/{kind}/{name}/dependencies",
    name: "c-blueprints asset dependencies",
    mimeType: "application/json",
  }, {
    uriTemplate: "blueprints://asset/{kind}/{name}/graph/{graph_id}",
    name: "c-blueprints normalized graph",
    mimeType: "application/json",
  }];
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
  const response = {
    jsonrpc: "2.0",
    error: { code, message },
  };
  if (id !== undefined) {
    response.id = id;
  }
  return response;
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
  const searchArguments = {
    type: "object",
    properties: {
      query: {
        type: "string",
        description: "Case-insensitive text to match against asset kind, name, path, or description.",
      },
      kinds: {
        type: "array",
        items: { type: "string" },
        description: "Optional asset families to include in the search.",
      },
    },
    required: ["query"],
    additionalProperties: false,
  };
  const dependencyArguments = {
    ...assetArguments,
    properties: {
      ...assetArguments.properties,
      direction: {
        type: "string",
        enum: ["outgoing", "incoming", "both"],
        description: "Which side of the dependency graph to return.",
      },
      transitive: {
        type: "boolean",
        description: "Traverse dependencies beyond the requested asset.",
      },
    },
  };
  const graphArguments = {
    ...assetArguments,
    properties: {
      ...assetArguments.properties,
      graph_id: {
        type: "integer",
        minimum: 0,
        description: "Optional stable graph identifier within the asset.",
      },
      graph_name: {
        type: "string",
        description: "Optional graph name selector. Omit both selectors to inspect all graphs and select the first graph.",
      },
    },
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
      name: "catalog",
      description: "Return the semantic asset catalog, dependency edges, diagnostics, and reflected registry metadata.",
      inputSchema: noArguments,
      outputSchema,
    },
    {
      name: "search_assets",
      description: "Search the semantic asset catalog by kind, name, path, or description.",
      inputSchema: searchArguments,
      outputSchema,
    },
    {
      name: "asset_dependencies",
      description: "Inspect direct or transitive incoming and outgoing asset dependencies.",
      inputSchema: dependencyArguments,
      outputSchema,
    },
    {
      name: "inspect_graph",
      description: "Return normalized graph nodes, pins, edges, calls, effects, and validation diagnostics.",
      inputSchema: graphArguments,
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
  const timeoutValue = Number(process.env.CBLUEPRINTS_CTL_TIMEOUT_MS || 30000);
  const timeout = Number.isSafeInteger(timeoutValue) && timeoutValue > 0 ? timeoutValue : 30000;
  const result = spawnSync(
    controllerPath(),
    ["--project-root", root],
    {
      cwd: root,
      input: `${JSON.stringify({ contract: 1, operation, ...args })}\n`,
      encoding: "utf8",
      maxBuffer: 32 * 1024 * 1024,
      timeout,
    },
  );
  if (result.error) {
    const code = result.error.code === "ETIMEDOUT" ? -32001 : -32603;
    throw protocolError(code, `blueprints_ctl failed: ${result.error.message}`);
  }
  if (result.signal) {
    throw protocolError(-32603, `blueprints_ctl terminated by ${result.signal}`);
  }
  const line = result.stdout.trim().split("\n").filter(Boolean).at(-1);
  if (!line) {
    throw protocolError(-32603, result.stderr.trim() || "blueprints_ctl returned no response");
  }
  let response;
  try {
    response = JSON.parse(line);
  } catch (error) {
    throw protocolError(-32603, `blueprints_ctl returned invalid JSON: ${error.message}`);
  }
  if (result.status !== 0 && response.ok === true) {
    throw protocolError(
      -32603,
      `blueprints_ctl exited with status ${result.status} after a successful response`,
    );
  }
  return response;
}

function validateSchema(value, schema, path) {
  if (schema.type === "object") {
    if (!isObject(value)) {
      throw protocolError(-32602, `${path} must be an object`);
    }
    for (const name of schema.required || []) {
      if (!Object.hasOwn(value, name)) {
        throw protocolError(-32602, `${path}.${name} is required`);
      }
    }
    if (schema.additionalProperties === false) {
      for (const name of Object.keys(value)) {
        if (!Object.hasOwn(schema.properties || {}, name)) {
          throw protocolError(-32602, `${path}.${name} is not supported`);
        }
      }
    }
    for (const [name, child] of Object.entries(schema.properties || {})) {
      if (Object.hasOwn(value, name)) {
        validateSchema(value[name], child, `${path}.${name}`);
      }
    }
    return;
  }
  if (schema.type === "string" && typeof value !== "string") {
    throw protocolError(-32602, `${path} must be a string`);
  }
  if (schema.type === "boolean" && typeof value !== "boolean") {
    throw protocolError(-32602, `${path} must be a boolean`);
  }
  if (schema.type === "integer" && (!Number.isSafeInteger(value) || value < (schema.minimum || 0))) {
    throw protocolError(-32602, `${path} must be a non-negative integer`);
  }
  if (schema.type === "array") {
    if (!Array.isArray(value)) {
      throw protocolError(-32602, `${path} must be an array`);
    }
    if (schema.items) {
      value.forEach((item, index) => validateSchema(item, schema.items, `${path}[${index}]`));
    }
  }
  if (schema.enum && !schema.enum.includes(value)) {
    throw protocolError(-32602, `${path} has an unsupported value`);
  }
}

function validateToolArguments(name, args) {
  const tool = toolDefinitions().find((candidate) => candidate.name === name);
  if (!tool) {
    throw protocolError(-32602, "tools/call requires a known tool name");
  }
  validateSchema(args === undefined ? {} : args, tool.inputSchema, "arguments");
}

function toolCall(name, args) {
  if (typeof name !== "string") {
    throw protocolError(-32602, "tools/call requires a tool name");
  }
  validateToolArguments(name, args);
  switch (name) {
    case "project_status": return nativeRequest("status");
    case "list_assets": return nativeRequest("list_assets");
    case "catalog": return nativeRequest("catalog");
    case "search_assets": return nativeRequest("search_assets", args || {});
    case "asset_dependencies": return nativeRequest("asset_dependencies", args || {});
    case "inspect_graph": return nativeRequest("inspect_graph", args || {});
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
  if (typeof uri !== "string" || !uri) {
    throw protocolError(-32602, "resources/read requires a URI string");
  }
  if (!uri.startsWith("blueprints://")) {
    throw protocolError(-32602, `invalid resource URI: ${uri}`);
  }
  const decode = (value) => {
    try {
      return decodeURIComponent(value);
    } catch (error) {
      throw protocolError(-32602, `invalid resource URI encoding: ${error.message}`);
    }
  };
  let response;
  if (uri === "blueprints://project/status") {
    response = nativeRequest("status");
  } else if (uri === "blueprints://catalog") {
    response = nativeRequest("catalog");
  } else {
    const graphMatch = /^blueprints:\/\/asset\/([^/]+)\/([^/]+)\/graph\/([^/]+)$/.exec(uri);
    if (graphMatch) {
      const graphId = Number(decode(graphMatch[3]));
      if (!Number.isSafeInteger(graphId) || graphId < 0) {
        throw protocolError(-32602, `invalid graph resource: ${uri}`);
      }
      response = nativeRequest("inspect_graph", {
        kind: decode(graphMatch[1]),
        name: decode(graphMatch[2]),
        graph_id: graphId,
      });
    }
    const dependencyMatch = /^blueprints:\/\/asset\/([^/]+)\/([^/]+)\/dependencies$/.exec(uri);
    if (response === undefined && dependencyMatch) {
      response = nativeRequest("asset_dependencies", {
        kind: decode(dependencyMatch[1]),
        name: decode(dependencyMatch[2]),
      });
    }
    const match = /^blueprints:\/\/asset\/([^/]+)\/([^/]+)$/.exec(uri);
    if (response === undefined && !match) {
      throw protocolError(-32002, `resource not found: ${uri}`);
    }
    if (response === undefined) {
      response = nativeRequest("read_asset", {
        kind: decode(match[1]),
        name: decode(match[2]),
      });
    }
  }
  if (response.ok !== true) {
    const code = response.error?.code === "asset_not_found" ? -32002 : -32603;
    throw protocolError(code, response.error?.message || "resource read failed");
  }
  return response;
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
    const params = message.params;
    if (!params || typeof params.protocolVersion !== "string" ||
        !params.capabilities || typeof params.capabilities !== "object" ||
        !params.clientInfo || typeof params.clientInfo !== "object") {
      return reply(message, errorResponse(message.id, -32602, "initialize params are incomplete"));
    }
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
        }, {
          uri: "blueprints://catalog",
          name: "c-blueprints semantic asset catalog",
          mimeType: "application/json",
        }],
      },
    });
  }
  if (message.method === "resources/templates/list") {
    return reply(message, {
      jsonrpc: "2.0",
      id: message.id,
      result: { resourceTemplates: resourceTemplates() },
    });
  }
  if (message.method === "resources/read") {
    try {
      if (!isObject(message.params)) {
        throw protocolError(-32602, "resources/read params are required");
      }
      const uri = message.params.uri;
      const response = resourceRead(uri);
      return reply(message, {
        jsonrpc: "2.0",
        id: message.id,
        result: { contents: [{ uri, mimeType: "application/json", text: JSON.stringify(response) }] },
      });
    } catch (error) {
      return reply(message, errorResponse(message.id, error.rpcCode || -32603, error.message));
    }
  }
  if (message.method === "tools/call") {
    try {
      if (!isObject(message.params)) {
        throw protocolError(-32602, "tools/call params are required");
      }
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
      send(errorResponse(undefined, -32700, `Parse error: ${error.message}`));
    }
  }
});
