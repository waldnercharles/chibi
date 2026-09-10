#!/usr/bin/env node

import { existsSync } from "node:fs";
import { dirname, isAbsolute, resolve } from "node:path";
import { spawn } from "node:child_process";
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

class ControllerSession {
  constructor() {
    this.child = spawn(
      controllerPath(),
      ["--project-root", root],
      { cwd: root, stdio: ["pipe", "pipe", "pipe"] },
    );
    this.child.stdout.setEncoding("utf8");
    this.child.stderr.setEncoding("utf8");
    this.buffer = "";
    this.stderr = "";
    this.queue = [];
    this.pending = null;
    this.timer = null;
    this.closed = false;
    this.child.stdout.on("data", (chunk) => this.receive(chunk));
    this.child.stderr.on("data", (chunk) => {
      this.stderr = `${this.stderr}${chunk}`.slice(-4096);
    });
    this.child.on("error", (error) => this.fail(error));
    this.child.on("exit", (code, signal) => {
      if (!this.closed) {
        const detail = signal ? `terminated by ${signal}` : `exited with status ${code}`;
        this.fail(new Error(`blueprints_ctl ${detail}${this.stderr ? `: ${this.stderr.trim()}` : ""}`));
      }
    });
  }

  request(payload, timeout) {
    return new Promise((resolveRequest, rejectRequest) => {
      this.queue.push({ payload, timeout, resolveRequest, rejectRequest });
      this.pump();
    });
  }

  pump() {
    if (this.closed || this.pending || !this.queue.length) {
      return;
    }
    this.pending = this.queue.shift();
    this.timer = setTimeout(() => {
      const error = protocolError(-32001, "blueprints_ctl request timed out");
      this.fail(error);
    }, this.pending.timeout);
    const line = `${JSON.stringify(this.pending.payload)}\n`;
    this.child.stdin.write(line, (error) => {
      if (error) {
        this.fail(error);
      }
    });
  }

  receive(chunk) {
    this.buffer += chunk;
    let newline;
    while ((newline = this.buffer.indexOf("\n")) >= 0) {
      const line = this.buffer.slice(0, newline).trim();
      this.buffer = this.buffer.slice(newline + 1);
      if (!line) {
        continue;
      }
      if (!this.pending) {
        this.fail(new Error("blueprints_ctl returned an unsolicited response"));
        return;
      }
      const request = this.pending;
      this.pending = null;
      clearTimeout(this.timer);
      this.timer = null;
      try {
        request.resolveRequest(JSON.parse(line));
      } catch (error) {
        request.rejectRequest(protocolError(-32603, `blueprints_ctl returned invalid JSON: ${error.message}`));
        this.fail(new Error("blueprints_ctl returned invalid JSON"));
        return;
      }
      this.pump();
    }
  }

  fail(error) {
    if (this.closed) {
      return;
    }
    this.closed = true;
    clearTimeout(this.timer);
    this.timer = null;
    const failure = error.rpcCode
      ? error
      : protocolError(-32603, `blueprints_ctl failed: ${error.message}`);
    if (this.pending) {
      this.pending.rejectRequest(failure);
      this.pending = null;
    }
    for (const request of this.queue) {
      request.rejectRequest(failure);
    }
    this.queue = [];
    if (this.child.exitCode === null && !this.child.killed) {
      this.child.kill();
    }
    if (controllerSession === this) {
      controllerSession = null;
    }
  }

  close() {
    if (this.closed) {
      return;
    }
    this.closed = true;
    clearTimeout(this.timer);
    this.timer = null;
    const error = protocolError(-32603, "blueprints_ctl session closed");
    if (this.pending) {
      this.pending.rejectRequest(error);
      this.pending = null;
    }
    for (const request of this.queue) {
      request.rejectRequest(error);
    }
    this.queue = [];
    if (this.child.exitCode === null && !this.child.killed) {
      this.child.kill();
    }
  }
}

let controllerSession;

function rawToolDefinitions() {
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
      operation: "status",
      description: "Return project paths, asset counts, and reflected registry counts.",
      inputSchema: noArguments,
      outputSchema,
    },
    {
      name: "list_assets",
      operation: "list_assets",
      description: "List authored JSON assets with stable paths, sizes, and content hashes.",
      inputSchema: noArguments,
      outputSchema,
    },
    {
      name: "catalog",
      operation: "catalog",
      description: "Return the semantic asset catalog, dependency edges, diagnostics, and reflected registry metadata.",
      inputSchema: noArguments,
      outputSchema,
    },
    {
      name: "search_assets",
      operation: "search_assets",
      description: "Search the semantic asset catalog by kind, name, path, or description.",
      inputSchema: searchArguments,
      outputSchema,
    },
    {
      name: "asset_dependencies",
      operation: "asset_dependencies",
      description: "Inspect direct or transitive incoming and outgoing asset dependencies.",
      inputSchema: dependencyArguments,
      outputSchema,
    },
    {
      name: "inspect_graph",
      operation: "inspect_graph",
      description: "Return normalized graph nodes, pins, edges, calls, effects, and validation diagnostics.",
      inputSchema: graphArguments,
      outputSchema,
    },
    {
      name: "read_asset",
      operation: "read_asset",
      description: "Read one authored asset as parsed JSON.",
      inputSchema: assetArguments,
      outputSchema,
    },
    {
      name: "validate_asset",
      operation: "validate_asset",
      description: "Load and validate one asset through the engine's native loader/compiler.",
      inputSchema: assetArguments,
      outputSchema,
    },
    {
      name: "describe_registry",
      operation: "describe_registry",
      description: "Describe reflected graph nodes, pins, types, services, operators, and components.",
      inputSchema: noArguments,
      outputSchema,
    },
    {
      name: "describe_contract",
      operation: "describe_contract",
      description: "Describe native operations, response versions, errors, safety annotations, and reflection counts.",
      inputSchema: noArguments,
      outputSchema,
    },
  ];
}

