#!/usr/bin/env node
// Exercise public authoring and refactoring against a disposable saved project.
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { cpSync, mkdirSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const engine = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const evidence = resolve(engine, "build/model-evidence");
mkdirSync(evidence, { recursive: true });
const project = mkdtempSync(resolve(evidence, "authoring-project-"));
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
  if (failure) {
    assert(failed, `${name} unexpectedly succeeded`);
    results.push({ name, expected_failure: true, response });
    return response;
  }
  assert(!failed, JSON.stringify(response));
  results.push({ name, arguments: args });
  return response.result.structuredContent.result;
}
assert.equal(resolve(call("getProjectStatus", {}).project_root), project);
function validate(name) {
  assert(call("validateAsset", { kind: "model", name }).valid, `Invalid model ${name}`);
}
function rename(selector, new_name) {
  const args = { ...selector, new_name };
  const dry = call("renameSymbol", args);
  assert(Object.keys(dry.expected_hashes).length, "Rename did not identify saved assets");
  for (const path of Object.keys(dry.expected_hashes)) {
    assert(!path.includes(".."), "Refactor path must remain in disposable project");
  }
  call("renameSymbol", { ...args, dry_run: false, expected_hashes: dry.expected_hashes });
}

const module = call("inspectGraph", { kind: "model", name: "EngineModule" });
const radius = module.graph.nodes.find(n => n.type === "Model Parameter");
assert(radius, "Public model parameter is discoverable");
call("buildGraph", { kind: "model", name: "EngineModule", expected_hash: module.hash,
  operations: [{ op: "set_input", node: radius.id, pin: "Default", value: 16 }] });
for (const name of ["EngineModule", "EngineVariants", "EngineRadialArray"]) validate(name);

const before = call("readAsset", { kind: "model", name: "EngineModule" });
call("buildGraph", { kind: "model", name: "EngineModule", expected_hash: before.hash,
  operations: [{ op: "set_input", node: radius.id, pin: "Default", value: 999 }] }, true);
assert.equal(call("readAsset", { kind: "model", name: "EngineModule" }).source, before.source,
  "Rejected invalid tool edit changed authored source");

rename({ asset: { kind: "model", name: "EngineModule" }, parameter: "Radius" }, "Outer Radius");
for (const name of ["EngineModule", "EngineVariants", "EngineRadialArray"]) validate(name);
const changed = call("readAsset", { kind: "model", name: "EngineVariants" });
assert(changed.source.includes("Outer Radius"), "Parameter rename did not refresh sparse override labels");

rename({ asset: { kind: "model", name: "EngineModule" } }, "ReusableEngine");
for (const name of ["ReusableEngine", "EngineVariants", "EngineRadialArray"]) validate(name);
assert(call("readAsset", { kind: "model", name: "EngineVariants" }).source.includes("ReusableEngine"));

const variants = call("inspectGraph", { kind: "model", name: "EngineVariants" });
const instance = variants.graph.nodes.find(n => n.type === "Model Instance" && n.in?.some(p => p.startsWith("Outer Radius:")));
assert(instance, "Sparse override input is discoverable");
call("buildGraph", { kind: "model", name: "EngineVariants", expected_hash: variants.hash,
  operations: [{ op: "remove_pin", node: instance.id, side: "input", pin: "Outer Radius" }] });
validate("EngineVariants");

const canonical = spawnSync(resolve(engine, "build/chibi_ctl"), ["--project-root", project, "--format"], { encoding: "utf8" });
assert.equal(canonical.status, 0, canonical.stderr);
const check = spawnSync(resolve(engine, "build/chibi_ctl"), ["--project-root", project, "--check"], { encoding: "utf8" });
assert.equal(check.status, 0, check.stderr);
assert.equal(readFileSync(resolve(project, "project.kdl"), "utf8").split("\n")[0], "version 5");
writeFileSync(resolve(evidence, "stage3-authoring.json"), JSON.stringify({ project, passed: true, results }, null, 2));
console.log(`Model authoring, atomic rejection, parameter and asset rename, sparse override removal, canonical reload: PASS (${project})`);
