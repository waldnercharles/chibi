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
import { controllerPath, parseControllerJson } from "./controller.mjs";
import { getToolCatalogEntry, toolCatalog } from "./tool-catalog.mjs";

const protocolVersion = "2025-11-25";
const supportedProtocolVersions = [protocolVersion, "2025-06-18"];
const serverVersion = "0.2.0";
const serverInstructions = [
  "Use Chibi MCP as the default authoring interface for shaders, behaviors, prototypes, scenes, and authored assets.",
  "1. Search or inspect assets, nodes, pins, fields, and content hashes with searchAssets, searchNodes, inspectGraph, inspectUi, describeComponents, or getProjectStatus.",
  "2. Build with buildGraph, buildPrototype, or buildScene. Write UI documents directly as files: content/ui/<Name>.rml, its markup, and content/ui/<Name>.rcss, its styles. Graph edits are typed, atomic, hash-checked for existing assets, and auto-arranged.",
  "3. Validate every published change with validateAsset. Use dry runs first when available and retry asset conflicts with the latest hash.",
  "4. Use previewShader, previewField, previewScene, runScenario, captureRuntimeSnapshot, or profileScenario for visual and runtime evidence.",
  "Tool results carry machine-readable data in structuredContent and include next_steps. Use the public camelCase names returned by tools/list; native operation names are implementation details.",
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

// Requests in flight, and those the client cancelled: a cancelled request gets no reply.
const inFlight = new Set();
const cancelled = new Set();

function isCancelled(requestId) {
  return requestId !== undefined && cancelled.has(String(requestId));
}

function cancelledError() {
  return protocolError(-32800, "request cancelled");
}

// A queued request that never reached a controller runs again on a fresh one.
function retryError() {
  const error = new Error("chibi_ctl session ended before the request ran");
  error.retry = true;
  return error;
}

async function builtRenderRequest(renderRoot, request, requestId) {
  await ensureController();
  if (isCancelled(requestId)) {
    return { contract: 1, operation: request.operation, ok: false, error: { code: "cancelled", message: "request cancelled" } };
  }
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

  // A rebuilt controller replaces the running one on the next request; while a
  // rebuild has removed the binary, the running one keeps serving.
  stale() {
    try {
      return statSync(this.executable).mtimeMs !== this.modified;
    } catch {
      return false;
    }
  }

  request(payload, timeout, requestId) {
    return new Promise((resolveRequest, rejectRequest) => {
      if (this.closed) {
        rejectRequest(retryError());
        return;
      }
      this.queue.push({ payload, timeout, requestId, resolveRequest, rejectRequest });
      this.pump();
    });
  }

  pump() {
    while (!this.closed && !this.pending && this.queue.length) {
      const request = this.queue.shift();
      if (isCancelled(request.requestId)) {
        request.rejectRequest(cancelledError());
        continue;
      }
      this.pending = request;
      this.timer = setTimeout(() => {
        this.fail(protocolError(-32001, "chibi_ctl request timed out"));
      }, request.timeout);
      this.child.stdin.write(`${JSON.stringify(request.payload)}\n`, (error) => {
        if (error) {
          this.fail(error);
        }
      });
    }
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
      if (this.retiring) {
        this.close();
        return;
      }
      this.pump();
    }
  }

  // Stops taking requests: queued ones move to a fresh session and the running
  // one finishes here.
  retire() {
    this.retiring = true;
    this.retryQueue();
    if (!this.pending) {
      this.close();
    }
  }

  retryQueue() {
    for (const request of this.queue) {
      request.rejectRequest(retryError());
    }
    this.queue = [];
  }

  // Fails only the running request; queued ones never reached this controller.
  fail(error) {
    if (this.closed) {
      return;
    }
    this.closed = true;
    clearTimeout(this.timer);
    this.timer = null;
    this.pending?.rejectRequest(error.rpcCode ? error : protocolError(-32603, `chibi_ctl failed: ${error.message}`));
    this.pending = null;
    this.retryQueue();
    if (this.child.exitCode === null && !this.child.killed) {
      this.child.kill();
    }
    if (controllerSession === this) {
      controllerSession = null;
    }
  }

  close() {
    this.fail(protocolError(-32603, "chibi_ctl session closed"));
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
        description: "Words that must each start a word in an asset's name, kind, path, description or member names, such as \"health\" or \"arena scene\"; name matches rank first. Omit it, or pass an empty string, to list every asset in kinds.",
      },
      kinds: {
        type: "array",
        items: { type: "string" },
        description: "Optional asset families to include in the search.",
      },
      ...pageProperties,
    },
    additionalProperties: false,
  };
  const dependencyArguments = {
    ...assetArguments,
    properties: {
      ...assetArguments.properties,
      direction: {
        type: "string",
        enum: ["outgoing", "incoming", "both"],
        description: "Which side of the dependency graph to return; defaults to outgoing.",
      },
      transitive: {
        type: "boolean",
        description: "Traverse dependencies beyond the requested asset; defaults to false.",
      },
    },
  };
  const symbolProperties = {
    asset: { ...assetArguments, description: "The asset {kind, name}; alone, the asset itself." },
    graph: {
      type: "string",
      description: "Function or macro name in asset; also a parameter's function or the graph of a local.",
    },
    graph_id: {
      type: "integer",
      minimum: 0,
      description: "Function or macro ID in asset, as inspectGraph lists it; also the graph of a local.",
    },
    event: {
      type: "string",
      description: "Event name instead of an asset: a host event such as Tick, an event definition, or a ui data-event or action name.",
    },
  };
  const referenceArguments = {
    type: "object",
    properties: {
      ...symbolProperties,
      variable: { type: "string", description: "Variable name in asset." },
      parameter: { type: "string", description: "Input or output name of the function that graph or graph_id selects." },
      local: { type: "string", description: "Named value (local) in asset." },
      component: { type: "string", description: "Component name or key instead of an asset." },
      field: { type: "string", description: "Field path of component, such as current." },
      ui_class: { type: "string", description: "A class ui assets and stylesheets name, instead of an asset." },
      token: { type: "string", description: "A custom property with its --, such as --primary, instead of an asset." },
      save_key: { type: "string", description: "A save key ui bindings read as save.<key>, instead of an asset." },
      roles: {
        type: "array",
        minItems: 1,
        maxItems: 10,
        items: {
          type: "string",
          enum: ["use", "call", "argument", "read", "write", "send", "handle", "spawn", "attach", "bind"],
        },
        description: "Only uses in these roles; counts still cover every role.",
      },
      limit: { type: "integer", minimum: 1, maximum: 200, description: "Uses per page; defaults to 50." },
      offset: { type: "integer", minimum: 0, description: "Zero-based offset of the first use." },
    },
    additionalProperties: false,
  };
  const hierarchyArguments = {
    type: "object",
    properties: {
      ...symbolProperties,
      direction: {
        type: "string",
        enum: ["incoming", "outgoing", "both"],
        description: "Callers (incoming), callees (outgoing) or both; defaults to both.",
      },
      depth: { type: "integer", minimum: 1, maximum: 6, description: "Levels below the root; defaults to 2." },
    },
    additionalProperties: false,
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
      include_pins: {
        type: "boolean",
        description: "List each node's pins as \"name: type\" with the types wildcards resolved to, as searchNodes writes them; defaults to true.",
      },
      include_uses: {
        type: "boolean",
        description: "Add project-wide use counts by role to the asset, its functions and macros, and a behavior's variables. Reads every asset; defaults to false.",
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
        target: { type: "string", description: "Scene entity name with a GameUi." },
        element: { type: "string", description: "RCSS selector of the element in its ui document, such as #volume." },
        event: { type: "string", description: "The event the element emits, as in data-event-click=\"emit('Name')\"." },
        value: { type: "number", description: "An input's new value, readable with Get UI Value." },
      }, required: ["target", "element", "event"], additionalProperties: false },
        description: "Player UI activations, delivered as the presenter does: an optional value, then the named event to the GameUi entity." },
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
  scenePreview.inputSchema.properties.saved_state = savedState;
  scenarioArguments.properties.saved_state = savedState;
  runtimeSnapshotArguments.properties.saved_state = savedState;
  const timeline = { type: "array", maxItems: 1800, items: { type: "object", additionalProperties: false,
    required: ["frame", "input"], properties: { frame: { type: "integer", minimum: 0, maximum: 107999 }, input: normalizedInputs.items } },
    description: "Sparse {frame, input} samples in increasing frame order, each frame below frames; use instead of inputs." };
  scenePreview.inputSchema.properties.input_timeline = {
    ...timeline, description: scenePreview.inputSchema.properties.input_timeline.description };
  scenarioArguments.properties.input_timeline = timeline;
  runtimeSnapshotArguments.properties.input_timeline = timeline;
  // One detailed input schema per tool; chibi_ctl validates every sample.
  const frameInputs = { type: "array", maxItems: 1800, items: { type: "object" },
    description: "One input per fixed frame, shaped like input_timeline[].input." };
  scenePreview.inputSchema.properties.inputs = frameInputs;
  scenarioArguments.properties.inputs = frameInputs;
  runtimeSnapshotArguments.properties.inputs = frameInputs;
  const recording = { type: "object", description: "A recording as the chibi://recording/<id> resource returns it; prefer the recording_path recordScenario returns." };
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
        description: "Prototype or scene asset name without the .yaml suffix.",
      },
      frames: {
        type: "integer",
        minimum: 0,
        maximum: 1800,
        description: "Number of deterministic fixed steps to record; at most 1800, unlike runScenario.",
      },
      fixed_dt: {
        type: "number",
        minimum: 0.000001,
        maximum: 0.25,
        description: "Fixed timestep in seconds. The engine currently accepts 1/60.",
      },
      inputs: normalizedInputs,
      input_timeline: timeline,
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
        description: "Recording artifact path returned by recordScenario.",
      },
    },
    oneOf: [
      { type: "object", required: ["recording"] },
      { type: "object", required: ["recording_path"] },
    ],
    additionalProperties: false,
  };
  // Rename and delete select a symbol as findReferences does.
  const editProperties = {
    ...symbolProperties,
    variable: referenceArguments.properties.variable,
    parameter: referenceArguments.properties.parameter,
    local: referenceArguments.properties.local,
    component: { type: "string", description: "Component name or key; refused, since C reflection names it." },
    field: { type: "string", description: "Field path of component; refused." },
    dry_run: { type: "boolean", default: true, description: "List every asset and change without publishing; defaults to true." },
    expected_hashes: {
      type: "object",
      additionalProperties: { type: "string" },
      description: "The dry run's expected_hashes, passed back unchanged; required when dry_run is false.",
    },
  };
  const uiArguments = {
    type: "object",
    properties: {
      kind: { type: "string", enum: ["ui"], description: "Always ui." },
      name: { type: "string", description: "UI asset name without the .rml suffix." },
    },
    required: ["kind", "name"],
    additionalProperties: false,
  };
  const inspectUiArguments = {
    type: "object",
    properties: {
      ...uiArguments.properties,
      complete: {
        type: "object",
        properties: {
          line: { type: "integer", minimum: 1, description: "1-based line of the file." },
          column: { type: "integer", minimum: 1, description: "1-based column, in bytes." },
          file: { type: "string", enum: ["rml", "rcss"], description: "The asset's markup (default) or its styles." },
        },
        required: ["line", "column"],
        additionalProperties: false,
        description: "Also return the completions Studio's Text Editor offers at this position: tags, attributes, widgets, classes, properties, custom properties and save keys.",
      },
    },
    required: ["kind", "name"],
    additionalProperties: false,
  };
  const renameArguments = {
    type: "object",
    properties: {
      ...editProperties,
      new_name: { type: "string", description: "The symbol's new name; an asset's also names its file." },
    },
    required: ["new_name"],
    additionalProperties: false,
  };
  // Native delete refuses parameters and locals.
  const { parameter, local, ...deleteProperties } = editProperties;
  const deleteArguments = {
    type: "object",
    properties: {
      ...deleteProperties,
      graph: { type: "string", description: "Function or macro name in asset." },
      force: { type: "boolean", description: "Delete while references remain, and list them as dangling." },
    },
    additionalProperties: false,
  };
  return [
    {
      name: "getProjectStatus",
      operation: "status",
      inputSchema: noArguments,
      outputSchema,
    },
    {
      name: "listAssets",
      operation: "list_assets",
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
      inputSchema: pageArguments,
      outputSchema,
    },
    {
      name: "searchAssets",
      operation: "search_assets",
      inputSchema: searchArguments,
      outputSchema,
    },
    {
      name: "inspectAssetDependencies",
      operation: "asset_dependencies",
      inputSchema: dependencyArguments,
      outputSchema,
    },
    {
      name: "findReferences",
      operation: "find_references",
      inputSchema: referenceArguments,
      outputSchema,
    },
    {
      name: "callHierarchy",
      operation: "call_hierarchy",
      inputSchema: hierarchyArguments,
      outputSchema,
    },
    {
      name: "renameSymbol",
      operation: "rename_symbol",
      inputSchema: renameArguments,
      outputSchema,
    },
    {
      name: "deleteSymbol",
      operation: "delete_symbol",
      inputSchema: deleteArguments,
      outputSchema,
    },
    {
      name: "inspectUi",
      operation: "inspect_ui",
      inputSchema: inspectUiArguments,
      outputSchema,
    },
    {
      name: "inspectGraph",
      operation: "inspect_graph",
      inputSchema: graphArguments,
      outputSchema,
    },
    {
      name: "inspectSystem",
      operation: "inspect_system",
      inputSchema: {
        ...assetArguments,
        properties: {
          kind: { type: "string", enum: ["system"], description: "Always system." },
          name: { type: "string", description: "System asset name without the .yaml suffix." },
        },
      },
      outputSchema,
    },
    {
      name: "getExecutionStatus",
      operation: "execution_status",
      inputSchema: executionStatusArguments,
      outputSchema,
    },
    {
      name: "profileScenario",
      operation: "profile_scenario",
      inputSchema: profileScenarioArguments,
      outputSchema,
    },
    {
      name: "runScenario",
      operation: "run_scenario",
      inputSchema: scenarioArguments,
      outputSchema,
    },
    {
      name: "captureRuntimeSnapshot",
      operation: "runtime_snapshot",
      inputSchema: runtimeSnapshotArguments,
      outputSchema,
    },
    {
      name: "recordScenario",
      operation: "record_scenario",
      inputSchema: recordArguments,
      outputSchema,
    },
    {
      name: "replayScenario",
      operation: "replay_scenario",
      inputSchema: replayArguments,
      outputSchema,
    },
    {
      name: "readAsset",
      operation: "read_asset",
      inputSchema: assetArguments,
      outputSchema,
    },
    {
      name: "validateAsset",
      operation: "validate_asset",
      inputSchema: assetArguments,
      outputSchema,
    },
    {
      name: "describeRegistry",
      operation: "describe_registry",
      inputSchema: {
        type: "object",
        properties: {
          sections: {
            type: "array",
            maxItems: 4,
            items: { type: "string", enum: ["nodes", "types", "services", "components"] },
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
      inputSchema: noArguments,
      outputSchema,
    },
    ...authoringToolDefinitions(),
  ];
}

function nativeTimeout() {
  // Long deterministic scenarios can run for minutes; a timeout restarts the controller.
  const value = Number(process.env.CHIBI_CTL_TIMEOUT_MS || 300000);
  return Number.isSafeInteger(value) && value > 0 ? value : 300000;
}

async function sessionRequest(payload, requestId) {
  const timeout = nativeTimeout();
  // A running controller keeps serving while a rebuild replaces its binary.
  if (!controllerSession) {
    await ensureController();
  }
  if (controllerSession?.stale()) {
    controllerSession.retire();
    controllerSession = null;
  }
  controllerSession ??= new ControllerSession(controllerPath());
  const session = controllerSession;
  session.rootCheck ??= session.request({ contract: 1, operation: "status" }, timeout)
    .then((status) => {
      const resolvedRoot = status.result?.project_root;
      if (!status.ok || !resolvedRoot || realpathSync(resolvedRoot) !== realpathSync(root)) {
        session.close();
        throw protocolError(-32603, `Chibi project root mismatch: expected ${root}, got ${resolvedRoot}`);
      }
      process.stderr.write(`chibi MCP verified project root: ${resolvedRoot}\n`);
    });
  await session.rootCheck;
  if (isCancelled(requestId)) {
    throw cancelledError();
  }
  return session.request(payload, timeout, requestId);
}

async function nativeRequest(operation, args = {}, requestId) {
  for (;;) {
    try {
      return await sessionRequest({ ...args, contract: 1, operation }, requestId);
    } catch (error) {
      if (!error.retry) {
        throw error;
      }
    }
  }
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

function invalidArguments(message) {
  return protocolError(-32602, message);
}

function matchesType(value, type) {
  switch (type) {
    case "object": return isObject(value);
    case "array": return Array.isArray(value);
    case "string": return typeof value === "string";
    case "boolean": return typeof value === "boolean";
    case "number": return typeof value === "number" && Number.isFinite(value);
    case "integer": return Number.isSafeInteger(value);
    case "null": return value === null;
  }
  throw new Error(`unsupported schema type: ${type}`);
}

// The JSON Schema subset tool schemas use; keywords apply to values of their type.
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
      throw invalidArguments(`${path} must match exactly one supported form`);
    }
  }
  if (schema.type !== undefined) {
    const types = [schema.type].flat();
    if (!types.some((type) => matchesType(value, type))) {
      throw invalidArguments(`${path} must be of type ${types.join(" or ")}`);
    }
  }
  if (isObject(value)) {
    for (const name of schema.required || []) {
      if (!Object.hasOwn(value, name)) {
        throw invalidArguments(`${path}.${name} is required`);
      }
    }
    const names = Object.keys(value);
    if (schema.maxProperties !== undefined && names.length > schema.maxProperties) {
      throw invalidArguments(`${path} has more than ${schema.maxProperties} properties`);
    }
    for (const name of names) {
      const child = schema.properties && Object.hasOwn(schema.properties, name)
        ? schema.properties[name]
        : schema.additionalProperties;
      if (child === false) {
        throw invalidArguments(`${path}.${name} is not supported`);
      }
      if (isObject(child)) {
        validateSchema(value[name], child, `${path}.${name}`);
      }
    }
  }
  if (typeof value === "string") {
    const length = [...value].length;
    if (schema.minLength !== undefined && length < schema.minLength) {
      throw invalidArguments(`${path} must be at least ${schema.minLength} characters`);
    }
    if (schema.maxLength !== undefined && length > schema.maxLength) {
      throw invalidArguments(`${path} must be at most ${schema.maxLength} characters`);
    }
  }
  if (typeof value === "number") {
    if (schema.minimum !== undefined && value < schema.minimum) {
      throw invalidArguments(`${path} must be at least ${schema.minimum}`);
    }
    if (schema.maximum !== undefined && value > schema.maximum) {
      throw invalidArguments(`${path} must be at most ${schema.maximum}`);
    }
  }
  if (Array.isArray(value)) {
    if (schema.minItems !== undefined && value.length < schema.minItems) {
      throw invalidArguments(`${path} must contain at least ${schema.minItems} items`);
    }
    if (schema.maxItems !== undefined && value.length > schema.maxItems) {
      throw invalidArguments(`${path} must contain at most ${schema.maxItems} items`);
    }
    if (schema.items) {
      value.forEach((item, index) => validateSchema(item, schema.items, `${path}[${index}]`));
    }
  }
  if (schema.enum && !schema.enum.includes(value)) {
    throw invalidArguments(`${path} must be one of ${schema.enum.join(", ")}`);
  }
}

