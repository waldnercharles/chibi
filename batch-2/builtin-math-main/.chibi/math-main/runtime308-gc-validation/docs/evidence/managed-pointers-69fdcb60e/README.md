# Managed pointers: fixed-point receipts at 69fdcb60e

These are the unedited logs for #695's final composition, 69fdcb60e. It is
the reviewed d650e456e plus the runtime source-review fixes (91b6d6321),
merged with main through c3fd1b42b. Main there adds #765 and #766. #766
changes bootstrap closure sources (`package_compiler/Scalar.chibi` among
them), so the bootstrap is regenerated again. The commit that adds this
directory also removes the d27dbacff receipts and changes nothing else.

The full CI chain runs on hosted CI for this head, so no local chain receipts
are recorded here. Host: Linux x64, 4 CPUs, clang 18, with `CC=clang
CXX=clang++ TOOLS_C_COMPILER=clang LIBCLANG_ROOT=/usr/lib/llvm-18
BUILD_HOST=/nonexistent/chibi-build-host`.

## Revisions since d27dbacff

| Commit | Change |
| --- | --- |
| d650e456e | The d27dbacff receipts. |
| 91b6d6321 | Fixes from the independent runtime source review: event payloads refuse pointers, staged replacement roots each preserved pointer, build entries and site generators own a managed heap, no collection while a reducing system is paused, packed arrays trace items only for authored records, unroot clears its slot, JIT pointer equality stores one byte. See `docs/managed-pointers.md`. |
| 37d23e634 | Merges main at c3fd1b42b (#765, #766). Only `tools/compiler/bootstrap/{bootstrap.json, compiler.chibip}` conflict; this branch's side is kept and then regenerated. The composed `Scalar.chibi` is 111,282 bytes, SHA-256 `5e0e4d3ba1acc273000ba1a7204a940dce23da9c1314655faf4a65f99d8ff276`, keeping #695's pointer `Prologue` argument beside #766's named-result lowering. Prepared format 23, project 36, build-host protocol 9 and the runtime pin are unchanged. |
| 6f46605da | Regenerated bootstrap, recorded as `37d23e634-dirty`. |
| 69fdcb60e | Records the clean revision 6f46605da in `bootstrap.json`. The closure is unchanged. |

## Commands

| Log | Command | Revision | Exit |
| --- | --- | --- | --- |
| `logs/format-check.log` | `build/sdk/chibi-compile --project-root tools/compiler --format --check` | 37d23e634 with the local `if(FALSE AND TARGET chibi-build-host` edit to `sdk/CMakeLists.txt` | 0 |
| `logs/regen-dirty.log` | `make bootstrap-artifact JOBS=4` | 37d23e634 with the same edit | 0 |
| `logs/regen-clean.log` | `make bootstrap-artifact JOBS=4` | 6f46605da, clean | 0 |

`make bootstrap-artifact` runs the fixed-point selector
`language-package-compiler-chibi-bootstrap` on a Release `test_language`:
stage one compiles the compiler sources, and stage two compiles them again to
the same bytes (`package_compiler_compiles_itself`, 25,628 assertions, 105 s
and 104 s). The host then writes the artifact from stage two. The clean run
recovers the same closure from the committed bootstrap, so only `revision`
changes.

## Build-process group at d18a9e126

| Log | Command | Revision | Exit |
| --- | --- | --- | --- |
| `logs/build-process.log` | `CHIBI_TOOLS_BUILD_DIR=build/test/tools CHIBI_BUILD_HOST=build/sdk/chibi-build-host python3 tools/test_policy.py tool build-process` | d18a9e126, clean, ASan/UBSan Debug `test_tools` and `chibi-build-host`, on an otherwise idle host | 1 |

No CI group runs this selector. `private_programs_allocate_managed_pointers`
passes on both backends. 47 of 48 cases pass. The one failure is
`private_host_cli_executes_source_and_reports_errors` at `build_process.c:624`:
its `CoreProgram.chibi` run hits the case's fixed 10-second timeout. The same
`chibi-build-host` command, given a 120-second timeout, took 12.9 s and
12.0 s when built from main c3fd1b42b, and 13.9 s and 13.3 s on this branch.
Both exit 7, so the timeout fails on main as well in this sanitizer build.

## Main 2c69729b7

675f2f2fe merges main at 2c69729b7 (#767) without conflicts. #767 changes no
compiler, bootstrap, runtime ABI or pin input, so the fixed point above still
holds. `tools/graph/engine/Targets.chibi` merges to blob
`687020569b9fa43cda0dd60cf72e32d2e99b98b1`.

Closure: 10,114,071 bytes, SHA-256
`3f075c12e0e860c373b4b7a80dbb143f1306a36be280e742dd25da3a6bdc8c82`.
