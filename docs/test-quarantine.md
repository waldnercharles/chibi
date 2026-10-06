## Disabled coverage inventory (temporary; NOT PASSED)

This inventory applies to ordinary Make/CI validation and direct test commands.
All assertions, fixtures and native selectors remain in source. No environment
flag re-enables these commands. Revisit their runtime and review an explicit
policy change before restoring them; do not split an identical suite into chunks.

| Disabled command / coverage | Reason |
| --- | --- |
| `test-full` (formerly default `test`) and `test-slow`: native `all_checks` | Measured native test execution 316.4s; historical baselines 215–221s. |
| `test-render`: scene documents, shader detail GPU, Studio documents, RmlUi Vulkan/GLES render, Arena play, three-frame app smoke and `check-mcp-gpu` | PR164 Debug aggregate 1059s, optimized aggregate 125.66s. Individual unadmitted components are not presumed fast. |
| Full `--studio-documents`, `--studio-panels`, `--studio-shell` | Debug Studio 982s, panels 319s, shell 103s; optimized Studio 83s. Only the exact tabs/readout invocations below are admitted. |
| `test-tools` and every existing `test-tool-*` target | Current aggregate and individual commands need timing admission. Historical 24.4s tooling aggregate covered an older set, not the current expanded inventory. |
| Generated/build-integrated checks formerly in `test-full`: `check-generation`, `check-reflect`, `check-reload`, `check-dev`, `check-mcp`, `check-assets`, `check-behavior-backend`; Studio CLI selector regression; whole-function audit | No current command-level admission. Compilation is separate from execution of these checks. |
| `test-compiler-parity`, no-SLJIT smoke execution, web `check-behavior-backend` | Unadmitted command/runtime. CI retains no-SLJIT smoke compilation, not rejection-test credit. |
| `test-package`, `benchmark`, `profile-compare`, CUT61 historical 2016-frame capture | Unadmitted command/runtime; no long capture authorized. |
| `agent-gpu-test`, all other native selector/argument tuples | Unmeasured or outside the small reviewed timing inventory. CPU/GPU classification alone is not timing evidence. |
| Multi-goal validation invocations, e.g. `make test test-render` | Aggregate is unadmitted; rejects before test/build execution. |

The exact existing tooling targets quarantined are:

```
test-tool-validation-plan
test-tool-behavior-code
test-tool-execution-policy
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
test-tool-cloud-entry
test-tool-cloud-emscripten-setup
test-tool-workspace-install
test-tool-mcp-package
test-tool-web-serve
test-tool-web-workspace
test-tool-web-storage
test-tool-web-frames
test-tool-web-isolation
test-tool-collab-client
```

## Retained short coverage

- `make test` / `make test-short`: CPU-only `test/test_policy_test.py` policy
  regressions, one 60-second aggregate deadline, no native build or GPU use.
- `--graph-optimize`: historical measured selector execution 0.5s.
- `--studio-documents tabs`: measured 8s.
- `--studio-shell readout_follows_camera`: measured 3s.
- `--bench-arena-scene-scale 2048 240 0`: measured 1.4s. Retain the original
  combat/parallel environment requirements and RelWithDebInfo configuration.

These are exact invocations, not permission to sweep Studio cases or rebuild
full suites from selectors. Timing evidence is host/configuration specific;
every new execution still has a hard 60-second deadline. Native commands use
`python3 tools/test_policy.py native <binary> <exact arguments>`. Retain the
existing hardware opt-in and display setup for Studio/scaling checks.
`make test-fast TEST_ARGS='--graph-optimize'` checks admission before building,
then applies the ceiling to test execution. `make agent-test` admits only the
measured CPU selector. Neither builds nor historic timings count as a test pass.

CI keeps existing check names, matrix configurations, formatting, web/app and
reload builds. It reports this inventory in the job summary, runs the short
policy regressions, compiles `test_suite`, then runs bounded graph optimization
and the original exact optimized Arena scaling invocation. It does not run the
full gates. Actions state, permissions and repository protections are unchanged.
Server-required checks still must actually pass; absent/disabled required checks
block landing. Short coverage cannot be described as full or batch certification.
