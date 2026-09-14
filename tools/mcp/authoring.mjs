#!/usr/bin/env node

import { existsSync } from "node:fs";
import { dirname, isAbsolute, resolve } from "node:path";
import { spawn } from "node:child_process";

const GRAPH_KINDS = ["behavior", "event", "library", "material", "shape", "shader-library"];
const SHADER_KINDS = ["material", "shape", "shader-library"];
const MAX_NODE_SEARCH_LIMIT = 50;
const MAX_GRAPH_OPERATIONS = 128;
const MAX_PREVIEW_SIZE = 512;
const activeRenders = new Map();

function invalid(message) {
  const error = new Error(message);
  error.rpcCode = -32602;
  throw error;
}

function object(properties, required = []) {
  return {
    type: "object",
    properties,
    required,
    additionalProperties: false,
  };
}

const assetSelectorProperties = {
  path: {
    type: "string",
    maxLength: 512,
    description: "Content-relative asset path, alternatively use kind and name.",
  },
  kind: {
    type: "string",
    enum: GRAPH_KINDS,
    description: "Graph asset family.",
  },
  name: {
    type: "string",
    maxLength: 256,
    description: "Asset name without the .json suffix.",
  },
};

const operationProperties = {
  op: {
    type: "string",
    enum: [
      "add_node",
      "set_input",
      "set_node",
      "connect",
      "disconnect",
      "remove_node",
      "set_ramp",
      "set_bounds",
      "add_graph",
      "add_pin",
      "add_variable",
      "set_variable",
      "add_variable_node",
      "add_self_node",
    ],
    description: "Semantic graph operation.",
  },
  ref: {
    type: "string",
    maxLength: 64,
    description: "Request-local reference for a new node, graph, or variable; add_graph also exposes <ref>.entry and <ref>.return node aliases.",
  },
  node: {
    description: "Node ID or reference returned by add_node.",
  },
  type: {
    description: "Node or variable type name or numeric pin type.",
  },
  name: {
    type: "string",
    maxLength: 128,
    description: "Node, pin, or event name.",
  },
  pin: {
    description: "Pin name or zero-based pin index.",
  },
  value: {
    description: "JSON default value for set_input or [half_width, half_height] bounds for set_bounds.",
  },
  inputs: {
    type: "object",
    description: "Input pin defaults keyed by pin name.",
  },
  reference_id: {
    type: "integer",
    minimum: 0,
    description: "Stable declaration reference ID.",
  },
  reference_name: {
    type: "string",
    maxLength: 128,
    description: "Declaration reference name.",
  },
  reference_asset: {
    type: "string",
    maxLength: 512,
    description: "Referenced asset name for a call or declaration node.",
  },
  from: {
    description: "Source node ID or reference.",
  },
  from_pin: {
    description: "Source pin name or zero-based pin index.",
  },
  to: {
    description: "Destination node ID or reference.",
  },
  to_pin: {
    description: "Destination pin name or zero-based pin index.",
  },
  kind: {
    type: "string",
    maxLength: 32,
    description: "Connection, graph, or variable-node kind.",
  },
  order: {
    type: "integer",
    minimum: 0,
    maximum: 65535,
    description: "Execution edge order.",
  },
  side: {
    type: "string",
    enum: ["input", "output"],
    description: "Pin side for add_pin.",
  },
  disabled: {
    type: "boolean",
    description: "Disable the node while preserving it in the graph.",
  },
  inline_source: {
    type: "boolean",
    description: "Allow a function node to inline its source graph.",
  },
  event_name: {
    type: "string",
    maxLength: 128,
    description: "Event node name.",
  },
  variable: {
    description: "Variable ID, name, or request-local reference.",
  },
  type_name: {
    type: "string",
    maxLength: 128,
    description: "Struct type name when type is struct.",
  },
  container: {
    description: "Variable or pin container name or numeric enum.",
  },
  default: {
    description: "Variable default value.",
  },
  category: {
    type: "string",
    maxLength: 128,
    description: "Variable category.",
  },
  pure: {
    type: "boolean",
    description: "Whether an added function graph is pure.",
  },
  stops: {
    type: "array",
    minItems: 2,
    maxItems: 16,
    items: object({
      position: { type: "number" },
      color: {
        oneOf: [
          {
            type: "array",
            minItems: 4,
            maxItems: 4,
            items: { type: "number" },
          },
          object({
            x: { type: "number" },
            y: { type: "number" },
            z: { type: "number" },
            w: { type: "number" },
          }, ["x", "y", "z", "w"]),
        ],
      },
    }, ["position", "color"]),
    description: "Ordered color-ramp stops for set_ramp.",
  },
};

const operationSchema = {
  ...object(operationProperties, ["op"]),
};

