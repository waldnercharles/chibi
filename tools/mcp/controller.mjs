import { existsSync } from "node:fs";
import { spawn } from "node:child_process";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
const buildDir = resolve(repoRoot, process.env.BUILD_DIR || "build");
const configured = process.env.CHIBI_CTL;
export const controllerPath = configured ? resolve(configured) : resolve(buildDir, "chibi_ctl");

let pendingBuild = null;

function buildController() {
  return new Promise((resolveBuild, rejectBuild) => {
    process.stderr.write(`chibi MCP building ${controllerPath}\n`);
    // Build output goes to stderr so stdout stays protocol-only.
    const child = spawn("make", ["-C", repoRoot, "mcp-controller", `BUILD_DIR=${buildDir}`], {
      stdio: ["ignore", 2, 2],
    });
    child.on("error", (error) => rejectBuild(new Error(`chibi_ctl build could not start: ${error.message}`)));
    child.on("exit", (code) => {
      if (code === 0 && existsSync(controllerPath)) {
        resolveBuild(controllerPath);
        return;
      }
      rejectBuild(new Error(`chibi_ctl build failed with status ${code}; run make mcp-controller for details`));
    });
  });
}

// Resolves once chibi_ctl exists, building it on first use so the MCP handshake never waits.
export function controllerExecutable() {
  if (existsSync(controllerPath)) {
    return Promise.resolve(controllerPath);
  }
  if (configured) {
    return Promise.reject(new Error(`CHIBI_CTL does not exist: ${controllerPath}`));
  }
  pendingBuild ||= buildController().finally(() => {
    pendingBuild = null;
  });
  return pendingBuild;
}
