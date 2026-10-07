# Test iteration

## Fast, targeted tests

Use deterministic C pico_unit tests as the primary regression checks. Cover a
specific behavior and its failure cases through the smallest public runtime
interface that exercises it. Advance a synthetic clock for time-dependent
behavior; assert values, ownership, geometry, callback counts and work counts
rather than elapsed-time thresholds. Keep compilation separate from execution.

The goal is for every test to be fast and targeted. When replacing a smoke test,
map each meaningful assertion to its replacement first. Remove duplicate
entrypoints and measurement-only loops from regression commands. Keep unique
pixel, renderer, backend and authored-workflow assertions until a focused test
covers their contract. A short unit result does not satisfy unrelated integration
requirements or clear a PR hold. The broad suites below still need migration.

For iframe changes, after building `test_suite`, use:

```sh
make agent-test AGENT_SELECTOR=--iframe-updates
make agent-test AGENT_SELECTOR=--kit-documents
```

`--iframe-updates` checks blank-document model isolation, same-frame fitted
geometry and callback lifetimes, bound whole-document replacement with
conditional/list views, and layout refresh with exact callback counts and
synthetic animation time. `--kit-documents` covers the kit's document placement,
focus, popup and input contracts. Neither needs a window or GPU rendering.

`make agent-test AGENT_SELECTOR=--ui-tab-list` runs the existing tab-strip input
and layout checks independently of the other control fixtures. It preserves all
10 assertions for selection, focus, disabled tabs, selected-row reveal, wheel
scrolling, layout stability and middle-click close. The reviewed CPU-only
entrypoint completed in 1.743 seconds under the production 60-second supervisor
with Debug ASan+UBSan during admission. Record fresh results on each candidate;
this does not replace authored Studio or visual tab checks.

The duplicate `--studio-panel-lifecycle` capture entrypoint and the optional
1,000-frame blank-document profiling loop have been retired. The authored panel
lifecycle assertions remain in the quarantined Studio suite; historical profiling
captures remain research evidence for their recorded revisions.

## Temporary 60-second policy

Managed test executions, including aggregates, have one hard 60-second ceiling.
Known slow and unmeasured commands are quarantined; hardware opt-in cannot clear
it. See [the inventory](test-quarantine.md) for preserved native coverage,
retired script coverage and exact admitted native tuples.

`make test` and `make test-short` run six pure C execution-policy cases through
`tools/test_policy.py`. This default checks execution-policy decisions only.
`make test-tools` runs the complete C tooling group: execution policy,
package/install and configure preservation, room service, publication,
supervisor cleanup/deadlines and validation-planner regressions. Packaging,
publication and room service implementations remain external programs; C tests
own their assertions. Select this complete group for affected tooling contracts.
CI runs `make test-tools`; the planner's `--tools` also selects that complete group.
Both modes retain one shared 60-second supervisor deadline. Retired script suites
have not been reproduced completely.

Configure/build separately and report that time separately from test execution:

```sh
make tools-build
make test
make build BUILD_TARGET=test_suite
make test-studio-selectors
make test-list
make test-fast TEST_ARGS=--graph-optimize
```

| Command | Current coverage |
| --- | --- |
| `test`, `test-short` | Six pure C execution-policy unit cases under one deadline. |
| `test-tools` | Complete C tooling and process integration group under one shared deadline. |
| `test-studio-selectors` | Bounded C process checks against a separately built native suite, CPU-only discovery and argument handling. |
| `test-fast` | One exact admitted native argument tuple, with complete existing assertions. |
| `test-list`, `agent-list` | Admission inventory without test execution. |
| `test-full`, `test-slow`, `test-render`, `test-compiler-parity` | Quarantined, disabled and not passed. |

Timeout is failure. Nested managed wrappers inherit the deadline; no child gets
a fresh aggregate budget. Multi-goal test execution is unadmitted. Direct native
binaries, raw CMake checks and controller captures do not universally enforce
the ceiling; do not use them to bypass it or rebuild slow suites from chunks.
The supervisor verifies cleanup discovery before test work. Linux subreaping
contains escaped sessions; arbitrary already-detached descendants are not proven
contained on macOS. Platform validation must report what actually ran.

