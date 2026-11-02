'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const receipt = require('../../.github/ci-receipt.js');
const sha = letter => letter.repeat(40);
const source = {sha: sha('a'), tree: sha('b'), base: sha('c'), head: sha('d')};
const context = {eventName: 'pull_request', ref: 'refs/pull/17/merge', sha: source.sha,
  runId: 42, repo: {owner: 'owner', repo: 'repo'}, payload: {
    repository: {id: 1}, pull_request: {number: 17,
      base: {sha: source.base, ref: 'main', repo: {id: 1, full_name: 'owner/repo'}},
      head: {sha: source.head, repo: {id: 2}}}}};
const expected = {id: 42, workflowId: 9, head: source.head, repository: 'owner/repo',
  repositoryId: 1, headRepositoryId: 2, pullRequest: 17};
const run = {id: 42, run_attempt: 3, workflow_id: 9, path: '.github/workflows/ci.yml',
  event: 'pull_request', head_sha: source.head, repository: {id: 1},
  head_repository: {id: 2}, status: 'completed', conclusion: 'success'};
const jobs = {total_count: 2, jobs: [{name: 'Engine build and native tests',
  status: 'completed', conclusion: 'success', steps: receipt.STEPS.map(name => ({name,
    status: 'completed', conclusion: 'success'}))},
  {name: 'Full only', status: 'completed', conclusion: 'skipped'}]};
const clone = value => structuredClone(value);
function temporary(fn) {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'chibi-receipt-test-'));
  try { return fn(directory); } finally { fs.rmSync(directory, {recursive: true, force: true}); }
}
function archive(contents, name = 'receipt.json') {
  return temporary(directory => {
    fs.writeFileSync(path.join(directory, name), contents);
    execFileSync('zip', ['-q', 'archive.zip', name], {cwd: directory});
    return fs.readFileSync(path.join(directory, 'archive.zip'));
  });
}
const good = receipt.makeReceipt(context, source, 3);

test('record binds actual synthetic checkout, source head, base, repository and attempt', () => {
  assert.equal(good.sha, source.sha);
  assert.equal(good.headRepositoryId, 2); // Fork PRs are not privileged or excluded.
  for (const change of [c => c.eventName = 'pull_request_target', c => c.sha = sha('e'),
    c => c.ref = 'refs/heads/main', c => c.payload.pull_request.head.sha = sha('e'),
    c => c.payload.pull_request.base.sha = sha('e'),
    c => c.payload.pull_request.base.repo.id = 3,
    c => c.payload.pull_request.base.repo.full_name = 'other/repo',
    c => c.payload.pull_request.base.ref = 'other', c => c.runId = 0]) {
    const changed = clone(context); change(changed);
    assert.throws(() => receipt.makeReceipt(changed, source, 3));
  }
  for (const attempt of [0, -1, 1.5, NaN, '3'])
    assert.throws(() => receipt.makeReceipt(context, source, attempt));
});

test('source head is not confused with the synthetic checkout SHA', () => {
  receipt.successfulRun(run, expected);
  const changed = clone(run); changed.head_sha = source.sha;
  assert.throws(() => receipt.successfulRun(changed, expected));
  for (const [key, value] of Object.entries({id: 43, run_attempt: 0, workflow_id: 10,
    path: '.github/workflows/other.yml', event: 'push', status: 'in_progress', conclusion: 'failure'}))
    assert.throws(() => receipt.successfulRun({...run, [key]: value}, expected));
  for (const key of ['repository', 'head_repository'])
    assert.throws(() => receipt.successfulRun({...run, [key]: {id: 999}}, expected));
});

test('canonical job requires all named steps, no hidden failures or extra executed jobs', () => {
  receipt.successfulJobs(jobs);
  for (const change of [j => j.total_count++, j => j.jobs[0].name = 'other',
    j => j.jobs[0].conclusion = 'failure', j => j.jobs[1].conclusion = 'success',
    j => j.jobs[1].name = j.jobs[0].name,
    j => j.jobs[0].steps.pop(), j => j.jobs[0].steps.push(j.jobs[0].steps[0]),
    j => j.jobs[0].steps[0].conclusion = 'skipped',
    j => j.jobs[0].steps[0].status = 'in_progress',
    j => j.jobs[0].steps.push({name: 'hidden', status: 'completed', conclusion: 'failure'})]) {
    const changed = clone(jobs); change(changed);
    assert.throws(() => receipt.successfulJobs(changed));
  }
});

