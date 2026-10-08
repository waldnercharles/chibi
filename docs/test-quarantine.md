# Temporary test quarantine

## Enforcement boundary

Managed Make and agent test commands use `tools/test_policy.py`, the retained
production deadline/quarantine supervisor. `make test` and `make test-short`
run six pure C execution-policy cases. `make test-tools` retains the complete
C tooling/process integration group, including package/configure, room service,
publication, supervisor and planner contracts. CI and the planner's `--tools`
select that complete group. Each mode has one shared 60-second deadline.
The default unit result covers only execution-policy decisions, not the external
process contracts. There are no Python or Node unit/integration test suites;
C owns the assertions while invoking retained external implementations.

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
| `--editor-checkpoint`: workspace recovery assertions | Unadmitted runtime; retained native assertions are not execution evidence. |
| Build-integrated runtime checks: `check-generation`, `check-reflect`, `check-host`, `check-assets`, `check-behavior-backend` | No admission for their complete execution; compilation alone is not a pass. |
| `test-compiler-parity`, no-SLJIT rejection checks, web backend execution | Unadmitted runtime; retained smoke compilation is not assertion coverage. |
| Make `benchmark` aggregate and long controller profiles/captures | No complete-command admission. |
| `agent-gpu-test`, other selector/argument tuples, multi-goal validation | Unmeasured or outside the exact admission inventory. |

Existing native assertions and fixtures remain. Retired Python/Node suites,
cloud tooling, MCP script checks and old script benchmark drivers are removed,
not quarantined. CUT65 also removed the reload loader/watch fixtures and the
Python application-recovery driver. `check-host` retains native process and
state-file assertions under quarantine. The minimal C suite does not reproduce
the former coverage.

## Retained short coverage

Build standalone tools with `make tools-build`; report compilation separately.
The default pure C mode and complete tooling group have separate coverage and
execution results. Historical timings do not certify either current mode.
Record fresh results on the reviewed source before claiming validation.

The exact native admission table is:

| Exact native arguments | Existing execution evidence |
| --- | --- |
| `--graph-optimize` | 0.5s |
| `--ui-tab-list` | 1.743s initial supervised Debug ASan+UBSan admission, all 10 input/layout checks, no window or rendering. |
| `--iframe-updates` | 2.379s for the targeted CUT61 command, Debug ASan+UBSan; 3 pico cases/385 assertions plus 12 existing checks. No profile loop. |
| `--kit-documents` | 0.469–0.551s at CUT61 `7b0389ea`, Debug ASan+UBSan, all 10 checks and 49 frames. |
| `--compiled-behaviors` | 47.2s |
| `--automation-search` | 17.2s |
| `--automation-profile-scenario-inputs` | 15.8s |
| `--editor-commands` | 12.5s |
| `--material-thermal` | 4.8s |
| `--material-mpm` | 3.6s |
| `--whole-function-audit` | 1.3s |
| `--studio-documents tabs` | 8s |
| `--studio-shell document-tabs` | 9.2s initial supervised admission, all 17 authored tab checks, Debug ASan+UBSan with native Metal. |
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
The dedicated 2016-frame `--studio-panel-lifecycle` capture entrypoint is now
retired. Its original authored-panel assertions remain in `--studio-panels`,
which stays quarantined, as do all other unadmitted UI/native tuples. The optional
1,000-frame blank-document measurement loop was also removed from
`--iframe-updates`; deterministic replacement/binding, animation and callback
contracts are now part of that focused command. See
[fast, targeted tests](testing.md#fast-targeted-tests).

`make test-studio-selectors` runs the C CLI process driver under the supervisor
against a separately built native `test_suite`; it does not render Studio.
The original 3.71-second script measurement does not certify the new C driver.

The planner accepts `--tools` or `--tool=native-tools` for `make test-tools`.
Other former tool choices are retired. The plan records unavailable coverage
without imposing a blanket full-suite landing requirement. Independently reviewed
affected-contract checks determine publication requirements; see
[reviewed validation](testing.md#batch-validation-cut62). Plans grant no merge authority.

## CI and restoration

CI labels partial coverage and runs only admitted bounded commands alongside
retained build/configure/format jobs. Their configuration is not execution
proof. Required server checks, independent review, PR/ticket holds and existing
approval rules remain binding. Local evidence is sufficient when CI is disabled
unless actual server rules require CI. Short passes certify only their affected
contracts; full native/render coverage remains disabled and not passed. An
unaffected quarantined suite does not block publication.

Restore admission only with exact source/command identity, complete execution
logs below 60 seconds, configuration/backend/sanitizer details and the enforced
deadline. Aggregates need their own total timing evidence. Early failing runs
cannot establish successful full-command runtime. No settings or permissions
are changed to manufacture a pass.
