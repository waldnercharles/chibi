# Temporary test quarantine

## Enforcement boundary

Managed Make and agent test commands use `tools/test_policy.py`, the retained
production deadline/quarantine supervisor. `make test`, `make test-short` and
`make test-tools` run the same minimal C pico_unit suite under one shared
60-second deadline. There are no Python or Node unit/integration test suites.
The C suite invokes retained packaging utilities and the room service as external
programs and includes minimal supervisor and validation-planner regressions.

Timeout returns failure, never a pass or skip. Nested managed wrappers inherit
one absolute monotonic deadline and process group. The owner cleans up remaining
group members; a nested timeout stops its live subtree without killing siblings.
Linux also uses subreaping for escaped sessions, with verified process discovery.
That escaped-session guarantee is not claimed for arbitrary detached children on
macOS. Discovery failure refuses test execution. No platform cleanup claim here
substitutes for current-host validation.

Direct native binaries, raw CMake check targets and controller/capture entrypoints
do not universally enforce this policy. Do not use them to bypass quarantine,
reconstruct slow suites through selectors or reset an aggregate budget per child.
Configuration and compilation remain separate from test execution.

## Quarantined coverage, disabled and not passed

| Coverage | Reason |
| --- | --- |
| `test-full`, `test-slow`: native `all_checks` aggregate and audit | Native suite measurements exceed 60 seconds. |
| `test-render`: scene/shader/Studio/RmlUi/Arena GPU and window checks | Measured render aggregates exceed 60 seconds; other components are unadmitted. |
| Full `--studio-documents`, `--studio-panels`, `--studio-shell` | Measured aggregates exceed 60 seconds; only exact tuples below are admitted. |
| Build-integrated runtime checks: `check-generation`, `check-reflect`, `check-reload`, `check-dev`, `check-assets`, `check-behavior-backend` | No admission for their complete execution; compilation alone is not a pass. |
| `test-compiler-parity`, no-SLJIT rejection checks, web backend execution | Unadmitted runtime; retained smoke compilation is not assertion coverage. |
| Make `benchmark` aggregate, long controller profiles/captures, CUT61 historical 2016-frame capture | No complete-command admission. |
| `agent-gpu-test`, other selector/argument tuples, multi-goal validation | Unmeasured or outside the exact admission inventory. |

Existing native assertions and fixtures remain. Retired Python/Node suites,
cloud tooling, MCP script checks and old script benchmark drivers are removed,
not quarantined. The minimal C suite does not reproduce their former coverage.

## Retained short coverage

Build standalone tools with `make tools-build`; the default C regression group
is bounded as one execution. Its earlier minimal-suite measurement was about
0.2 seconds before adding supervisor/planner cases. That is historical evidence,
not a timing or pass for the final expanded suite. Record fresh execution results
on the reviewed source before claiming validation.

The exact native admission table is:

| Exact native arguments | Existing execution evidence |
| --- | --- |
| `--graph-optimize` | 0.5s |
| `--iframe-updates` | 2.410s at CUT61 `7b0389ea`, Debug ASan+UBSan, all 12 checks and optional 1000 profile frames. |
| `--kit-documents` | 0.469–0.551s at CUT61 `7b0389ea`, Debug ASan+UBSan, all 10 checks and 49 frames. |
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


These measurements are historical and host/configuration specific. They do not
certify current runs. Retain existing hardware/display/sanitizer requirements
and the Arena combat/parallel environment and optimized configuration. Each
independently requested command has a maximum 60-second deadline. Do not combine
the table into a reconstructed full suite. `make test-fast` checks its exact
`TEST_ARGS` tuple before building.

The two CUT61 CPU-only tuples were independently reviewed for scoped admission.
Their complete logs, binary identity, configuration and fresh supervised runs
are retained in [the iframe completion record](research/cut61-iframe-profile/finish/README.md).
This admission leaves the 2016-frame lifecycle and all other unadmitted UI/native tuples quarantined.

`make test-studio-selectors` runs the C CLI process driver under the supervisor
against a separately built native `test_suite`; it does not render Studio.
The original 3.71-second script measurement does not certify the new C driver.

The planner accepts `--tools` or `--tool=native-tools` for `make test-tools`.
Other former tool choices are retired. Quarantined full integration requirements
remain pending metadata; the plan never satisfies them or grants merge authority.

## CI and restoration

CI labels partial coverage and runs only admitted bounded commands alongside
retained build/configure/format jobs. Their configuration is not execution
proof. Required server checks, independent review, PR/ticket holds and existing
approval rules remain binding. Short passes do not certify the full native,
render, backend, web or release gates.

Restore admission only with exact source/command identity, complete execution
logs below 60 seconds, configuration/backend/sanitizer details and the enforced
deadline. Aggregates need their own total timing evidence. Early failing runs
cannot establish successful full-command runtime. No settings or permissions
are changed to manufacture a pass.
