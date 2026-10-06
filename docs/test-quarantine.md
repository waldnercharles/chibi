## Enforcement boundary

The temporary ceiling and quarantine are enforced by managed Make test targets,
`tools/test_policy.py`, and agent test commands. Direct `test_suite`, raw test
scripts, CMake `check-*` targets and controller/capture entry points are unchanged;
they do **not** universally enforce this policy. Do not invoke them to bypass it.
Underlying assertions and fixtures remain intact. No hardware opt-in clears the
managed quarantine. Do not reconstruct a slow suite through repeated selectors.

Managed tests run on supported POSIX hosts, including macOS. The outer runner
owns a process group; nested policy/agent test wrappers stay in it and inherit
one absolute monotonic deadline. On timeout/interruption and completion, the
owner kills remaining group members. A nested portable timeout freezes and
walks its live child subtree with the system `ps`, then kills descendants before
the parent; it never kills the shared ancestor group or sibling processes.
Linux additionally uses an isolated
subreaper to own/reap orphaned descendants that escape with `setsid`. That
stronger escaped-session guarantee is Linux-specific: arbitrary non-managed
children that have already detached and been reparented before cleanup are not
proven contained on macOS. This
limitation does not disable Mac short coverage or label it slow. No system
permission, configuration change or dependency is required. Platform-selection, group-reuse and nested subtree cleanup regressions run here;
actual macOS policy checks also pass, with four Linux-specific tests skipped.
The original Linux-hosted Darwin-branch regression
reproduced the old delayed-marker escape in 0.737s, then passed in 0.731s after
the fix. It uses a 0.15s nested deadline, a 0.4s grandchild marker and 0.5s
observation while the outer aggregate stays alive, and verifies that a sibling
in the same shared group survives.


Linux probes its own `/proc/self/task/<pid>/children` reader before test work.
Some kernels support subreaping without exposing that file. If it is missing,
unreadable or malformed, the supervisor verifies the existing system `ps`
PID/parent reader and uses it to find/reap its adopted children instead. The
one-second read-only probe must see the supervisor and excludes its own completed
probe PID from cleanup. The same reader serves portable live-subtree cleanup.
If no usable discovery reader exists (including denied, missing, malformed or
timed-out `ps` when needed), validation fails before launching test workloads.
No kernel option, installation or security setting is changed. A
missing-file simulation verifies ordinary success, timeout cleanup of a real
`setsid` descendant, and refusal before test launch when both readers fail.

Cleanup timeout fixtures now allow two seconds for nested interpreter startup,
require a child startup marker, and keep parents alive for six seconds. Escaped
descendants attempt their marker after three seconds; a 3.1-second observation
after cleanup detects a surviving descendant. The portable fixture's sibling
also remains alive beyond the timeout. The interruption fixture allows three
seconds for startup before sending SIGTERM, within a six-second wrapper deadline.
These fixture margins replace the startup-sensitive 0.15–0.35-second budgets;
they do not change the inherited production deadline or reset the default
aggregate's single 60-second ceiling.

## Disabled coverage inventory (temporary; NOT PASSED)

| Disabled command / coverage | Reason |
| --- | --- |
| `test-full` (formerly default `test`) and `test-slow`: native `all_checks` aggregate | Native test execution 316.4s; historical baselines 215–221s. |
| `test-render`: scene documents, shader detail GPU, Studio documents, RmlUi Vulkan/GLES render, Arena play, three-frame app smoke and `check-mcp-gpu` | PR164 Debug aggregate 1059s, optimized aggregate 125.66s. Individual unadmitted components are not presumed fast. |
| Full `--studio-documents`, `--studio-panels`, `--studio-shell` | Debug Studio 982s, panels 319s, shell 103s; optimized Studio 83s. Only the exact tabs/readout commands below are admitted. |
| `test-tools` aggregate | Historical 24.4s covered the earlier 21-command set; the expanded aggregate remains unadmitted. |
| `test-tool-behavior-code`, `test-tool-cloud-entry`, `test-tool-cloud-emscripten-setup` | No command-level timing admission yet. |
| Build-integrated checks formerly in `test-full`: `check-generation`, `check-reflect`, `check-reload`, `check-dev`, `check-mcp`, `check-assets`, `check-behavior-backend` | No command-level admission. Compilation is separate from executing these checks. |
| `test-compiler-parity`, no-SLJIT smoke execution, web `check-behavior-backend` | Unadmitted runtime; CI retains smoke compilation, not rejection-test credit. |
| `test-package`, `benchmark`, `profile-compare`, CUT61 historical 2016-frame capture | Unadmitted runtime; no long capture authorized. Short lifecycle/profile/selector regressions are distinct from this capture. |
| `agent-gpu-test`, other native selector/argument tuples | Unmeasured or outside the explicit timing inventory. Capability classification is not timing evidence. |
| Multi-goal validation, e.g. `make test test-render` | Unadmitted aggregate; rejects before test/build execution. |

## Retained short coverage

