import { accessSync, constants, existsSync, readFileSync, realpathSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";

export function enginePackage(path) {
  const root = realpathSync(resolve(path));
  const manifest = JSON.parse(readFileSync(resolve(root, "share/chibi/engine.json"), "utf8"));
  if (typeof manifest.build_id !== "string" || !manifest.build_id ||
      !Number.isInteger(manifest.project_version)) {
    throw new Error(`Invalid engine manifest in ${root}`);
  }
  for (const binary of ["chibi", "chibi_ctl", "chibi-mcp"]) {
    accessSync(resolve(root, "bin", binary), constants.X_OK);
  }
  return { root, manifest };
}

export function checkEngineLock(projectRoot, engine, update = false) {
  const lockPath = resolve(projectRoot, ".chibi-engine.json");
  const expected = { format: 1, build_id: engine.manifest.build_id, project_version: engine.manifest.project_version };
  const lock = existsSync(lockPath) ? JSON.parse(readFileSync(lockPath, "utf8")) : null;
  const matches = lock && lock.format === expected.format && lock.build_id === expected.build_id &&
    lock.project_version === expected.project_version;
  if (!matches && !update) {
    const quote = value => `'${value.replaceAll("'", "'\\''")}'`;
    const command = `${quote(resolve(engine.root, "bin/chibi-mcp"))} --setup ${quote(projectRoot)} --update-engine`;
    throw new Error(`${lock ? `Game pins engine ${lock.build_id}; selected engine is ${expected.build_id}` : "Game has no engine pin"}. To select this engine, run:\n${command}`);
  }
  if (!matches) writeFileSync(lockPath, `${JSON.stringify(expected, null, 2)}\n`);
}
