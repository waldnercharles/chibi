# Corrected task prerequisite checkpoint

Date: 2026-10-22. Repository: Chibi engine/compiler/runtime.

Base: `81702f624bd025f4e3ae43daa5fd86fc89c48178`.
Immutable source tree: `6353f5a382785977b0b9948cfcbcde37facd0d0c`.
Patch SHA256: `7671cd2db13732011d847edd4325881c25789cfdb98410b47b93a9beae2da301`. The patch and manifest contain 21 changed files.
This is a local, uncommitted snapshot. No source was pushed or merged, no compiler
artifact was regenerated, and no compiler default changed.

## Result and scope

The Chibi package compiler now lowers the existing authored task subset: eager
start through first suspension, yield, direct await, handle await, cancellation,
cancellation deadlines and Delay. Typed success remains distinct from Cancelled,
including zero, false, empty string, embedded-NUL string and record results.
The fixture executes on bytecode and compiled backends. Existing native task
runtime and prepared ABI21 are retained.

No authored component dependency inference or component-lifetime runtime is
included. This is not full task parity: multiple-result and union-valued task
handles, task annotations, running-handle conditions, extra result patterns and
native async commands besides Delay remain unsupported. Current-main/game-compiler,
query, collection CoW and final bootstrap integration remain separate.

## Executed checks

Every selector used:
`CHIBI_LANGUAGE_BUILD_DIR=build-release-cmake/test/language python3 tools/test_policy.py tool SELECTOR`

| Selector | Cases | Assertions | Test wall time | Log |
| --- | ---: | ---: | ---: | --- |
| `language-package-compiler-chibi-behaviors-rest` | 4 | 31,166 | 18.365s | `../tasks-test-8.log` |
| `language-package-compiler-chibi` | 1 | 12,273 | 5.584s | `../base-test.log` |
| `language-package-compiler-chibi-behaviors` | 1 | 11,884 | 4.578s | `../behaviors-test.log` |
| `language-package-compiler-chibi-collections` | 1 | 4,044 | 8.156s | `../collections-test.log` |
| `language-package-compiler-chibi-collections-compiled` | 1 | 3,798 | 7.096s | `../collections-compiled-test.log` |
| `language-bounded-compiler-chibi` | 1 | 37,483 | 0.860s | `../bounded-test.log` |
| `language-prepared-writer-chibi` | 2 | 1,034 | 1.979s | `../writer-test-2.log` |

Total selected passing cases: 11; assertions: 101,682. Test timings exclude external
configuration/build. Build commands used
`cmake --build build-release-cmake --target test_language chibi-compiler-host --parallel 2`.
Latest incremental build9 succeeded; timing is in `../build-9.log`. Final binary
identities and all log hashes are in manifest.json.

Compiler/runtime sources were unchanged after build4. The collection checks used
the build4 driver; task/base/behavior/bounded checks used build8; writer used build9.
Later edits were fixtures and PLAN only. Earlier driver hashes were not recorded;
final build9 hashes are not presented as retroactive hashes for those runs.

## Failures retained and corrected

Original source-only review tree93c325e was not preparable. Tests1–3 exposed the
reserved names action/branch, omitted required Host/Callable fields, and anonymous
append literals without contextual record types. Corrections use legal names,
explicit constructor fields and typed Cell/Slot values. Maps/References are newly
included for their Host constructor migrations. Original patch/manifest/review
and failed logs remain intact in the parent directory.

Additional fixtures exposed two test errors: Chibi embedded NUL uses \u0000,
and the longer task chain needed more than12 updates. The test now completes
pending work within a32-update bound. Both text and record checks passed before
the original bound stopped the final direct-await continuation. Failure diagnostics
now identify backend, state and actual/expected values.

The writer equivalence check exposed its unmigrated C Module.Tables fixture.
It now supplies typed empty Command rows, uses all28 spans, and shifts subsequent
indices correctly. Its diagnostic byte loop also avoids dereferencing absent
output after a failed writer call. The corrected writer check passes both native
byte equivalence and fingerprint/closure cases. The failed invocation exited139;
that result is preserved in `../writer-test.log`, not reported as a pass.

## Review and hygiene

Independent source review found no defects in the preparation corrections and
new task result fixtures. A further independent review checked all Module.Tables
consumers and the writer fixture migration. These reviews do not replace the
executed tests. clang-format22 dry-run passed for all three changed C files;
`git diff --check` passed. No broad suites, bootstrap reproduction, current-main
combined tree validation, component-lifetime tests, or CoW draft tests ran here.

Scratch retained: this review directory with immutable patches, manifests and
logs; the existing build-release-cmake directory contains the test and compiler
host binaries. No new standalone probe executable or project copy was created.
