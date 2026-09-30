#!/usr/bin/env node
// Controller session contract against a scripted fake chibi_ctl: a timeout
// fails only its request, cancelled requests get no reply and queued ones never
// run, and a controller binary removed mid-rebuild keeps serving.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";

const root = resolve(process.argv[2] || process.cwd());
const work = mkdtempSync(join(tmpdir(), "chibi-session-"));
const controller = join(work, "chibi_ctl");
const log = join(work, "requests.log");

// Answers status at once; list_assets waits limit * 100 ms, or forever at limit 9.
writeFileSync(controller, `#!${process.execPath}
const { appendFileSync } = require("node:fs");
const root = process.argv[process.argv.indexOf("--project-root") + 1];
let buffer = "";
let chain = Promise.resolve();
process.stdin.setEncoding("utf8");
process.stdin.on("data", (chunk) => {
  buffer += chunk;
  let newline;
  while ((newline = buffer.indexOf("\\n")) >= 0) {
    const request = JSON.parse(buffer.slice(0, newline));
    buffer = buffer.slice(newline + 1);
    chain = chain.then(() => handle(request));
  }
});
function handle(request) {
  appendFileSync(${JSON.stringify(log)}, request.operation + " " + (request.limit ?? "") + "\\n");
  const reply = () => process.stdout.write(JSON.stringify({ contract: 1, operation: request.operation, ok: true,
    result: request.operation === "status" ? { project_root: root } : { limit: request.limit } }) + "\\n");
  if (request.operation !== "list_assets") return reply();
  if (request.limit === 9) return new Promise(() => {});
  return new Promise((done) => setTimeout(() => { reply(); done(); }, request.limit * 100));
}
`, { mode: 0o755 });
writeFileSync(log, "");

function start() {
  const child = spawn(process.execPath, [join(root, "tools/mcp/server.mjs")], {
    env: { ...process.env, CHIBI_ROOT: work, CHIBI_CTL: controller, CHIBI_CTL_TIMEOUT_MS: "1000" },
    stdio: ["pipe", "pipe", "ignore"],
  });
  const responses = new Map();
  const waiters = new Set();
  let buffer = "";
  child.stdout.setEncoding("utf8");
  child.stdout.on("data", (chunk) => {
    buffer += chunk;
    let newline;
    while ((newline = buffer.indexOf("\n")) >= 0) {
      const message = JSON.parse(buffer.slice(0, newline));
      buffer = buffer.slice(newline + 1);
      responses.set(message.id, message);
      waiters.forEach((check) => check());
    }
  });
  const send = (message) => child.stdin.write(`${JSON.stringify({ jsonrpc: "2.0", ...message })}\n`);
  return {
    responses,
    send,
    call: (id, name, args = {}) => send({ id, method: "tools/call", params: { name, arguments: args } }),
    wait: (id) => new Promise((done) => {
      const check = () => responses.has(id) && done(responses.get(id));
      waiters.add(check);
      check();
    }),
    end: () => child.stdin.end(),
  };
}

const sleep = (ms) => new Promise((done) => setTimeout(done, ms));

try {
  {
    const server = start();
    server.call(1, "listAssets", { limit: 9 });
    await sleep(100);
    server.call(2, "listAssets", { limit: 1 });
    server.call(3, "getProjectStatus");
    const [timedOut, queued, status] = await Promise.all([server.wait(1), server.wait(2), server.wait(3)]);
    assert.equal(timedOut.error?.code, -32001, JSON.stringify(timedOut));
    assert.equal(queued.result?.isError, false, JSON.stringify(queued));
    assert.equal(status.result?.isError, false, JSON.stringify(status));
    server.end();
  }
  {
    writeFileSync(log, "");
    const server = start();
    server.call(1, "listAssets", { limit: 5 });
    await sleep(200);
    server.call(2, "listAssets", { limit: 2 });
    await sleep(50);
    server.send({ method: "notifications/cancelled", params: { requestId: 2 } });
    server.send({ method: "notifications/cancelled", params: { requestId: 1 } });
    server.call(3, "listAssets", { limit: 3 });
    await server.wait(3);
    await sleep(300);
    assert(!server.responses.has(1) && !server.responses.has(2), "a cancelled request was answered");
    assert.doesNotMatch(readFileSync(log, "utf8"), /^list_assets 2$/m, "a queued request ran after it was cancelled");
    server.end();
  }
  {
    const server = start();
    server.call(1, "getProjectStatus");
    await server.wait(1);
    rmSync(controller);
    server.call(2, "getProjectStatus");
    assert.equal((await server.wait(2)).result?.isError, false, "a removed controller binary broke the session");
    server.end();
  }
  console.log("MCP session smoke passed: timeouts, cancellation, controller rebuilds");
} finally {
  rmSync(work, { recursive: true, force: true });
}
