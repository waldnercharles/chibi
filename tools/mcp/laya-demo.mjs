#!/usr/bin/env node
// A bounded, reproducible authoring experiment. Run only against a new lab directory.
import { mkdirSync, existsSync, writeFileSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { dirname, resolve, join } from "node:path";
import { fileURLToPath } from "node:url";
import { chooseEditorAction } from "./laya.mjs";

const repo = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
const project = process.argv[2] && resolve(process.argv[2]);
const name = process.argv[3];
const reviewed = process.argv.includes("--reviewed-blue-circle");
if (!project || !name || !/^[A-Za-z][A-Za-z0-9_]{0,63}$/.test(name) || existsSync(project)) {
  console.error("Usage: node tools/mcp/laya-demo.mjs NEW_PROJECT_DIRECTORY ASSET_NAME [--reviewed-blue-circle]");
  process.exit(1);
}

mkdirSync(project, { recursive: true });
for (const family of ["materials", "shapes", "prototypes", "scenes"]) mkdirSync(join(project, "content", family), { recursive: true });
writeFileSync(join(project, "project.json"), JSON.stringify({ version: 1, name: "Laya lab", content: "content", start_scene: "scenes/LayaLab.json" }, null, 2));
const trace = { goal: "Create a blue circle marker.", decisions: [], authoring: [], completed: false };

function call(tool, args = {}) {
  const request = { jsonrpc: "2.0", id: 1, method: "tools/call", params: { name: tool, arguments: args } };
  const child = spawnSync(process.execPath, [join(repo, "tools/mcp/server.mjs")], {
    env: { ...process.env, CHIBI_ROOT: project, CHIBI_CTL: process.env.CHIBI_CTL || join(repo, "build/chibi_ctl") },
    input: JSON.stringify(request) + "\n", encoding: "utf8", timeout: 45000, maxBuffer: 32 * 1024 * 1024,
  });
  if (child.error) throw child.error;
  const message = child.stdout.trim().split("\n").filter(Boolean).map((line) => JSON.parse(line)).find((item) => item.id === 1);
  const result = message?.result?.structuredContent;
  if (!result?.ok) throw new Error(`${tool}: ${JSON.stringify(message?.error || result || child.stderr)}`);
  trace.authoring.push({ tool, args, result });
  return result.result;
}

async function choose(observation, actions) {
  const request = { goal: trace.goal, observation, actions };
  const result = (await chooseEditorAction(request)).result;
  const record = { request, result };
  trace.decisions.push(record);
  if (result.status === "needs_supervisor") {
    if (!reviewed || !["blue", "circle"].includes(result.action_id)) {
      throw new Error(`Supervisor review needed: ${result.action_id} (${result.confidence}). See laya-run.json.`);
    }
    record.supervisor = "Codex reviewed this blue/circle choice against the stated goal before authorizing this demo.";
  }
  return result;
}

function publish(tool, args) {
  call(tool, { ...args, dry_run: true });
  call(tool, args);
  const valid = call("validateAsset", { kind: args.kind, name: args.name });
  if (valid.valid !== true) throw new Error(`Validation failed for ${args.name}`);
}

try {
  const status = call("getProjectStatus");
  if (resolve(status.project_root) !== project) throw new Error("Wrong project root");
  call("searchAssets", { query: name });
  call("searchNodes", { query: "Shader Surface Output", limit: 1 });
  call("searchNodes", { query: "Shader Circle", limit: 1 });
  call("searchNodes", { query: "Shader Position", limit: 1 });
  call("describeComponents", { names: ["GameTransform", "GameAppearance", "GameShader", "GameCamera"] });
  const naming = await choose("A new asset needs a name. The name field is empty.", [
    { id: "name", label: "Enter an asset name", needs_text: true }, { id: "cancel", label: "Cancel asset creation" },
  ]);
  if (naming.status !== "needs_text") throw new Error("Laya did not request text");
  trace.decisions.at(-1).text_from_codex = name;
  const color = await choose("Choose the color for the marker.", [{ id: "blue", label: "Blue" }, { id: "red", label: "Red" }]);
  if (color.action_id !== "blue") throw new Error("Laya chose the wrong color");
  publish("buildShader", { kind: "material", name: `${name}Material`, create: true, operations: [
    { op: "set_input", node: "output", pin: "Base Color", value: { x: 0.08, y: 0.4, z: 1 } },
    { op: "set_input", node: "output", pin: "Emission", value: { x: 0.04, y: 0.2, z: 0.5 } },
    { op: "set_input", node: "output", pin: "Unlit", value: true },
  ] });
  const shape = await choose("Choose the shape for the marker.", [{ id: "circle", label: "Circle" }, { id: "square", label: "Square" }]);
  if (shape.action_id !== "circle") throw new Error("Laya chose the wrong shape");
  publish("buildShader", { kind: "shape", name: `${name}Shape`, create: true, operations: [
    { op: "set_bounds", value: [32, 32] },
    { op: "add_node", type: "Shader Position", ref: "position" },
    { op: "add_node", type: "Shader Circle", ref: "circle", inputs: { Radius: 24 } },
    { op: "connect", from: "position", from_pin: "Position", to: "circle", to_pin: "Position", kind: "data" },
    { op: "connect", from: "circle", from_pin: "Value", to: "output", to_pin: "Distance", kind: "data" },
  ] });
  publish("buildPrototype", { kind: "prototype", name, create: true, operations: [
    { op: "add_component", component: "GameTransform" },
    { op: "add_component", component: "GameAppearance", fields: { width: 48, height: 48, shape: 1, color: 4294967295, antialias: 1 } },
    { op: "add_component", component: "GameShader", fields: { material: `${name}Material` } },
  ] });
  publish("buildPrototype", { kind: "prototype", name: "LayaLabCamera", create: true, operations: [
    { op: "add_component", component: "GameTransform" },
    { op: "add_component", component: "GameCamera", fields: { width: 160, height: 160 } },
  ] });
  publish("buildScene", { kind: "scene", name: "LayaLab", create: true, operations: [
    { op: "set_presentation", presentation: "2d" },
    { op: "add_entity", name: "Marker", prototype: name },
    { op: "add_entity", name: "Camera", prototype: "LayaLabCamera" },
  ] });
  trace.runtime = call("runScenario", { kind: "scene", name: "LayaLab", frames: 30 });
  trace.completed = true;
  console.log(JSON.stringify({ project, completed: true, decisions: trace.decisions.map((x) => x.result), runtime: trace.runtime }, null, 2));
} catch (error) {
  trace.error = error.message;
  console.error(error.message);
  process.exitCode = 1;
} finally {
  writeFileSync(join(project, "laya-run.json"), JSON.stringify(trace, null, 2));
}