test('receipt rejects stale attempts and every proof identity mismatch', () => {
  receipt.verifyReceipt(good, expected, source, run);
  for (const key of Object.keys(good)) {
    const changed = {...good, [key]: typeof good[key] === 'number' ? 999 : 'wrong'};
    assert.throws(() => receipt.verifyReceipt(changed, expected, source, run), key);
  }
});

test('ZIP is never extracted and is bounded before and after inflation', () => {
  assert.deepEqual(receipt.readArchive(archive(JSON.stringify(good))), good);
  assert.throws(() => receipt.readArchive(Buffer.alloc(65537)));
  assert.throws(() => receipt.readArchive(Buffer.from('not zip')));
  assert.throws(() => receipt.readArchive(archive('invalid json')));
  assert.throws(() => receipt.readArchive(archive('{}', 'other.json')));
  assert.throws(() => receipt.readArchive(archive(' '.repeat(100000))));
  temporary(directory => {
    fs.writeFileSync(path.join(directory, 'receipt.json'), '{}');
    fs.writeFileSync(path.join(directory, 'extra'), 'x');
    execFileSync('zip', ['-q', 'archive.zip', 'receipt.json', 'extra'], {cwd: directory});
    assert.throws(() => receipt.readArchive(fs.readFileSync(path.join(directory, 'archive.zip'))));
  });
});

test('real shallow merge checkout preserves raw tree and both parents', () => {
  temporary(directory => {
    const git = args => execFileSync('git', args, {cwd: directory, encoding: 'utf8'}).trim();
    git(['init', '-q']); git(['config', 'user.name', 'CI test']);
    git(['config', 'user.email', 'ci-test@example.invalid']);
    fs.writeFileSync(path.join(directory, 'base'), 'base');
    git(['add', '.']); git(['commit', '-qm', 'base']);
    const base = git(['rev-parse', 'HEAD']);
    git(['checkout', '-qb', 'feature']);
    fs.writeFileSync(path.join(directory, 'feature'), 'feature');
    git(['add', '.']); git(['commit', '-qm', 'feature']);
    const head = git(['rev-parse', 'HEAD']);
    git(['checkout', '-q', '--detach', base]); git(['merge', '-q', '--no-ff', '-m', 'merge', head]);
    const actual = receipt.checkout(directory);
    assert.equal(actual.base, base); assert.equal(actual.head, head);
    fs.writeFileSync(path.join(directory, '.git', 'shallow'), `${actual.sha}\n`);
    assert.deepEqual(receipt.checkout(directory), actual);
    assert.equal(git(['show', '-s', '--format=%P']), '');
  });
});

test('bounded download never forwards authentication to the signed ZIP URL', async () => {
  const calls = [];
  const download = receipt.boundedFetch(Date.now() + 10000, true, async (url, options) => {
    calls.push({url, options});
    return calls.length === 1 ? new Response(null, {status: 302,
      headers: {location: 'https://storage.example/opaque?signature=private'}}) : new Response('zip');
  });
  const response = await download('https://api.github.com/example', {headers: {authorization: 'secret'}});
  assert.equal(await response.text(), 'zip');
  assert.equal(calls[0].options.redirect, 'manual');
  assert.equal(calls[1].options.headers, undefined);
  assert.equal(calls[1].options.redirect, 'error');
  await assert.rejects(() => download('https://evil.example', {}));
  await assert.rejects(() => receipt.boundedFetch(Date.now() - 1)(
    'https://api.github.com/example', {}));
  await assert.rejects(() => receipt.boundedFetch(Date.now() + 1000, true,
    async () => new Response('x'.repeat(65537)))('https://api.github.com/example', {}));
  await assert.rejects(() => receipt.boundedFetch(Date.now() + 1000, true,
    async () => new Response(null, {status: 302, headers: {location: 'http://storage.example'}}))(
    'https://api.github.com/example', {}));
});

