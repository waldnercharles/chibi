#!/usr/bin/env node

import { existsSync } from "node:fs";
import { dirname, isAbsolute, resolve } from "node:path";
import { spawn } from "node:child_process";
import { getToolCatalogEntry } from "./tool-catalog.mjs";

const GRAPH_KINDS = ["behavior", "event", "library", "material", "shape", "shader-library"];
const SHADER_KINDS = ["material", "shape", "shader-library"];
const MAX_NODE_SEARCH_LIMIT = 50;
const MAX_GRAPH_OPERATIONS = 128;
const MAX_PROTOTYPE_OPERATIONS = 128;
const MAX_SCENE_OPERATIONS = 128;
const MAX_SCENE_BEHAVIORS = 64;
const MAX_PREVIEW_SIZE = 512;
const MAX_PREVIEW_SAMPLE_SIDE = 16;
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
      "set_output",
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
      "select_graph",
      "rename_graph",
      "remove_graph",
      "remove_variable",
      "set_pin",
      "remove_pin",
      "split_pin",
      "recombine_pin",
      "expose_fields",
      "hide_fields",
      "bind_input",
      "clear_binding",
      "create_local",
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
  graph: {
    oneOf: [{ type: "integer", minimum: 0 }, { type: "string", maxLength: 128 }],
    description: "Graph ID, unique name, or request-local ref for select_graph, rename_graph, or remove_graph.",
  },
  component: {
    type: "string",
    maxLength: 256,
    description: "Stable reflected component key for bind_input with kind component.",
  },
  field: {
    type: "string",
    maxLength: 256,
    description: "Optional reflected field for a component binding; omission reads the whole component.",
  },
  type: {
    description: "Node or variable type name or numeric pin type; for connect, an optional explicit scalar or named struct type resolves shader wildcard pins.",
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
    description: "JSON value for set_input/set_output or [half_width, half_height] bounds for set_bounds.",
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
    description: "Pin side for pin operations; defaults to input except add_pin, which requires it.",
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
  name: "searchNodes",
  operation: "search_nodes",
  description: "Find graph nodes by name, category, keyword, domain, or typed pin signature without loading the full registry.",
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
    pin_type: {
      type: "string",
      maxLength: 128,
      description: "Require a matching pin type or struct type name, such as float or ShaderVec3.",
    },
    pin_container: {
      type: "string",
      enum: ["single", "array", "set", "map"],
      description: "Require a matching pin container.",
    },
    pin_kind: {
      type: "string",
      enum: ["data", "exec"],
      description: "Require a matching data or execution pin.",
    },
    pure: {
      type: "boolean",
      description: "Require pure or impure node descriptors.",
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

function graphDefinition(name, kinds, kindDescription) {
  return {
    name,
    operation: "edit_graph",
    description: "Apply a bounded semantic graph edit atomically, compile it, and optionally publish it with a content hash check.",
    inputSchema: object({
      ...assetSelectorProperties,
      kind: {
        type: "string",
        enum: kinds,
        description: kindDescription,
      },
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
      description: {
        type: "string",
        maxLength: 2048,
        description: "Top-level asset description; an empty string clears it.",
      },
      owner_type: {
        type: "string",
        minLength: 1,
        maxLength: 256,
        description: "Optional owner handle type for behavior and library assets.",
      },
      graph_description: {
        type: "string",
        maxLength: 2048,
        description: "Description for the selected graph; an empty string clears it.",
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
}

const buildShaderDefinition = graphDefinition(
  "buildShader",
  SHADER_KINDS,
  "Shader asset family: material, shape, or shader-library.",
);
const buildBehaviorDefinition = graphDefinition(
  "buildBehavior",
  ["behavior", "event", "library"],
  "Behavior asset family: behavior, event, or behavior library.",
);

const prototypeAssetSelectorProperties = {
  ...assetSelectorProperties,
  kind: {
    type: "string",
    enum: ["prototype"],
    description: "Prototype asset family.",
  },
};

const prototypeOperationSchema = object({
  op: {
    type: "string",
    enum: ["add_component", "remove_component", "set_field", "add_behavior", "remove_behavior"],
    description: "Semantic prototype operation.",
  },
  component: {
    type: "string",
    maxLength: 256,
    description: "Reflected component name or stable component key.",
  },
  field: {
    type: "string",
    maxLength: 256,
    description: "Reflected field name within component.",
  },
  value: {
    description: "JSON field value converted by the native reflected type.",
  },
  fields: {
    type: "object",
    description: "Optional initial field values for add_component; omitted fields use native defaults.",
  },
  behavior: {
    type: "string",
    maxLength: 512,
    description: "Behavior asset name to attach or detach.",
  },
}, ["op"]);

const editPrototypeDefinition = {
  name: "buildPrototype",
  operation: "edit_prototype",
  description: "Create or edit a prototype with semantic component, field, and behavior operations; native defaults and serialization are applied automatically.",
  inputSchema: object({
    ...prototypeAssetSelectorProperties,
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
      description: "Validate and serialize the proposed edit without publishing it.",
    },
    operations: {
      type: "array",
      maxItems: MAX_PROTOTYPE_OPERATIONS,
      items: prototypeOperationSchema,
      description: "Ordered semantic component, field, and behavior operations.",
    },
  }, ["operations"]),
  outputSchema: { type: "object", additionalProperties: true },
};

const sceneAssetSelectorProperties = {
  ...assetSelectorProperties,
  kind: {
    type: "string",
    enum: ["scene"],
    description: "Scene asset family.",
  },
};

const sceneOperationSchema = object({
  op: {
    type: "string",
    enum: [
      "set_presentation",
      "add_entity",
      "remove_entity",
      "set_entity_prototype",
      "set_entity_components",
      "set_entity_retained_components",
      "set_entity_behaviors",
      "add_behavior",
      "remove_behavior",
    ],
    description: "Semantic scene operation.",
  },
  name: {
    type: "string",
    maxLength: 512,
    description: "Entity name for add_entity.",
  },
  entity: {
    type: "string",
    maxLength: 512,
    description: "Existing entity name.",
  },
  prototype: {
    type: "string",
    maxLength: 512,
    description: "Prototype asset name.",
  },
  behavior: {
    type: "string",
    maxLength: 512,
    description: "Behavior asset name to attach or detach.",
  },
  behaviors: {
    type: "array",
    maxItems: MAX_SCENE_BEHAVIORS,
    items: { type: "string", maxLength: 512 },
    description: "Ordered behavior asset names attached to an entity.",
  },
  components: {
    type: "object",
    description: "Component field overrides keyed by reflected component name or stable key.",
  },
  retained_components: {
    type: "object",
    description: "Editor retained component values keyed by reflected component name or stable key.",
  },
  presentation: {
    type: "string",
    enum: ["2d", "3d"],
    description: "Scene presentation mode.",
  },
}, ["op"]);

const editSceneDefinition = {
  name: "buildScene",
  operation: "edit_scene",
  description: "Create or edit a scene with semantic entity composition, component overrides, and behavior attachments; entity and node placement is automatic.",
  inputSchema: object({
    ...sceneAssetSelectorProperties,
    expected_hash: {
      type: "string",
      maxLength: 16,
      description: "Current 16-character content hash. Required when editing an existing asset.",
    },
    create: {
      type: "boolean",
      description: "Create the named scene when it does not exist; omit expected_hash for a new scene.",
    },
    dry_run: {
      type: "boolean",
      description: "Validate and serialize the proposed scene edit without publishing it.",
    },
    operations: {
      type: "array",
      maxItems: MAX_SCENE_OPERATIONS,
      items: sceneOperationSchema,
      description: "Ordered scene operations addressed by entity names. World positions and UI layout are authored component fields; graph placement is automatic.",
    },
  }, ["operations"]),
  outputSchema: { type: "object", additionalProperties: true },
};

const optimizeGraphDefinition = {
  name: "optimizeGraph",
  operation: "optimize_graph",
  description: "Analyze or apply proven graph readability improvements, preserving debug state and reporting every skipped opportunity.",
  inputSchema: object({
    ...assetSelectorProperties,
    expected_hash: {
      type: "string",
      maxLength: 16,
      description: "Current 16-character content hash. Required when applying an optimization; optional for a dry run.",
    },
    dry_run: {
      type: "boolean",
      default: true,
      description: "Return the proposed optimized graph without publishing it; defaults to true.",
    },
    graph_id: {
      type: "integer",
      minimum: 0,
      description: "Stable graph ID to optimize.",
    },
    graph_name: {
      type: "string",
      maxLength: 128,
      description: "Graph name to optimize.",
    },
    fanout_threshold: {
      type: "integer",
      minimum: 2,
      maximum: 64,
      description: "Minimum compatible fanout for creating a local alias; defaults to 3.",
    },
    preserve_debug: {
      type: "boolean",
      description: "Preserve watched, disabled, and breakpoint graph state; defaults to true.",
    },
    local_aliases: {
      type: "boolean",
      description: "Allow safe high-fanout values to become local aliases; defaults to true.",
    },
  }),
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

const previewSceneDefinition = {
  name: "previewScene", operation: "preview_scene",
  description: "Render a running scene or prototype using the game renderer, authored camera, fog and player UI. Returns a PNG. Inputs and saved state use the scenario contract; storage stays isolated.",
  inputSchema: { type: "object", additionalProperties: false, required: ["kind", "name"], properties: {
    kind: { type: "string", enum: ["scene", "prototype"] }, name: { type: "string", minLength: 1, maxLength: 512 },
    width: { type: "integer", minimum: 64, maximum: 1024, default: 640 },
    height: { type: "integer", minimum: 64, maximum: 1024, default: 360 },
    frames: { type: "integer", minimum: 0, maximum: 1800, default: 1 },
  } },
  outputSchema: { type: "object", additionalProperties: true },
};

const previewShaderDefinition = {
  name: "previewShader",
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
    parameters: {
      type: "object",
      description: "Optional shader parameter overrides keyed by authored Shader Parameter name. Use a finite number or boolean for scalar parameters and an exact-width numeric array for vector parameters.",
      additionalProperties: true,
    },
    camera: object({
      center: {
        type: "array",
        minItems: 2,
        maxItems: 2,
        items: { type: "number" },
        description: "Preview center in authored canvas coordinates [x, y].",
      },
      span: {
        type: "array",
        minItems: 2,
        maxItems: 2,
        items: { type: "number" },
        description: "Preview width and height in authored canvas units [width, height].",
      },
    }, ["center", "span"]),
    sample_grid: object({
      columns: {
        type: "integer",
        minimum: 1,
        maximum: MAX_PREVIEW_SAMPLE_SIDE,
        description: "Number of CPU samples across the UV rectangle; defaults to 5.",
      },
      rows: {
        type: "integer",
        minimum: 1,
        maximum: MAX_PREVIEW_SAMPLE_SIDE,
        description: "Number of CPU samples down the UV rectangle; defaults to 5.",
      },
      uv_min: {
        type: "array",
        minItems: 2,
        maxItems: 2,
        items: { type: "number" },
        description: "Lower UV coordinate [u, v]; defaults to [0, 0].",
      },
      uv_max: {
        type: "array",
        minItems: 2,
        maxItems: 2,
        items: { type: "number" },
        description: "Upper UV coordinate [u, v]; defaults to [1, 1].",
      },
    }),
    include_source: {
      type: "boolean",
      description: "Include generated shader source alongside the image.",
    },
  }),
  outputSchema: { type: "object", additionalProperties: true },
};

const definitions = [
  searchNodesDefinition,
  buildShaderDefinition,
  buildBehaviorDefinition,
  editPrototypeDefinition,
  editSceneDefinition,
  optimizeGraphDefinition,
  previewShaderDefinition,
  previewSceneDefinition,
].map((definition) => {
  const catalogEntry = getToolCatalogEntry(definition.name);
  if (!catalogEntry) {
    throw new Error("authoring definition is missing from the public catalog: " + definition.name);
  }
  return {
    ...definition,
    title: catalogEntry.title,
    description: catalogEntry.description,
  };
});
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
  if (name === "buildShader" || name === "buildBehavior") {
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
  if (name === "buildPrototype") {
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
  if (name === "buildScene") {
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
  if (name === "optimizeGraph") {
    if (!hasAssetSelector(args)) {
      invalid("arguments requires path or both kind and name");
    }
    if (args.expected_hash !== undefined && !/^[0-9a-f]{16}$/.test(args.expected_hash)) {
      invalid("arguments.expected_hash must be the current 16-character lowercase content hash");
    }
    if (args.dry_run === false && !args.expected_hash) {
      invalid("arguments.expected_hash is required when dry_run is false");
    }
    return;
  }
  if (name === "previewShader" && !hasAssetSelector(args)) {
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
  const entry = getToolCatalogEntry(name);
  if (!entry || entry.adapter !== "authoring") {
    return undefined;
  }
  if (entry.operation === "preview_shader" || entry.operation === "preview_scene") {
    return context.renderRequest(
      context.root,
      { ...(args || {}), contract: 1, operation: entry.operation },
      context.requestId,
    );
  }
  return context.nativeRequest(entry.operation, args || {});
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

function previewSummary(response, name) {
  const preview = response?.result?.preview;
  const summary = {
    ok: response?.ok === true,
    tool: name,
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
  if (Array.isArray(response?.next_steps) && response.next_steps.length) {
    summary.next_steps = response.next_steps;
  }
  return summary;
}

export function authoringToolResult(response, name) {
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
      { type: "text", text: JSON.stringify(previewSummary(response, name)) },
    ],
    structuredContent: sanitizePreview(response),
    isError: response.ok !== true,
  };
}
