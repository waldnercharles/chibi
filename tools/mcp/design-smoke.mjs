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
for (const family of ["behaviors", "events", "prototypes", "scenes"]) {
  mkdirSync(join(project, "content", family), { recursive: true });
}

function request(name, args, expectError = false) {
  const run = spawnSync(process.execPath, [join(root, "tools/mcp/server.mjs")], {
    cwd: root,
    env: { ...process.env, CHIBI_ROOT: project, CHIBI_CTL: controller },
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
    if (args.name === "Dock") assert.ok(decoded.at(425, 265).slice(0, 3).some(value => value > 15), "dock button must be visible in the image center");
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
      assert(decoded.at(800, 450)[1] > 30, "explored map terrain must be in the captured UI");
    }
  }
  return response.result?.structuredContent?.result || response;
}

function create(kind, name, operations, extra = {}) {
  const tool = kind === "prototype"
    ? "buildPrototype"
    : kind === "scene"
      ? "buildScene"
      : ["material", "shape", "shader-library"].includes(kind)
        ? "buildShader"
        : "buildBehavior";
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
  assert.equal(request("searchNodes", { query: "Raycast" }).nodes[0].name, "Raycast");
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
  create("event", "WidgetAction", [
    { op: "add_variable", name: "Source", type: "handle", type_name: "ecs.entity" },
    { op: "add_variable", name: "Value", type: "float", default: 0 },
  ]);
  create("behavior", "Banking", [
    { op: "add_node", type: "Event", event_name: "WidgetAction", ref: "click" },
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
    { op: "add_component", component: "GameWidget", fields: {
      kind: 2, text: "Extract", event: "WidgetAction", size: { x: 180, y: 44 },
      anchor: { x: 0.5, y: 0.5 }, pivot: { x: 0.5, y: 0.5 }, rounding: 8,
    } },
    { op: "add_behavior", behavior: "Banking" },
  ]);
  create("scene", "Dock", [{ op: "add_entity", name: "bank", prototype: "Bank", behaviors: ["Banking"] }]);
  create("behavior", "RecipeSlot", [
    { op: "add_node", type: "Event", event_name: "WidgetAction", ref: "drop" },
    { op: "add_node", type: "Set_GameWidget_text", ref: "label", inputs: { Value: "Ricochet fitted" } },
    connect("drop", "Then", "label", "Exec", "exec"),
    { op: "add_node", type: "Set_GameWidget_value", ref: "part" },
    connect("label", "Then", "part", "Exec", "exec"),
    connect("drop", "Value", "part", "Value"),
    { op: "add_node", type: "Play Sound", ref: "sound", inputs: { Frequency: 600, "End Frequency": 900, Duration: 0.12, Volume: 0.3 } },
    connect("part", "Then", "sound", "Exec", "exec"),
    { op: "add_node", type: "Event", event_name: "Undo", ref: "undo" },
    { op: "add_node", type: "Set_GameWidget_text", ref: "restored", inputs: { Value: "Empty modifier slot" } },
    { op: "add_node", type: "Set_GameWidget_value", ref: "clear", inputs: { Value: 0 } },
    connect("undo", "Then", "restored", "Exec", "exec"),
    connect("restored", "Then", "clear", "Exec", "exec"),
  ], { owner_type: "ecs.entity" });
  create("prototype", "PartSlot", [
    { op: "add_component", component: "GameWidget", fields: {
      kind: 5, event: "WidgetAction", text: "Empty modifier slot", drop_group: "modifier", size: { x: 220, y: 50 },
      offset: { x: 24, y: 24 }, rounding: 8,
    } }, { op: "add_behavior", behavior: "RecipeSlot" },
  ]);
  create("scene", "Refit", [
    { op: "add_entity", name: "slot", prototype: "PartSlot" },
    { op: "add_entity", name: "ricochet", prototype: "PartSlot", components: { GameWidget: {
      kind: 5, event: "WidgetAction", drop_group: "modifier", draggable: true, value: 2, text: "Ricochet x2",
      size: { x: 220, y: 50 }, offset: { x: 24, y: 100 }, rounding: 8,
    } } },
  ]);
  const fitted = request("captureRuntimeSnapshot", { kind: "scene", name: "Refit", frames: 2,
    inputs: [{ ui: [{ target: "slot", action: "drop", source: "ricochet" }] }] });
  assert.equal(field(fitted, "slot", "GameWidget", "text"), "Ricochet fitted");
  assert.equal(field(fitted, "slot", "GameWidget", "value"), 2);
  assert.equal(fitted.sound_cues, 1, "a successful refit produces its authored sound cue");
  const undone = request("captureRuntimeSnapshot", { kind: "scene", name: "Refit", frames: 72000,
    input_timeline: [{ frame: 0, input: { ui: [{ target: "slot", action: "drop", source: "ricochet" }] } },
      { frame: 71999, input: { events: [{ target: "slot", name: "Undo" }] } }] });
  assert.equal(field(undone, "slot", "GameWidget", "value"), 0, "a sparse 20-minute scenario can undo a refit");
  assert.equal(field(undone, "slot", "GameWidget", "text"), "Empty modifier slot");
  request("runScenario", { kind: "scene", name: "Refit", frames: 3,
    input_timeline: [{ frame: 2, input: {} }, { frame: 1, input: {} }] }, true);
  request("captureRuntimeSnapshot", { kind: "scene", name: "Refit", frames: 1,
    inputs: [{ ui: [{ target: "slot", action: "drop", source: "slot" }] }] }, true);
  create("prototype", "FogSettings", [
    { op: "add_component", component: "GameTransform" },
    { op: "add_component", component: "GameCamera", fields: { width: 200, height: 120 } },
    { op: "add_component", component: "GameLighting2D", fields: { ambient: 0.8, emission: 1, fog: 0.03 } },
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
  create("prototype", "Map", [{ op: "add_component", component: "GameWidget", fields: {
    kind: 6, anchor: { x: 1, y: 1 }, pivot: { x: 1, y: 1 }, offset: { x: -16, y: -16 }, size: { x: 240, y: 144 },
  } }, { op: "add_component", component: "GameMapMarker", fields: { position: { x: -70, y: -35 }, label: "Dock", always_visible: true, color: 0x8AF2DDFF } }]);
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
    { op: "add_entity", name: "map", prototype: "Map" },
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
    inputs: [{ ui: [{ target: "bank", action: "activate" }] }] });
  assert.deepEqual(banked.saved_state, { "test.blueprint": "ricochet" });
  const nextRun = request("captureRuntimeSnapshot", { kind: "scene", name: "Dock", frames: 1, saved_state: banked.saved_state });
  assert.equal(field(nextRun, "bank", "GameHealth", "current"), 77, "a committed unlock survives a fresh run");
  const unsecured = request("captureRuntimeSnapshot", { kind: "scene", name: "Dock", frames: 1,
    inputs: [{ events: [{ name: "Stage", target: "bank" }] }] });
  assert.deepEqual(unsecured.saved_state, {}, "uncommitted discoveries do not survive a run");
  const dockRecord = request("recordScenario", { kind: "scene", name: "Dock", frames: 2,
    saved_state: banked.saved_state, inputs: [{ ui: [{ target: "bank", action: "activate" }] }] });
  assert.equal(request("replayScenario", { recording_path: dockRecord.recording_path }).deterministic, true);
  const inputs = [{ movement: { x: 1, y: 0 }, aim: { x: 100, y: 20, point: true },
    events: [{ name: "Award", target: "ship", values: [{ type: "int", value: 42 }] }] }];
  const snapshot = request("captureRuntimeSnapshot", { kind: "scene", name: "Route", frames: 1, inputs });
  assert.equal(snapshot.live_entities, 4, "a prototype absent from the scene must still spawn");
  assert.equal(field(snapshot, "ship", "GameHealth", "current"), 42);
  assert.deepEqual(field(snapshot, "ship", "GameInput", "move"), { x: 1, y: 0 });
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
  console.log("MCP design smoke passed: graph authoring, spawned dependencies, controls, refit/undo, saved progression, sound, visibility, 20-minute timeline and replay");
} finally {
  if (process.env.CHIBI_KEEP_DESIGN_FIXTURE === "1") {
    console.error(`Design fixture: ${project}`);
  } else {
    rmSync(project, { recursive: true, force: true });
  }
}
