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
| `--editor-checkpoint`: workspace recovery assertions | Unadmitted runtime; retained native assertions are not execution evidence. |
| Build-integrated runtime checks: `check-generation`, `check-host`, `check-assets`, `check-behavior-backend` | No admission for their complete execution; compilation alone is not a pass. |
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

`make test-native` and `make test-language-all` run every selector of the full
drivers under one 60-second deadline each; `tools/test_policy.py` lists them in
`GROUPS`. Under Debug ASan and UBSan on a 4-core Linux container the native group
took 4.7 seconds and the language group 5.3 seconds (2026-10-09). The selectors
of the retired `test_suite` (Studio, iframe, material and Arena tuples) are gone
with it; Studio's checks belong to `chibi-studio`.

The planner accepts `--tools` or `--tool=native-tools` for `make test-tools`.
Other former tool choices are retired. The plan records unavailable coverage
without imposing a blanket full-suite landing requirement. Independently reviewed
affected-contract checks determine publication requirements; see
[reviewed validation](testing.md#batch-validation-cut62). Plans grant no merge authority.

## CI and restoration

CI runs the tooling group, the full engine build with the native and language
groups, a Studio build with two hidden frames, and clang-format on every pull
request. Their configuration is not execution
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
