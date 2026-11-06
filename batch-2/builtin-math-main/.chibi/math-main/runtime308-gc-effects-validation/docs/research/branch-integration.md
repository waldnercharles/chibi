# Branch integration inventory

September 13, 2026. This pass inspected 18 existing worktrees and the local
branch inventory. Worktree names and ahead counts alone are insufficient to
choose changes: several older review branches contain equivalent changes under
different commit IDs.

| Work | Integration path | Decision |
| --- | --- | --- |
| Compiled behavior runtime, Tick scheduling, rendering and physics performance | PR #78, `cwaldner/compiled-behavior-paths` | Merged; combined main CI and Pages deployment passed. |
| Latest scrollbar smoothing and wheel travel | PR #79, `cwaldner/fix-scrollbar-smoothing` | Merged; main CI and Pages deployment passed. Includes the later wheel-travel correction from the primary checkout. |
| MCP graph authoring, registry discovery and GPU previews | PR #81, `cwaldner/mcp-authoring` | Merged; main CI and Pages deployment passed. |
| Editor authoring and reusable gameplay changes from the primary checkout | PR #80, `cwaldner/editor-authoring-integration` | Merged as `c271fed9`; main CI and Pages passed. Project, Scene Hierarchy and Document Outline navigation verified on the published editor. |
| `cwaldner/editor-workspace-ux` | Preserved worktree | Most changes overlap the primary snapshot. The primary has later node-control ordering, graph-local and optimizer fixes; merging this older tree wholesale would lose those fixes. |
| `cwaldner/issue-8-compiled-behaviors` | Superseded prototype | The complete runtime migration in #78 supersedes the earlier scalar compiler slice. |
| MCP 50/63/64/65 and PR 74 review worktrees | Historical review work | Their feature work is represented in merged PR history; do not replay stale review branches wholesale. |
| `build/architecture-worktree` and `build/editor-worktree` | Preserved historical prototypes | Contain dirty old experiments. Excluded from the current batch; any unique useful changes need extraction and current-contract validation. |

The primary checkout and older dirty worktrees were not reset, cleaned, or
rewritten. The editor snapshot excludes generated build directories and local
machine configuration.

## Editor integration

The combined branch preserves the activity rail, stacked vector controls, graph
locals, optimizer and authoring tools alongside compiled behavior scheduling.
Nested component bindings and scalar-to-vector conversions lower into the shared
IR. Instrumented data blocks resolve the full nested field offset at publication.
An existing optimizer runtime test caught the latter integration bug. Added
backend parity checks combine nested component fields, graph locals and vector
broadcasts in both normal and instrumented execution.

The profile callback retains main's separate setup and execution intervals and
also reports inclusive execution for the authoring profiler. Explicit MCP
profiling enables those timers; normal execution keeps them disabled.

The combined native UI suite passes, including the Project/Scene/Document activity
rail and query diagnostic navigation. The authored audit retains 34/34 events,
32/32 effectful local functions, 39/39 pure local functions, and 4/4 linked library
functions. Full native checks pass with leak detection. The web bytecode checks
also pass. Browser interaction confirms Project, Scene Hierarchy and Document
Outline navigation on the published site. Linux Debug and optimized tests,
rendering, no-SLJIT rejection and development reload all pass.

Rendered validation exposed a false missing-graph error in editor previews:
the preview compiler received authored library references before linking. Each
view now retains a linked snapshot, rebuilding it when the source or catalog
generation changes. Preview values map back to authored node IDs. Focused checks
cover edits, missing-library diagnostics and recovery when a library returns.

A final scale check caught an outdated benchmark population assumption and
exposed a full-population input-event lookup. The fixture now includes authored
enemies in its requested count. Event availability uses active asset generations
and compiled event tables; [paired measurements](input-action-lookup.md) record
the resulting gain and the remaining frame-time limits.

The older UI worktree still holds local edits. A content comparison found 177
identical changed files against the primary snapshot. Differences in node draw
order and optimizer cleanup include later fixes in the primary; the old
`src/editor/graph/arrange.*` files were moved into shared graph authoring.

## Validation issues discovered in this pass

- Native suspension after nested-call parameter copies used caller frame
  metadata after entering the callee. On sanitized x86 this restored an invalid
  parent step counter and falsely exceeded the execution budget. Fixed frame
  identity at the transition and added oracle step-count parity to the existing
  suspended-command regression. The focused sanitized x86 reproducer and full
  native suite pass.
- MCP `oneOf` validation returned before checking enclosing property constraints.
  Removed the early return; the existing null-recording regression passes.
- GPU preview smoke checks now use the same leak-check setting as existing
  native GPU checks because Cute shader compilation retains allocations at
  shutdown. Headless authoring and the main native suite retain leak checking.
- Linux LeakSanitizer found unfreed selection and undo-history storage in the
  graph-optimization test fixture. The fixture now releases both allocations.

Runtime measurements and the next optimization priorities are recorded in
[remaining runtime costs](remaining-runtime-costs.md).
