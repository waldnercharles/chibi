import { existsSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), "../..");

// CHIBI_CTL (set by launch.sh), else the repository's own build.
export function controllerPath() {
  const candidates = [
    process.env.CHIBI_CTL,
    resolve(repoRoot, "build", "chibi_ctl"),
    resolve(repoRoot, "build-release", "chibi_ctl"),
  ].filter(Boolean);
  const found = candidates.find((candidate) => existsSync(candidate));
  if (!found) {
    throw new Error(`chibi_ctl not found at ${candidates.join(" or ")}; build chibi_ctl (make mcp-controller)`);
  }
  return found;
}

// Keeps 64-bit IDs and hashes exact: integers beyond 2^53 become decimal strings.
export function parseControllerJson(text) {
  return JSON.parse(text, (key, value, context) =>
    typeof value === "number" && !Number.isSafeInteger(value) && /^-?\d+$/.test(context.source)
      ? context.source
      : value);
}
