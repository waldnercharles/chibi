#!/usr/bin/env node

import { existsSync, realpathSync, statSync } from "node:fs";
import { dirname, isAbsolute, resolve } from "node:path";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import {
  authoringToolCall,
  authoringToolDefinitions,
  authoringToolResult,
  cancelAuthoringRequest,
  isAuthoringTool,
  renderRequest,
  validateAuthoringArguments,
} from "./authoring.mjs";
import { parseControllerJson } from "./controller.mjs";
import { editorCommand, editorCommandDefinition } from "./editor-command.mjs";
import { getToolCatalogEntry, toolCatalog } from "./tool-catalog.mjs";

const protocolVersion = "2025-11-25";
const supportedProtocolVersions = [protocolVersion, "2025-06-18"];
const serverVersion = "0.2.0";
const serverInstructions = [
  "Use Chibi MCP as the default authoring interface for shaders, behaviors, prototypes, scenes, and authored assets.",
  "1. Search or inspect assets, nodes, pins, fields, and content hashes with searchAssets, searchNodes, inspectGraph, describeComponents, or getProjectStatus.",
  "2. Build with buildGraph, buildPrototype, or buildScene. Graph edits are typed, atomic, hash-checked for existing assets, and auto-arranged.",
  "3. Validate every published change with validateAsset. Use dry runs first when available and retry asset conflicts with the latest hash.",
  "4. Use previewShader, previewField, previewScene, runScenario, captureRuntimeSnapshot, or profileScenario for visual and runtime evidence.",
  "Tool results keep complete machine-readable data in structuredContent and include next_steps. Use the public camelCase names returned by tools/list; native operation names are implementation details.",
].join(" ");
const serverRoot = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
const configuredRoot = process.env.CHIBI_ROOT;
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

function responseLimitBytes() {
  const value = Number(process.env.CHIBI_CTL_MAX_RESPONSE_BYTES || 32 * 1024 * 1024);
  return Number.isSafeInteger(value) && value > 0 ? value : 32 * 1024 * 1024;
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
    uriTemplate: "chibi://asset/{kind}/{name}",
    name: "Chibi authored asset",
    mimeType: "application/json",
  }, {
    uriTemplate: "chibi://asset/{kind}/{name}/dependencies",
    name: "Chibi asset dependencies",
    mimeType: "application/json",
  }, {
    uriTemplate: "chibi://asset/{kind}/{name}/graph/{graph_id}",
    name: "Chibi normalized graph",
    mimeType: "application/json",
  }, {
    uriTemplate: "chibi://recording/{id}",
    name: "Chibi deterministic recording",
    mimeType: "application/json",
  }];
}

function controllerPath() {
  const candidates = [
    process.env.CHIBI_CTL,
    resolve(root, "build", "chibi_ctl"),
    resolve(root, "build-release", "chibi_ctl"),
  ].filter(Boolean);
  return candidates.find((candidate) => existsSync(candidate)) || "chibi_ctl";
}

// launch.sh sets CHIBI_CTL to the build output; build it on demand so the MCP
// handshake never waits on a native compile.
let controllerBuild = null;

function controllerReady() {
  return !process.env.CHIBI_CTL || existsSync(process.env.CHIBI_CTL);
}

function ensureController() {
  if (controllerReady()) {
    return Promise.resolve();
  }
  const controller = process.env.CHIBI_CTL;
  controllerBuild ??= new Promise((resolveBuild, rejectBuild) => {
    process.stderr.write("chibi MCP building native controllers\n");
    const child = spawn("make", ["-C", serverRoot, "build", `BUILD_DIR=${dirname(controller)}`, "BUILD_TARGET=chibi_ctl"], {
      stdio: ["ignore", process.stderr, process.stderr],
    });
    child.on("error", rejectBuild);
    child.on("exit", (code, signal) => {
      if (code === 0 && existsSync(controller)) {
        resolveBuild();
        return;
      }
      rejectBuild(new Error(signal ? `terminated by ${signal}` : `exited with status ${code}`));
    });
  }).catch((error) => {
    controllerBuild = null;
    throw protocolError(-32603, `chibi MCP could not build ${controller}: make build ${error.message}`);
  });
  return controllerBuild;
}

// Renders waiting on the build are not yet cancellable by authoring.mjs.
const rendersAwaitingBuild = new Map();

