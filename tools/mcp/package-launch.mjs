import { existsSync, realpathSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { enginePackage, checkEngineLock } from "./engine-package.mjs";

try {
  if (process.argv.length > 3) throw new Error("Usage: chibi-mcp [PROJECT_ROOT]");
  const engine = enginePackage(resolve(dirname(fileURLToPath(import.meta.url)), "../../.."));
  const root = realpathSync(resolve(process.argv[2] || process.env.CHIBI_ROOT || process.cwd()));
  if (!existsSync(resolve(root, "project.yaml"))) throw new Error(`No project.yaml in ${root}`);
  checkEngineLock(root, engine);
  process.env.CHIBI_ROOT = root;
  process.env.CHIBI_CTL = resolve(engine.root, "bin/chibi_ctl");
  process.env.CHIBI_RESOURCE_DIR = resolve(engine.root, "share/chibi");
  process.env.CHIBI_RENDER_SHADER_DIR = resolve(engine.root, "share/chibi/shaders");
  process.env.CHIBI_ENGINE_PACKAGE = "1";
  process.chdir(root);
  await import("./server.mjs");
} catch (error) {
  process.stderr.write(`chibi MCP: ${error.message}\n`);
  process.exitCode = 1;
}
