import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

// Fixture mutations cross the test's JSON boundary into the shared KDL writer.
export function fixtureKdl(value) {
  const encoded = spawnSync("python3", [
    "-c",
    "import json,sys; sys.path.insert(0,sys.argv[1]); import kdl_asset; sys.stdout.write(kdl_asset.dumps(json.load(sys.stdin)))",
    fileURLToPath(new URL("../", import.meta.url)),
  ], { input: JSON.stringify(value), encoding: "utf8" });
  if (encoded.status !== 0) throw new Error(encoded.stderr || "KDL fixture serialization failed");
  return encoded.stdout;
}