function apiFixture(change = () => {}) {
  const state = {run: clone(run), newest: clone(run), jobs: clone(jobs), receipt: clone(good),
    pr: {number: 17, merged: true, state: 'closed', merge_commit_sha: source.sha,
      ...clone(context.payload.pull_request)}, artifacts: {total_count: 1, artifacts: [{id: 91,
      name: 'ci-tested-tree', expired: false, size_in_bytes: 1000}]}};
  change(state);
  const calls = [];
  let listings = 0;
  let runs = 0;
  const github = {request: async (route, parameters) => {
    calls.push({route, parameters});
    if (state.error) throw new Error('private signed URL should not be logged');
    let data;
    if (route.endsWith('/commits/{commit_sha}/pulls')) data = [state.pr];
    else if (route.endsWith('/pulls/{pull_number}')) data = state.pr;
    else if (route.endsWith('/actions/workflows/ci.yml')) data = {id: 9, path: '.github/workflows/ci.yml'};
    else if (route.endsWith('/actions/workflows/{workflow_id}/runs'))
      data = {total_count: 1, workflow_runs: [listings++ ? state.newest : state.run]};
    else if (route.endsWith('/actions/runs/{run_id}')) data = runs++ && state.fresh ? state.fresh : state.run;
    else if (route.endsWith('/attempts/{attempt_number}/jobs')) {
      assert.equal(parameters.attempt_number, state.run.run_attempt); data = state.jobs;
    } else if (route.endsWith('/actions/runs/{run_id}/artifacts')) data = state.artifacts;
    else if (route.endsWith('/actions/artifacts/{artifact_id}/zip')) data = archive(JSON.stringify(state.receipt));
    else if (route.endsWith('/git/commits/{commit_sha}')) data = state.tested || {sha: source.sha,
      tree: {sha: source.tree}, parents: [{sha: source.base}, {sha: source.head}]};
    else throw new Error(`Unexpected route: ${route}`);
    return {data};
  }};
  return {github, calls};
}
const pushed = {...context, eventName: 'push', ref: 'refs/heads/main'};

test('complete read-only lookup binds merged PR, latest run and attempt-specific job evidence', async () => {
  const {github, calls} = apiFixture();
  await receipt.findProof(github, pushed, source);
  assert.equal(calls.length, 11);
  assert.ok(calls.every(call => call.route.startsWith('GET ')));
  assert.equal(calls.filter(call => call.route.endsWith('/runs')).length, 2);
});

test('lookup fails closed for fork/source mismatch, failed latest run, missing receipt and rerun races', async () => {
  for (const change of [s => s.pr.head.sha = sha('f'), s => s.pr.merged = false,
    s => s.pr.base.repo.id = 3, s => s.pr.head.repo.id = 3,
    s => s.run.conclusion = 'failure', s => s.run.status = 'queued',
    s => s.receipt.tree = sha('f'), s => s.receipt.attempt--,
    s => s.artifacts.artifacts[0].expired = true,
    s => s.artifacts.artifacts[0].digest = 'sha256:wrong',
    s => s.tested = {sha: source.sha, tree: {sha: sha('f')}, parents: []},
    s => s.artifacts.artifacts = [], s => s.artifacts.artifacts.push(s.artifacts.artifacts[0]),
    s => s.fresh = {...s.run, run_attempt: 4},
    s => s.newest = {...s.run, id: 43, conclusion: 'failure'}, s => s.error = true]) {
    const {github} = apiFixture(change);
    await assert.rejects(() => receipt.findProof(github, pushed, source));
  }
});

test('PR, manual, schedule and unknown push never skip or require API access', async () => {
  for (const candidate of [context, {...context, eventName: 'schedule'},
    {...context, eventName: 'workflow_dispatch'}, {...pushed, ref: 'refs/heads/other'}, pushed]) {
    const output = [], logs = [];
    await receipt.gate({context: candidate, github: {request: async () => {throw new Error('secret');}},
      core: {setOutput: (...args) => output.push(args), info: message => logs.push(message)}});
    assert.deepEqual(output, [['skip', 'false']]);
    assert.ok(logs.every(line => !line.includes('secret')));
  }
});

