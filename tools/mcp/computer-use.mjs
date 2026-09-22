import { spawn } from "node:child_process";
import { randomUUID } from "node:crypto";
import { existsSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const repo = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
const python = resolve(repo, "tools/typesafe-computer-use/.venv/bin/python");
const runs = resolve(repo, ".chibi/computer-use");
const active = new Map();
const text = (maxLength) => ({ type: "string", minLength: 1, maxLength });
const id = { type: "string", pattern: "^[a-f0-9-]{32,36}$" };
const object = (properties, required) => ({ type: "object", properties, required, additionalProperties: false });

export const computerToolDefinitions = [
  { name: "startComputerTask", inputSchema: object({
    goal: text(2000), target_app: text(120), act: { type: "boolean", default: false },
    steps: { type: "integer", minimum: 1, maximum: 100 },
    burst: { type: "integer", minimum: 1, maximum: 5 },
    min_confidence: { type: "number", minimum: 0, maximum: 1 },
    editor_socket: text(103), workflow: { type: "string", enum: ["create_asset"] },
  }, ["goal", "target_app"]) },
  { name: "resumeComputerTask", inputSchema: object({
    run_id: id, request_id: id,
    reply: object({
      retry: { type: "boolean" }, approve: { type: "boolean" }, text: text(4096), url: text(2048),
      confirm_canvas_field: { type: "boolean" }, answer: text(4000), achieved: { type: "boolean" },
      decision: object({ kind: text(30), item: text(8), offscreen: text(8), site: text(40) }, ["kind"]),
    }, []),
  }, ["run_id", "request_id", "reply"]) },
  { name: "getComputerTask", inputSchema: object({ run_id: id }, ["run_id"]) },
].map((tool) => ({ ...tool, outputSchema: {
  type: "object", required: ["ok", "result"],
  properties: { ok: { type: "boolean" }, result: { type: "object" } },
} }));

export function cancelComputerRequest(requestId) {
  const child = active.get(requestId);
  if (!child) return;
  child.kill("SIGTERM");
  const timer = setTimeout(() => child.kill("SIGKILL"), 2000);
  timer.unref();
  child.once("close", () => clearTimeout(timer));
}

process.once("exit", () => { for (const child of active.values()) child.kill("SIGTERM"); });

export async function computerToolCall(name, args, requestId) {
  if (!existsSync(python)) throw new Error("Install the local executor: uv sync --project tools/typesafe-computer-use --python 3.12");
  const runId = name === "startComputerTask" ? randomUUID() : args.run_id;
  if (!/^[a-f0-9-]{32,36}$/.test(runId)) throw new Error("Invalid run_id");
  const out = resolve(runs, runId);
  let command;
  if (name === "startComputerTask") {
    command = ["start", args.goal, "--target-app", args.target_app, "--out", out];
    if (args.act) command.push("--act");
    for (const key of ["editor_socket", "workflow"]) {
      if (args[key] !== undefined) command.push(`--${key.replaceAll("_", "-")}`, args[key]);
    }
    for (const key of ["steps", "burst", "min_confidence"]) {
      if (args[key] !== undefined) command.push(`--${key.replaceAll("_", "-")}`, String(args[key]));
    }
  } else if (name === "resumeComputerTask") {
    command = ["resume", out, args.request_id];
  } else if (name === "getComputerTask") {
    command = ["status", out];
  } else throw new Error("Unknown computer-use tool");
  return new Promise((accept, reject) => {
    const child = spawn(python, ["-m", "typesafe_computer_use.cli", ...command], { cwd: repo, stdio: ["pipe", "pipe", "pipe"] });
    active.set(requestId, child);
    let stdout = "", stderr = "";
    child.stdout.on("data", (data) => { stdout += data; });
    child.stderr.on("data", (data) => { stderr = (stderr + data).slice(-8000); });
    const timer = setTimeout(() => cancelComputerRequest(requestId), 90000);
    const clean = () => { clearTimeout(timer); active.delete(requestId); };
    child.on("error", (error) => { clean(); reject(error); });
    child.stdin.on("error", () => {}); // An early permission/setup failure may close stdin.
    child.on("close", (code) => {
      clean();
      let result;
      try { result = JSON.parse(stdout); }
      catch { reject(new Error(`Computer-use worker exited (${code}): ${stderr}`)); return; }
      if (code !== 0) { reject(new Error(result.error || `Computer-use worker exited (${code})`)); return; }
      accept({ ok: true, result: { run_id: runId, ...result } });
    });
    child.stdin.end(name === "resumeComputerTask" ? JSON.stringify(args.reply) : "");
  });
}
