## Enforcement boundary

The temporary ceiling and quarantine are enforced by managed Make test targets,
`tools/test_policy.py`, and agent test commands. Direct `test_suite`, raw test
scripts, CMake `check-*` targets and controller/capture entry points are unchanged;
they do **not** universally enforce this policy. Do not invoke them to bypass it.
Underlying assertions and fixtures remain intact. No hardware opt-in clears the
managed quarantine. Do not reconstruct a slow suite through repeated selectors.

Managed test execution currently requires Linux subreaper support. Its isolated
supervisor owns descendants even after `setsid` or nested wrappers, passes the
same absolute monotonic deadline to children, and kills/reaps adopted descendants
on timeout or interruption. Unsupported hosts fail before launching the test.
This is a per-process mechanism, not a system permission or configuration change.

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
not a partition of the old full native suite. The revised default command took 4.860s including fixture setup: 14 policy,
11 validation-plan and 18 execution-policy Python checks passed; the MCP check
failed. Its failure makes the default aggregate fail; it is not omitted to
preserve a green result. Each independently requested native invocation below retains
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

Additionally, `test-tool-validation-plan` passed 11 checks in 0.349s, and the
existing `test-tool-execution-policy` aggregate was measured in 1.652s including
its tiny C fixture compilation. **The latter did not pass:** Python's 18 checks
passed, but the unchanged MCP regression failed (`active_device` was `null`,
expected `fixture GPU`). It remains admitted and failing, not quarantined to hide
the failure. `test-tool-workspace-make` passed 12 checks in 1.108s. These are local
policy-revision results; native tests were not run again to justify admission.

## CI and restoration

CI now labels its jobs **Native partial coverage (Debug/RelWithDebInfo)**, reports
this inventory, runs the short aggregate (including its currently failing MCP check) and bounded graph/scaling checks, and
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
