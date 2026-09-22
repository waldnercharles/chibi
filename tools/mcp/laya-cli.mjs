#!/usr/bin/env node
// One observed decision per invocation. Codex owns execution and text input.
import { readFileSync } from "node:fs";
import { chooseEditorAction } from "./laya.mjs";

try {
  const args = JSON.parse(readFileSync(process.argv[2] || 0, "utf8"));
  process.stdout.write(`${JSON.stringify(await chooseEditorAction(args), null, 2)}\n`);
} catch (error) {
  process.stderr.write(`${error.message}\n`);
  process.exitCode = 1;
}
