import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const root = resolve(process.argv[2] ?? process.cwd());
const controller = process.env.CHIBI_CTL;
if (!controller) throw new Error('check-mcp must set CHIBI_CTL to the built controller');

const sourcePath = resolve(root, 'tools/behavior-code/examples/ProjectilePulse.code');
const mapPath = sourcePath + '.map.json';
const assetPath = resolve(root, 'content/behaviors/ProjectilePulse.kdl');
const before = [sourcePath, mapPath, assetPath].map((path) => readFileSync(path, 'utf8'));
const cli = fileURLToPath(new URL('./cli.mjs', import.meta.url));
const result = spawnSync(process.execPath, [
  cli, 'decompile', sourcePath, '--project-root', root, '--ctl', controller,
], {
  cwd: root,
  env: { ...process.env, CHIBI_CTL: controller, CHIBI_ROOT: root },
  encoding: 'utf8',
  maxBuffer: 32 * 1024 * 1024,
});

assert.equal(result.status, 0, result.stderr);
assert.equal(result.stdout, before[0], 'ProjectilePulse decompile must preserve source byte-for-byte');
assert.deepEqual(
  [sourcePath, mapPath, assetPath].map((path) => readFileSync(path, 'utf8')),
  before,
  'decompile must not rewrite the source, source map, or graph asset',
);
process.stdout.write('behavior-code ProjectilePulse decompile smoke: ok\n');
