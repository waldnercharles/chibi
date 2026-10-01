import assert from "node:assert/strict";
import { chmodSync, cpSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, resolve } from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { test } from "node:test";

const repo = resolve(dirname(fileURLToPath(import.meta.url)), "..");

test("packaged MCP pins an external workspace, preserves config, and runs without sources", () => {
  const scratch = mkdtempSync(resolve(tmpdir(), "chibi-package-"));
  try {
    const pkg = resolve(scratch, "engine package");
    const game = resolve(scratch, "game workspace");
    mkdirSync(resolve(pkg, "bin"), { recursive: true });
    mkdirSync(resolve(pkg, "share/chibi/mcp"), { recursive: true });
    mkdirSync(resolve(game, ".codex"), { recursive: true });
    for (const name of ["server.mjs", "authoring.mjs", "controller.mjs", "tool-catalog.mjs", "engine-package.mjs", "package-launch.mjs", "setup-codex.mjs"]) {
      cpSync(resolve(repo, "tools/mcp", name), resolve(pkg, "share/chibi/mcp", name));
    }
    cpSync(resolve(repo, "tools/mcp/launch-packaged.sh"), resolve(pkg, "bin/chibi-mcp"));
    chmodSync(resolve(pkg, "bin/chibi-mcp"), 0o755);
    for (const name of ["chibi", "chibi_ctl"]) {
      writeFileSync(resolve(pkg, "bin", name), "#!/bin/sh\nexit 0\n", { mode: 0o755 });
    }
    const manifestPath = resolve(pkg, "share/chibi/engine.json");
    const manifest = build_id => writeFileSync(manifestPath, JSON.stringify({ build_id, project_version: 7 }));
    manifest("first");
    writeFileSync(resolve(game, "project.kdl"), "version 7\n");
    const configPath = resolve(game, ".codex/config.toml");
    const other = 'model = "gpt-6.1-sol"\n[agents.default]\nmodel_reasoning_effort = "medium"\n[mcp_servers.other]\ncommand = "other"\n';
    const skills = '[[skills.config]] # Keep unrelated array tables after the replaced server\npath = "/my/skill"\nenabled = true\n\n[[skills.config]]\npath = "/my/second-skill"\nenabled = false\n';
    writeFileSync(configPath, `${other}[mcp_servers.chibi]\ncommand = "old"\n[mcp_servers.chibi.env]\nOLD = "1"\n${skills}`);
    const run = (args, input = "") => spawnSync("sh", [resolve(pkg, "bin/chibi-mcp"), ...args], { cwd: game, input, encoding: "utf8", timeout: 10000 });
    let result = run([]);
    assert.equal(result.status, 1);
    assert.match(result.stderr, /no engine pin/);
    result = run(["--setup", game]);
    assert.equal(result.status, 0, result.stderr);
    assert.equal(JSON.parse(readFileSync(resolve(game, ".chibi-engine.json"))).build_id, "first");
    const config = readFileSync(configPath, "utf8");
    assert.ok(config.startsWith(other));
    assert.ok(config.includes(skills));
    assert.ok(!config.includes("OLD"));
    assert.equal((config.match(/\[mcp_servers.chibi\]/g) || []).length, 1);
    assert.ok(config.includes(resolve(pkg, "bin/chibi-mcp")));
    result = run([], JSON.stringify({ jsonrpc: "2.0", id: 1, method: "initialize", params: { protocolVersion: "2025-11-25", capabilities: {}, clientInfo: { name: "package-test", version: "1" } } }) + "\n");
    assert.equal(result.status, 0, result.stderr);
    const handshake = JSON.parse(result.stdout.trim()).result;
    assert.equal(handshake.serverInfo.name, "chibi");
    assert.match(handshake.instructions, /pinned engine build first/);
    assert.match(handshake.instructions, /small reproducing project or scenario/);
    assert.match(handshake.instructions, /expected and actual behavior/);
    manifest("second");
    for (const args of [[], ["--setup", game]]) {
      result = run(args);
      assert.equal(result.status, 1);
      assert.match(result.stderr, /pins engine first; selected engine is second/);
      assert.match(result.stderr, /--update-engine/);
    }
    result = run(["--setup", game, "--update-engine"]);
    assert.equal(result.status, 0, result.stderr);
    assert.equal(JSON.parse(readFileSync(resolve(game, ".chibi-engine.json"))).build_id, "second");
    rmSync(resolve(pkg, "bin/chibi_ctl"));
    result = run([]);
    assert.equal(result.status, 1);
    assert.match(result.stderr, /chibi_ctl/);
    assert.ok(!result.stderr.includes("building native"));
  } finally {
    rmSync(scratch, { recursive: true, force: true });
  }
});
