'use strict';

// This receipt only saves a replay. Every unknown or unavailable proof runs CI.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {createHash} = require('node:crypto');
const {execFileSync} = require('node:child_process');
const PROFILE = 'ci-pr-v1';
const WORKFLOW = '.github/workflows/ci.yml';
const JOB = 'Engine build and native tests';
const STEPS = ['Format and static checks', 'Core functional tests',
  'Native fixed-point proof', 'Record tested tree', 'Upload tested tree'];
const SHA = /^[0-9a-f]{40}$/;
const ZIP_LIMIT = 65536;
const JSON_LIMIT = 16384;
const positive = value => Number.isSafeInteger(value) && value > 0;
function requireProof(condition) {
  if (!condition) throw new Error('No exact successful PR proof');
}

function checkout(cwd = process.cwd()) {
  const git = args => execFileSync('git', args, {cwd, encoding: 'utf8',
    timeout: 3000, maxBuffer: 1048576}).trim();
  const sha = git(['rev-parse', '--verify', 'HEAD']);
  requireProof(SHA.test(sha));
  // Raw headers retain parents in a shallow Actions checkout; rev-list does not.
  const headers = git(['cat-file', '-p', sha]).split('\n\n', 1)[0].split('\n');
  const trees = headers.filter(line => line.startsWith('tree ')).map(line => line.slice(5));
  const parents = headers.filter(line => line.startsWith('parent ')).map(line => line.slice(7));
  requireProof(trees.length === 1 && SHA.test(trees[0]) &&
    parents.length === 2 && parents.every(parent => SHA.test(parent)));
  return {sha, tree: trees[0], base: parents[0], head: parents[1]};
}

function makeReceipt(context, source, attempt) {
  requireProof(['sha', 'tree', 'base', 'head'].every(key => SHA.test(source[key])));
  const pr = context.payload.pull_request;
  const repository = `${context.repo.owner}/${context.repo.repo}`;
  requireProof(context.eventName === 'pull_request' && pr &&
    context.sha === source.sha && context.ref === `refs/pull/${pr.number}/merge` &&
    positive(pr.number) && positive(context.runId) && positive(attempt) &&
    positive(context.payload.repository.id) && positive(pr.head.repo.id) &&
    pr.base.repo.id === context.payload.repository.id &&
    pr.base.repo.full_name === repository && pr.base.ref === 'main' &&
    pr.base.sha === source.base && pr.head.sha === source.head);
  return {version: 1, profile: PROFILE, repository, repositoryId: pr.base.repo.id,
    pullRequest: pr.number, headRepositoryId: pr.head.repo.id,
    runId: context.runId, attempt, ...source};
}

function successfulRun(run, expected) {
  requireProof(run.id === expected.id && positive(run.run_attempt) &&
    run.workflow_id === expected.workflowId && run.path === WORKFLOW &&
    run.event === 'pull_request' && run.head_sha === expected.head &&
    run.repository.id === expected.repositoryId &&
    run.head_repository.id === expected.headRepositoryId &&
    run.status === 'completed' && run.conclusion === 'success');
}

function successfulJobs(result) {
  requireProof(Array.isArray(result.jobs) && result.total_count === result.jobs.length &&
    result.jobs.length > 0 && result.jobs.length <= 100);
  // Conditional full-mode jobs can appear as skipped in a PR run.
  const active = result.jobs.filter(job => job.conclusion !== 'skipped');
  requireProof(active.length === 1 && active[0].name === JOB &&
    active[0].status === 'completed' && active[0].conclusion === 'success' &&
    result.jobs.filter(job => job.name === JOB).length === 1);
  for (const name of STEPS) {
    const steps = active[0].steps.filter(step => step.name === name);
    requireProof(steps.length === 1 && steps[0].status === 'completed' &&
      steps[0].conclusion === 'success');
  }
  // Do not accept a hidden continue-on-error failure in another executed step.
  requireProof(active[0].steps.every(step => step.status === 'completed' &&
    ['success', 'skipped'].includes(step.conclusion)));
}

