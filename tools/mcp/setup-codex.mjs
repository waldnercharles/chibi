#!/usr/bin/env node

import { existsSync, mkdirSync, readFileSync, realpathSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";
import { enginePackage, checkEngineLock } from "./engine-package.mjs";

export function registerChibi(config, registration) {
  const lines = config.split("\n");
  const retained = [];
  let chibiSection = false;
  for (const line of lines) {
    const header = line.match(/^\s*\[([^\]]+)\]\s*(?:#.*)?$/);
    if (header) chibiSection = /^mcp_servers\.(?:chibi|"chibi"|'chibi')(?:\.|$)/.test(header[1].trim());
    if (!chibiSection) retained.push(line);
  }
  return `${retained.join("\n").trimEnd()}\n\n${registration}\n`.trimStart();
}

export function setupCodex(args) {
  const rootArg = args[0];
  if (rootArg?.startsWith("--")) throw new Error("Usage: setup-codex.mjs [PROJECT_ROOT] [--engine PACKAGE_ROOT] [--update-engine]");
  let packageRoot;
  let update = false;
  for (let i = 1; i < args.length; i++) {
    if (args[i] === "--engine" && args[i + 1]) packageRoot = args[++i];
    else if (args[i] === "--update-engine") update = true;
    else throw new Error(`Unknown setup argument: ${args[i]}`);
  }
  const root = realpathSync(resolve(rootArg ?? process.cwd()));
  const engine = packageRoot ? enginePackage(packageRoot) : null;
  const launcher = resolve(engine ? engine.root : root, engine ? "bin/chibi-mcp" : "tools/mcp/launch.sh");
  if (!existsSync(launcher)) throw new Error(`MCP launcher not found: ${launcher}`);
  if (engine) {
    if (!existsSync(resolve(root, "project.yaml"))) throw new Error(`No project.yaml in ${root}`);
    checkEngineLock(root, engine, update || !existsSync(resolve(root, ".chibi-engine.json")));
  } else if (update) throw new Error("--update-engine requires --engine PACKAGE_ROOT");

  const tomlString = value => JSON.stringify(value);
  const registration = [
    "[mcp_servers.chibi]",
    `command = ${tomlString("sh")}`,
    `args = [${tomlString(launcher)}${engine ? `, ${tomlString(root)}` : ""}]`,
    `cwd = ${tomlString(root)}`,
    "enabled = true",
    "startup_timeout_sec = 120",
    "tool_timeout_sec = 120",
  ].join("\n");
  const configPath = resolve(root, ".codex/config.toml");
  const config = existsSync(configPath) ? readFileSync(configPath, "utf8") : "";
  mkdirSync(resolve(root, ".codex"), { recursive: true });
  writeFileSync(configPath, registerChibi(config, registration), { mode: 0o600 });
  console.log(`Wrote ${configPath}${engine ? `; pinned engine ${engine.manifest.build_id}` : ""}`);
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  try { setupCodex(process.argv.slice(2)); }
  catch (error) { console.error(error.message); process.exitCode = 1; }
}