// Arguments the model can correct fail as a tool result it sees, not a protocol error.
function argumentError(name, args) {
  try {
    validateSchema(args, toolDefinition(name).inputSchema, "arguments");
    if (isAuthoringTool(name)) {
      validateAuthoringArguments(name, args);
    }
    return null;
  } catch (error) {
    if (error.rpcCode !== -32602) {
      throw error;
    }
    return { ok: false, error: { code: "invalid_arguments", message: error.message } };
  }
}

async function toolCall(name, args = {}, requestId) {
  const entry = typeof name === "string" ? getToolCatalogEntry(name) : undefined;
  if (!entry) {
    throw protocolError(-32602, `unknown tool: ${name}`);
  }
  const invalid = argumentError(name, args);
  if (invalid) {
    return invalid;
  }
  if (entry.adapter === "authoring") {
    return authoringToolCall(name, args, { nativeRequest, renderRequest: builtRenderRequest, root, requestId });
  }
  return nativeRequest(entry.operation, args, requestId);
}

function defaultNextSteps(name) {
  return getToolCatalogEntry(name)?.nextSteps || [];
}

// Words an agent uses for the player's UI, whose events come from ui assets.
const uiQuery = /\b(ui|hud)\b|\b(button|click|menu|label|widget|slider)/i;

