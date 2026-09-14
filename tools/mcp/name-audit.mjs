#!/usr/bin/env node

import { spawnSync } from "node:child_process";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { Tiktoken } from "js-tiktoken/lite";
import o200kBase from "js-tiktoken/ranks/o200k_base";
import { toolCatalog } from "./tool-catalog.mjs";

const scriptRoot = dirname(fileURLToPath(import.meta.url));
const projectRoot = resolve(process.argv[2] ?? resolve(scriptRoot, "../.."));
const serverPath = resolve(scriptRoot, "server.mjs");
const encoding = new Tiktoken(o200kBase);
const failures = [];

function tokens(value) {
  return encoding.encode(value);
}

function pieces(value) {
  return tokens(value).map(token => encoding.decode([token]));
}

function check(condition, message) {
  if (!condition) failures.push(message);
}

function camelWords(name) {
  return name
    .replace(/([a-z0-9])([A-Z])/g, "$1 $2")
    .toLowerCase()
    .split(/\s+/)
    .filter(Boolean);
}

function candidateStyles(entry) {
  const words = camelWords(entry.name);
  const candidates = [
    { style: "camelCase", value: entry.name },
    { style: "snake_case", value: words.join("_") },
    { style: "kebab-case", value: words.join("-") },
  ];
  for (const value of entry.candidates) {
    candidates.push({
      style: value.length > entry.name.length ? "longer-intent" : "intent-variant",
      value,
    });
  }
  const seen = new Set();
  return candidates
    .filter(candidate => {
      if (seen.has(candidate.value)) return false;
      seen.add(candidate.value);
      return true;
    })
    .map(candidate => ({
      ...candidate,
      count: tokens(candidate.value).length,
      pieces: pieces(candidate.value),
    }))
    .sort((left, right) => left.count - right.count || left.value.localeCompare(right.value));
}

function liveToolsList() {
  const request = JSON.stringify({
    jsonrpc: "2.0",
    id: 1,
    method: "tools/list",
    params: {},
  }) + "\n";
  const result = spawnSync(process.execPath, [serverPath], {
    cwd: projectRoot,
    env: { ...process.env, CHIBI_ROOT: projectRoot },
    input: request,
    encoding: "utf8",
    maxBuffer: 32 * 1024 * 1024,
  });
  if (result.error) throw result.error;
  if (result.status !== 0) {
    throw new Error("MCP server exited with status " + result.status + ": " + result.stderr.trim());
  }
  const lines = result.stdout.trim().split(/\r?\n/).filter(Boolean);
  let response;
  for (const line of lines) {
    try {
      const message = JSON.parse(line);
      if (message.id === 1) response = message;
    } catch {
      failures.push("server emitted non-JSON stdout: " + line.slice(0, 120));
    }
  }
  if (!response?.result?.tools) {
    const detail = result.stderr.trim() || result.stdout.trim() || ("exit status " + result.status);
    throw new Error("tools/list failed: " + detail);
  }
  return response.result.tools;
}

const liveTools = liveToolsList();
const catalogNames = toolCatalog.map(entry => entry.name);
const liveNames = liveTools.map(tool => tool.name);
const serializedList = JSON.stringify({ tools: liveTools });

check(liveNames.length === catalogNames.length,
  "tools/list has " + liveNames.length + " tools; catalog has " + catalogNames.length);
check(JSON.stringify(liveNames) === JSON.stringify(catalogNames),
  "tools/list order or names differ from the public catalog");
check(new Set(liveNames).size === liveNames.length, "tools/list contains duplicate names");
check(tokens(serializedList).length <= 20_000,
  "serialized tools/list uses " + tokens(serializedList).length + " tokens; limit is 20000");

for (const entry of toolCatalog) {
  const tool = liveTools.find(candidate => candidate.name === entry.name);
  check(/^[a-z][A-Za-z0-9]{0,127}$/.test(entry.name),
    "invalid MCP name: " + entry.name);
  check(tokens(entry.name).length <= 8,
    entry.name + " uses " + tokens(entry.name).length + " tokens; limit is 8");
  check(tool !== undefined, "missing public tool: " + entry.name);
  if (!tool) continue;
  check(typeof tool.title === "string" && tool.title.length > 0,
    entry.name + " is missing a title");
  check(typeof tool.description === "string" && tool.description.length > 0,
    entry.name + " is missing a description");
  check(tokens(tool.description).length <= 160,
    entry.name + " description uses " + tokens(tool.description).length + " tokens; limit is 160");
  check(tool.inputSchema?.type === "object", entry.name + " is missing an input schema");
  check(tool.outputSchema?.type === "object", entry.name + " is missing an output schema");
  check(tool.annotations && typeof tool.annotations === "object",
    entry.name + " is missing safety annotations");
  const searchableText = (entry.name + " " + tool.description).toLowerCase();
  for (const term of entry.intentTerms) {
    check(searchableText.includes(term.toLowerCase()),
      entry.name + " is missing intent term " + term);
  }
}

console.log("MCP name audit: o200k_base");
console.log("project: " + projectRoot);
console.log("public tools: " + liveTools.length);
console.log("serialized tools/list: " + tokens(serializedList).length + " tokens");
console.log("serialized tools/list pieces: " + JSON.stringify(pieces(serializedList)));
for (const entry of toolCatalog) {
  console.log("\n" + entry.name);
  for (const candidate of candidateStyles(entry)) {
    console.log("  " + candidate.style.padEnd(15) + " " +
      String(candidate.count).padStart(2) + " " +
      JSON.stringify(candidate.pieces) + " " + candidate.value);
  }
}

if (failures.length > 0) {
  console.error("\nMCP name audit failed:");
  for (const failure of failures) console.error("- " + failure);
  process.exitCode = 1;
} else {
  console.log("\nMCP name audit passed");
}