async function nativeRequest(operation, args = {}) {
  const timeoutValue = Number(process.env.CBLUEPRINTS_CTL_TIMEOUT_MS || 30000);
  const timeout = Number.isSafeInteger(timeoutValue) && timeoutValue > 0 ? timeoutValue : 30000;
  if (!controllerSession || controllerSession.closed) {
    controllerSession = new ControllerSession();
  }
  return controllerSession.request(
    { contract: 1, operation, ...args },
    timeout,
  );
}

let contractMetadataCache;

function contractMetadata() {
  if (contractMetadataCache !== undefined) {
    return contractMetadataCache;
  }
  contractMetadataCache = nativeRequest("describe_contract")
    .then((response) => response.ok === true ? response.result : null)
    .catch(() => null);
  return contractMetadataCache;
}

function toolDefinition(name) {
  return rawToolDefinitions().find((candidate) => candidate.name === name);
}

async function toolDefinitions() {
  const contract = await contractMetadata();
  return rawToolDefinitions().map(({ operation, ...definition }) => {
    const metadata = contract?.operations?.find((candidate) => candidate.name === operation);
    if (!metadata) {
      return definition;
    }
    return {
      ...definition,
      description: metadata.summary,
      annotations: {
        readOnlyHint: metadata.read_only,
        idempotentHint: metadata.idempotent,
        destructiveHint: metadata.destructive,
      },
    };
  });
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
  const tool = toolDefinition(name);
  if (!tool) {
    throw protocolError(-32602, "tools/call requires a known tool name");
  }
  validateSchema(args === undefined ? {} : args, tool.inputSchema, "arguments");
}

async function toolCall(name, args) {
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
    case "describe_contract": return nativeRequest("describe_contract");
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

async function resourceRead(uri) {
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
      response = await nativeRequest("status");
  } else if (uri === "blueprints://catalog") {
    response = await nativeRequest("catalog");
  } else {
    const graphMatch = /^blueprints:\/\/asset\/([^/]+)\/([^/]+)\/graph\/([^/]+)$/.exec(uri);
    if (graphMatch) {
      const graphId = Number(decode(graphMatch[3]));
      if (!Number.isSafeInteger(graphId) || graphId < 0) {
        throw protocolError(-32602, `invalid graph resource: ${uri}`);
      }
      response = await nativeRequest("inspect_graph", {
        kind: decode(graphMatch[1]),
        name: decode(graphMatch[2]),
        graph_id: graphId,
      });
    }
    const dependencyMatch = /^blueprints:\/\/asset\/([^/]+)\/([^/]+)\/dependencies$/.exec(uri);
    if (response === undefined && dependencyMatch) {
      response = await nativeRequest("asset_dependencies", {
        kind: decode(dependencyMatch[1]),
        name: decode(dependencyMatch[2]),
      });
    }
    const match = /^blueprints:\/\/asset\/([^/]+)\/([^/]+)$/.exec(uri);
    if (response === undefined && !match) {
      throw protocolError(-32002, `resource not found: ${uri}`);
    }
    if (response === undefined) {
      response = await nativeRequest("read_asset", {
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

async function dispatch(message) {
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
    return reply(message, { jsonrpc: "2.0", id: message.id, result: { tools: await toolDefinitions() } });
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
      const response = await resourceRead(uri);
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
      const response = await toolCall(message.params?.name, message.params?.arguments);
      return reply(message, { jsonrpc: "2.0", id: message.id, result: toolResult(response) });
    } catch (error) {
      return reply(message, errorResponse(message.id, error.rpcCode || -32603, error.message));
    }
  }
  return reply(message, errorResponse(message.id, -32601, `Method not found: ${message.method}`));
}

let pending = "";
let activeDispatches = 0;
let inputEnded = false;

function maybeCloseController() {
  if (inputEnded && activeDispatches === 0) {
    controllerSession?.close();
  }
}

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
      activeDispatches++;
      dispatch(message)
        .then((response) => {
          if (response) send(response);
        })
        .catch((error) => {
          if (!isNotification(message)) {
            send(errorResponse(message?.id, error.rpcCode || -32603, error.message));
          }
        })
        .finally(() => {
          activeDispatches--;
          maybeCloseController();
        });
    } catch (error) {
      send(errorResponse(undefined, -32700, `Parse error: ${error.message}`));
    }
  }
});
process.stdin.on("end", () => {
  inputEnded = true;
  if (pending.trim()) {
    send(errorResponse(undefined, -32700, "Parse error: incomplete JSON frame"));
    pending = "";
  }
  maybeCloseController();
});
process.on("exit", () => controllerSession?.close());
