import assert from "node:assert/strict";
import { mkdtempSync, writeFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import { renderRequest } from "../tools/mcp/authoring.mjs";

test("render process errors and timeouts cannot be reported as passes", async () => {
  const directory = mkdtempSync(join(tmpdir(), "chibi-policy-"));
  const executable = join(directory, "controller");
  const keys = ["CHIBI_CTL", "CHIBI_EXECUTION_PROFILE", "CHIBI_RENDER_CTL_TIMEOUT_MS", "SDL_VIDEO_DRIVER", "CHIBI_ALLOW_FULL_GPU"];
  const saved = Object.fromEntries(keys.map(key => [key, process.env[key]]));
  process.env.CHIBI_CTL = executable;
  process.env.CHIBI_EXECUTION_PROFILE = "iteration";
  process.env.CHIBI_RENDER_CTL_TIMEOUT_MS = "1000";
  process.env.SDL_VIDEO_DRIVER = "offscreen";
  const fixture = text => writeFileSync(executable, `#!${process.execPath}\n${text}\n`, { mode: 0o700 });
  try {
    fixture('console.error("Vulkan Device: fixture GPU"); console.log(JSON.stringify({ok:true,window_hidden:true,planned_gpu_frames:2,result:{backend:"VULKAN",video_driver:process.env.SDL_VIDEO_DRIVER,preview:{width:96,height:64}}}));');
    const success = await renderRequest(directory, { operation: "preview_scene" });
    assert.equal(success.execution.active_device, "fixture GPU");
    assert.equal(success.execution.video_driver, "offscreen");
    assert.equal(success.execution.render_mode, "offscreen");
    assert.equal(success.execution.display_context, "offscreen");
    assert.equal(success.execution.window_hidden, true);
    assert.equal(success.execution.width, 96);
    assert.equal(success.execution.height, 64);
    assert.equal(success.execution.planned_gpu_frames, 2);
    assert.equal(success.execution.time_budget_seconds, 1);
    assert.ok(success.execution.elapsed_seconds >= 0);
    process.env.SDL_VIDEO_DRIVER = "x11";
    fixture('console.log(JSON.stringify({ok:true,window_hidden:true,result:{video_driver:process.env.SDL_VIDEO_DRIVER}}));');
    const explicit = await renderRequest(directory, { operation: "preview_scene" });
    assert.equal(explicit.execution.video_driver, "x11");
    assert.equal(explicit.execution.render_mode, "hidden-window");
    fixture('console.log(JSON.stringify({ok:false,status:"blocked",error:{code:"video_driver_unavailable"}})); process.exit(2);');
    const unavailable = await renderRequest(directory, { operation: "preview_scene" });
    assert.equal(unavailable.ok, false);
    assert.equal(unavailable.error.code, "video_driver_unavailable");
    // A native backend check can reject before reading any request bytes.
    const early = await renderRequest(directory, { operation: "preview_scene", payload: "x".repeat(8 * 1024 * 1024) });
    assert.equal(early.ok, false);
    assert.equal(early.error.code, "video_driver_unavailable");
    process.env.CHIBI_EXECUTION_PROFILE = "hardware";
    process.env.CHIBI_ALLOW_FULL_GPU = "1";
    fixture('console.log(JSON.stringify({ok:true,window_hidden:false,result:{video_driver:"x11"}}));');
    const visible = await renderRequest(directory, { operation: "preview_scene" });
    assert.equal(visible.execution.render_mode, "visible-window");
    assert.equal(visible.execution.window_hidden, false);
    process.env.CHIBI_EXECUTION_PROFILE = "iteration";
    fixture('console.log(JSON.stringify({ok:true})); process.exit(7);');
    const failed = await renderRequest(directory, { operation: "preview_scene" });
    assert.equal(failed.ok, false);
    assert.equal(failed.full_verification, false);
    assert.equal(failed.error.code, "process_error");
    assert.equal(failed.execution.render_mode, "not_reported");
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
