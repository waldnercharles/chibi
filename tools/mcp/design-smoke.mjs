#!/usr/bin/env node

// Author every fixture through the public protocol, then exercise its runtime.
import assert from "node:assert/strict";
import { pngPixels } from "./png-check.mjs";
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { resolve, join } from "node:path";
import { spawnSync } from "node:child_process";

const root = resolve(process.argv[2] || ".");
const project = mkdtempSync(join(tmpdir(), "chibi-design-mcp-"));
const controller = process.env.CHIBI_CTL || join(root, "build/chibi_ctl");
for (const family of ["behaviors", "events", "prototypes", "scenes", "ui"]) {
  mkdirSync(join(project, "content", family), { recursive: true });
}
// UI documents are authored as files; their data-event names reach behaviors.
writeFileSync(join(project, "content/ui/Bank.yaml"), [
  "rml: '<div id=\"extract\" data-event=\"Extract\">Extract</div>'",
  "rcss: '#extract { position: absolute; left: 50%; top: 50%; width: 180dp; height: 44dp; margin-left: -90dp; margin-top: -22dp; background-color: #3a4a66; line-height: 44dp; text-align: center; }'",
  "",
].join("\n"));
writeFileSync(join(project, "content/ui/Slot.yaml"), [
  "rml: '<div id=\"slot\" data-event=\"Fit\">Empty modifier slot</div>'",
  "",
].join("\n"));
// A widget host: it uses Slot and sends Award, an event definition.
writeFileSync(join(project, "content/ui/Rack.yaml"), [
  "rml: '<use ui=\"Slot\"/><div id=\"award\" data-keys=\"Enter:Award\">Award</div>'",
  "",
].join("\n"));

function request(name, args, expectError = false) {
  const run = spawnSync(process.execPath, [join(root, "tools/mcp/server.mjs")], {
    cwd: root,
    env: {
      ...process.env, CHIBI_ROOT: project, CHIBI_CTL: controller,
      // Scene previews use the same GPU sanitizer policy as native render checks.
      ...(name === "previewScene" ? { ASAN_OPTIONS: `${process.env.ASAN_OPTIONS || ""}:detect_leaks=0` } : {}),
    },
    input: `${JSON.stringify({ jsonrpc: "2.0", id: 1, method: "tools/call", params: { name, arguments: args } })}\n`,
    encoding: "utf8", maxBuffer: 16 * 1024 * 1024,
  });
  assert.equal(run.status, 0, run.stderr);
  const response = JSON.parse(run.stdout.trim());
  const failure = response.error || response.result?.isError;
  assert.equal(Boolean(failure), expectError, JSON.stringify(response));
  const image = response.result?.content.find(item => item.type === "image");
  if (image) {
    const png = Buffer.from(image.data, "base64");
    writeFileSync(join(project, `preview-${args.name}.png`), png);
    const decoded = pngPixels(png);
    assert.equal(decoded.width, args.width); assert.equal(decoded.height, args.height);
    if (args.name === "Dock") assert.ok(decoded.at(480, 270).slice(0, 3).some(value => value > 15), "dock button must be visible in the image center");
    if (args.name === "VisibilityLab") {
      let cyan = 0, yellow = 0, red = 0;
      for (let y = 50; y < 400; y++) for (let x = 100; x < 700; x++) {
        const [r, g, b] = decoded.at(x, y);
        cyan += g > 120 && b > 150 && r < 180;
        yellow += r > 150 && g > 100 && b < 160;
        red += r > 150 && g < 80 && b < 80;
      }
      assert(cyan > 20 && yellow > 100, "the ship and wall must retain visible geometry");
      assert.equal(red, 0, "the enemy behind the wall must stay concealed");
      assert(decoded.at(615, 270).slice(0, 3).every(value => value < 40), "unseen world pixels must remain fogged");
    }
  }
  return response.result?.structuredContent?.result || response;
}

