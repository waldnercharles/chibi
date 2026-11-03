const assert = require('node:assert/strict');
const fs = require('node:fs');
const {execFileSync, spawnSync} = require('node:child_process');
const {tmpdir} = require('node:os');
const path = require('node:path');
const test = require('node:test');

// Execute both native-proof source fences against real disposable Git trees.
// No compiler, network, repository hooks or language fixtures are involved.
test('source fences reject changed HEAD and tracked or untracked edits', () => {
  const proof = fs.readFileSync(path.resolve(__dirname, '../../.github/ci/native-proof.sh'), 'utf8');
  const marker = '# Preserve the tested source identity within the existing native proof budget.';
  assert.ok(proof.includes(marker));
  const start = '# A failed core test may precede this phase.';
  assert.ok(proof.includes(start));
  const commands = [
    proof.slice(proof.indexOf(start), proof.indexOf('set -o pipefail')),
    proof.slice(proof.indexOf(marker)),
  ];
  for (const variant of ['clean', 'tracked', 'untracked', 'head', 'restored', 'status-error']) {
    const root = fs.mkdtempSync(path.join(tmpdir(), 'chibi-ci-source-'));
    try {
      const git = args => execFileSync('git', ['-c', 'core.hooksPath=/dev/null', ...args], {
        cwd: root, encoding: 'utf8', timeout: 3000, stdio: ['ignore', 'pipe', 'pipe']}).trim();
      git(['init', '-q']);
      fs.writeFileSync(path.join(root, 'source'), 'original\n');
      git(['add', '.']);
      git(['-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '-qm', 'original']);
      const sha = git(['rev-parse', 'HEAD']);
      if (variant === 'tracked' || variant === 'restored') fs.writeFileSync(path.join(root, 'source'), 'changed\n');
      if (variant === 'untracked') fs.writeFileSync(path.join(root, 'untracked'), 'changed\n');
      if (variant === 'restored') git(['checkout', '--', 'source']);
      const env = {...process.env, GITHUB_SHA: variant === 'head' ? '0'.repeat(40) : sha};
      if (variant === 'status-error') {
        const bin = path.join(root, '.git/shim');
        fs.mkdirSync(bin);
        fs.writeFileSync(path.join(bin, 'git'), '#!/bin/sh\nif [ "$1" = status ]; then exit 19; fi\nprintf "%s\\n" "$GITHUB_SHA"\n', {mode: 0o755});
        env.PATH = bin + ':' + env.PATH;
      }
      for (const command of commands) {
        const result = spawnSync('bash', ['-e', '-c', command], {cwd: root,
          env,
          encoding: 'utf8', timeout: 3000});
        assert.equal(result.status, variant === 'status-error' ? 19 : ['clean', 'restored'].includes(variant) ? 0 : 1, variant + ': ' + result.stderr);
      }
    } finally { fs.rmSync(root, {recursive: true, force: true}); }
  }
});
