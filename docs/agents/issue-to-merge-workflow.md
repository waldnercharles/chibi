# Issue-to-merge workflow

The reusable parent-controlled workflow lives at `.pi/workflows/issue-to-merge.js`.
It uses `wt` (worktrunk/worktree) isolation for the single writer, then runs
three fresh read-only reviewers per round. The parent remains responsible for
acceptance, final diff inspection, merge, and issue closure.

For human invocation, use the native prompt-workflow slash command:

```text
/prompt-workflow issue-to-merge 123 10 --bg
```

The first argument is the GitHub issue number; the second is the maximum review
rounds (default 3). The prompt delegates to the explicitly authorized
`issue-orchestrator` agent, which invokes the workflow script.

The underlying tool invocation is:

```json
{
  "workflowScriptPath": ".pi/workflows/issue-to-merge.js",
  "args": {
    "issue": "123",
    "maxRounds": 3,
    "repo": "waldnercharles/c-blueprints"
  },
  "async": true
}
```

`maxRounds` is clamped to 1–10. Each round has correctness, validation, and
API/user-flow review angles. A worker receives only concrete P0/P1 fixes before
the next round. P2 notes are report-only unless the parent explicitly promotes
them.

The implementation worker must report `WORKTREE_PATH`, `BRANCH`, and `PR_URL`.
Reviewers use the reported worktree path and inspect the actual files directly.
The workflow never merges or closes an issue automatically. Before publication,
the parent must confirm:

- every issue acceptance criterion has evidence;
- required tests, format checks, and CI are green;
- deferred visual evidence is explicitly documented;
- no P0/P1 findings remain; and
- the PR and issue relationship is accurate.

The worker should use the repository's `wt` installation and existing worktrunk
configuration. If `wt` is unavailable, it must stop and report the environment
blocker rather than silently editing the parent checkout.
