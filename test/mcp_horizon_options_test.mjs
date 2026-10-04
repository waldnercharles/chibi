import assert from "node:assert/strict";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { resolve } from "node:path";
import { spawnSync } from "node:child_process";
import { test } from "node:test";

// Reach the real MCP validator, but use a failing stub instead of a GPU process.
test("preview overrides validate horizon booleans and analytic normal selection", () => {
  const root = resolve(import.meta.dirname, "..");
  const scratch = mkdtempSync(resolve(tmpdir(), "chibi-horizon-options-"));
  const ctl = resolve(scratch, "chibi_ctl");
  const received = resolve(scratch, "request.json");
  writeFileSync(ctl, `#!/bin/sh\ncat > '${received}'\nexit 74\n`, { mode: 0o755 });
  const request = (horizon_reuse, surface_quality, controls = {}) => {
    const args = { kind: "scene", name: "SdfShapes", ...controls };
    if (horizon_reuse !== undefined) args.horizon_reuse = horizon_reuse;
    if (surface_quality !== undefined) args.surface_quality = surface_quality;
    const child = spawnSync(process.execPath, [resolve(root, "tools/mcp/server.mjs")], {
      cwd: root,
      env: { ...process.env, CHIBI_ROOT: resolve(root, "projects/sdf_lab"), CHIBI_CTL: ctl },
      input: JSON.stringify({ jsonrpc: "2.0", id: 1, method: "tools/call", params: { name: "previewScene", arguments: args } }) + "\n",
      encoding: "utf8", timeout: 10000,
    });
    assert.equal(child.status, 0, child.stderr);
    return JSON.parse(child.stdout.trim());
  };
  try {
    for (const reuse of [null, [], false, { cache: 1 }, { reproject: "true" }, { hierarchy: null }, { unsupported: true }]) {
      const response = request(reuse);
      assert.equal(response.error?.code, -32602, JSON.stringify(response));
    }
    for (const reuse of [undefined, {}, { cache: false }, { fuse_relief: true, reproject: false, hierarchy: true }]) {
      const response = request(reuse);
      assert.equal(response.error, undefined, JSON.stringify(response));
      assert.equal(response.result?.isError, true, "accepted arguments must reach the failing renderer stub");
      const forwarded = JSON.parse(readFileSync(received, "utf8"));
      assert.deepEqual(forwarded.horizon_reuse, reuse, "omission and explicit false must survive forwarding");
    }
    for (const normal_samples of [1, 5, "0", null, -1]) {
      const response = request(undefined, { normal_samples });
      assert.equal(response.error?.code, -32602, JSON.stringify(response));
    }
    for (const normal_samples of [0, 4, 6]) {
      const response = request(undefined, { normal_samples });
      assert.equal(response.error, undefined, JSON.stringify(response));
      assert.equal(response.result?.isError, true, "normal selection must reach the renderer stub");
      const forwarded = JSON.parse(readFileSync(received, "utf8"));
      assert.deepEqual(forwarded.surface_quality, { normal_samples });
    }
    for (const field of ["hidden_rejection", "spatial_scratch"]) {
      for (const value of [0, 1, "false", null, []]) {
        const response = request(undefined, undefined, { [field]: value });
        assert.equal(response.error?.code, -32602, JSON.stringify(response));
      }
      for (const value of [undefined, false, true]) {
        const response = request(undefined, undefined, { [field]: value });
        assert.equal(response.error, undefined, JSON.stringify(response));
        assert.equal(response.result?.isError, true, "accepted controls must reach the renderer stub");
        const forwarded = JSON.parse(readFileSync(received, "utf8"));
        assert.equal(forwarded[field], value, "omission and explicit false must survive forwarding");
      }
    }
  } finally {
    rmSync(scratch, { recursive: true, force: true });
  }
});
