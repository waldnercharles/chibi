#!/usr/bin/env node
// Exercise public authoring and refactoring against a disposable saved project.
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { cpSync, existsSync, mkdirSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs";
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
const model = call("inspectGraph", { kind: "model", name: "EngineModule" });
assert(model.model_compilation.valid, "Inspection compiles the current saved model");
assert(model.model_compilation.source_node_count > 0);
assert(model.model_compilation.expanded_shader_node_count > 0 && model.model_compilation.expanded_shader_node_count <= 1024);
assert(model.model_compilation.public_parameter_slots > 0);
assert(model.model_compilation.compile_ms >= 0);
assert(model.model_compilation.parts.some(part => part.name && part.path && part.node));
assert.equal(model.model_compilation.bounds.empty, false);
const radius = model.model_parameters.find(p => p.name === "Radius");
assert(radius, "Public uniform metadata is discoverable");
assert.equal(radius.runtime, true);
assert.equal(radius.width, 1);
assert.deepEqual(radius.minimum, [9]);
assert.deepEqual(radius.maximum, [24]);
assert(radius.id > 0 && radius.slot >= 0, "Uniform exposes stable declaration identity and slot");
function prototype(name, parameters, failure = false) {
  return call("buildPrototype", { kind: "prototype", name, create: true, operations: [
    { op: "add_component", component: "GameShader", fields: { shape: "EngineModule", parameters } },
  ] }, failure);
}
prototype("ValidUniform", "Radius 20\n");
assert(call("validateAsset", { kind: "prototype", name: "ValidUniform" }).valid);
for (const [name, text, expected] of [
  ["RangeViolation", "Radius 1000\n", "Radius"],
  ["WidthViolation", "(array)Radius 12 13\n", "Radius"],
  ["UnknownUniform", "MissingDimension 12\n", "MissingDimension"],
]) {
  const failed = prototype(name, text, true);
  assert(JSON.stringify(failed).includes(expected), "Invalid override identifies its public name");
  assert(!existsSync(resolve(project, `content/prototypes/${name}.kdl`)), "Rejected prototype was not published");
}
call("buildScene", { kind: "scene", name: "InvalidSceneOverride", create: true, operations: [
  { op: "add_entity", name: "Invalid instance", prototype: "ValidUniform", components: {
    GameShader: { parameters: "Radius 1000\n" },
  } },
] }, true);
assert(!existsSync(resolve(project, "content/scenes/InvalidSceneOverride.kdl")), "Rejected scene was not published");
writeFileSync(resolve(evidence, "stage4-authoring-overrides.json"), JSON.stringify({ project, passed: true, results }, null, 2));
console.log(`Model uniform metadata and authored prototype/scene override rejection: PASS (${project})`);
