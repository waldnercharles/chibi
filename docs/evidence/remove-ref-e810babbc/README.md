# Remove authored ref: receipts at e810babbc

These are the unedited logs for #698 at e810babbc. That revision merges #695's
final composition d78ea7ca2 into #698. d78ea7ca2 is the prepared 23 pin
5ba49d864 (runtime
`compiler-runtime-prepared23-7d90bcfd23bd96ddecadb31d55e8f701ce422a3a`)
plus main at 376e762ec. The commit that adds this directory also removes the
509acba10 receipts and changes nothing else.

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

## Fixed point at the final revision

| Log | Command | Revision | Exit |
| --- | --- | --- | --- |
| `logs/format-check.log` | `build/sdk/chibi-compile --project-root tools/compiler --format --check` | f23a874e1 with the local `if(FALSE AND TARGET chibi-build-host` edit to `sdk/CMakeLists.txt` | 0 |
| `logs/regen-dirty.log` | `make bootstrap-artifact JOBS=4` | f23a874e1 with the same edit | 0 |
| `logs/regen-clean.log` | `make bootstrap-artifact JOBS=4` | d4088f44a, clean | 0 |

`make bootstrap-artifact` runs the fixed-point selector
`language-package-compiler-chibi-bootstrap`: stage one compiles the compiler
sources, and stage two compiles them again to the same bytes
(`package_compiler_compiles_itself`, 26,398 assertions). Closure: 10,070,338
bytes, SHA-256
`3e2a578bf1200f3abe99796f034fdf50a1ef24b9b9f6447723f225e5c1a7e8af`.

## CI chain at 8cdd3e659

`chain-8cdd3e659/` holds the local CI chain on the previous composition, all
exit 0: `test-build`, `release` (`build-release`), `test-native`,
`test-materials`, `test-projects`, `test-language-all` (with
`CHIBI_LLVM_BIN=/usr/lib/llvm-18/bin`), `test-tools`, `check-format`,
`test-recovery` and `sdk` (`package-sdk`). Charles asked to keep local work to
what must be produced locally, so the chain for e810babbc runs on hosted CI.
