# Test inventory

Choose checks for the affected contracts and report their actual results. A past
measurement or an existing assertion does not establish a pass on today's
candidate. Use [testing instructions](testing.md) and `chibi build test --list`
to find each suite's entry point and platform requirements.

## Coverage

| Coverage | Scope and reporting |
| --- | --- |
| `chibi build test unit` | Six pure C execution-policy cases. These cases do not cover the external process contracts. |
| `chibi build test short` | `test_tools`' default cases: the retained C tooling and process integrations. |
| Native, projects and language groups | Correctness selectors listed in the `group` rows of `tools/tests.tsv`, excluding benchmarks. Report the selected groups and their configurations. |
| Authored `@test` suites | `chibi test DIR`. The `chibi-test-*` rows of `tools/tests.tsv` run the compiler's, `packages/core`'s and the compiler samples' suites. |
| Full native suites | Use their current owning entry points and required runtime/backend configuration. Earlier short-group passes do not establish full-suite coverage. |
| Editor checkpoint and host/generation checks | Retained native process, state-file and workspace recovery assertions. Compilation alone does not establish that these assertions ran. Studio checks belong to `chibi-studio`. |
| Compiler parity and backend checks | Distinguish source preparation, bytecode, SLJIT, generated C, Wasm and no-SLJIT behavior. Smoke compilation does not prove execution parity. |
| Benchmarks | Opt-in only, not routine CI. Use `chibi build test benchmarks` or an explicit benchmark selector; report workload, resource settings and measured result. A benchmark is not a replacement for a correctness check. |
| Multi-goal validation | Preserve the selected backend, device, sanitizer and workload requirements. Report unavailable capabilities and checks that were not run. |

Retired Python/Node suites, cloud tooling, MCP script checks and old script
benchmark drivers were removed. CUT65 also removed reload loader/watch fixtures
and the Python application-recovery driver. The current C suite does not
reproduce all of that historical coverage. The renderer, material and GPU
selectors are archived with the game runtime (`archive/README.md`).

Historical measurement on 2026-10-09 used Debug ASan/UBSan in a four-core Linux
container: the native group took 4.7 seconds and the language group 5.3 seconds.
Those results do not validate a current source tree or configuration.

## Execution and cleanup

`chibi build test` runs each selector as its own process under a deadline:
`--timeout SECONDS`, or a `timeout NAME SECONDS` row of `tools/tests.tsv`. A
selector that passes its deadline is killed and reported as
`FAIL NAME (timed out after Ns)`, never as a pass or a skip; the others still
run.

Build the test drivers with `chibi build --target test_tools --target
test_language` and report build time separately from assertion execution.
Direct binaries have their own interfaces; record the command that actually
ran.

## Evidence and publication

Record the exact candidate, command, platform, backend, sanitizer configuration,
result and unexecuted coverage. Historical timing is not evidence for a changed
command or source tree. A configured CI job is not execution proof.

Independently reviewed affected-contract checks determine publication
requirements. Required server checks, PR/ticket holds and explicit approval
requirements remain binding. Local evidence is sufficient when CI is disabled
unless actual server rules require CI. Report missing evidence directly; do not
substitute a narrower check for the contract that needs validation.
