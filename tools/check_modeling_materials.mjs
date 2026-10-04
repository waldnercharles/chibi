#!/usr/bin/env node
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { cpSync, mkdirSync, mkdtempSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const engine = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const evidence = resolve(engine, "build/model-evidence");
mkdirSync(evidence, { recursive: true });
const project = mkdtempSync(resolve(evidence, "material-authoring-"));
cpSync(resolve(engine, "projects/modeling_lab/content"), resolve(project, "content"), { recursive: true });
cpSync(resolve(engine, "projects/modeling_lab/project.kdl"), resolve(project, "project.kdl"));
const results = [];
function call(name, args, failure = false) {
  const run = spawnSync(process.execPath, [resolve(engine, "tools/mcp/server.mjs")], {
    cwd: engine, env: { ...process.env, CHIBI_ROOT: project, CHIBI_CTL: resolve(engine, "build/chibi_ctl") },
    input: JSON.stringify({ jsonrpc: "2.0", id: 1, method: "tools/call", params: { name, arguments: args } }) + "\n",
    encoding: "utf8", maxBuffer: 64 * 1024 * 1024,
  });
  assert.equal(run.status, 0, run.stderr);
  const response = JSON.parse(run.stdout.trim());
  const failed = Boolean(response.error || response.result?.isError);
  assert.equal(failed, failure, JSON.stringify(response));
  results.push({ name, arguments: args, expected_failure: failure, ...(failure ? { response } : {}) });
  return failure ? response : response.result.structuredContent.result;
}
assert.equal(resolve(call("getProjectStatus", {}).project_root), project);
const module = call("inspectGraph", { kind: "model", name: "PaintedModule" });
const slot = module.model_material_slots.find(s => s.name === "Body");
assert(slot && slot.id > 0 && slot.slot >= 0 && slot.material === "ModelBluePaint");
assert(module.model_compilation.material_dependencies.includes("ModelBluePaint"));
assert(module.model_compilation.parts.some(p => p.material_slot === slot.slot));
const slotNode = module.graph.nodes.find(n => n.type === "Model Material Slot" && n.id === slot.id);
assert(slotNode, "Authored slot declaration is discoverable");
const before = call("readAsset", { kind: "model", name: "PaintedModule" });
call("buildGraph", { kind: "model", name: "PaintedModule", expected_hash: before.hash, operations: [
  { op: "set_node", node: slotNode.id, reference_asset: "MissingMaterial" },
] }, true);
assert.equal(call("readAsset", { kind: "model", name: "PaintedModule" }).source, before.source);
function rename(selector, new_name) {
  const args = { ...selector, new_name };
  const dry = call("renameSymbol", args);
  assert(Object.keys(dry.expected_hashes).length);
  call("renameSymbol", { ...args, dry_run: false, expected_hashes: dry.expected_hashes });
}
rename({ asset: { kind: "material", name: "ModelBluePaint" } }, "RenamedBluePaint");
assert(call("readAsset", { kind: "model", name: "PaintedModule" }).source.includes("RenamedBluePaint"));
rename({ asset: { kind: "material", name: "ModelOrangePaint" } }, "RenamedOrangePaint");
assert(call("readAsset", { kind: "model", name: "PaintedVariants" }).source.includes("RenamedOrangePaint"));
rename({ asset: { kind: "model", name: "PaintedModule" }, parameter: "Body" }, "Body Finish");
const variants = call("inspectGraph", { kind: "model", name: "PaintedVariants" });
const instance = variants.graph.nodes.find(n => n.type === "Model Instance" && n.in?.some(p => p.startsWith("Body Finish:")));
assert(instance, "Slot rename refreshes sparse override labels");
call("buildGraph", { kind: "model", name: "PaintedVariants", expected_hash: variants.hash, operations: [
  { op: "remove_pin", node: instance.id, side: "input", pin: "Body Finish" },
] });
for (const name of ["PaintedModule", "PaintedVariants"]) assert(call("validateAsset", { kind: "model", name }).valid);
const deleted = call("deleteSymbol", { asset: { kind: "material", name: "RenamedBluePaint" } }, true);
assert(JSON.stringify(deleted).includes("PaintedModule"), "Used material deletion reports its model reference");
writeFileSync(resolve(evidence, "stage6-authoring-materials.json"), JSON.stringify({ project, passed: true, results }, null, 2));
console.log(`Model material metadata, invalid assignment, rename, inheritance, and deletion guard: PASS (${project})`);
