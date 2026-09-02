# Pi subagent and orchestration options

Research date: 2026-08-31

## Recommendation

Start with **`pi-subagents` (`nicobailon/pi-subagents`)**. It is the best lightweight foundation for the stated requirements:

- Native child Pi sessions, foreground or background.
- Custom agents as Markdown files with YAML frontmatter and a dedicated system prompt.
- Per-agent model, skills, context inheritance, extension loading, and strict tool allowlists.
- Explicit support for extension tools and direct MCP tools, e.g. `mcp:github/search_repositories`.
- Scripted orchestration via `workflowScript`: sequential chains, parallel fan-out, structured outputs, retries/steering, bounded budgets, and dynamic fan-out.
- Built-in review-loop prompt and a recursion/depth guard.
- Strongest public adoption found: about 3.4k GitHub stars, 595 forks, MIT license, and 362K monthly downloads on pi.dev at research time.

Install:

```sh
pi install npm:pi-subagents
```

Example project agent (`.pi/agents/github-researcher.md`):

```md
---
name: github-researcher
description: Research GitHub issues and APIs
extensions: pi-github-tools
skills: github-research
# Or use direct MCP tools when pi-mcp-adapter is installed:
# tools: read, bash, mcp:github/search_repositories
inheritProjectContext: true
---

Research the assigned question. Use the available GitHub tools, cite sources, and return a compact actionable brief.
```

A parent can use the package's normal `subagent` tool for one child, or a `workflowScript` for a loop-like pattern. The package recommends `clarify -> scout -> worker -> fresh reviewers -> worker`; the parent remains the orchestrator, which keeps the always-loaded system prompt small.

## Best fit when a task board matters

Pair it with **`@tintinweb/pi-tasks`**:

```sh
pi install npm:@tintinweb/pi-tasks
```

It provides structured task CRUD, dependencies, a persistent widget, background task output, and `TaskExecute`. Its integration with `pi-subagents` is explicit: tasks with `agentType` run as subagents, completion events update the task, and auto-cascade can execute unblocked dependents through the DAG. This is the closest match to “orchestrator plus todo list” without adopting a larger workflow runtime.

Caveat: the project labels itself an early release. It is a separate task system, so using it adds seven task tools and a widget. If minimal prompt/tool surface is more important than task visibility, skip it and use only `pi-subagents` plus a small project prompt.

## Strongest all-in-one orchestrator, but higher risk

**`@harms-haus/pi-subagent-tasks`** is the most literal match for autonomous goal execution. It exposes `run_tasks`, accepts dependent task pools, supports named profiles, worktree isolation, concurrency limits, `sequential`/`parallel`/`loop`/`gateLoop` composition, retries, and resumable pools. It is installed with:

```sh
pi install @harms-haus/pi-subagent-tasks
```

However, its public repository was only 46 commits and had zero stars at research time. It is therefore a good experiment, not my first production recommendation. Verify that its child profiles load the external tools and skills you need before relying on it; its task prompts are passed verbatim, while the profile owns tools/extensions/skills.

## Durable workflow alternative

**`@osolmaz/pi-workflows`** is attractive if pause/resume, durable state, checkpoints, controllers, or always-on automation are more important than lightweightness. It stores run state in SQLite, supports TypeScript workflow graphs, human decision checkpoints, controllers, and loops through graph edges. It can be installed with:

```sh
pi install npm:@osolmaz/pi-workflows
```

The trade-off is runtime surface and conceptual weight. Its agent steps primarily operate in the origin Pi conversation, so it is less directly a “fleet of independently customized child agents” than `pi-subagents`. Do not confuse this package with the unrelated `pi-agent-workflows` package; verify provenance when installing.

## Options not selected

- **`mjakl/pi-subagent`**: explicitly lightweight and supports custom child sessions, but lacks the stronger built-in orchestration/task ecosystem and has a much smaller audience.
- **`pi-tasks` (`nczz`)**: evidence-gated, compaction-safe task contracts, but its own README says it does not provide subagent orchestration yet.
- The official Pi example subagent extension is useful as a minimal reference, but it is intentionally a building block rather than a goal-oriented runtime.

## Suggested minimal stack

1. Install `pi-subagents` only.
2. Define two or three project agents in `.pi/agents/` with narrow prompts and explicit tools/skills. Use `extensions:` or `mcp:` entries rather than inheriting the entire ambient tool set.
3. Add a short project instruction such as: “For implementation tasks, use scout, worker, then reviewer; loop only when reviewer reports actionable findings; cap at three rounds.”
4. Add `@tintinweb/pi-tasks` only if a visible persistent DAG and automatic dependent-task execution are valuable.
5. Evaluate `pi-subagent-tasks` separately if you want one-call fire-and-forget execution with worktrees.

This keeps the base setup small and makes the orchestrator policy a project skill/prompt instead of permanently enlarging Pi's global system prompt.

## Sources

- [`nicobailon/pi-subagents` README](https://github.com/nicobailon/pi-subagents) — installation, built-ins, async runs, workflows, adoption.
- [`pi-subagents` agent reference](https://github.com/nicobailon/pi-subagents/blob/main/docs/agents.md) — custom prompts, tools, extensions, MCP tools, skills, context inheritance.
- [`pi-subagents` workflow reference](https://github.com/nicobailon/pi-subagents/blob/main/docs/workflows.md) — scripts, chains, fan-out, steering, budgets, loops, worktrees.
- [`@tintinweb/pi-tasks` README](https://github.com/tintinweb/pi-tasks) — task tools, dependency DAG, auto-cascade, and subagent RPC integration.
- [`@harms-haus/pi-subagent-tasks` README](https://github.com/harms-haus/pi-subagent-tasks) — dependent task pools, profiles, compose DSL, loops, worktrees, resume.
- [`@osolmaz/pi-workflows` README](https://github.com/osolmaz/pi-workflows) — durable SQLite state, workflows, checkpoints, controllers, and runtime model.
- [Pi package catalog: `pi-subagents`](https://pi.dev/packages/pi-subagents) — current package metadata and download signal.
- [Pi package catalog: `pi-tasks`](https://pi.dev/packages/pi-tasks) — comparison against task-contract tooling.
