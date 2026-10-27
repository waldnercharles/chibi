# Managed pointers: fixed-point receipts at ac000975c

These are the unedited logs for #695's final composition, ac000975c. It is
the prepared 23 pin commit 5ba49d864 (runtime
`compiler-runtime-prepared23-7d90bcfd23bd96ddecadb31d55e8f701ce422a3a`,
published by run 40560682136) merged with main at 3511d73a3 and then
376e762ec. Main there adds #756 to #760 and #754. #754 changes bootstrap
closure sources (`package_compiler/Scalar.chibi`, `scalar_facts/Facts.chibi`,
`scalar_llvm/Emit.chibi`, `llvm_native/Emit.chibi`, `native_abi/Abi.chibi`),
so the bootstrap is regenerated. The commit that adds this directory also
removes the 75e2e20a0 receipts, which were pushed at 7d90bcfd2, and changes
nothing else.

The full CI chain runs on hosted CI for this head, so no local chain receipts
are recorded here. Host: Linux x64, 4 CPUs, clang 18, with `CC=clang
CXX=clang++ TOOLS_C_COMPILER=clang LIBCLANG_ROOT=/usr/lib/llvm-18
BUILD_HOST=/nonexistent/chibi-build-host`.

## Revisions

| Commit | Change |
| --- | --- |
| 63bae25da | Merges main at 3511d73a3 (#756 to #759). No conflict. |
| 09c010e31 | Merges main at 376e762ec (#760, #754). Only `tools/compiler/bootstrap/{bootstrap.json, compiler.chibip}` conflict; this branch's side is kept and then regenerated. `Scalar.chibi` keeps #695's pointer `Prologue` argument beside #754's changes. `native_calls.c` keeps `Bump` (mutating reference) and `Read` (managed pointer) beside `UnusedRef`. Prepared format 23, project 36, build-host protocol 9 and the runtime pin are unchanged. |
| a0aa568bc | Regenerated bootstrap, recorded as `09c010e31-dirty`. |
| ac000975c | Records the clean revision a0aa568bc in `bootstrap.json`. The closure is unchanged. |

## Commands

| Log | Command | Revision | Exit |
| --- | --- | --- | --- |
| `logs/format-check.log` | `build/sdk/chibi-compile --project-root tools/compiler --format --check` | 09c010e31 with the local `if(FALSE AND TARGET chibi-build-host` edit to `sdk/CMakeLists.txt` | 0 |
| `logs/regen-dirty.log` | `make bootstrap-artifact JOBS=4` | 09c010e31 with the same edit | 0 |
| `logs/regen-clean.log` | `make bootstrap-artifact JOBS=4` | a0aa568bc, clean | 0 |

`make bootstrap-artifact` runs the fixed-point selector
`language-package-compiler-chibi-bootstrap` on a Release `test_language`:
stage one compiles the compiler sources, and stage two compiles them again to
the same bytes (`package_compiler_compiles_itself`, 26,653 assertions, about
91 s in each log). The host then writes the artifact from stage two. The clean
run recovers the same closure from the committed bootstrap, so only
`revision` changes.

Closure: 9,953,901 bytes, SHA-256
`2ede1737123796e711c9d94dc3464cef851bd79d94daf31bf53f1231e0536a38`.