// A UI search that finds no events: the ui asset has to name them first.
function uiEventSteps(response, args) {
  const nodes = response?.result?.nodes;
  if (!Array.isArray(nodes) || !uiQuery.test(args?.query || "") ||
      nodes.some((node) => node.type === "Event")) {
    return [];
  }
  return [
    "To react to a click or key, give the element a data-event (or data-keys) in content/ui/<Name>.rml (kind ui), call validateAsset, then handle it with an Event node of that event_name.",
  ];
}

function responseNextSteps(name, response, args) {
  const code = response?.error?.code;
  if (code === "invalid_arguments") {
    return ["Correct the arguments to match the tool's inputSchema, then call it again."];
  }
  if (code === "asset_not_found") {
    return ["Call searchAssets with a broader query or the expected asset kind."];
  }
  const refactor = name === "renameSymbol" || name === "deleteSymbol";
  if (refactor && code === "asset_conflict") {
    return [`Call ${name} again as a dry run for current expected_hashes, then publish with them.`];
  }
  if (refactor && code === "invalid_request") {
    return name === "deleteSymbol" && Array.isArray(response?.result?.references) && response.result.references.length
      ? ["Remove the listed references, or pass force to delete anyway and leave them dangling."]
      : ["Correct the symbol selector or the new name, then run the dry run again."];
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
  if (name === "buildGraph" && code === "invalid_request" && /node|pin|reference/i.test(response?.error?.message || "")) {
    return ["Call searchNodes for the node or pin signature, then retry the graph edit."];
  }
  // The default steps follow a success; the error message covers other failures.
  if (response?.ok !== true) {
    return [];
  }
  const result = response?.result;
  if (name === "searchAssets" && Array.isArray(result?.assets) && result.assets.length === 0) {
    return ["Broaden the search query or call getAssetCatalog for project-wide context."];
  }
  const uiSteps = name === "searchNodes" ? uiEventSteps(response, args) : [];
  if (name === "searchNodes" && Array.isArray(result?.nodes) && result.nodes.length === 0) {
    return [...uiSteps, "Use fewer or different words, remove a restrictive pin filter, or call searchNodes without a query to list the node types."];
  }
  if (uiSteps.length) {
    return [...uiSteps, ...defaultNextSteps(name)];
  }
  if (refactor && result?.dry_run === true) {
    return [
      "Review the listed assets, changes and mentions.",
      `Call ${name} with dry_run false and the returned expected_hashes to publish.`,
    ];
  }
  if ((name === "buildGraph" || name === "buildPrototype" || name === "buildScene") &&
      result?.dry_run === true) {
    return [
      "Review the dry-run diagnostics and proposed changes.",
      "To publish, call the same build tool with dry_run false and the same expected_hash, or create: true for a new asset.",
    ];
  }
  if (name === "optimizeGraph" && result?.dry_run === true) {
    return [
      "Review the applied and skipped changes.",
      "Call optimizeGraph with dry_run false and the returned before_hash as expected_hash to publish.",
    ];
  }
  if (name === "replayScenario" && result?.diverged === false) {
    return [];
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

function withToolGuidance(name, response, args) {
  const publicResponse = addPublicContractNames(name, response);
  const nextSteps = responseNextSteps(name, publicResponse, args);
  return nextSteps.length ? { ...publicResponse, next_steps: nextSteps } : publicResponse;
}

function toolResult(name, response, args) {
  const guidedResponse = leanResponse(name, withToolGuidance(name, response, args), args);
  const authoringResult = authoringToolResult(guidedResponse);
  if (authoringResult) {
    return authoringResult;
  }
  const text = guidedResponse.ok === true
    ? boundedJson(toolSummary(name, guidedResponse), "tool summary")
    : boundedJson({
      ok: false,
      error: guidedResponse.error,
      ...errorDetails(guidedResponse.result),
      next_steps: guidedResponse.next_steps,
    }, "tool error");
  return {
    content: [{ type: "text", text }],
    structuredContent: guidedResponse,
    isError: guidedResponse.ok !== true,
  };
}

function without(record, keys) {
  return Object.fromEntries(Object.entries(record).filter(([key]) => !keys.includes(key)));
}

// Reflection records: binary layout and C spellings mean nothing to an author.
function leanReflectedField(field) {
  const lean = without(field, ["offset", "bytes"]);
  return lean.type_name === lean.type || lean.type_name === "_Bool" ? without(lean, ["type_name"]) : lean;
}

function leanComponent(component) {
  const lean = without(component, ["id", "bytes", "layout_hash", ...(component.type === component.name ? ["type"] : [])]);
  return Array.isArray(lean.fields) ? { ...lean, fields: lean.fields.map(leanReflectedField) } : lean;
}

function leanRegistryType(type) {
  const lean = without(type, ["bytes"]);
  return Array.isArray(lean.fields) ? { ...lean, fields: lean.fields.map(leanReflectedField) } : lean;
}

// Snapshot fields as name and value; describeComponents has the types.
function leanSnapshotComponent(component) {
  return {
    name: component.name,
    fields: component.fields?.map(({ name, value }) => ({ name, value })),
    ...(component.fields_truncated ? { field_count: component.field_count, fields_truncated: true } : {}),
  };
}

function leanSnapshotBehavior(behavior) {
  const empty = [
    ...(behavior.performance_diagnostics?.length ? [] : ["performance_diagnostics"]),
    ...(behavior.last_failure === null ? ["last_failure"] : []),
  ];
  return without(behavior, empty);
}

function leanSnapshotEntity(entity) {
  const lean = without(entity, [
    "slot", "generation",
    ...(entity.components_truncated ? [] : ["component_count", "components_truncated"]),
    ...(entity.behaviors_truncated ? [] : ["behavior_count", "behaviors_truncated"]),
  ]);
  return {
    id: lean.id,
    name: lean.name,
    prototype: lean.prototype,
    ...lean,
    components: lean.components?.map(leanSnapshotComponent),
    behaviors: lean.behaviors?.map(leanSnapshotBehavior),
  };
}

// Catalog records keep relationships and diagnostics only when there are some.
function leanAsset(asset) {
  return Object.fromEntries(Object.entries(asset).filter(([key, value]) =>
    !(Array.isArray(value) && value.length === 0) &&
    !((key === "errors" || key === "warnings") && value === 0)));
}

function leanResult(name, result, args) {
  switch (name) {
    case "getProjectStatus":
      return without(result, ["operations"]);
    case "getAssetCatalog":
    case "searchAssets":
      return { ...without(result, ["project_root", "query"]), assets: result.assets?.map(leanAsset) };
    case "listAssets":
      return without(result, ["project_root"]);
    case "describeComponents":
      return { ...result, components: result.components?.map(leanComponent) };
    case "describeRegistry":
      return {
        ...result,
        ...(result.types ? { types: result.types.map(leanRegistryType) } : {}),
        ...(result.components ? { components: result.components.map(leanComponent) } : {}),
      };
    case "captureRuntimeSnapshot":
      return { ...result, entities: result.entities?.map(leanSnapshotEntity) };
    case "previewScene":
      // Timings are measured only past two rendered frames.
      return (args?.render_frames ?? 2) > 2 ? result : without(result, ["performance"]);
  }
  return result;
}

/* Clients such as Claude Code show structuredContent to the model, so it
   leaves out the native envelope and fields no authoring decision uses. */
function leanResponse(name, response, args) {
  const lean = without(response, ["contract", "operation"]);
  return isObject(lean.result) ? { ...lean, result: leanResult(name, lean.result, args) } : lean;
}

/* Clients may show only the text, so a failure names what to fix. */
function errorDetails(result) {
  if (!isObject(result)) {
    return {};
  }
  const details = {};
  if (Number.isInteger(result.failed_operation)) {
    details.failed_operation = result.failed_operation;
  }
  // A refused delete names what still uses the symbol; a failed publish, what no longer validates.
  if (Array.isArray(result.references) && result.references.length) {
    details.references = result.references.slice(0, 8).map((item) => ({
      role: item.role,
      asset: item.asset,
      graph_id: item.graph_id,
      graph: item.graph,
      node: item.node,
      pin: item.pin,
      object: item.object,
      component: item.component,
    }));
  }
  if (Array.isArray(result.invalid) && result.invalid.length) {
    details.invalid = result.invalid.slice(0, 8);
  }
  const diagnostics = result.diagnostics ?? result.validation?.diagnostics;
  if (Array.isArray(diagnostics) && diagnostics.length) {
    details.diagnostics = diagnostics.slice(0, 8).map((item) => ({
      severity: item.severity,
      path: item.path,
      graph: item.graph,
      node: item.node,
      pin: item.pin,
      rule: item.rule_name,
      message: item.message,
      line: item.line,
      column: item.column,
    }));
  }
  return details;
}

function toolSummary(name, response) {
  const summary = { ok: true };
  const result = response.result;
  if (!isObject(result)) {
    return summary;
  }
  const scalarKeys = [
    ...(name === "getProjectStatus" ? ["project_root", "content_root"] : []),
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
    "created_expressions",
    "changed_edges",
    "world_hash",
    "live_entities",
    "valid",
    "deterministic",
    "diverged",
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
  // findReferences: the uses by role.
  if (isObject(result.counts)) {
    summary.counts = result.counts;
  }
  for (const key of ["assets", "graphs", "nodes", "edges", "entities", "components", "operations", "diagnostics", "behaviors", "skipped", "expressions", "locals", "uses", "mentions", "dangling", "references", "invalid"]) {
    if (Array.isArray(result[key]) && result[key].length !== result.count) {
      summary[`${key}_count`] = result[key].length;
    }
  }
  // Inputs a source edit left without their wire or binding.
  if (Array.isArray(result.unfed_inputs)) {
    summary.unfed_inputs = result.unfed_inputs;
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

async function resourceRead(uri, requestId) {
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
    response = await nativeRequest("status", {}, requestId);
  } else if (uri === "chibi://catalog") {
    response = await nativeRequest("catalog", {}, requestId);
  } else {
    const recordingMatch = /^chibi:\/\/recording\/([^/]+)$/.exec(uri);
    if (recordingMatch) {
      const id = decode(recordingMatch[1]);
      if (!/^[A-Za-z0-9._-]{1,128}$/.test(id)) {
        throw protocolError(-32602, `invalid recording resource: ${uri}`);
      }
      response = await nativeRequest("read_recording", { id }, requestId);
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
      }, requestId);
    }
    const dependencyMatch = /^chibi:\/\/asset\/([^/]+)\/([^/]+)\/dependencies$/.exec(uri);
    if (response === undefined && dependencyMatch) {
      response = await nativeRequest("asset_dependencies", {
        kind: decode(dependencyMatch[1]),
        name: decode(dependencyMatch[2]),
      }, requestId);
    }
    const match = /^chibi:\/\/asset\/([^/]+)\/([^/]+)$/.exec(uri);
    if (response === undefined && !match) {
      throw protocolError(-32002, `resource not found: ${uri}`);
    }
    if (response === undefined) {
      response = await nativeRequest("read_asset", {
        kind: decode(match[1]),
        name: decode(match[2]),
      }, requestId);
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
      const key = String(message.params.requestId);
      if (inFlight.has(key)) {
        cancelled.add(key);
        cancelAuthoringRequest(key);
      }
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
      const response = await resourceRead(uri, message.id);
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
        result: toolResult(message.params.name, response, message.params.arguments),
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
      const key = isObject(message) && Object.hasOwn(message, "id") ? String(message.id) : undefined;
      if (key !== undefined) {
        inFlight.add(key);
      }
      const answer = (response) => {
        if (response && !cancelled.has(key)) {
          send(response);
        }
      };
      activeDispatches++;
      dispatch(message)
        .then(answer)
        .catch((error) => {
          if (key !== undefined) {
            answer(errorResponse(message.id, error.rpcCode || -32603, error.message));
          }
        })
        .finally(() => {
          inFlight.delete(key);
          cancelled.delete(key);
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
