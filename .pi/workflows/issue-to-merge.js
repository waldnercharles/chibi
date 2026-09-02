/*
 * Parent-controlled issue implementation and review workflow.
 *
 * Invoke with subagent({
 *   workflowScriptPath: ".pi/workflows/issue-to-merge.js",
 *   args: { issue: "123", maxRounds: 3 }
 * })
 *
 * The implementation worker owns a worktrunk (`wt`) worktree. Reviewers are
 * read-only; only the worker may edit. The parent still decides publication
 * and merge after this workflow returns.
 */

const issue = String(args.issue || "");
const maxRounds = Math.max(1, Math.min(10, Number(args.maxRounds || 3)));
const repo = String(args.repo || "waldnercharles/c-blueprints");

if (!issue) {
  throw new Error("args.issue is required");
}

function report(run) {
  if (run && run.output) {
    return String(run.output);
  }
  return JSON.stringify(run);
}

const reconnaissance = await runs.run("issue-scout", {
  agent: "scout",
  task: `Inspect GitHub issue #${issue} in repository ${repo} and the current checkout. Read AGENTS.md, CONTEXT.md, relevant ADRs, and issue comments. Produce a concise implementation brief: every acceptance criterion, dependencies, current code seams, likely files, tests, and explicit risks. Do not edit files, create branches, or run subagents.`
});

const implementation = await runs.run("implementation", {
  agent: "worker",
  task: `Implement GitHub issue #${issue} in repository ${repo}. Use worktrunk (wt) for isolation: create/switch a dedicated wt worktree and branch from current main, and report WORKTREE_PATH, BRANCH, and PR_URL in your handoff. Read the reconnaissance below, AGENTS.md, CONTEXT.md, and the issue directly. Implement every acceptance criterion through the smallest coherent public seams, add regression tests, run focused validation, and open/update a PR if authorized. Do not merge. Preserve unrelated parent changes. If an acceptance criterion needs an unapproved product decision, stop and report it.\n\nRECONNAISSANCE:\n${report(reconnaissance)}`
});

let lastReport = report(implementation);
let stopped = false;
let roundsRun = 0;

for (let round = 1; round <= maxRounds; round += 1) {
  roundsRun = round;
  const reviews = await runs.all([
    {
      key: `correctness-${round}`,
      agent: "reviewer",
      context: "fresh",
      task: `Review the current implementation for GitHub issue #${issue} in ${repo}. The worker handoff follows; use its WORKTREE_PATH to read the actual files directly. Read AGENTS.md, CONTEXT.md, issue criteria, and the current diff. Focus on correctness, regressions, ownership, and public behavior. Report only concrete P0/P1/P2 findings with source proof and tests/repro. End with exactly Merge verdict: BLOCK or Merge verdict: OK (or OK with notes). Do not edit or run subagents.\n\nWORKER HANDOFF:\n${lastReport}`
    },
    {
      key: `validation-${round}`,
      agent: "reviewer",
      context: "fresh",
      task: `Review the current implementation for GitHub issue #${issue} in ${repo}. Use the WORKTREE_PATH in the worker handoff to inspect the actual files. Focus on every acceptance criterion, tests, CI, documentation, and evidence. Report only concrete P0/P1/P2 findings with proof; distinguish deferred evidence from unmet criteria. End with exactly Merge verdict: BLOCK or Merge verdict: OK (or OK with notes). Do not edit or run subagents.\n\nWORKER HANDOFF:\n${lastReport}`
    },
    {
      key: `api-${round}`,
      agent: "reviewer",
      context: "fresh",
      task: `Review the current implementation for GitHub issue #${issue} in ${repo}. Use the WORKTREE_PATH in the worker handoff to inspect the actual files. Focus on API/user-flow parity, maintainability, integration, and scope creep. Report only concrete P0/P1/P2 findings with proof. End with exactly Merge verdict: BLOCK or Merge verdict: OK (or OK with notes). Do not edit or run subagents.\n\nWORKER HANDOFF:\n${lastReport}`
    }
  ]);

  const reviewText = reviews.map(report).join("\n\n--- REVIEW ---\n\n");
  const decision = await runs.run(`decision-${round}`, {
    agent: "oracle",
    context: "fresh",
    task: `Synthesize these three fresh reviews for issue #${issue}. The user requires all acceptance criteria and no blocking findings. Ignore optional/speculative notes. If any P0/P1 remains, output the first line FIX_REQUIRED. If none remain, output the first line MERGE_READY. Then summarize concrete reasons and any report-only P2 notes. Do not edit files.\n\nREVIEWS:\n${reviewText}`
  });
  const decisionText = report(decision);

  if (decisionText.includes("MERGE_READY")) {
    stopped = true;
    lastReport = `${lastReport}\n\nFINAL REVIEWS:\n${reviewText}\n\nDECISION:\n${decisionText}`;
    break;
  }

  if (round === maxRounds) {
    lastReport = `${lastReport}\n\nFINAL REVIEWS:\n${reviewText}\n\nDECISION:\n${decisionText}`;
    break;
  }

  const fix = await runs.run(`fix-${round}`, {
    agent: "worker",
    task: `Apply only the concrete P0/P1 fixes identified below to the current issue #${issue} implementation. Use the same WORKTREE_PATH and branch from the prior handoff via wt; do not create a competing writer/worktree. Preserve approved scope, add regression tests, validate, push/update the PR, and report WORKTREE_PATH, BRANCH, PR_URL, changed files, and commands/results. Do not merge. If a finding requires an unapproved product decision, stop and report it.\n\nPRIOR HANDOFF:\n${lastReport}\n\nREVIEWS:\n${reviewText}\n\nDECISION:\n${decisionText}`
  });
  lastReport = report(fix);
}

return {
  issue,
  repo,
  roundsRun,
  mergeReady: stopped,
  handoff: lastReport,
  note: "Parent must inspect the final diff, confirm CI, and decide whether to merge and close the issue."
};