async function builtRenderRequest(renderRoot, request, requestId) {
  if (!controllerReady() && requestId !== undefined) {
    const key = String(requestId);
    const cancelled = new Promise((resolveCancel) => rendersAwaitingBuild.set(key, () => resolveCancel(true)));
    try {
      if (await Promise.race([ensureController().then(() => false), cancelled])) {
        const operation = request.operation || "preview_shader";
        return { contract: 1, operation, ok: false, error: { code: "cancelled", message: "shader preview was cancelled" } };
      }
    } finally {
      rendersAwaitingBuild.delete(key);
    }
  }
  await ensureController();
  return renderRequest(renderRoot, request, requestId);
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
  constructor(executable) {
    this.executable = executable;
    this.modified = statSync(executable).mtimeMs;
    this.child = spawn(
      executable,
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
    this.maxResponseBytes = responseLimitBytes();
    this.child.stdout.on("data", (chunk) => this.receive(chunk));
    this.child.stderr.on("data", (chunk) => {
      this.stderr = `${this.stderr}${chunk}`.slice(-4096);
    });
    this.child.on("error", (error) => this.fail(error));
    this.child.on("exit", (code, signal) => {
      if (!this.closed) {
        const detail = signal ? `terminated by ${signal}` : `exited with status ${code}`;
        this.fail(new Error(`chibi_ctl ${detail}${this.stderr ? `: ${this.stderr.trim()}` : ""}`));
      }
    });
  }

  // A rebuilt controller replaces the running one on the next request.
  stale() {
    return statSync(this.executable).mtimeMs !== this.modified;
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
      const error = protocolError(-32001, "chibi_ctl request timed out");
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
    if (Buffer.byteLength(this.buffer, "utf8") > this.maxResponseBytes) {
      this.fail(protocolError(-32003, "chibi_ctl response exceeded the configured size limit"));
      return;
    }
    let newline;
    while ((newline = this.buffer.indexOf("\n")) >= 0) {
      const line = this.buffer.slice(0, newline).trim();
      this.buffer = this.buffer.slice(newline + 1);
      if (!line) {
        continue;
      }
      if (!this.pending) {
        this.fail(new Error("chibi_ctl returned an unsolicited response"));
        return;
      }
      const request = this.pending;
      this.pending = null;
      clearTimeout(this.timer);
      this.timer = null;
      try {
        request.resolveRequest(parseControllerJson(line));
      } catch (error) {
        request.rejectRequest(protocolError(-32603, `chibi_ctl returned invalid JSON: ${error.message}`));
        this.fail(new Error("chibi_ctl returned invalid JSON"));
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
      : protocolError(-32603, `chibi_ctl failed: ${error.message}`);
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
    const error = protocolError(-32603, "chibi_ctl session closed");
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

let toolDefinitionsCache;

function rawToolDefinitions() {
  toolDefinitionsCache ||= buildToolDefinitions();
  return toolDefinitionsCache;
}

function buildToolDefinitions() {
  const noArguments = { type: "object", additionalProperties: false };
  const outputSchema = { type: "object", additionalProperties: true };
  const pageProperties = {
    limit: {
      type: "integer",
      minimum: 1,
      maximum: 256,
      description: "Maximum number of records to return.",
    },
    cursor: {
      type: "integer",
      minimum: 0,
      description: "Zero-based result offset returned by a previous page.",
    },
    summary: {
      type: "boolean",
      description: "Return compact asset records without full dependency details.",
    },
  };
  const pageArguments = {
    type: "object",
    properties: pageProperties,
    additionalProperties: false,
  };
  const assetArguments = {
    type: "object",
    properties: {
      kind: {
        type: "string",
        description: "Asset family, such as behavior, prototype, or scene.",
      },
      name: {
        type: "string",
        description: "Asset filename without the .yaml suffix.",
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
      ...pageProperties,
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
        description: "Optional graph name selector. The selected graph is returned in full; other graphs are returned as compact summaries.",
      },
      include_layout: {
        type: "boolean",
        description: "Include editor coordinates, inline hints, comment boxes, and wire routing. Omit for semantic graph data.",
      },
    },
  };
  const scenarioArguments = {
    type: "object",
    properties: {
      kind: {
        type: "string",
        enum: ["prototype", "scene"],
        description: "Target asset family; defaults to prototype.",
      },
      name: {
        type: "string",
        description: "Prototype or scene asset name without the .yaml suffix.",
      },
      frames: {
        type: "integer",
        minimum: 0,
        maximum: 108000,
        description: "Number of deterministic fixed steps to run.",
      },
      fixed_dt: {
        type: "number",
        minimum: 0.000001,
        maximum: 0.25,
        description: "Fixed timestep in seconds. The engine currently accepts 1/60.",
      },
    },
    required: ["name"],
    additionalProperties: false,
  };
  const runtimeSnapshotArguments = {
    type: "object",
    properties: {
      kind: {
        type: "string",
        enum: ["prototype", "scene"],
        description: "Target asset family; defaults to prototype.",
      },
      name: {
        type: "string",
        description: "Prototype or scene asset name without the .yaml suffix.",
      },
      frames: {
        type: "integer",
        minimum: 0,
        maximum: 108000,
        description: "Number of deterministic fixed steps to run before capturing the snapshot.",
      },
      fixed_dt: {
        type: "number",
        minimum: 0.000001,
        maximum: 0.25,
        description: "Fixed timestep in seconds. The engine currently accepts 1/60.",
      },
      entity_limit: {
        type: "integer",
        minimum: 1,
        maximum: 256,
        description: "Maximum number of ordered entities to include; defaults to 32.",
      },
      component_limit: {
        type: "integer",
        minimum: 1,
        maximum: 64,
        description: "Maximum number of components to include per entity.",
      },
      field_limit: {
        type: "integer",
        minimum: 1,
        maximum: 128,
        description: "Maximum number of reflected fields to include per component.",
      },
      behavior_limit: {
        type: "integer",
        minimum: 1,
        maximum: 64,
        description: "Maximum number of attached behaviors to include per entity.",
      },
      fields: {
        type: "array",
        maxItems: 32,
        items: { type: "string" },
        description: "Optional reflected field selectors such as GameTransform.x.",
      },
    },
    required: ["name"],
    additionalProperties: false,
  };
  const profileScenarioArguments = {
    type: "object",
    properties: {
      kind: {
        type: "string",
        enum: ["prototype", "scene"],
        description: "Target asset family; defaults to prototype.",
      },
      name: {
        type: "string",
        description: "Prototype or scene asset name without the .yaml suffix.",
      },
      frames: {
        type: "integer",
        minimum: 0,
        maximum: 1800,
        description: "Number of fixed steps to sample after warmup.",
      },
      warmup_frames: {
        type: "integer",
        minimum: 0,
        maximum: 1800,
        description: "Fixed steps to run before collecting timing and behavior samples.",
      },
      fixed_dt: {
        type: "number",
        minimum: 0.000001,
        maximum: 0.25,
        description: "Fixed timestep in seconds. The engine currently accepts 1/60.",
      },
    },
    required: ["name"],
    additionalProperties: false,
  };
  const executionStatusArguments = {
    type: "object",
    properties: {
      kind: {
        type: "string",
        enum: ["behavior", "system"],
        description: "Asset family to inspect.",
      },
      name: {
        type: "string",
        description: "Behavior or system asset name without the .yaml suffix.",
      },
    },
    required: ["kind", "name"],
    additionalProperties: false,
  };
  const normalizedVector = {
    type: "object",
    properties: {
      x: {
        type: "number",
        minimum: -1,
        maximum: 1,
        description: "Normalized horizontal input.",
      },
      y: {
        type: "number",
        minimum: -1,
        maximum: 1,
        description: "Normalized vertical input.",
      },
    },
    required: ["x", "y"],
    additionalProperties: false,
  };
  const normalizedAim = {
    type: "object",
    properties: {
      x: {
        type: "number",
        description: "Horizontal direction in [-1, 1], or world coordinate when point is true.",
      },
      y: {
        type: "number",
        description: "Vertical direction in [-1, 1], or world coordinate when point is true.",
      },
      firing: { type: "boolean" },
      point: { type: "boolean" },
    },
    required: ["x", "y"],
    additionalProperties: false,
  };
  const normalizedAction = {
    type: "object",
    properties: {
      name: {
        type: "string",
        description: "Normalized action name.",
      },
      pressed: {
        type: "boolean",
        description: "Whether the action is pressed during this frame.",
      },
    },
    required: ["name", "pressed"],
    additionalProperties: false,
  };
  const normalizedEvent = {
    type: "object",
    properties: {
      name: { type: "string" },
      target: { type: "string", description: "Scene entity name; omit to target the prototype owner or first scene entity." },
      values: {
        type: "array", maxItems: 16,
        items: {
          type: "object",
          properties: {
            type: { type: "string", enum: ["bool", "int", "uint", "float", "double", "name", "string", "entity"] },
            value: {},
          },
          required: ["type", "value"], additionalProperties: false,
        },
      },
    },
    required: ["name"],
    additionalProperties: false,
  };
  const normalizedInput = {
    type: "object",
    properties: {
      controls: { type: "array", maxItems: 41, items: { type: "integer", minimum: 1, maximum: 41 },
        description: "Held physical GameControl codes, interpreted by authored action bindings." },
      ui: { type: "array", maxItems: 32, items: { type: "object", properties: {
        target: { type: "string" },
        action: { type: "string", enum: ["activate", "change", "drop"] },
        source: { type: "string" },
        value: { type: "number" },
      }, required: ["target", "action"], additionalProperties: false },
        description: "Player UI interactions. Change requires value; drop requires a source widget name. Uses normal widget validation and behavior events." },
      movement: normalizedVector,
      aim: normalizedAim,
      actions: {
        type: "array",
        maxItems: 32,
        items: normalizedAction,
        description: "Bounded normalized action transitions for this frame.",
      },
      events: {
        type: "array",
        maxItems: 32,
        items: normalizedEvent,
        description: "Bounded normalized runtime events for this frame.",
      },
    },
    additionalProperties: false,
  };
  const normalizedInputs = {
    type: "array",
    maxItems: 1800,
    items: normalizedInput,
    description: "One normalized input sample per fixed frame.",
  };
  const savedState = { type: "object", maxProperties: 1024, additionalProperties: { type: "string" },
    description: "Initial committed save values in an isolated scenario store. Use returned saved_state for the next run." };
  const scenePreview = authoringToolDefinitions().find(tool => tool.name === "previewScene");
  scenePreview.inputSchema.properties.inputs = normalizedInputs;
  scenePreview.inputSchema.properties.saved_state = savedState;
  scenarioArguments.properties.saved_state = savedState;
  runtimeSnapshotArguments.properties.saved_state = savedState;
  const timeline = { type: "array", maxItems: 1800, items: { type: "object", additionalProperties: false,
    required: ["frame", "input"], properties: { frame: { type: "integer", minimum: 0, maximum: 107999 }, input: normalizedInputs.items } } };
  scenarioArguments.properties.frames.maximum = 108000;
  runtimeSnapshotArguments.properties.frames.maximum = 108000;
  scenePreview.inputSchema.properties.input_timeline = timeline;
  scenarioArguments.properties.input_timeline = timeline;
  runtimeSnapshotArguments.properties.input_timeline = timeline;
  // One detailed input schema per tool; chibi_ctl validates every sample.
  const frameInputs = { type: "array", maxItems: 1800, items: { type: "object" },
    description: "One input per fixed frame, shaped like input_timeline[].input." };
  scenePreview.inputSchema.properties.inputs = frameInputs;
  scenarioArguments.properties.inputs = frameInputs;
  runtimeSnapshotArguments.properties.inputs = frameInputs;
  const recording = { type: "object", description: "Recording returned by recordScenario, passed back unchanged." };
  const recordArguments = {
    type: "object",
    properties: {
      kind: {
        type: "string",
        enum: ["prototype", "scene"],
        description: "Prototype or scene target family.",
      },
      name: {
        type: "string",
        description: "Prototype asset name without the .yaml suffix.",
      },
      frames: {
        type: "integer",
        minimum: 0,
        maximum: 1800,
        description: "Number of deterministic fixed steps to record.",
      },
      fixed_dt: {
        type: "number",
        minimum: 0.000001,
        maximum: 0.25,
        description: "Fixed timestep in seconds. The engine currently accepts 1/60.",
      },
      inputs: normalizedInputs,
      saved_state: savedState,
    },
    required: ["name"],
    additionalProperties: false,
  };
  const replayArguments = {
    type: "object",
    properties: {
      recording,
      recording_path: {
        type: "string",
        description: "Safe recording artifact path returned by record_scenario.",
      },
    },
    oneOf: [
      { type: "object", required: ["recording"] },
      { type: "object", required: ["recording_path"] },
    ],
    additionalProperties: false,
  };
  return [
    {
      name: "getProjectStatus",
      operation: "status",
      description: "Return project paths, asset counts, and reflected registry counts.",
      inputSchema: noArguments,
      outputSchema,
    },
    {
      name: "listAssets",
      operation: "list_assets",
      description: "List authored YAML assets with stable paths, sizes, and content hashes.",
      inputSchema: {
        type: "object",
        properties: { limit: pageProperties.limit, cursor: pageProperties.cursor },
        additionalProperties: false,
      },
      outputSchema,
    },
    {
      name: "getAssetCatalog",
      operation: "catalog",
      description: "Return the semantic asset catalog, dependency edges, diagnostics, and reflected registry metadata.",
      inputSchema: pageArguments,
      outputSchema,
    },
    {
      name: "searchAssets",
      operation: "search_assets",
      description: "Search the semantic asset catalog by kind, name, path, or description.",
      inputSchema: searchArguments,
      outputSchema,
    },
    {
      name: "inspectAssetDependencies",
      operation: "asset_dependencies",
      description: "Inspect direct or transitive incoming and outgoing asset dependencies.",
      inputSchema: dependencyArguments,
      outputSchema,
    },
    {
      name: "inspectGraph",
      operation: "inspect_graph",
      description: "Return normalized graph nodes, pins, edges, calls, effects, and validation diagnostics.",
      inputSchema: graphArguments,
      outputSchema,
    },
    {
      name: "inspectSystem",
      operation: "inspect_system",
      description: "Inspect a typed system query, compiled graph, inferred component effects, and diagnostics.",
      inputSchema: assetArguments,
      outputSchema,
    },
    {
      name: "getExecutionStatus",
      operation: "execution_status",
      description: "Report the backend selected for a behavior or system and whether its Tick path is runnable.",
      inputSchema: executionStatusArguments,
      outputSchema,
    },
    {
      name: "profileScenario",
      operation: "profile_scenario",
      description: "Run a bounded prototype or scene scenario with warmup and return engine timing, per-behavior inclusive/exclusive samples, and scoped xalloc allocation counters.",
      inputSchema: profileScenarioArguments,
      outputSchema,
    },
    {
      name: "runScenario",
      operation: "run_scenario",
      description: "Run a bounded deterministic prototype or scene scenario and return structured runtime evidence.",
      inputSchema: scenarioArguments,
      outputSchema,
    },
    {
      name: "captureRuntimeSnapshot",
      operation: "runtime_snapshot",
      description: "Capture bounded deterministic ECS entities, reflected fields, behaviors, and timing.",
      inputSchema: runtimeSnapshotArguments,
      outputSchema,
    },
    {
      name: "recordScenario",
      operation: "record_scenario",
      description: "Record bounded normalized inputs and deterministic frame hashes for a prototype scenario.",
      inputSchema: recordArguments,
      outputSchema,
    },
    {
      name: "replayScenario",
      operation: "replay_scenario",
      description: "Replay a versioned deterministic scenario recording and report the first divergence.",
      inputSchema: replayArguments,
      outputSchema,
    },
    {
      name: "readAsset",
      operation: "read_asset",
      description: "Read one authored asset as parsed JSON.",
      inputSchema: assetArguments,
      outputSchema,
    },
    {
      name: "validateAsset",
      operation: "validate_asset",
      description: "Load and validate one asset through the engine's native loader/compiler.",
      inputSchema: assetArguments,
      outputSchema,
    },
    {
      name: "describeRegistry",
      operation: "describe_registry",
      description: "Describe reflected graph nodes, pins, types, services, operators, and components.",
      inputSchema: {
        type: "object",
        properties: {
          sections: {
            type: "array",
            maxItems: 5,
            items: { type: "string", enum: ["nodes", "types", "services", "operators", "components"] },
            description: "Sections to return; defaults to all but nodes, which searchNodes covers.",
          },
        },
        additionalProperties: false,
      },
      outputSchema,
    },
    {
      name: "describeComponents",
      operation: "describe_components",
      description: "Describe reflected prototype component fields, stable keys, and native defaults.",
      inputSchema: {
        type: "object",
        properties: {
          names: {
            type: "array",
            maxItems: 64,
            items: { type: "string" },
            description: "Optional component names or stable keys; omit to describe every component.",
          },
        },
        additionalProperties: false,
      },
      outputSchema,
    },
    {
      name: "describeContract",
      operation: "describe_contract",
      description: "Describe native operations, response versions, errors, safety annotations, and reflection counts.",
      inputSchema: noArguments,
      outputSchema,
    },
    ...authoringToolDefinitions(),
    editorCommandDefinition,
  ];
}

async function nativeRequest(operation, args = {}) {
  // Long deterministic scenarios can run for minutes; a timeout restarts the controller.
  const timeoutValue = Number(process.env.CHIBI_CTL_TIMEOUT_MS || 300000);
  const timeout = Number.isSafeInteger(timeoutValue) && timeoutValue > 0 ? timeoutValue : 300000;
  await ensureController();
  const executable = controllerPath();
  if (controllerSession && !controllerSession.closed && controllerSession.stale()) {
    controllerSession.close();
  }
  if (!controllerSession || controllerSession.closed) {
    controllerSession = new ControllerSession(executable);
  }
  const session = controllerSession;
  if (!session.rootCheck) {
    session.rootCheck = session.request({ contract: 1, operation: "status" }, timeout)
      .then((status) => {
        const resolvedRoot = status.result?.project_root;
        if (!status.ok || !resolvedRoot || realpathSync(resolvedRoot) !== realpathSync(root)) {
          session.close();
          throw protocolError(-32603, `Chibi project root mismatch: expected ${root}, got ${resolvedRoot}`);
        }
        process.stderr.write(`chibi MCP verified project root: ${resolvedRoot}\n`);
      });
  }
  await session.rootCheck;
  return session.request({ ...args, contract: 1, operation }, timeout);
}

function toolDefinition(name) {
  return rawToolDefinitions().find((candidate) => candidate.name === name);
}

function toolDefinitions() {
  const definitions = new Map(rawToolDefinitions().map((definition) => [definition.name, definition]));
  return toolCatalog.map((entry) => {
    const definition = definitions.get(entry.name);
    if (!definition) {
      throw new Error(`MCP catalog is missing definition for ${entry.name}`);
    }
    const { operation, ...schema } = definition;
    return {
      ...schema,
      name: entry.name,
      title: entry.title,
      description: entry.description,
      annotations: entry.annotations,
    };
  });
}

function validateSchema(value, schema, path) {
  if (schema.oneOf) {
    const matches = schema.oneOf.filter((candidate) => {
      try {
        validateSchema(value, candidate, path);
        return true;
      } catch {
        return false;
      }
    }).length;
    if (matches !== 1) {
      throw protocolError(-32602, `${path} must match exactly one supported form`);
    }
  }
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
  if (schema.type === "string" &&
      (typeof value !== "string" ||
       (schema.maxLength !== undefined && value.length > schema.maxLength))) {
    throw protocolError(-32602, `${path} must be a string within the supported length`);
  }
  if (schema.type === "boolean" && typeof value !== "boolean") {
    throw protocolError(-32602, `${path} must be a boolean`);
  }
  if (schema.type === "number" &&
      (typeof value !== "number" || !Number.isFinite(value) ||
       (schema.minimum !== undefined && value < schema.minimum) ||
       (schema.maximum !== undefined && value > schema.maximum))) {
    throw protocolError(-32602, `${path} must be a finite number within the supported range`);
  }
  if (schema.type === "integer" &&
      (!Number.isSafeInteger(value) ||
       value < (schema.minimum || 0) ||
       (schema.maximum !== undefined && value > schema.maximum))) {
    throw protocolError(-32602, `${path} must be a safe integer within the supported range`);
  }
  if (schema.type === "array") {
    if (!Array.isArray(value)) {
      throw protocolError(-32602, `${path} must be an array`);
    }
    if (schema.minItems !== undefined && value.length < schema.minItems) {
      throw protocolError(-32602, `${path} must contain at least ${schema.minItems} items`);
    }
    if (schema.maxItems !== undefined && value.length > schema.maxItems) {
      throw protocolError(-32602, `${path} has too many items`);
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
  if (isAuthoringTool(name)) {
    validateAuthoringArguments(name, args === undefined ? {} : args);
  }
}

async function toolCall(name, args, requestId) {
  if (typeof name !== "string") {
    throw protocolError(-32602, "tools/call requires a tool name");
  }
  validateToolArguments(name, args);
  if (name === "editorCommand") {
    return editorCommand(args);
  }
  if (isAuthoringTool(name)) {
    return authoringToolCall(name, args, { nativeRequest, renderRequest: builtRenderRequest, root, requestId });
  }
  const entry = getToolCatalogEntry(name);
  if (!entry || entry.adapter !== "native") {
    throw protocolError(-32602, `unknown tool: ${name}`);
  }
  return nativeRequest(entry.operation, args || {});
}

function defaultNextSteps(name) {
  return getToolCatalogEntry(name)?.nextSteps || [];
}

function editorCommandNextSteps(status) {
  switch (status) {
    case "pending": return ["Poll status with the returned task_id."];
    case "needs_confirmation": return ["Confirm the irreversible operations by responding with the task_id and approve, or cancel."];
    case "needs_clarification": return ["Respond with a listed choice or answer, or choose a browser session."];
    case "failed": case "unsupported": case "busy":
      return ["Read the message, inspect the current revision, and correct the request before retrying."];
    case "ready": return ["The dry run passed. Submit the operations with dry_run false and the current revision."];
    case "applied": case "cancelled": case "undone": case "resolved": return [];
    default: return ["Use this revision and the observed object IDs for the next command."];
  }
}

function responseNextSteps(name, response) {
  if (name === "editorCommand") {
    return editorCommandNextSteps(response?.status);
  }
  const code = response?.error?.code;
  if (code === "asset_not_found") {
    return ["Call searchAssets with a broader query or the expected asset kind."];
  }
  if (code === "asset_conflict") {
    return ["Call inspectGraph or readAsset for the current content hash, then retry the edit."];
  }
  if (code === "validation_failed") {
    return ["Correct the reported diagnostics, then call validateAsset again."];
  }
  if (code === "recording_not_found") {
    return ["Call recordScenario to create a recording for the current project state."];
  }
  if (code === "invalid_request" && /node|pin|reference/i.test(response?.error?.message || "")) {
    return ["Call searchNodes for the node or pin signature, then retry the graph edit."];
  }
  const result = response?.result;
  if (name === "searchAssets" && Array.isArray(result?.assets) && result.assets.length === 0) {
    return ["Broaden the search query or call getAssetCatalog for project-wide context."];
  }
  if (name === "searchNodes" && Array.isArray(result?.nodes) && result.nodes.length === 0) {
    return ["Broaden the node query or remove a restrictive pin filter."];
  }
  if ((name === "buildGraph" || name === "buildPrototype" || name === "buildScene") &&
      result?.dry_run === true) {
    return [
      "Review the dry-run diagnostics and proposed changes.",
      "Call the same build tool with dry_run false and the current expected_hash to publish.",
    ];
  }
  return defaultNextSteps(name);
}

function addPublicContractNames(name, response) {
  if (name !== "describeContract" || !isObject(response?.result) || !Array.isArray(response.result.operations)) {
    return response;
  }
  const operations = response.result.operations.map((operation) => ({
    ...operation,
    public_tools: toolCatalog
      .filter((entry) => entry.operation === operation.name)
      .map((entry) => entry.name),
  }));
  return { ...response, result: { ...response.result, operations } };
}

function withToolGuidance(name, response) {
  const publicResponse = addPublicContractNames(name, response);
  const nextSteps = responseNextSteps(name, publicResponse);
  return nextSteps.length ? { ...publicResponse, next_steps: nextSteps } : publicResponse;
}

// Inspections are catalogs and commands return task records; both use top-level
// fields rather than the offline controller's result envelope.
function editorCommandResult(response, guidedResponse) {
  const ok = response.ok ?? typeof response.revision === "string";
  const summary = { ok, tool: "editorCommand" };
  const keys = ["status", "task_id", "revision", "after_revision", "message", "error", "summary", "choices", "sessions", "objects"];
  for (const key of keys) {
    if (Object.hasOwn(response, key)) summary[key] = response[key];
  }
  if (guidedResponse.next_steps) summary.next_steps = guidedResponse.next_steps;
  return {
    content: [{ type: "text", text: boundedJson(summary, "editor task") }],
    structuredContent: { ...guidedResponse, ok },
    isError: !ok && response.status !== "needs_clarification",
  };
}

function toolResult(name, response) {
  const guidedResponse = withToolGuidance(name, response);
  if (name === "editorCommand") {
    return editorCommandResult(response, guidedResponse);
  }
  const authoringResult = authoringToolResult(guidedResponse, name);
  if (authoringResult) {
    return authoringResult;
  }
  const text = guidedResponse.ok === true
    ? boundedJson(toolSummary(name, guidedResponse), "tool summary")
    : boundedJson({
      ok: false,
      tool: name,
      error: guidedResponse.error,
      next_steps: guidedResponse.next_steps,
    }, "tool error");
  return {
    content: [{ type: "text", text }],
    structuredContent: guidedResponse,
    isError: guidedResponse.ok !== true,
  };
}

function toolSummary(name, response) {
  const summary = { ok: true, tool: name };
  const result = response.result;
  if (!isObject(result)) {
    return summary;
  }
  const scalarKeys = [
    "project_root",
    "content_root",
    "kind",
    "name",
    "path",
    "graph_id",
    "layout_included",
    "offset",
    "total",
    "count",
    "has_more",
    "next_cursor",
    "operation_count",
    "component_count",
    "behavior_count",
    "before_hash",
    "after_hash",
    "after_bytes",
    "published",
    "changed",
    "dry_run",
    "warmup_frames",
    "frames",
    "fixed_dt",
    "backend",
    "ready",
    "jit",
    "fully_jit",
    "jit_available",
    "folded_inputs",
    "removed_nodes",
    "created_aliases",
    "changed_edges",
    "world_hash",
    "live_entities",
    "valid",
    "deterministic",
    "recording_id",
    "recording_path",
    "first_divergence_frame",
  ];
  for (const key of scalarKeys) {
    if (Object.hasOwn(result, key) &&
        (typeof result[key] === "string" || typeof result[key] === "number" ||
         typeof result[key] === "boolean")) {
      summary[key] = result[key];
    }
  }
  for (const key of ["assets", "graphs", "nodes", "edges", "entities", "components", "operations", "diagnostics", "behaviors", "skipped"]) {
    if (Array.isArray(result[key])) {
      summary[`${key}_count`] = result[key].length;
    }
  }
  if (Array.isArray(response.next_steps) && response.next_steps.length) {
    summary.next_steps = response.next_steps;
  }
  return summary;
}

function boundedJson(value, label) {
  const text = JSON.stringify(value);
  if (Buffer.byteLength(text, "utf8") > responseLimitBytes()) {
    throw protocolError(-32003, `${label} exceeded the configured size limit`);
  }
  return text;
}

async function resourceRead(uri) {
  if (typeof uri !== "string" || !uri) {
    throw protocolError(-32602, "resources/read requires a URI string");
  }
  if (!uri.startsWith("chibi://")) {
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
  if (uri === "chibi://project/status") {
      response = await nativeRequest("status");
  } else if (uri === "chibi://catalog") {
    response = await nativeRequest("catalog");
  } else {
    const recordingMatch = /^chibi:\/\/recording\/([^/]+)$/.exec(uri);
    if (recordingMatch) {
      const id = decode(recordingMatch[1]);
      if (!/^[A-Za-z0-9._-]{1,128}$/.test(id)) {
        throw protocolError(-32602, `invalid recording resource: ${uri}`);
      }
      response = await nativeRequest("read_recording", { id });
    }
    const graphMatch = /^chibi:\/\/asset\/([^/]+)\/([^/]+)\/graph\/([^/]+)$/.exec(uri);
    if (response === undefined && graphMatch) {
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
    const dependencyMatch = /^chibi:\/\/asset\/([^/]+)\/([^/]+)\/dependencies$/.exec(uri);
    if (response === undefined && dependencyMatch) {
      response = await nativeRequest("asset_dependencies", {
        kind: decode(dependencyMatch[1]),
        name: decode(dependencyMatch[2]),
      });
    }
    const match = /^chibi:\/\/asset\/([^/]+)\/([^/]+)$/.exec(uri);
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
    const missing = response.error?.code === "asset_not_found" || response.error?.code === "recording_not_found";
    const code = missing ? -32002 : -32603;
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
  if (message.method === "notifications/cancelled") {
    if (isObject(message.params) && message.params.requestId !== undefined) {
      rendersAwaitingBuild.get(String(message.params.requestId))?.();
      cancelAuthoringRequest(message.params.requestId);
    }
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
        serverInfo: { name: "chibi", version: serverVersion },
        instructions: serverInstructions,
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
          uri: "chibi://project/status",
          name: "Chibi project status",
          mimeType: "application/json",
        }, {
          uri: "chibi://catalog",
          name: "Chibi semantic asset catalog",
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
        result: { contents: [{ uri, mimeType: "application/json", text: boundedJson(response, "resource") }] },
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
      const response = await toolCall(message.params?.name, message.params?.arguments, message.id);
      return reply(message, {
        jsonrpc: "2.0",
        id: message.id,
        result: toolResult(message.params.name, response),
      });
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

// Start a missing build now so the first tool call waits less; failures resurface on use.
ensureController().catch(() => {});

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