function verifyReceipt(receipt, expected, source, run) {
  requireProof(receipt.version === 1 && receipt.profile === PROFILE &&
    receipt.repository === expected.repository && receipt.repositoryId === expected.repositoryId &&
    receipt.headRepositoryId === expected.headRepositoryId &&
    receipt.pullRequest === expected.pullRequest && receipt.runId === run.id &&
    receipt.attempt === run.run_attempt && SHA.test(receipt.sha) &&
    receipt.tree === source.tree && receipt.base === source.base && receipt.head === source.head);
}

function readArchive(bytes) {
  requireProof(Buffer.isBuffer(bytes) && bytes.length > 0 && bytes.length <= ZIP_LIMIT);
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'chibi-ci-receipt-'));
  try {
    const archive = path.join(directory, 'receipt.zip');
    fs.writeFileSync(archive, bytes, {mode: 0o600});
    const options = {encoding: 'utf8', timeout: 3000, maxBuffer: JSON_LIMIT,
      stdio: ['ignore', 'pipe', 'pipe']};
    // Never extract paths. Reject extra entries, duplicates and directory traversal.
    requireProof(execFileSync('unzip', ['-Z1', archive], options) === 'receipt.json\n');
    return JSON.parse(execFileSync('unzip', ['-p', archive, 'receipt.json'], options));
  } finally {
    fs.rmSync(directory, {recursive: true, force: true});
  }
}

// Octokit's documented custom fetch keeps both JSON and ZIP responses bounded.
// Only GitHub receives its authentication headers. Its signed ZIP redirect gets none.
function boundedFetch(deadline, archive = false, fetcher = fetch) {
  return async (url, options) => {
    requireProof(new URL(url).origin === 'https://api.github.com');
    const remaining = deadline - Date.now();
    requireProof(remaining > 0);
    const signal = AbortSignal.timeout(Math.min(5000, remaining));
    let response = await fetcher(url, {...options, signal, redirect: 'manual'});
    if (archive && response.status === 302) {
      const location = new URL(response.headers.get('location'));
      requireProof(location.protocol === 'https:' && !location.username && !location.password);
      await response.body?.cancel();
      response = await fetcher(location.href, {signal, redirect: 'error'});
    }
    requireProof(response.status === 200);
    const limit = archive ? ZIP_LIMIT : 1048576;
    const chunks = [];
    let size = 0;
    try {
      for await (const chunk of response.body) {
        size += chunk.length;
        requireProof(size <= limit);
        chunks.push(chunk);
      }
    } finally {
      if (!response.bodyUsed) await response.body?.cancel();
    }
    return new Response(Buffer.concat(chunks), {status: 200, headers: {
      'content-type': archive ? 'application/zip' : 'application/json'}});
  };
}

