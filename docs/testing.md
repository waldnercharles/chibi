# Test iteration

## Temporary 60-second policy

Managed test executions, including aggregates, have one hard 60-second ceiling.
Known slow and unmeasured commands are quarantined; hardware opt-in cannot clear
it. See [the inventory](test-quarantine.md) for preserved native coverage,
retired script coverage and the original 12 exact admitted native tuples.

`make test`, `make test-short` and `make test-tools` run the same minimal C
pico_unit suite through the production `tools/test_policy.py` supervisor. The
suite covers essential execution policy, package/install preservation, room
service behavior and minimal supervisor/planner regressions. Packaging and room
service implementations remain Python/Node external programs; their tests are C.
The removed script suites have not been reproduced completely.

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
| `test`, `test-short`, `test-tools` | Same C tooling group under one shared 60-second deadline. |
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

CI remains partial coverage. Existing server-required checks, source review,
PR/ticket holds and configuration requirements still apply; short results cannot
replace unavailable required gates.

## Batch validation (CUT62)

Use focused checks during each PR's iteration and admission, then run the full
required validation once on the **exact combined reviewed tree before any part
of the batch lands on main**. Preserve assertions, sanitizers, reference images,
loads and release validation for each retained native gate. The tooling cleanup retires script suites and
does not claim equivalent coverage. This removes redundant full runs
on component PRs; it does not replace CUT50/CUT61 profiling and optimization of
the actual slow tests.

Batch-policy approval does not clear an explicit PR/ticket hold or a denied-action
approval requirement. The integration owner must enumerate every source member
and dependency, verify independent review and eligibility under the existing
approval rules, and retain the supporting approval references. Held code must
not enter an integration PR through cherry-picks, merges, copied patches or
dependencies to evade its hold. A new PR number or passing batch gate does not
clear the original restriction. Obtain the required approval through the existing
process before admitting it; no new authorization mechanism is created here.

1. **Name the integration owner.** One owner coordinates the batch, build slot,
   source review, evidence and eventual landing. Fetch main normally and record
   its full SHA. Pin each PR's full source SHA, dependencies and review result;
   a branch name alone is insufficient. The author reports focused commands,
   exit codes, timings and logs plus every pending check. Admission is provisional
   and is not permission to merge. Do not start costly gates before independent
   source review and batch coordination.
2. **Review scope explicitly.** An independent reviewer inspects source, tests,
   callers, generated outputs and dependency/build effects, and explains why the
   chosen checks cover the changed contracts. File extensions, a selector name,
   or a green focused run cannot establish absence of native/render/web impact.
   Unknown or mixed scope, broad contracts and unreviewed selectors fall back to
   full required validation; they receive no focused-only admission. Full work
   still waits for the review/build slot. Use mixed scope for changes spanning
   independent contracts or subsystems whose interactions are not bounded by
   the selected checks. Retain required backend, sanitizer,
   compiler parity, representative-load and web/UI checks for the affected code.
3. **Freeze the candidate.** In a clean isolated integration worktree based on
   the pinned main, combine only the reviewed source revisions. Independently
   review the combined diff and all conflict resolutions, including interactions
   between PRs. Commit the complete candidate and record `git rev-parse HEAD`
   and `git rev-parse 'HEAD^{tree}'` before testing. No tracked or untracked
   source edits may accompany the run. Build artifacts must originate from this
   tree and the recorded configuration; a shared stale binary is not evidence.
4. **Obtain required complete verification once per candidate.** The temporary
   quarantine currently disables native `all_checks`, the audit and render gates.
   Those requirements remain unsatisfied until measured admission is restored.
   Tooling, compiler parity or collected selectors alone do not establish the
   aggregate gate. Once admitted, retain explicit hardware opt-in and display
   requirements.
   Complete the required configurations below, format checks, affected native
   backends and web/UI validation (`make web` for UI changes), and existing
   release validation when preparing a release. Do not substitute previews for
   assertions or lower thresholds to gain speed.
