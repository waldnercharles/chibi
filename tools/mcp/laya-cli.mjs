#!/usr/bin/env node
// One observed decision or bounded path per invocation. Codex owns execution and text input.
import { readFileSync } from "node:fs";
import { chooseEditorAction, chooseEditorPath } from "./laya.mjs";

try {
  const args = JSON.parse(readFileSync(process.argv[2] || 0, "utf8"));
  let result;
  if (args.tree) {
    if (!Array.isArray(args.available_action_ids) ||
      !args.available_action_ids.every((id) => typeof id === "string")) {
      throw new Error("A read-only available_action_ids catalog is required for path search");
    }
    const available = new Set(args.available_action_ids);
    const isAvailable = (leaf) => available.has(leaf.id);
    result = await chooseEditorPath(args, { availableLeaf: isAvailable, validateLeaf: isAvailable });
  } else {
    result = await chooseEditorAction(args);
  }
  process.stdout.write(`${JSON.stringify(result, null, 2)}\n`);
} catch (error) {
  process.stderr.write(`${error.message}\n`);
  process.exitCode = 1;
}