`make test` / `make test-short` runs policy, validation-plan and execution-policy
regressions under **one shared 60-second deadline**. This is a purposeful policy regression group,
not a partition of the old full native suite. The default contains the policy, validation-plan and execution-policy Python
checks and the MCP check, including missing-cleanup-capability regressions.
Exact final-head counts and command wall times are recorded in the PR evidence.
 Each independently requested native invocation below retains
its complete existing assertions and has its own maximum 60-second deadline:

| Exact native arguments | Existing execution evidence |
| --- | --- |
| `--graph-optimize` | 0.5s |
| `--compiled-behaviors` | 47.2s |
| `--automation-search` | 17.2s |
| `--automation-refactor` | 5.0s |
| `--automation-profile-scenario-inputs` | 15.8s |
| `--editor-commands` | 12.5s |
| `--material-thermal` | 4.8s |
| `--material-mpm` | 3.6s |
| `--whole-function-audit` | 1.3s |
| `--studio-documents tabs` | 8s |
| `--studio-shell readout_follows_camera` | 3s |
| `--bench-arena-scene-scale 2048 240 0` | 1.4s |

These times are historical, host/configuration-specific evidence, not new passes
or guarantees. Source: [documented baseline/selector timings](testing.md#baseline)
and PR164/scaling evidence. Existing hardware/display/capability requirements
remain in force. Preserve the original Arena combat/parallel environment and
RelWithDebInfo configuration. Do not combine this table into a reconstructed full
suite. `make test-fast TEST_ARGS='<exact arguments>'` checks admission before
building, then runs the bounded command; configuration/build is separately timed.

`make test-studio-selectors` builds the suite separately and runs the existing
CPU-only Studio CLI regression command, under one 60-second deadline. Its supplied
main-source timing is 3.71s; it does not launch Studio rendering. CUT61's supplied
PR183 lifecycle (0.25s), context-profile aggregate (2.25s) and default-profile
(0.34s) results are separate evidence for that source/command identity; they do
not authorize the long capture or imply those PR183 tests exist on this base.

The following original tooling commands are individually admitted, each with
its complete existing script under the ceiling. Their command shapes appear in
the preserved 21-tool baseline at `96322c7191565c543e8375cde20e07d4fd2ab45a`, followed
by the documented 24.4s aggregate measurement. This evidence supports individual
admission just as historical native timings do; it does not certify current runs.

```
test-tool-kdl-assets
test-tool-fetchcontent-cache
test-tool-configure-cache
test-tool-dependency-package
test-tool-package-web
test-tool-publish-web
test-tool-package-native
test-tool-install-game-runtime
test-tool-runtime-update-build
test-tool-quiet
test-tool-workspaces
test-tool-workspace-retirement
test-tool-workspace-make
test-tool-workspace-install
test-tool-mcp-package
test-tool-web-serve
test-tool-web-workspace
test-tool-web-storage
test-tool-web-frames
test-tool-web-isolation
test-tool-collab-client
```

Additionally, `test-tool-validation-plan` and `test-tool-execution-policy` remain
admitted and included in the default aggregate. The latter includes its small C
fixture compilation. `test-tool-workspace-make` passed 12 checks in 1.108s on the
preceding revision. Native tests were not rerun to justify admission.

The initial MCP failure (`active_device: null` rather than `fixture GPU`) was a
restricted execution-environment launch failure. One matched base/candidate
reproduction completed in 0.238s; both failed identically. A launch probe exposed
Node child creation returning `EPERM`, with no controller output. Scoped approved
execution outside that tool restriction passed the unchanged fixture and default
aggregate. No MCP implementation, fixture, expected value, OS setting or
repository permission was changed to obtain the pass. A restricted environment
that cannot spawn the fixture must still report failure, not skip it.

The planner rejects quarantined `--tools` aggregate requests and unadmitted
GPU/CPU selectors. Use repeatable `--tool=validation-plan` (or another admitted
tool name) for complete individual tooling commands. Quarantined integration
requirements are recorded separately from runnable commands; the plan never
satisfies those requirements or silently substitutes a full suite.

## CI and restoration

CI now labels its jobs **Native partial coverage (Debug/RelWithDebInfo)**, reports
this inventory, runs the short aggregate (including its MCP check) and bounded graph/scaling checks, and
retains compile/configuration, formatting and web/reload builds. On 2026-10-06,
read-only GitHub queries returned no classic `main` protection (HTTP 404, Branch
not protected) and no applicable branch rules (`[]`). No settings were modified.
If a required old context is introduced elsewhere, it remains unsatisfied; the
new partial label is not a substitute for full certification. Existing holds,
independent review and actual server-required checks remain binding.

Admission/restoration needs exact command/source identity, timing logs,
configuration/backend/sanitizer details and the enforced deadline. Use measured
complete runs below 60 seconds for ordinary admission. A bounded diagnostic run
can retain a currently failing short regression; its early failure does not
certify remaining assertions or successful full-command runtime. Failing short
regressions stay visible. Admit an
aggregate only with its own total timing evidence; never reset its budget per
child or manufacture chunks of a slow suite. Configuration/build overhead must be
reported separately from test execution. No Actions state, permission, protection,
engine assertion, fixture or long-capture behavior is changed here.