5. **Verify identity and land one combination.** Before landing, fetch main and
   source refs again and recheck every member's holds and required approvals.
   Eligibility can change without a Git change; a new hold blocks landing.
   Compare the pinned base, all source SHAs, candidate SHA/tree
   and validation configuration with the evidence; verify a clean worktree again.
   The proposed landing tree must equal the tested tree, and its main parent/base
   must be the pinned main. Land the complete combination in one atomic main
   update through the authorized protected workflow. Do not merge component PRs
   sequentially using the final combination's results: those intermediate trees
   were not tested. If atomic landing is unavailable, stop batch publication or
   fully review and validate each actual intermediate candidate separately.

Any main advance (even unrelated), source-head change, batch membership change,
rebase, conflict resolution, source/generated-file edit, dependency/toolchain
change or compiler/build/runtime flag change invalidates the candidate evidence.
Reassemble, review and repeat the full required gate on the new frozen candidate.
Timeouts, failures, unavailable hardware, missing logs and absent/skipped CI jobs
remain pending or failed, never passes. No Actions, protection, permission or
credential changes are part of this policy.

Existing automation instructions requiring current-head gates still apply. A
batch meets them by making the exact combined reviewed candidate the current
integration head and running the gates there. A server-created merge/squash
commit may have a different commit ID: the owner must prove identical tree,
unchanged base and sources, and satisfy the server's required current-head checks
and review rules for that commit. Tree equality alone cannot transfer a status
check or satisfy a check that depends on commit metadata. If those rules require
new execution, run it. Component-head passes and this plan helper confer no
merge authority and cannot bypass branch protection.

### Configured checks versus execution evidence

The current definitions are [.github/workflows/ci.yml](../.github/workflows/ci.yml)
and [deploy-web.yml](../.github/workflows/deploy-web.yml). Inspect the definitions
on the frozen candidate; their presence describes configuration, not a run.

| Configured check | Required evidence on the candidate |
| --- | --- |
| Native Debug, sanitizers ON, SLJIT ON | Actual `make test` and `xvfb-run -a make test-render` results with the configured toolchain/flags |
| Native RelWithDebInfo, sanitizers OFF, SLJIT ON | Actual aggregate and render results in that configuration |
| RelWithDebInfo Arena scaling | `CBLUEPRINT_BENCH_COMBAT=1 CBLUEPRINT_BENCH_REQUIRE_PARALLEL=1 build/test_suite --bench-arena-scene-scale 2048 240 0` under the configured display |
| Debug job's no-SLJIT check | Separate Debug/sanitized, `CHIBI_ENABLE_SJLIT=OFF` configuration and `check-behavior-backend` result |
| Debug job's development reload build | Separate RelWithDebInfo, reload ON, sanitizers OFF, SLJIT ON configuration and `chibi chibi_dll` build result |
| Format | `make check-format` (CI uses `sh tools/clang-format.sh --check`) |
| Web workflow / affected web or UI work | Required Emscripten build, packaging and web/UI verification; record threaded validation when requested/applicable |

`CHIBI_ENABLE_SJLIT` is the existing CMake option spelling. One local Debug run
does not establish the other matrix rows. Record each required row as executed,
failed or pending, with exact commit/tree, commands, exit codes, elapsed times,
logs, toolchain/dependency identity, build/runtime flags and actual backend/device.
If CI is unavailable, the owner must obtain equivalent authorized execution and
still meet server rules; absence of CI cannot turn the table green. Release
packaging, optimized-build and release-specific validation remain additional
requirements, not waived by a successful development batch.

### Planning helper

```sh
python3 tools/agent.py validation-plan --base origin/main --scope focused --review-note 'Reviewer: essential tooling changes' --tools
```

`--tools` and `--tool=native-tools` emit `make test-tools`. Other former tool
choices are retired. Repeat `--source` for every reviewed batch member and use
`--confirm-eligible-members` only after checking review, holds and approvals.
Exact admitted CPU selectors can be requested with `--selector=--name`.
Unknown/mixed scope keeps full requirements pending; it does not run a full gate.
`--verify <saved-plan>` checks local identity, not remote freshness or execution.
Keep saved plans outside the worktree. A plan grants no merge authority.

## Historical timing evidence

The original exact native admissions are recorded in
[test quarantine](test-quarantine.md#retained-short-coverage). The former tooling
suite's 24.4-second parallel run and longer native/Studio/render aggregates
belong to their recorded revisions and removed runners. They are not measurements
of the new C group. PR186 retains the original process-cleanup reproduction and
startup-margin evidence; current results must identify the actual reviewed head.
