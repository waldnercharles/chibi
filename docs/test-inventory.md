# Test inventory

Choose checks for the affected contracts and report their actual results. A past
measurement or an existing assertion does not establish a pass on today's
candidate. Use [testing instructions](testing.md) and the current command list
to find each suite's entry point and platform requirements.

## Coverage

| Coverage | Scope and reporting |
| --- | --- |
| `make test`, `make test-short` | Six pure C execution-policy cases. These cases do not cover the external process contracts. |
| `make test-tools` | Retained C tooling/process integrations, including package/configure, room service, publication, supervisor and planner contracts. |
| Native, materials, projects and language groups | Correctness selectors listed in `tools/test_policy.py` under `GROUPS`, excluding benchmarks. Report the selected groups and their configurations. |
| Full native and render suites | Use their current owning entry points and required runtime/backend configuration. Earlier short-group passes do not establish full-suite coverage. |
| Editor checkpoint and host/generation checks | Retained native process, state-file and workspace recovery assertions. Compilation alone does not establish that these assertions ran. Studio checks belong to `chibi-studio`. |
| Compiler parity and backend checks | Distinguish source preparation, bytecode, SLJIT, generated C, Wasm and no-SLJIT behavior. Smoke compilation does not prove execution parity. |
| Benchmarks and controller profiles/captures | Opt-in only, not routine CI. Use `make benchmark` or an explicit benchmark selector; report workload, resource settings and measured result. A benchmark is not a replacement for a correctness check. |
| GPU and multi-goal validation | Preserve the selected backend, device, sanitizer and workload requirements. Report unavailable capabilities and checks that were not run. |

Retired Python/Node suites, cloud tooling, MCP script checks and old script
benchmark drivers were removed. CUT65 also removed reload loader/watch fixtures
and the Python application-recovery driver. The current C suite does not
reproduce all of that historical coverage.

Historical measurement on 2026-10-09 used Debug ASan/UBSan in a four-core Linux
container: the native group took 4.7 seconds and the language group 5.3 seconds.
Those results do not validate a current source tree or configuration.

## Execution and cleanup

Managed Make and agent commands use `tools/test_policy.py`. Explicit operation
timeouts remain failures, never passes or skips. Nested wrappers share process
ownership and cancellation. The owner
cleans up remaining group members; a nested timeout stops its live subtree
without killing siblings. Linux also uses subreaping for escaped sessions.
That cleanup guarantee is not claimed for arbitrary detached children on macOS.
Process-discovery failures remain errors.

Build standalone tools with `make tools-build` and report compilation separately
from assertion execution. Direct binaries and CMake check targets have their own
interfaces; use the current owning command and record what actually ran.

The planner accepts `--tools` or `--tool=native-tools` for `make test-tools`.
It also accepts the registered focused `--tool` choices. Its plan records
selected and unavailable coverage; it does not grant merge authority.

## Evidence and publication

Record the exact candidate, command, platform, backend, sanitizer configuration,
result and unexecuted coverage. Historical timing is not evidence for a changed
command or source tree. A configured CI job is not execution proof.

Independently reviewed affected-contract checks determine publication
requirements; see [reviewed validation](testing.md#batch-validation-cut62).
Required server checks, PR/ticket holds and explicit approval requirements remain
binding. Local evidence is sufficient when CI is disabled unless actual server
rules require CI. Report missing evidence directly; do not substitute a narrower
check for the contract that needs validation.

