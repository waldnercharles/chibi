---
description: Implement an issue, review-loop it, and prepare a PR
subagent: issue-orchestrator
fresh: true
---
Launch the project issue-to-merge workflow for GitHub issue `$1` with a maximum
of `${2:-3}` review rounds. Use the repository's `.pi/workflows/issue-to-merge.js`
workflow and `wt` worktrees. Do not merge or close the issue automatically.
Return the workflow receipt, PR URL, validation, findings, and any reason the
parent must intervene.
