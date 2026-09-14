# Branch integration inventory

September 13, 2026. This pass inspected 18 existing worktrees and the local
branch inventory. Worktree names and ahead counts alone are insufficient to
choose changes: several older review branches contain equivalent changes under
different commit IDs.

| Work | Integration path | Decision |
| --- | --- | --- |
| Compiled behavior runtime, Tick scheduling, rendering and physics performance | PR #78, `cwaldner/compiled-behavior-paths` | Integrate with current main and MCP authoring; validate the combined native and web builds. |
| Latest scrollbar smoothing and wheel travel | PR #79, `cwaldner/fix-scrollbar-smoothing` | Merged; main CI and Pages deployment passed. Includes the later wheel-travel correction from the primary checkout. |
| MCP graph authoring, registry discovery and GPU previews | PR #81, `cwaldner/mcp-authoring` | Integrate after Linux sanitizer checks; included in the combined performance branch. |
| Editor authoring and reusable gameplay changes from the primary checkout | Draft PR #80, `cwaldner/editor-authoring-integration` | Committed and pushed from an isolated snapshot. Must reconcile the newer graph contracts with the compiled runtime before merging. |
| `cwaldner/editor-workspace-ux` | Preserved worktree | Most changes overlap the primary snapshot. The primary has later node-control ordering, graph-local and optimizer fixes; merging this older tree wholesale would lose those fixes. |
| `cwaldner/issue-8-compiled-behaviors` | Superseded prototype | The complete runtime migration in #78 supersedes the earlier scalar compiler slice. |
| MCP 50/63/64/65 and PR 74 review worktrees | Historical review work | Their feature work is represented in merged PR history; do not replay stale review branches wholesale. |
| `build/architecture-worktree` and `build/editor-worktree` | Preserved historical prototypes | Contain dirty old experiments. Excluded from the current batch; any unique useful changes need extraction and current-contract validation. |

The primary checkout and older dirty worktrees were not reset, cleaned, or
rewritten. The editor snapshot excludes generated build directories and local
machine configuration.

## Remaining editor integration

The editor snapshot adds `PIN_BINDING_LOCAL`, newer field projection and vector
binding behavior, plus authored gameplay and tooling changes. Its standalone
suite does not prove those paths run through the compiled behavior runtime.
The combined follow-up must preserve those authoring contracts while lowering
them into the shared behavior program, then exercise native SLJIT and portable
bytecode, inspection, hot apply, editor interaction and the web build.

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
