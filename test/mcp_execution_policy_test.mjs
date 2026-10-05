import assert from "node:assert/strict";
import { mkdtempSync, writeFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import { renderRequest } from "../tools/mcp/authoring.mjs";

test("render process errors and timeouts cannot be reported as passes", async () => {
  const directory = mkdtempSync(join(tmpdir(), "chibi-policy-"));
  const executable = join(directory, "controller");
  const keys = ["CHIBI_CTL", "CHIBI_EXECUTION_PROFILE", "CHIBI_RENDER_CTL_TIMEOUT_MS"];
  const saved = Object.fromEntries(keys.map(key => [key, process.env[key]]));
  process.env.CHIBI_CTL = executable;
  process.env.CHIBI_EXECUTION_PROFILE = "iteration";
  process.env.CHIBI_RENDER_CTL_TIMEOUT_MS = "1000";
  const fixture = text => writeFileSync(executable, `#!${process.execPath}\n${text}\n`, { mode: 0o700 });
  try {
    fixture('console.log(JSON.stringify({ok:true})); process.exit(7);');
    const failed = await renderRequest(directory, { operation: "preview_scene" });
    assert.equal(failed.ok, false);
    assert.equal(failed.full_verification, false);
    assert.equal(failed.error.code, "process_error");
    fixture('console.log(JSON.stringify({ok:false,status:"blocked"})); process.exit(2);');
    assert.equal((await renderRequest(directory, { operation: "preview_scene" })).status, "blocked");
    fixture('setInterval(() => {}, 1000);');
    process.env.CHIBI_RENDER_CTL_TIMEOUT_MS = "50";
    const timeout = await renderRequest(directory, { operation: "preview_scene" });
    assert.equal(timeout.ok, false);
    assert.equal(timeout.error.code, "timeout");
    assert.equal(timeout.full_verification, false);
  } finally {
    for (const key of keys) {
      if (saved[key] === undefined) delete process.env[key];
      else process.env[key] = saved[key];
    }
    rmSync(directory, { recursive: true, force: true });
  }
});
