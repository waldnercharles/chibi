# Pi workflow options: audience and maintenance review

Research date: 2026-08-31

## Important correction

The earlier recommendation of `pi-agent-workflows` was too confident. The npm package currently reports version `0.1.2` and its repository is `an2tha/pi-workflows`, which currently has **0 stars** and 1 open issue. `osolmaz/pi-workflows` is a separate repository with 243 stars; it should not be treated as the same package without verifying the package provenance. We should not use `pi-agent-workflows` as a production dependency based on its current public audience.

Stars and downloads are imperfect maintenance signals, but they are useful for the requested audience/community filter. The figures below were retrieved from GitHub/NPM on the research date.

## Candidates

| Project | GitHub signal | Recent activity | What it provides | Assessment |
|---|---:|---|---|---|
| `pi-subagents` / `nicobailon/pi-subagents` | 3,410 stars, 595 forks; 362K monthly npm downloads | Active same-day commits; MIT | Child agents, parallel/chain workflows, async runs, project agents/chains, session/artifact handling | **Best community-backed Pi foundation**, but not a durable GitHub controller |
| `pi-dynamic-workflows` / `QuintinShaw/pi-dynamic-workflows` | 468 stars, 93 forks | Active daily-ish activity; MIT | Dynamic workflows, concurrency, model routing, journaled replay/resume | Promising, but still a small project and background runs require Pi to stay alive |
| `pi-workflows` / `osolmaz/pi-workflows` | 243 stars, 14 forks | Active same-day commits; MIT | Typed workflow graphs, live viewer, durable queue, controller resources, resume | More compelling than `pi-agent-workflows` by audience, but package identity/provenance must be pinned and verified |
| `pi-fabric` / `monotykamary/pi-fabric` | 164 stars, 15 forks | Active same-day commits; MIT | Agents, actors, workflows, mesh, approvals, isolation, budgets, durable coordination | Broad and active, but complex and relatively young; trusted JS is not a security boundary |
| `taskflow` / `heggria/taskflow` | 67 stars, 8 forks | Active within the week; MIT | Declarative DAGs, verification, retries, approvals, isolation, resume/replay, host adapters | Strong engineering claims and tests, but small audience and 0.2/0.3 beta status |
| `pi-subagentura` / `lmn451/pi-subagentura` | 9 stars, 5 forks | Active, but tiny audience; MIT | Async workflow graphs, pipelines, attached children | Not suitable as the production foundation under the audience requirement |
| `pi-open-agents` | 12 stars, 4 forks | Active, but tiny audience | `.agent.md` roles, per-agent models, permissions, skills | Useful role layer, not orchestration |
| `pi-subagents-workflows` | 0 stars | Active but tiny | Saved typed workflows atop `pi-subagents` | Explicitly does not add journal replay/edit-resume; not enough alone |

## What the popular option actually solves

`pi-subagents` is the clear audience/maintenance winner. It provides built-in scout, researcher, worker, reviewer, oracle, and delegate roles; parallel and chained delegation; async execution; project-local agents and chains; and configurable concurrency. It does not automatically implement issue-to-PR automation, durable external claims, or a GitHub event loop.

Sources:
- https://pi.dev/packages/pi-subagents
- https://github.com/nicobailon/pi-subagents

## What remains custom in every option

No researched Pi package provides the complete desired GitHub lifecycle. A GitHub adapter still needs to handle:

- eligible issue/PR discovery
- atomic task claims and duplicate suppression
- worktree allocation
- issue implementation and PR creation
- review and review-feedback routing
- stale/interrupted run recovery
- GitHub App writes and human approval gates

The adapter should be a thin deterministic controller. The selected Pi package should own agent execution, not GitHub state truth.

## Decision

Given the audience requirement, use **`pi-subagents` as the initial dependency** and add only a small project-local deterministic controller around it. Do not adopt `pi-agent-workflows` without a substantial change in its public maintenance/audience signal.

Use `pi-dynamic-workflows`, `pi-workflows`, `pi-fabric`, or `taskflow` only as explicit experiments after reviewing their source and pinning exact versions. Do not combine multiple workflow runtimes.

If durable queueing/resume becomes mandatory before we can build the controller, the next candidate to evaluate is `osolmaz/pi-workflows` (not the unrelated `pi-agent-workflows` npm package), followed by `taskflow` if its beta status is acceptable. `pi-fabric` is the broadest but should not be the first production choice because it adds substantially more runtime surface.

## Sources

- https://pi.dev/packages/pi-subagents
- https://pi.dev/packages/pi-fabric
- https://pi.dev/packages/pi-taskflow
- https://pi.dev/packages/pi-dynamic-workflow
- https://pi.dev/packages/pi-subagents-workflows
- https://github.com/osolmaz/pi-workflows
- https://github.com/QuintinShaw/pi-dynamic-workflows
- https://github.com/heggria/taskflow
- https://github.com/lmn451/pi-subagentura
- https://github.com/andrea-tomassi/pi-open-agents