async function findProof(github, context, source) {
  const repository = `${context.repo.owner}/${context.repo.repo}`;
  const repositoryId = context.payload.repository.id;
  requireProof(positive(repositoryId) && context.sha === source.sha);
  const deadline = Date.now() + 30000;
  const get = async (route, parameters = {}, archive = false) => {
    requireProof(Date.now() < deadline);
    const result = await github.request(`GET /repos/{owner}/{repo}${route}`, {
      ...context.repo, ...parameters, request: {fetch: boundedFetch(deadline, archive)}});
    return result.data;
  };
  const linked = await get('/commits/{commit_sha}/pulls', {commit_sha: source.sha, per_page: 100});
  requireProof(Array.isArray(linked) && linked.length < 100);
  const candidates = linked.filter(pr => pr.merge_commit_sha === source.sha);
  requireProof(candidates.length === 1 && positive(candidates[0].number));
  const pr = await get('/pulls/{pull_number}', {pull_number: candidates[0].number});
  requireProof(pr.merged === true && pr.state === 'closed' && pr.merge_commit_sha === source.sha &&
    pr.base.ref === 'main' && pr.base.repo.id === repositoryId &&
    pr.base.repo.full_name === repository && pr.head.sha === source.head &&
    positive(pr.head.repo.id));
  const workflow = await get('/actions/workflows/ci.yml');
  requireProof(positive(workflow.id) && workflow.path === WORKFLOW);
  const expected = {repository, repositoryId, pullRequest: pr.number,
    headRepositoryId: pr.head.repo.id, workflowId: workflow.id, head: source.head};
  const latest = async () => {
    const listing = await get('/actions/workflows/{workflow_id}/runs', {
      workflow_id: workflow.id, head_sha: source.head, event: 'pull_request', per_page: 100});
    requireProof(Array.isArray(listing.workflow_runs) && listing.total_count > 0 &&
      listing.total_count === listing.workflow_runs.length && listing.total_count <= 100);
    return listing.workflow_runs.reduce((a, b) => a.id > b.id ? a : b);
  };
  const selected = await latest();
  expected.id = selected.id;
  requireProof(positive(expected.id));
  const run = await get('/actions/runs/{run_id}', {run_id: expected.id});
  successfulRun(run, expected);
  successfulJobs(await get('/actions/runs/{run_id}/attempts/{attempt_number}/jobs', {
    run_id: run.id, attempt_number: run.run_attempt, per_page: 100}));
  const artifacts = await get('/actions/runs/{run_id}/artifacts', {run_id: run.id, per_page: 100});
  requireProof(Array.isArray(artifacts.artifacts) &&
    artifacts.total_count === artifacts.artifacts.length && artifacts.total_count <= 100);
  const matches = artifacts.artifacts.filter(item => item.name === 'ci-tested-tree');
  requireProof(matches.length === 1 && positive(matches[0].id) && !matches[0].expired &&
    matches[0].size_in_bytes > 0 && matches[0].size_in_bytes <= ZIP_LIMIT);
  const bytes = await get('/actions/artifacts/{artifact_id}/zip', {artifact_id: matches[0].id}, true);
  const archive = Buffer.from(bytes);
  if (matches[0].digest !== undefined && matches[0].digest !== null) {
    requireProof(matches[0].digest === `sha256:${createHash('sha256').update(archive).digest('hex')}`);
  }
  const receipt = readArchive(archive);
  verifyReceipt(receipt, expected, source, run);
  const tested = await get('/git/commits/{commit_sha}', {commit_sha: receipt.sha});
  requireProof(tested.sha === receipt.sha && tested.tree.sha === source.tree &&
    tested.parents.length === 2 && tested.parents[0].sha === source.base &&
    tested.parents[1].sha === source.head);
  // Reruns invalidate older successful attempts, including reruns started during lookup.
  const fresh = await get('/actions/runs/{run_id}', {run_id: run.id});
  successfulRun(fresh, expected);
  requireProof(fresh.run_attempt === run.run_attempt);
  const newest = await latest();
  requireProof(newest.id === run.id && newest.run_attempt === run.run_attempt &&
    newest.status === 'completed' && newest.conclusion === 'success');
}

async function gate({github, context, core}) {
  core.setOutput('skip', 'false');
  if (context.eventName !== 'push' || context.ref !== 'refs/heads/main') return;
  try {
    await findProof(github, context, checkout());
    core.setOutput('skip', 'true');
    core.info('Exact merged tree already passed the canonical PR proof.');
  } catch {
    // Do not log API errors: they can contain signed artifact URLs or request details.
    core.info('No reusable exact PR proof; running canonical validation.');
  }
}

async function record({context, core}) {
  // HEAD identifies the tested source only if no proof left source mutations.
  requireProof(execFileSync('git', ['status', '--porcelain', '--untracked-files=normal'],
    {encoding: 'utf8', timeout: 3000, maxBuffer: 1048576}).trim() === '');
  const receipt = makeReceipt(context, checkout(), Number(process.env.GITHUB_RUN_ATTEMPT));
  requireProof(process.env.RUNNER_TEMP);
  const directory = path.join(process.env.RUNNER_TEMP, 'ci-tested-tree');
  fs.mkdirSync(directory, {recursive: true});
  fs.writeFileSync(path.join(directory, 'receipt.json'), `${JSON.stringify(receipt)}\n`, {mode: 0o600});
  core.info('Recorded the actual tested merge tree and current run attempt.');
}

module.exports = {gate, record, checkout, makeReceipt, successfulRun, successfulJobs,
  verifyReceipt, readArchive, boundedFetch, findProof, STEPS};