test('public gate reuses a real merge proof; record rejects dirty tested source', async () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'chibi-gate-test-'));
  const previous = process.cwd();
  const runner = fs.mkdtempSync(path.join(os.tmpdir(), 'chibi-record-test-'));
  const oldTemp = process.env.RUNNER_TEMP, oldAttempt = process.env.GITHUB_RUN_ATTEMPT;
  try {
    const git = args => execFileSync('git', args, {cwd: directory, encoding: 'utf8'}).trim();
    git(['init', '-q']); git(['config', 'user.name', 'CI test']);
    git(['config', 'user.email', 'ci-test@example.invalid']);
    fs.writeFileSync(path.join(directory, 'base'), 'base');
    git(['add', '.']); git(['commit', '-qm', 'base']);
    const base = git(['rev-parse', 'HEAD']);
    git(['checkout', '-qb', 'feature']);
    fs.writeFileSync(path.join(directory, 'feature'), 'feature');
    git(['add', '.']); git(['commit', '-qm', 'feature']);
    const head = git(['rev-parse', 'HEAD']);
    git(['checkout', '-q', '--detach', base]); git(['merge', '-q', '--no-ff', '-m', 'merge', head]);
    const actual = receipt.checkout(directory);
    const {github} = apiFixture(s => {
      s.pr.merge_commit_sha = actual.sha; s.pr.head.sha = actual.head;
      s.run.head_sha = actual.head; s.newest.head_sha = actual.head;
      Object.assign(s.receipt, actual);
      s.tested = {sha: actual.sha, tree: {sha: actual.tree},
        parents: [{sha: actual.base}, {sha: actual.head}]};
    });
    process.chdir(directory);
    const output = [];
    await receipt.gate({github, context: {...pushed, sha: actual.sha},
      core: {setOutput: (...args) => output.push(args), info: () => {}}});
    assert.deepEqual(output, [['skip', 'false'], ['skip', 'true']]);
    process.env.RUNNER_TEMP = runner; process.env.GITHUB_RUN_ATTEMPT = '3';
    const recordedContext = clone(context);
    recordedContext.sha = actual.sha;
    recordedContext.payload.pull_request.base.sha = actual.base;
    recordedContext.payload.pull_request.head.sha = actual.head;
    const record = () => receipt.record({context: recordedContext, core: {info: () => {}}});
    await record();
    const recordedPath = path.join(runner, 'ci-tested-tree', 'receipt.json');
    const recorded = fs.readFileSync(recordedPath, 'utf8');
    assert.equal(JSON.parse(recorded).tree, actual.tree);
    fs.writeFileSync(path.join(directory, 'base'), 'mutated');
    await assert.rejects(record);
    assert.equal(fs.readFileSync(recordedPath, 'utf8'), recorded);
    fs.writeFileSync(path.join(directory, 'base'), 'base');
    fs.writeFileSync(path.join(directory, 'untracked'), 'new source');
    await assert.rejects(record);
    fs.unlinkSync(path.join(directory, 'untracked'));
    await record();
    assert.equal(fs.readFileSync(recordedPath, 'utf8'), recorded);
  } finally {
    if (oldTemp === undefined) delete process.env.RUNNER_TEMP; else process.env.RUNNER_TEMP = oldTemp;
    if (oldAttempt === undefined) delete process.env.GITHUB_RUN_ATTEMPT; else process.env.GITHUB_RUN_ATTEMPT = oldAttempt;
    fs.rmSync(runner, {recursive: true, force: true});
    process.chdir(previous);
    fs.rmSync(directory, {recursive: true, force: true});
  }
});

test('ZIP traversal, duplicate names and corrupt data are rejected without extraction', () => {
  // Replacing a same-length ZIP entry name in both headers keeps a valid archive.
  const renamed = name => {
    const bytes = archive('{}');
    return Buffer.from(bytes.toString('latin1').replaceAll('receipt.json', name), 'latin1');
  };
  assert.throws(() => receipt.readArchive(renamed('../evil.json')));
  temporary(directory => {
    fs.writeFileSync(path.join(directory, 'receipt.json'), '{}');
    fs.writeFileSync(path.join(directory, 'receipt.jsox'), '{}');
    execFileSync('zip', ['-q', 'archive.zip', 'receipt.json', 'receipt.jsox'], {cwd: directory});
    const bytes = fs.readFileSync(path.join(directory, 'archive.zip'));
    assert.throws(() => receipt.readArchive(Buffer.from(bytes.toString('latin1')
      .replaceAll('receipt.jsox', 'receipt.json'), 'latin1')));
  });
  const corrupt = archive('{}');
  const dataOffset = 30 + corrupt.readUInt16LE(26) + corrupt.readUInt16LE(28);
  corrupt[dataOffset] ^= 1;
  assert.throws(() => receipt.readArchive(corrupt));
});
