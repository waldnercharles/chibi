# Managed pointers: fixed-point receipts at d27dbacff

These are the unedited logs for #695's final composition, d27dbacff. It is
the prepared 23 pin commit 5ba49d864 (runtime
`compiler-runtime-prepared23-7d90bcfd23bd96ddecadb31d55e8f701ce422a3a`,
published by run 40560682136) merged with main through 88b2720f6, plus the
two reviewed runtime fixes. Main there adds #754 to #764. #764 changes
bootstrap closure sources (`package_compiler/Scalar.chibi`,
`scalar_facts/Facts.chibi`, `scalar_llvm/Emit.chibi`, `llvm_native/Emit.chibi`,
`native_abi/Abi.chibi`), so the bootstrap is regenerated again. The commit
that adds this directory also removes the ac000975c receipts and changes
nothing else.

The full CI chain runs on hosted CI for this head, so no local chain receipts
are recorded here. Host: Linux x64, 4 CPUs, clang 18, with `CC=clang
CXX=clang++ TOOLS_C_COMPILER=clang LIBCLANG_ROOT=/usr/lib/llvm-18
BUILD_HOST=/nonexistent/chibi-build-host`.

## Revisions since ac000975c

| Commit | Change |
| --- | --- |
| cabe7b9f9 | Each authored test run owns a managed heap. |
| 4483ef5c9 | Task-root marking uses a worklist and an open-addressed visited set instead of recursion. |
| 7cd9a4f6a, 753cb8533 | Authored pointer tests on the task pool, with naturally parallel identity tests. |
| ae00e5a33 | Merges main at 135c36a0a (#761, #762). No conflict and no bootstrap input change. |
| 16c800b0d | Merges main at 88b2720f6 (#764). Only `tools/compiler/bootstrap/{bootstrap.json, compiler.chibip}` conflict; this branch's side is kept and then regenerated. `Scalar.chibi` keeps #695's pointer `Prologue` argument beside #764's text-native changes. Prepared format 23, project 36, build-host protocol 9 and the runtime pin are unchanged. |
| 3e8f1612d | Regenerated bootstrap, recorded as `16c800b0d-dirty`. |
| d27dbacff | Records the clean revision 3e8f1612d in `bootstrap.json`. The closure is unchanged. |

## Commands

| Log | Command | Revision | Exit |
| --- | --- | --- | --- |
| `logs/format-check.log` | `build/sdk/chibi-compile --project-root tools/compiler --format --check` | 16c800b0d with the local `if(FALSE AND TARGET chibi-build-host` edit to `sdk/CMakeLists.txt` | 0 |
| `logs/regen-dirty.log` | `make bootstrap-artifact JOBS=4` | 16c800b0d with the same edit | 0 |
| `logs/regen-clean.log` | `make bootstrap-artifact JOBS=4` | 3e8f1612d, clean | 0 |

`make bootstrap-artifact` runs the fixed-point selector
`language-package-compiler-chibi-bootstrap` on a Release `test_language`:
stage one compiles the compiler sources, and stage two compiles them again to
the same bytes (`package_compiler_compiles_itself`, 25,628 assertions, 123 s
and 109 s). The host then writes the artifact from stage two. The clean run
recovers the same closure from the committed bootstrap, so only `revision`
changes.

Closure: 10,003,148 bytes, SHA-256
`39b104db7482345c0cf80f0709bde8aacdc1b21e56c8d99b552dc9b992b31082`.
