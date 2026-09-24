#!/usr/bin/env node
import assert from "node:assert/strict";
import { mkdtempSync, mkdirSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { resolve, join } from "node:path";
import { spawnSync } from "node:child_process";
import { pngPixels } from "./png-check.mjs";

const root = resolve(process.argv[2] || ".");
const project = mkdtempSync(join(tmpdir(), "chibi-material-mcp-"));
const controller = process.env.CHIBI_CTL || join(root, "build/chibi_ctl");
for (const family of ["fields", "systems", "libraries"]) {
  mkdirSync(join(project, "content", family), { recursive: true });
}
function request(name, args, fail = false) {
  const run = spawnSync(process.execPath, [join(root, "tools/mcp/server.mjs")], {
    cwd: root, env: { ...process.env, CHIBI_ROOT: project, CHIBI_CTL: controller },
    input: `${JSON.stringify({ jsonrpc: "2.0", id: 1, method: "tools/call", params: { name, arguments: args } })}\n`,
    encoding: "utf8", maxBuffer: 16 * 1024 * 1024,
  });
  assert.equal(run.status, 0, run.stderr);
  const response = JSON.parse(run.stdout.trim());
  assert.equal(Boolean(response.error || response.result?.isError), fail, JSON.stringify(response));
  return response.result;
}
const result = (name, args, fail = false) => request(name, args, fail)?.structuredContent?.result;
const connect = (from, from_pin, to, to_pin, kind = "data") => ({ op: "connect", from, from_pin, to, to_pin, kind });
try {
  const field = { kind: "field", name: "Disc" };
  result("buildGraph", { ...field, create: true, operations: [
    { op: "add_node", type: "Field X", ref: "x" },
    { op: "add_node", type: "Field Y", ref: "y" },
    { op: "add_node", type: "Field Circle", ref: "disc", inputs: { Radius: 12 } },
    { op: "add_node", type: "Field Output", ref: "out" },
    connect("x", "Value", "disc", "X"), connect("y", "Value", "disc", "Y"),
    connect("disc", "Value", "out", "Value"),
  ] });
  assert.equal(result("validateAsset", field).valid, true);
  const before = result("readAsset", field);
  const preview = request("previewField", { ...field, width: 64, height: 64, cell_size: 1, seed: 1729 });
  const image = preview.content.find(item => item.type === "image");
  assert.ok(image, "field previews must return an MCP image");
  const png = pngPixels(Buffer.from(image.data, "base64"));
  assert.equal(png.width, 64); assert.equal(png.height, 64);
  assert.ok(png.at(32, 32)[1] > png.at(0, 0)[1]);
  const again = result("previewField", { ...field, width: 64, height: 64, cell_size: 1, seed: 1729 });
  assert.equal(again.field_hash, preview.structuredContent.result.field_hash);
  result("buildGraph", { ...field, expected_hash: before.hash, operations: [
    { op: "add_node", type: "Print String", ref: "impure" },
  ] }, true);
  assert.equal(result("readAsset", field).hash, before.hash, "invalid edits must be atomic");
  const system = { kind: "system", name: "Move" };
  const query = { op: "has", component: "GameTransform" };
  result("buildGraph", { ...system, create: true, owner_type: "ecs.entity", query, operations: [
    { op: "add_node", type: "Event", event_name: "Tick", ref: "tick" },
    { op: "add_node", type: "Set_GameTransform", ref: "set", inputs: { Value: { x: 3, y: 4 } } },
    connect("tick", "Then", "set", "Exec", "exec"),
  ] });
  assert.equal(result("validateAsset", system).valid, true);
  const inspected = result("inspectSystem", system);
  assert.ok(inspected, "standalone systems must be inspectable");
  const status = result("getExecutionStatus", system);
  assert.equal(status.execution.ready, true);
  const saved = result("readAsset", system);
  result("buildGraph", { ...system, expected_hash: saved.hash, query: { op: "has", component: "GameVelocity" }, operations: [] });
  assert.notEqual(result("readAsset", system).hash, saved.hash);
  console.log("PASS: MCP field creation, atomic validation, deterministic PNG preview, System creation/query/inspection");
} finally {
  rmSync(project, { recursive: true, force: true });
}
