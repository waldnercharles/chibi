# Remove authored ref: receipts at 01d01914a

These are the unedited logs for #698 at 01d01914a. That revision merges #695's
final composition d650e456e into #698. d650e456e is the prepared 23 pin
5ba49d864 (runtime
`compiler-runtime-prepared23-7d90bcfd23bd96ddecadb31d55e8f701ce422a3a`)
plus main at 88b2720f6 and #695's two reviewed runtime fixes. The commit that
adds this directory renames the e810babbc receipts and replaces their
fixed-point logs; the chain logs at 8cdd3e659 are kept.

Host: Linux x64, 4 CPUs, clang 18, with `CC=clang CXX=clang++
TOOLS_C_COMPILER=clang LIBCLANG_ROOT=/usr/lib/llvm-18
BUILD_HOST=/nonexistent/chibi-build-host`. `build` uses ASan/UBSan and
`build-release` is Release.

## Revisions

| Commit | Change |
| --- | --- |
| 8cdd3e659 | Merges #695 at 7d90bcfd2 (main d79a3fb2e with #755). #755's `UnusedRef` takes `^int`. `native_calls.c` keeps `Bump` (scoped `^int`, writes through it) and `Read` (managed, held in a local). No compiler source changes, so no regeneration. |
| f23a874e1 | Merges #695 at d78ea7ca2. Only `tools/compiler/bootstrap/{bootstrap.json, compiler.chibip}` conflict; this branch's side is kept and then regenerated. Main's #754 to #760 add no authored `ref`. |
| d4088f44a | Regenerated bootstrap, recorded as `f23a874e1-dirty`. |
| e810babbc | Records the clean revision d4088f44a in `bootstrap.json`. The closure is unchanged. |
| c2ab74bed, 70dc1832e, 877978932 | Merge #695's runtime fixes, its pointer test coverage and main 135c36a0a. No compiler source change. |
| 8e078d029 | Merges #695 at d650e456e (main 88b2720f6, #764). Only the bootstrap pair conflicts; #764's sources and tests add no authored `ref`. |
| e546da799 | Regenerated bootstrap, recorded as `8e078d029-dirty`. |
| 01d01914a | Records the clean revision e546da799 in `bootstrap.json`. The closure is unchanged. |

## Fixed point at the final revision

| Log | Command | Revision | Exit |
| --- | --- | --- | --- |
| `logs/format-check.log` | `build/sdk/chibi-compile --project-root tools/compiler --format --check` | 8e078d029 with the local `if(FALSE AND TARGET chibi-build-host` edit to `sdk/CMakeLists.txt` | 0 |
| `logs/regen-dirty.log` | `make bootstrap-artifact JOBS=4` | 8e078d029 with the same edit | 0 |
| `logs/regen-clean.log` | `make bootstrap-artifact JOBS=4` | e546da799, clean | 0 |

`make bootstrap-artifact` runs the fixed-point selector
`language-package-compiler-chibi-bootstrap`: stage one compiles the compiler
sources, and stage two compiles them again to the same bytes
(`package_compiler_compiles_itself`, 25,373 assertions). Closure: 10,119,585
bytes, SHA-256
`16fac943a8fb9fcef81b9f2cfae634afc8b7b0dc6589a5f30ce503808c304dd0`.

## CI chain at 8cdd3e659

`chain-8cdd3e659/` holds the local CI chain on the previous composition, all
exit 0: `test-build`, `release` (`build-release`), `test-native`,
`test-materials`, `test-projects`, `test-language-all` (with
`CHIBI_LLVM_BIN=/usr/lib/llvm-18/bin`), `test-tools`, `check-format`,
`test-recovery` and `sdk` (`package-sdk`). Charles asked to keep local work to
what must be produced locally, so the chain for 01d01914a runs on hosted CI.