For repeated admitted C workloads, `benchmark_trials` accepts three or five trials
and a total 3–60-second timeout including cleanup. It preserves workload output
and reports elapsed time. The Make benchmark aggregate remains quarantined.
Retired Python drivers' generated-C comparisons and report formats were not
migrated individually. Research/review command transcripts, measurements and
hashes are historical records of their revisions, not current reproduction
instructions or passes for this cleanup.

CI remains partial coverage. Report the checks that actually ran. Applicable
server-required checks, independent review, explicit holds and approval rules
remain binding.

## Batch validation (CUT62)

Publication uses independently reviewed validation for the affected contracts.
A change spanning several subsystems can use focused checks when review accounts
for each changed contract and their interactions. A mixed scope label or an
unavailable full suite does not automatically block landing.

1. **Review and choose checks.** Independently inspect the diff, callers, tests
   and dependency/build effects. Record which contracts changed and why the
   selected admitted checks cover them. Include applicable backend, sanitizer,
   load, web/UI and release validation. File extensions and selector names alone
   do not establish coverage. Resolve unknown impact before publication. When
   affected coverage is quarantined, provide a reviewed admitted replacement;
   otherwise that specific coverage gap blocks landing. Unaffected quarantined
   suites remain disabled and not passed, without blocking the change.
2. **Freeze and validate.** Use a clean candidate based on freshly fetched main.
   For a batch, pin each reviewed source and review the combined interactions
   and conflict resolutions. Record the candidate commit/tree, configuration,
   commands, exit codes and logs. Run selected checks, formatting and affected
   builds against that candidate. Use `make web` and web verification for UI
   changes. Preserve assertions and failure cases; previews do not replace them.
3. **Publish the tested tree.** Recheck main and sources, explicit PR/ticket
   holds, required approvals and actual server protections. Land the complete
   tested combination atomically through the authorized workflow. A new hold
   blocks publication even when Git is unchanged. Held code stays excluded,
   including indirect dependencies and copied patches. If the base, source,
   tree, conflict resolution, dependency or validation flags change, review and
   validate the new candidate. Intermediate batch trees require their own review
   and validation.

Local execution evidence is sufficient when CI is disabled or unavailable unless
server protections actually require CI checks. Workflow configuration describes
possible jobs, not mandatory execution of every matrix row. Inspect applicable
server requirements before landing; required checks and approvals remain binding.
An absent CI job is not a pass. Preserve existing backend and configuration
requirements for each selected check, and report other coverage as unexecuted.
Full native/render suites remain quarantined under the 60-second policy.

A server-created merge/squash commit may have a different ID. Prove identical
tested tree and unchanged base and source revisions, and satisfy current-head
server requirements. Tree equality does not transfer a required status check or
replace a metadata-dependent check. Plans and component-head results grant no
merge authority.

### Planning helper

```sh
python3 tools/agent.py validation-plan --base origin/main --scope mixed --review-note 'Independent review: affected tooling and runtime contracts covered by selected checks' --tools --selector=--graph-optimize
```

The helper records Git identity and admitted commands; it does not execute them
or certify coverage. Both `focused` and `mixed` require a review note and selected
checks. `unknown` records unresolved scope. Use `--tools` (or
`--tool=native-tools`) for the C tooling group and repeat `--selector=--name` for
admitted CPU selectors. For batches, repeat `--source` and confirm eligibility
only after checking every source's review, holds and approvals.
`--verify <saved-plan>` checks local identity, not remote freshness or execution.
Keep saved plans outside the worktree.

## Historical timing evidence

The original exact native admissions are recorded in
[test quarantine](test-quarantine.md#retained-short-coverage). The former tooling
suite's 24.4-second parallel run and longer native/Studio/render aggregates
belong to their recorded revisions and removed runners. They are not measurements
of the new C group. PR186 retains the original process-cleanup reproduction and
startup-margin evidence; current results must identify the actual reviewed head.
