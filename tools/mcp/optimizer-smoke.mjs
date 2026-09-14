#!/usr/bin/env node
import assert from "node:assert/strict";
import { mkdtempSync, mkdirSync, copyFileSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { resolve, join } from "node:path";
import { spawnSync } from "node:child_process";

const root = resolve(process.argv[2] || process.cwd());
const project = mkdtempSync(join(tmpdir(), "chibi-optimize-"));
function rpc(name, args) {
  const requests = [
    { jsonrpc: "2.0", id: 1, method: "initialize", params: { protocolVersion: "2025-06-18", capabilities: {}, clientInfo: { name: "optimizer-smoke", version: "1" } } },
    { jsonrpc: "2.0", id: 2, method: "tools/call", params: { name, arguments: args } },
  ];
  const run = spawnSync(process.execPath, [join(root, "tools/mcp/server.mjs")], {
    cwd: project,
    env: { ...process.env, CHIBI_ROOT: project, CHIBI_CTL: process.env.CHIBI_CTL || join(root, "build/chibi_ctl"), MallocNanoZone: "0", ASAN_OPTIONS: "detect_leaks=0" },
    input: requests.map(r => JSON.stringify(r)).join("\n") + "\n",
    encoding: "utf8", maxBuffer: 4 * 1024 * 1024,
  });
  assert.equal(run.status, 0, run.stderr);
  const response = run.stdout.trim().split("\n").map(JSON.parse).find(r => r.id === 2);
  assert(response, run.stdout);
  return response;
}
try {
  mkdirSync(join(project, "content/behaviors"), { recursive: true });
  for (const name of ["WavePulseBehavior", "ArenaShardBehavior"]) {
    const file = `content/behaviors/${name}.json`;
    copyFileSync(join(root, file), join(project, file));
    const before = readFileSync(join(project, file), "utf8");
    const dry = rpc("optimizeGraph", { path: file }).result?.structuredContent;
    assert(dry?.ok, JSON.stringify(dry));
    assert.equal(dry.result.dry_run, true);
    if (name === "WavePulseBehavior") assert(dry.result.created_aliases >= 1);
    assert(dry.result.folded_inputs >= 1);
    assert(Array.isArray(dry.result.skipped));
    assert.equal(readFileSync(join(project, file), "utf8"), before);
    const denied = rpc("optimizeGraph", { path: file, dry_run: false });
    assert.equal(denied.error?.code, -32602);
    const applied = rpc("optimizeGraph", { path: file, dry_run: false, expected_hash: dry.result.before_hash }).result?.structuredContent;
    assert(applied?.ok && applied.result.published, JSON.stringify(applied));
    assert.equal(applied.result.after_hash, dry.result.after_hash);
    const after = readFileSync(join(project, file), "utf8");
    assert.notEqual(after, before);
    const document = JSON.parse(after);
    if (name === "WavePulseBehavior") assert(document.graphs.some(g => g.locals?.length));
    if (name === "ArenaShardBehavior") {
      const graph = document.graphs[0];
      const age = document.variables.find(v => v.name === "Age");
      const setter = graph.nodes.find(n => n.type === "Set Variable" && n.reference_id === age.id);
      assert(setter, "Optimize must retain the Age assignment");
      assert(!graph.edges.some(e => e.from === setter.id && e.kind === "data"));
      assert(graph.nodes.some(n => n.type === "Self" && n.inline_source));
      assert(graph.nodes.flatMap(n => n.inputs).filter(p =>
        p.binding?.kind === "variable" && p.binding.variable_id === age.id).length >= 3);
    }
    assert.equal(document.variables.length, JSON.parse(before).variables.length);
    const noop = rpc("optimizeGraph", { path: file, dry_run: false, expected_hash: applied.result.after_hash }).result?.structuredContent;
    assert(noop?.ok && !noop.result.changed);
    assert.equal(readFileSync(join(project, file), "utf8"), after);
    const conflict = rpc("optimizeGraph", { path: file, dry_run: false, expected_hash: dry.result.before_hash }).result?.structuredContent;
    assert.equal(conflict?.ok, false);
    assert.equal(readFileSync(join(project, file), "utf8"), after);
    console.log(`PASS: ${name} MCP optimizer dry run, report, apply, no-op, persistent-state exclusion, and stale-hash rejection`);
  }
} finally {
  rmSync(project, { recursive: true, force: true });
}
