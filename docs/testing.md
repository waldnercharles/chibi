# Test iteration

Use `make agent-list` and the capability-aware commands in [agent execution](agent-execution.md)
while iterating. Full/mixed GPU suites now require explicit hardware opt-in;
CI sets it explicitly and retains complete coverage. A focused selector may contain
GPU work: the old `test-fast` name alone does not establish CPU-only coverage.

## Commands and coverage

| Command | Coverage |
| --- | --- |
| `make test-fast TEST_ARGS=--graph-optimize` | Builds or reuses the native `test_suite` in `BUILD_DIR`, then runs only that selector. It does not run tooling scripts or the full suite. Coverage depends on the selector; GPU/mixed and unclassified selectors require the hardware profile. |
| `make test-list` | Builds or reuses `test_suite` and prints common focused selectors. |
| `make test-tools` | Runs the isolated Python and Node tooling tests, with `TEST_JOBS` workers (default 4). |
| `make test-slow` | Runs the full native `all_checks` suite and the separate whole-function audit. |
| `make test-compiler-parity` | Builds the behavior-backend check and runs the focused native behavior parity selector. |
| `make test-render` | Runs native GPU previews and hidden-window checks. It remains separate from `make test`. |
| `make test` or `make test-full` | Runs the complete tooling and native regression gate, including generated/build checks, `all_checks`, and the whole-function audit. |

`TEST_ARGS` is only accepted by `make test-fast` and is passed through to
`test_suite`. An empty selector is an error. Unknown selectors exit with status
2 instead of falling through to the complete suite. A second `--...` selector
or option is rejected with status 2 rather than silently ignored. With no
selector, `test_suite` runs the full `all_checks` suite. When a selector accepts
positional arguments, pass them after the selector as ordinary Make arguments.

The common focused selector list includes asset/support checks such as
`--asset-documents`, `--rcss`, and `--tree-watch`; authoring and graph checks such as
`--automation-authoring`, `--graph-editing`, and `--graph-optimize`;
runtime/compiler checks such as `--runtime-execution`, `--behavior-parity`,
and `--simulation`; Studio checks such as `--editor-graph`,
`--studio-documents`, and `--rmlui-kit`; modeling/material checks such as
`--modeling` and `--materials`; and game checks such as `--arena-combat`
and `--arena-encounter`. Run `make test-list` for the current common list.

The timing sweep found several costly calls inside the native full suite.
Final per-call readings were: `test_compiled_behaviors` 49.0s,
`test_automation_profile_scenario_inputs` 18.0s, `test_automation_search`
17.2s, `test_refactor` 16.8s, `test_rmlui_kit` 13.7s,
`run_editor_command_checks` 12.3s, `test_encounter` 11.9s, and
`test_materials` 10.7s. Their coverage remains in `make test`; focused
selectors expose these checks individually (`--compiled-behaviors`,
`--automation-profile-scenario-inputs`, `--automation-search`, `--refactor`,
`--rmlui-kit`, `--editor-commands`, `--arena-encounter`, `--material-thermal`,
and `--material-mpm`). The thermal and MPM selectors avoid running the larger
materials group when one is the relevant change. These timings describe one
debug/sanitizer build on one Mac and are guides, not performance guarantees.

The costly cases exercise distinct behavior, so I kept them in full coverage.
`test_compiled_behaviors` compares compiled behavior execution across the
bytecode, oracle, and SLJIT backends. Automation search checks thirteen search
contracts, including 38 user-intent queries that must rank their intended node
in the first three results. Refactor coverage uses separate Arena, general
content, and Studio projects to check event, asset, value, function, member,
refusal, and delete cases; its repeated project loads keep mutations isolated
and references realistic. Profile-scenario coverage includes HRC fluid work,
sparse input playback through warmup and measured frames, and named scenarios.
None of those repetitions was a proven duplicate check. The time savings come
from selecting only the closest check during iteration and parallelizing
isolated tool-test processes rather than deleting assertions.

The full gate is unchanged for CI: CI can continue to call `make test`.
Focused results are deliberately narrower coverage; they are useful feedback
for the changed code path and admit reviewed work to a batch under CUT62 below.
They do not replace the full gate.

## Batch validation (CUT62)

Use focused checks during each PR's iteration and admission, then run the full
required validation once on the **exact combined reviewed tree before any part
of the batch lands on main**. Assertions, coverage, sanitizers, reference images,
loads and release validation remain unchanged. This removes redundant full runs
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
4. **Run the complete gate once per candidate.** Keep `make test`/`test-full`
   intact: tooling, generated/build checks, native `all_checks` and the separate
   whole-function audit. Run `make test-render` as well. `test-tools`, `test-slow`,
   compiler parity or a collection of selectors alone is not the aggregate gate.
   Use the explicit hardware opt-in above and the existing display requirements.
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