function create(kind, name, operations, extra = {}) {
  const tool = kind === "prototype" ? "buildPrototype" : kind === "scene" ? "buildScene" : "buildGraph";
  return request(tool,
    { kind, name, create: true, operations, ...extra });
}

function connect(from, from_pin, to, to_pin, kind = "data") {
  return { op: "connect", from, from_pin, to, to_pin, kind };
}

function field(snapshot, entity, component, name) {
  const actor = snapshot.entities.find(item => item.name === entity);
  assert.ok(actor, `missing scene entity ${entity}`);
  return actor.components.find(item => item.name === component)?.fields.find(item => item.name === name)?.value;
}

try {
  assert.equal(request("searchNodes", { query: "Raycast" }).nodes[0].type, "Raycast");
  const extract = request("searchNodes", { query: "extract", include_pins: false }).nodes[0];
  assert.equal(extract.event_name, "Extract");
  assert.equal(extract.description, "Sent when the player clicks #extract in Bank.");
  create("event", "Award", [{ op: "add_variable", name: "Amount", type: "int", default: 0 }]);
  create("behavior", "Receiver", [
    { op: "add_variable", name: "Reward", type: "int", default: 9, ref: "reward" },
    { op: "add_variable", name: "Obsolete", type: "float", ref: "old" },
    { op: "remove_variable", variable: "old" },
    { op: "add_graph", name: "Unused", kind: "function", pure: true, ref: "unused" },
    { op: "remove_graph", graph: "unused" },
    { op: "select_graph", graph: 0 },
    { op: "add_node", type: "Event", event_name: "Award", ref: "award" },
    { op: "add_node", type: "Set_GameHealth_current", ref: "set" },
    connect("award", "Then", "set", "Exec", "exec"),
    connect("award", "Amount", "set", "Value"),
    { op: "add_node", type: "Event", event_name: "Probe", ref: "probe" },
    { op: "add_node", type: "Raycast", ref: "ray", inputs: {
      Start: { x: 0, y: 0 }, End: { x: 100, y: 0 }, Categories: 8,
    } },
    { op: "expose_fields", node: "ray", pin: "Normal", side: "output" },
    { op: "add_node", type: "Branch", ref: "branch" },
    { op: "add_node", type: "Set_GameHealth_current", ref: "reward_set" },
    { op: "bind_input", node: "reward_set", pin: "Value", kind: "variable", variable: "reward" },
    connect("probe", "Then", "branch", "Exec", "exec"),
    connect("ray", "Found", "branch", "Condition"),
    connect("branch", "True", "reward_set", "Exec", "exec"),
  ], { owner_type: "ecs.entity" });
  create("behavior", "Spawner", [
    { op: "add_node", type: "Event", event_name: "Begin", ref: "begin" },
    { op: "add_node", type: "Spawn Prototype", ref: "spawn", inputs: { Prototype: "Payload" } },
    connect("begin", "Then", "spawn", "Exec", "exec"),
  ], { owner_type: "ecs.entity" });
  create("prototype", "Payload", [{ op: "add_component", component: "GameHealth", fields: { current: 3, maximum: 3 } }]);
  create("prototype", "Controller", [{ op: "add_behavior", behavior: "Spawner" }]);
  create("prototype", "Ship", [
    { op: "add_component", component: "GameHealth", fields: { current: 1, maximum: 100 } },
    { op: "add_component", component: "GameTransform", fields: { x: 0, y: 0 } },
    { op: "add_component", component: "GameInput" },
    { op: "add_component", component: "GameAim" },
    { op: "add_behavior", behavior: "Receiver" },
  ]);
  create("prototype", "Wall", [
    { op: "add_component", component: "GameTransform", fields: { x: 50, y: 0 } },
    { op: "add_component", component: "GameCollider", fields: { shape: 1, width: 4, height: 40, category: 8, mask: 0 } },
  ]);
  create("scene", "Route", [
    { op: "add_entity", name: "controller", prototype: "Controller", behaviors: ["Spawner"] },
    { op: "add_entity", name: "ship", prototype: "Ship", behaviors: ["Receiver"] },
    { op: "add_entity", name: "wall", prototype: "Wall" },
  ]);
  create("behavior", "Banking", [
    { op: "add_node", type: "Event", event_name: "Extract", ref: "click" },
    { op: "add_node", type: "Set Saved Text", ref: "save", inputs: { Key: "test.blueprint", Text: "ricochet" } },
    { op: "add_node", type: "Commit Save", ref: "commit" },
    connect("click", "Then", "save", "Exec", "exec"),
    connect("save", "Then", "commit", "Exec", "exec"),
    { op: "add_node", type: "Event", event_name: "Stage", ref: "stage" },
    { op: "add_node", type: "Set Saved Text", ref: "pending", inputs: { Key: "test.unsecured", Text: "payload" } },
    connect("stage", "Then", "pending", "Exec", "exec"),
    { op: "add_node", type: "Event", event_name: "Begin", ref: "load" },
    { op: "add_node", type: "Read Saved Text", ref: "read", inputs: { Key: "test.blueprint" } },
    { op: "add_node", type: "Branch", ref: "found" },
    { op: "add_node", type: "Set_GameHealth_current", ref: "unlock", inputs: { Value: 77 } },
    connect("load", "Then", "found", "Exec", "exec"),
    connect("read", "Found", "found", "Condition"),
    connect("found", "True", "unlock", "Exec", "exec"),
  ], { owner_type: "ecs.entity" });
  create("prototype", "Bank", [
    { op: "add_component", component: "GameHealth", fields: { current: 1, maximum: 100 } },
    { op: "add_component", component: "GameUi", fields: { document: "Bank" } },
    { op: "add_behavior", behavior: "Banking" },
  ]);
  create("scene", "Dock", [{ op: "add_entity", name: "bank", prototype: "Bank", behaviors: ["Banking"] }]);
  const bankUsers = request("inspectAssetDependencies", { kind: "ui", name: "Bank", direction: "incoming" });
  assert.ok(bankUsers.edges.some(edge => edge.from.kind === "prototype" && edge.from.name === "Bank"),
    "a GameUi document is a dependency of its prototype");
  const rack = request("inspectAssetDependencies", { kind: "ui", name: "Rack" });
  assert.ok(rack.edges.some(edge => edge.to.kind === "ui" && edge.to.name === "Slot"),
    "a ui document depends on the widgets it uses");
  assert.ok(rack.edges.some(edge => edge.to.kind === "event" && edge.to.name === "Award"),
    "a ui document depends on the event definitions it sends");
  const award = request("inspectAssetDependencies", { kind: "event", name: "Award", direction: "incoming" });
  assert.ok(award.edges.some(edge => edge.from.kind === "behavior" && edge.from.name === "Receiver"),
    "an event definition's handlers depend on it");
  const spawners = request("inspectAssetDependencies", { kind: "prototype", name: "Payload", direction: "incoming" });
  assert.ok(spawners.edges.some(edge => edge.from.kind === "behavior" && edge.from.name === "Spawner"),
    "a spawned prototype is a dependency of the graph that spawns it");
  // A ui document's data-keys send Award; Receiver's handler runs on it.
  const awards = request("callHierarchy", { event: "Award" });
  assert.ok(awards.root.callers.some(item => item.kind === "place" && item.asset.kind === "ui" && item.asset.name === "Rack"),
    "a ui document sending an event is its caller");
  assert.ok(awards.root.callees.some(item => item.kind === "handler" && item.asset.name === "Receiver"),
    "an event's handlers are its callees");
  const awardUses = request("findReferences", { event: "Award", roles: ["send"] });
  assert.ok(awardUses.definition.asset.kind === "event" && awardUses.uses.every(use => use.role === "send") &&
    awardUses.uses.some(use => use.asset.name === "Rack") && awardUses.counts.handle >= 1,
    "findReferences filters uses by role and counts every role");
  // A UI activation fits a part: GameHealth records it, the label shows it.
  create("behavior", "RecipeSlot", [
    { op: "add_node", type: "Event", event_name: "Fit", ref: "fit" },
    { op: "add_node", type: "Set UI Text", ref: "label", inputs: { Element: "#slot", Text: "Ricochet fitted" } },
    connect("fit", "Then", "label", "Exec", "exec"),
    { op: "add_node", type: "Get UI Value", ref: "chosen", inputs: { Element: "#slot" } },
    { op: "add_node", type: "Set_GameHealth_current", ref: "part" },
    connect("label", "Then", "part", "Exec", "exec"),
    connect("chosen", "Value", "part", "Value"),
    { op: "add_node", type: "Play Sound", ref: "sound", inputs: { Frequency: 600, "End Frequency": 900, Duration: 0.12, Volume: 0.3 } },
    connect("part", "Then", "sound", "Exec", "exec"),
    { op: "add_node", type: "Event", event_name: "Undo", ref: "undo" },
    { op: "add_node", type: "Set UI Text", ref: "restored", inputs: { Element: "#slot", Text: "Empty modifier slot" } },
    { op: "add_node", type: "Set_GameHealth_current", ref: "clear", inputs: { Value: 0 } },
    connect("undo", "Then", "restored", "Exec", "exec"),
    connect("restored", "Then", "clear", "Exec", "exec"),
  ], { owner_type: "ecs.entity" });
  create("prototype", "PartSlot", [
    { op: "add_component", component: "GameUi", fields: { document: "Slot" } },
    { op: "add_component", component: "GameHealth", fields: { current: 0, maximum: 10 } },
    { op: "add_behavior", behavior: "RecipeSlot" },
  ]);
  create("scene", "Refit", [
    { op: "add_entity", name: "slot", prototype: "PartSlot", behaviors: ["RecipeSlot"] },
    { op: "add_entity", name: "wall", prototype: "Wall" },
  ]);
  const fit = { target: "slot", element: "#slot", event: "Fit", value: 2 };
  const fitted = request("captureRuntimeSnapshot", { kind: "scene", name: "Refit", frames: 2,
    inputs: [{ ui: [fit] }] });
  assert.equal(field(fitted, "slot", "GameHealth", "current"), 2);
  assert.equal(fitted.sound_cues, 1, "a successful refit produces its authored sound cue");
  const undone = request("captureRuntimeSnapshot", { kind: "scene", name: "Refit", frames: 72000,
    input_timeline: [{ frame: 0, input: { ui: [fit] } },
      { frame: 71999, input: { events: [{ target: "slot", name: "Undo" }] } }] });
  assert.equal(field(undone, "slot", "GameHealth", "current"), 0, "a sparse 20-minute scenario can undo a refit");
  request("runScenario", { kind: "scene", name: "Refit", frames: 3,
    input_timeline: [{ frame: 2, input: {} }, { frame: 1, input: {} }] }, true);
  request("captureRuntimeSnapshot", { kind: "scene", name: "Refit", frames: 1,
    inputs: [{ ui: [{ ...fit, target: "wall" }] }] }, true);
  create("prototype", "FogSettings", [
    { op: "add_component", component: "GameTransform" },
    { op: "add_component", component: "GameCamera", fields: { width: 200, height: 120 } },
    { op: "add_component", component: "GameHRC2D", fields: { ambient: 0.8, intensity: 1, fog: 0.03 } },
    { op: "add_component", component: "GameFog", fields: { origin: { x: -100, y: -60 }, columns: 40, rows: 24, cell_size: 5 } },
  ]);
  create("prototype", "Observer", [
    { op: "add_component", component: "GameTransform", fields: { x: -50, y: 0 } },
    { op: "add_component", component: "GameVision", fields: { radius: 75, occluders: 8, remember: true } },
    { op: "add_component", component: "GameAppearance", fields: { width: 14, height: 10, color: 0x73E7F5FF, shape: 3 } },
    { op: "add_component", component: "GameMaterial", fields: { scene: 0x73E7F5FF } },
    { op: "add_component", component: "GameGeometry3D", fields: { primitive: 7, width: 14, depth: 10, height: 6, elevation: 3 } },
    { op: "add_component", component: "GameActionBinding", fields: { source: 1, action: "Fire", firing: true } },
    { op: "add_component", component: "GameInput" }, { op: "add_component", component: "GameAim" },
  ]);
  const visibilityScene = [
    { op: "add_entity", name: "fog", prototype: "FogSettings" },
    { op: "add_entity", name: "observer", prototype: "Observer" },
    { op: "add_entity", name: "wall", prototype: "Wall", components: {
      GameTransform: { x: 0, y: 0 }, GameCollider: { shape: 1, width: 8, height: 100, category: 8, mask: 0 },
      GameAppearance: { width: 8, height: 100, color: 0xF2BF71FF, shape: 0 },
      GameMaterial: { scene: 0xF2BF71FF }, GameGeometry3D: { width: 8, depth: 100, height: 12 },
    } },
    { op: "add_entity", name: "hidden contact", prototype: "Observer", components: {
      GameTransform: { x: 30, y: 0 }, GameVision: { disabled: true }, GameActionBinding: { disabled: true },
      GameMaterial: { scene: 0xFF2020FF }, GameRenderState: { visibility_culled: true }, GameAppearance: { width: 20, height: 20, color: 0xFF2020FF, shape: 1 },
      GameSensorContact: { position: { x: 30, y: 0 }, remaining: 2, color: 0xFFC66DFF },
    } },
  ];
  create("scene", "VisibilityLab", visibilityScene);
  create("scene", "VisibilityLab3D", [{ op: "set_presentation", presentation: "3d" }, ...visibilityScene]);
  const sight = request("captureRuntimeSnapshot", { kind: "scene", name: "VisibilityLab", frames: 2,
    inputs: [{ aim: { x: 1, y: 0 }, controls: [1] }, { controls: [] }] });
  assert.equal(field(sight, "observer", "GameAim", "firing"), false);
  assert.ok(sight.visibility.visible_cells > 0 && sight.visibility.visible_cells < 600);
  assert.equal(sight.visibility.visible_cells, sight.visibility.explored_cells);
  const firing = request("captureRuntimeSnapshot", { kind: "scene", name: "VisibilityLab", frames: 1,
    inputs: [{ aim: { x: 1, y: 0 }, controls: [1] }] });
  assert.equal(field(firing, "observer", "GameAim", "firing"), true);
  if (process.env.CHIBI_MCP_GPU === "1") {
    for (const name of ["VisibilityLab", "VisibilityLab3D", "Refit", "Dock"]) {
      const preview = request("previewScene", { kind: "scene", name, width: 960, height: 540 });
      assert.equal(preview.preview.width, 960);
      assert.equal(preview.preview.height, 540);
    }
  }
  const banked = request("captureRuntimeSnapshot", { kind: "scene", name: "Dock", frames: 1,
    inputs: [{ ui: [{ target: "bank", element: "#extract", event: "Extract" }] }] });
  assert.deepEqual(banked.saved_state, { "test.blueprint": "ricochet" });
  const nextRun = request("captureRuntimeSnapshot", { kind: "scene", name: "Dock", frames: 1, saved_state: banked.saved_state });
  assert.equal(field(nextRun, "bank", "GameHealth", "current"), 77, "a committed unlock survives a fresh run");
  const unsecured = request("captureRuntimeSnapshot", { kind: "scene", name: "Dock", frames: 1,
    inputs: [{ events: [{ name: "Stage", target: "bank" }] }] });
  assert.deepEqual(unsecured.saved_state, {}, "uncommitted discoveries do not survive a run");
  const dockRecord = request("recordScenario", { kind: "scene", name: "Dock", frames: 2,
    saved_state: banked.saved_state, inputs: [{ ui: [{ target: "bank", element: "#extract", event: "Extract" }] }] });
  assert.equal(request("replayScenario", { recording_path: dockRecord.recording_path }).deterministic, true);
  const inputs = [{ movement: { x: 1, y: 0 }, aim: { x: 100, y: 20, point: true },
    events: [{ name: "Award", target: "ship", values: [{ type: "int", value: 42 }] }] }];
  const snapshot = request("captureRuntimeSnapshot", { kind: "scene", name: "Route", frames: 1, inputs });
  assert.equal(snapshot.live_entities, 4, "a prototype absent from the scene must still spawn");
  assert.equal(field(snapshot, "ship", "GameHealth", "current"), 42);
  assert.deepEqual(field(snapshot, "ship", "GameInput", "move"), { x: 1, y: 0 });
  if (process.env.CHIBI_MCP_GPU === "1") {
    // An event after preparation must execute during rendered frames. Frozen
    // rendering of that endpoint must leave the complete runtime state alone.
    const timeline = [
      { frame: 0, input: { events: [{ name: "Award", target: "ship", values: [{ type: "int", value: 11 }] }] } },
      { frame: 3, input: { events: [{ name: "Award", target: "ship", values: [{ type: "int", value: 42 }] }] } },
    ];
    const expected = request("captureRuntimeSnapshot", { kind: "scene", name: "Route", frames: 20, input_timeline: timeline });
    assert.equal(field(expected, "ship", "GameHealth", "current"), 42);
    const live = request("previewScene", { kind: "scene", name: "Route", frames: 2, render_frames: 18,
      width: 320, height: 180, input_timeline: timeline });
    assert.equal(live.initial_state.frame, 2);
    assert.equal(live.final_state.frame, 20);
    assert.equal(live.final_state.world_hash, expected.world_hash);
    assert.notEqual(live.initial_state.world_hash, live.final_state.world_hash);
    const frozen = request("previewScene", { kind: "scene", name: "Route", frames: 20, render_frames: 18,
      width: 320, height: 180, input_timeline: timeline, freeze_simulation: true });
    assert.deepEqual(frozen.initial_state, frozen.final_state);
    assert.equal(frozen.final_state.world_hash, expected.world_hash);
    assert.equal(frozen.performance.simulation_average_ms, 0);
    request("previewScene", { kind: "scene", name: "Route", frames: 2, render_frames: 18,
      freeze_simulation: true, input_timeline: timeline }, true);
    request("previewScene", { kind: "scene", name: "Route", metal_capture: "/tmp/invalid.txt" }, true);
    request("previewScene", { kind: "scene", name: "Route", capture_frame: 0 }, true);
    console.log("PASS: rendered input timeline matches headless replay; frozen state is unchanged; invalid capture requests fail");
  }
  const probe = request("captureRuntimeSnapshot", { kind: "scene", name: "Route", frames: 1,
    inputs: [{ events: [{ name: "Probe", target: "ship" }] }] });
  assert.equal(field(probe, "ship", "GameHealth", "current"), 9, "raycast branch reads its bound reward variable");
  const record = request("recordScenario", { kind: "scene", name: "Route", frames: 2, inputs });
  const replay = request("replayScenario", { recording_path: record.recording_path });
  assert.equal(replay.deterministic, true);
  const payload = request("readAsset", { kind: "prototype", name: "Payload" });
  request("buildPrototype", { kind: "prototype", name: "Payload", expected_hash: payload.hash,
    operations: [{ op: "set_field", component: "GameHealth", field: "current", value: 2 }] });
  request("replayScenario", { recording_path: record.recording_path }, true);
  assert.equal(request("runScenario", { kind: "prototype", name: "Controller", frames: 1 }).live_entities, 2);
  for (const input of [{ movement: { x: 2, y: 0 } }, { movement: { x: 0 } },
    { events: [{ name: "Award", target: "ship", values: [{ type: "int", value: "bad" }] }] }]) {
    request("captureRuntimeSnapshot", { kind: "scene", name: "Route", frames: 1, inputs: [input] }, true);
  }
  request("captureRuntimeSnapshot", { kind: "scene", name: "Route", frames: 1,
    inputs: [{ events: [{ name: "Award", target: "missing" }] }] }, true);
  // Component nodes: Break Struct splits a Get, Set writes only the field
  // inputs it names, Has reads another component and Add attaches one; each
  // lowers when the scene runs.
  assert.equal(request("searchNodes", { query: "Set Component" }).nodes[0].type, "Set Component");
  // A query names a configured node: the type and setting add_node takes.
  const breakExperience = request("searchNodes", { query: "break experience" }).nodes[0];
  assert.deepEqual([breakExperience.type, breakExperience.inputs], ["Break Struct", { Type: "GameExperience" }]);
  create("behavior", "Salvage", [
    { op: "add_node", type: "Event", event_name: "Begin", ref: "begin" },
    { op: "add_node", type: "Get Component", ref: "haul", inputs: { Component: "GameExperience" } },
    { op: "add_node", type: "Break Struct", ref: "cargo", inputs: { Type: "GameExperience" } },
    connect("haul", "Value", "cargo", "GameExperience"),
    { op: "add_node", type: "Set Component", ref: "condition", inputs: { Component: "GameHealth" } },
    connect("begin", "Then", "condition", "Exec", "exec"),
    connect("cargo", "current", "condition", "current"),
    { op: "add_node", type: "Has Component", ref: "aimed", inputs: { Component: "GameAim" } },
    { op: "add_node", type: "Branch", ref: "armed" },
    { op: "add_node", type: "Set Component", ref: "capacity", inputs: { Component: "GameHealth", maximum: 250 } },
    connect("condition", "Then", "armed", "Exec", "exec"),
    connect("aimed", "Value", "armed", "Condition"),
    connect("armed", "True", "capacity", "Exec", "exec"),
    { op: "add_node", type: "Add Component", ref: "boost", inputs: { Component: "GameDash" } },
    connect("capacity", "Then", "boost", "Exec", "exec"),
  ], { owner_type: "ecs.entity" });
  create("prototype", "Hauler", [
    { op: "add_component", component: "GameExperience", fields: { current: 12 } },
    { op: "add_component", component: "GameHealth", fields: { current: 1, maximum: 100 } },
    { op: "add_component", component: "GameAim" },
    { op: "add_behavior", behavior: "Salvage" },
  ]);
  create("scene", "Salvage", [{ op: "add_entity", name: "hauler", prototype: "Hauler", behaviors: ["Salvage"] }]);
  const salvaged = request("captureRuntimeSnapshot", { kind: "scene", name: "Salvage", frames: 1 });
  assert.equal(field(salvaged, "hauler", "GameHealth", "current"), 12, "Set Component writes the field Get Component exposes");
  assert.equal(field(salvaged, "hauler", "GameHealth", "maximum"), 250, "Has Component gates the second write");
  const hauler = salvaged.entities.find(item => item.name === "hauler");
  assert.ok(hauler.components.some(item => item.name === "GameDash"), "Add Component attaches Dash");
  console.log("MCP design smoke passed: graph authoring, spawned dependencies, controls, refit/undo, saved progression, sound, visibility, component nodes, 20-minute timeline and replay");
} finally {
  if (process.env.CHIBI_KEEP_DESIGN_FIXTURE === "1") {
    console.error(`Design fixture: ${project}`);
  } else {
    rmSync(project, { recursive: true, force: true });
  }
}