const searchNodesDefinition = {
  name: "search_nodes",
  operation: "search_nodes",
  description: "Find graph nodes by name, category, keyword, or domain without loading the full registry.",
  inputSchema: object({
    query: {
      type: "string",
      maxLength: 256,
      description: "Case-insensitive text matched against node names, categories, and keywords.",
    },
    domain: {
      type: "string",
      enum: ["behavior", "shader"],
      description: "Restrict results to general behavior nodes or shader nodes.",
    },
    limit: {
      type: "integer",
      minimum: 1,
      maximum: MAX_NODE_SEARCH_LIMIT,
      description: "Maximum number of nodes to return; defaults to 10.",
    },
    offset: {
      type: "integer",
      minimum: 0,
      description: "Zero-based result offset for pagination.",
    },
    include_pins: {
      type: "boolean",
      description: "Include input and output pin descriptors; defaults to true.",
    },
  }),
  outputSchema: { type: "object", additionalProperties: true },
};

const editGraphDefinition = {
  name: "edit_graph",
  operation: "edit_graph",
  description: "Apply a bounded semantic graph edit atomically, compile it, and optionally publish it with a content hash check.",
  inputSchema: object({
    ...assetSelectorProperties,
    expected_hash: {
      type: "string",
      maxLength: 16,
      description: "Current 16-character content hash. Required when editing an existing asset.",
    },
    create: {
      type: "boolean",
      description: "Create the named asset when it does not exist; omit expected_hash for a new asset.",
    },
    dry_run: {
      type: "boolean",
      description: "Validate and compile the proposed edit without publishing it.",
    },
    graph_id: {
      type: "integer",
      minimum: 0,
      description: "Stable graph ID to edit.",
    },
    graph_name: {
      type: "string",
      maxLength: 128,
      description: "Graph name to edit.",
    },
    operations: {
      type: "array",
      maxItems: MAX_GRAPH_OPERATIONS,
      items: operationSchema,
      description: "Ordered semantic operations. Node references can be used after add_node.",
    },
  }, ["operations"]),
  outputSchema: { type: "object", additionalProperties: true },
};

const shaderAssetSelectorProperties = {
  ...assetSelectorProperties,
  kind: {
    type: "string",
    enum: SHADER_KINDS,
    description: "Shader asset family.",
  },
};

const previewShaderDefinition = {
  name: "preview_shader",
  operation: "preview_shader",
  description: "Compile a material or shape graph and return a bounded rendered preview image with optional generated source.",
  inputSchema: object({
    ...shaderAssetSelectorProperties,
    width: {
      type: "integer",
      minimum: 16,
      maximum: MAX_PREVIEW_SIZE,
      description: "Preview width in pixels; defaults to 256.",
    },
    height: {
      type: "integer",
      minimum: 16,
      maximum: MAX_PREVIEW_SIZE,
      description: "Preview height in pixels; defaults to 256.",
    },
    time: {
      type: "number",
      minimum: -3600,
      maximum: 3600,
      description: "Shader time uniform in seconds; defaults to zero.",
    },
    include_source: {
      type: "boolean",
      description: "Include generated shader source alongside the image.",
    },
  }),
  outputSchema: { type: "object", additionalProperties: true },
};

const definitions = [searchNodesDefinition, editGraphDefinition, previewShaderDefinition];
const definitionByName = new Map(definitions.map((definition) => [definition.name, definition]));

export function authoringToolDefinitions() {
  return definitions;
}

export function isAuthoringTool(name) {
  return definitionByName.has(name);
}

function hasAssetSelector(args) {
  return typeof args?.path === "string" ||
    (typeof args?.kind === "string" && typeof args?.name === "string");
}

export function validateAuthoringArguments(name, args = {}) {
  if (name === "edit_graph") {
    if (!hasAssetSelector(args)) {
      invalid("arguments requires path or both kind and name");
    }
    const create = args.create === true;
    if (create && args.expected_hash !== undefined) {
      invalid("arguments.expected_hash must be omitted when create is true");
    }
    if (!create && !/^[0-9a-f]{16}$/.test(args.expected_hash || "")) {
      invalid("arguments.expected_hash must be the current 16-character lowercase content hash");
    }
    return;
  }
  if (name === "preview_shader" && !hasAssetSelector(args)) {
    invalid("arguments requires path or both kind and name");
  }
}

function renderControllerPath(root) {
  const configured = process.env.CHIBI_RENDER_CTL;
  const controller = process.env.CHIBI_CTL;
  const candidates = [
    configured,
    controller && (isAbsolute(controller) || controller.includes("/")
      ? resolve(dirname(controller), "chibi_render_ctl")
      : undefined),
    resolve(root, "build", "chibi_render_ctl"),
    resolve(root, "build-release", "chibi_render_ctl"),
  ].filter(Boolean);
  return candidates.find((candidate) => existsSync(candidate)) || "chibi_render_ctl";
}

function renderTimeout() {
  const value = Number(process.env.CHIBI_RENDER_CTL_TIMEOUT_MS || process.env.CHIBI_CTL_TIMEOUT_MS || 30000);
  return Number.isSafeInteger(value) && value > 0 ? value : 30000;
}