### Small planning helper

After source review, on a clean committed worktree:

```sh
python3 tools/agent.py validation-plan --base origin/main --source reviewed-topic --confirm-eligible-members --scope focused --review-note 'Reviewer: graph optimizer only; checked callers and dependency effects' --selector=--graph-optimize > /tmp/cut62-plan.json
# Repeat the identical arguments after fetching main/source refs, adding:
python3 tools/agent.py validation-plan --base origin/main --source reviewed-topic --confirm-eligible-members --scope focused --review-note 'Reviewer: graph optimizer only; checked callers and dependency effects' --selector=--graph-optimize --verify /tmp/cut62-plan.json
make test-tool-validation-plan
```

Repeat `--source` for every batch member and `--selector` for relevant existing
CPU/GPU allowlisted selectors; `--tools` adds `make test-tools`. The default scope
is `unknown`; `unknown` and `mixed` list the full gate instead of focused commands.
Any declared source members require `--confirm-eligible-members`, including when
verifying a saved plan. Supply it only after freshly checking every member's
review, holds and required approvals. It records an operator statement, grants
no authority and cannot clear a hold or replace a denied-action approval. The
helper does not query the approval system; the owner retains those references
separately. Without source members it reports eligibility as `not-declared`, so
a generic plan is not an eligible batch roster. Omitting a held source from the
roster does not make its code eligible.
Classification is a reviewer's recorded declaration, never inferred from paths.
Review notes are data and are never executed. This helper only resolves local
refs and lists commands, changed paths, exact base/head/tree and pending gates.
It performs no fetch, builds, rendering, push, merge or status publication.
Save output outside the worktree. Run through `tools/cloud/bin/start_skill --`
where required by the cloud entry instructions.

`--verify` exits nonzero for dirty worktrees, changed refs/plans or Git failures.
Exit zero means only that the local plan still matches; it always reports
`full_verification:false`. The owner separately verifies remote freshness,
independent reviews, dependency/flag identity and actual execution evidence.
The helper does not run a selector and cannot convert one into a full pass.

## Test timings

Set `TEST_TIMINGS=1` on `make test`, `make test-fast`, or
`make test-slow` to add per-call elapsed timings to the native suite log.
`tools/quiet.py` prints the log path for each stage. Timing is opt-in so normal
test output stays compact.

## Baseline

On 2026-10-04, a clean isolated worktree at `96322c7191565c543e8375cde20e07d4fd2ab45a`
ran the original serial `make test JOBS=8` in 340.9 seconds. The full native
`all_checks` stage took 221.4 seconds, the tooling scripts took about 76
seconds, configure plus build checks took 37.8 seconds, and the separate
whole-function audit took 1.3 seconds. A focused
`build/test_suite --graph-optimize` invocation took 0.47 seconds on that
already-built sanitized binary. The focused invocation runs much less coverage;
it is not an equivalent-speed comparison with the full suite.

The first full gate after parallelizing tools completed in 277.8 seconds
(4m38s), with a 32.8-second build and 218.8-second native suite. A separate
`make test-tools TEST_JOBS=4` run took 24.4 seconds wall time. Against the
serial baseline, the complete gate was 63.1 seconds (18.5%) faster, and the
tooling stage dropped from about 76 seconds to 24.4 seconds. After the final
selector and argument-validation edits, the full gate passed again in 267.2
seconds (4m27s) with an incremental 26.0-second build, a 215.2-second native
suite, and a 1.3-second audit. These full runs still include all 21 tooling
tests and native checks; the 267.2-second rerun benefited from the current
build and should not be compared to the clean baseline as a cold-build result.

A separate warm
`make test-fast TEST_ARGS=--graph-optimize` completed in 2.83 seconds total;
the selector itself took 0.5 seconds, with configure at 1.0 second and an
incremental build at 0.1 second.

The newly split selectors were all run individually after the final argument
validation change: compiled behaviors 47.2s, automation search 17.2s,
automation refactor 5.0s, profile-scenario inputs 15.8s, editor commands
12.5s, material thermal 4.8s, and material MPM 3.6s. These are selector-stage
times from the quiet runner and exclude each wrapper's configure/build steps.
An unknown selector and a combined `--graph-optimize --materials` invocation
both exited 2 quickly; neither ran the full suite or silently ignored the
second flag.

The largest routine cost is running every native test in `all_checks` for
each small code change. Prefer the closest focused selector during iteration
and retain `make test` as the complete gate. Tool tests now run concurrently
in isolated processes with a four-worker default; raising `TEST_JOBS`
increases CPU and memory pressure.