function renderResponseLimit() {
  const value = Number(process.env.CHIBI_RENDER_CTL_MAX_RESPONSE_BYTES || process.env.CHIBI_CTL_MAX_RESPONSE_BYTES || 32 * 1024 * 1024);
  return Number.isSafeInteger(value) && value > 0 ? value : 32 * 1024 * 1024;
}

function failedRender(operation, code, message) {
  return {
    contract: 1,
    operation,
    ok: false,
    error: { code, message },
  };
}

export function renderRequest(root, request, requestId) {
  const executable = renderControllerPath(root);
  const timeout = renderTimeout();
  const maxBytes = renderResponseLimit();
  return new Promise((resolveRequest) => {
    let settled = false;
    let buffer = "";
    let stderr = "";
    let timer = null;
    const child = spawn(executable, ["--project-root", root], {
      cwd: root,
      stdio: ["pipe", "pipe", "pipe"],
    });
    const finish = (value) => {
      if (settled) {
        return;
      }
      settled = true;
      if (timer) {
        clearTimeout(timer);
      }
      if (requestId !== undefined) {
        activeRenders.delete(String(requestId));
      }
      resolveRequest(value);
    };
    timer = setTimeout(() => {
      finish(failedRender(request.operation || "preview_shader", "timeout", "shader preview timed out"));
      child.kill("SIGKILL");
    }, timeout);
    if (requestId !== undefined) {
      activeRenders.set(String(requestId), () => {
        finish(failedRender(request.operation || "preview_shader", "cancelled", "shader preview was cancelled"));
        child.kill("SIGKILL");
      });
    }
    child.stdout.setEncoding("utf8");
    child.stderr.setEncoding("utf8");
    child.stdout.on("data", (chunk) => {
      buffer += chunk;
      if (Buffer.byteLength(buffer, "utf8") > maxBytes) {
        finish(failedRender(request.operation || "preview_shader", "response_too_large", "shader preview response exceeded the configured size limit"));
        child.kill("SIGKILL");
      }
    });
    child.stderr.on("data", (chunk) => {
      stderr = `${stderr}${chunk}`.slice(-4096);
    });
    child.on("error", (error) => {
      finish(failedRender(request.operation || "preview_shader", "process_error", `chibi_render_ctl failed: ${error.message}`));
    });
    child.on("close", (code, signal) => {
      if (settled) {
        return;
      }
      const line = buffer.trim().split("\n").filter(Boolean).at(-1);
      if (line) {
        try {
          finish(JSON.parse(line));
          return;
        } catch (error) {
          finish(failedRender(request.operation || "preview_shader", "invalid_response", `chibi_render_ctl returned invalid JSON: ${error.message}`));
          return;
        }
      }
      const detail = signal ? `terminated by ${signal}` : `exited with status ${code}`;
      finish(failedRender(
        request.operation || "preview_shader",
        "process_error",
        `chibi_render_ctl ${detail}${stderr ? `: ${stderr.trim()}` : ""}`,
      ));
    });
    child.stdin.end(`${JSON.stringify(request)}\n`);
  });
}

export function cancelAuthoringRequest(requestId) {
  const cancel = activeRenders.get(String(requestId));
  if (!cancel) {
    return false;
  }
  cancel();
  return true;
}

export async function authoringToolCall(name, args, context) {
  if (name === "search_nodes" || name === "edit_graph") {
    return context.nativeRequest(name, args || {});
  }
  if (name === "preview_shader") {
    return context.renderRequest(
      context.root,
      { ...(args || {}), contract: 1, operation: name },
      context.requestId,
    );
  }
  return undefined;
}

function sanitizePreview(response) {
  const result = response?.result;
  const preview = result?.preview;
  if (!result || !preview || typeof preview !== "object") {
    return response;
  }
  const sanitizedPreview = { ...preview };
  delete sanitizedPreview.data;
  return {
    ...response,
    result: {
      ...result,
      preview: sanitizedPreview,
    },
  };
}

function previewSummary(response) {
  const preview = response?.result?.preview;
  const summary = {
    ok: response?.ok === true,
    operation: "preview_shader",
  };
  if (preview && typeof preview === "object") {
    for (const key of ["mime_type", "width", "height"]) {
      if (preview[key] !== undefined) {
        summary[key] = preview[key];
      }
    }
  }
  if (response?.result?.source !== undefined) {
    summary.source_included = true;
  }
  return summary;
}

export function authoringToolResult(response) {
  const preview = response?.result?.preview;
  if (response?.operation !== "preview_shader" && (!preview || typeof preview !== "object")) {
    return null;
  }
  const data = preview?.data;
  if (typeof data !== "string" || !data) {
    return null;
  }
  const mimeType = typeof preview.mime_type === "string" && preview.mime_type
    ? preview.mime_type
    : "image/png";
  return {
    content: [
      { type: "image", data, mimeType },
      { type: "text", text: JSON.stringify(previewSummary(response)) },
    ],
    structuredContent: sanitizePreview(response),
    isError: response.ok !== true,
  };
}
