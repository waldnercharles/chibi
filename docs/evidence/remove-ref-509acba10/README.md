# Remove authored ref: raw receipts at 509acba10

These are the unedited logs behind #698's source candidate, 509acba10, run
once on that clean tree. It recomposes the reviewed repairs (4690dd69b,
accepted at 94ac97294) onto #695's current candidate. The commit that adds
this directory also removes the 4690dd69b receipts, which remain in this
branch's history, and changes nothing else. No earlier #698 result is carried
over.

Host: Linux x64, 4 CPUs, clang 18. `build` uses ASan/UBSan and `build-release`
is Release. Every command ran with `CC=clang CXX=clang++
TOOLS_C_COMPILER=clang LIBCLANG_ROOT=/usr/lib/llvm-18
BUILD_HOST=/nonexistent/chibi-build-host`, so no external build host was used.
Each focused log starts with its command, the revision and the number of
modified tracked files, and ends with its exit status.

## Revisions

| Commit | Change |
| --- | --- |
| 9e4eb91f8 | Merges #695 at c25987b6e (main 0b4483241). Main's body preparation (#697) moved emission into `FunctionBody`, so the scoped-target forest is built once for every callable before preparation. Debug points skip elided targets. Main's ref-marker helpers go with authored `ref`, and #697's ordered `Append` sample lends its array with `&xs`. |
| 382ce4b2a, 998abaffd | Merge #695 at 1d64db5fb and 3281cc63f (main d913c0a98: #705, #706, #707). The ref-call, vector and angle-form steps read their inputs with the grammar of the version they start from. |
| 504d0d2f4 | Migrates the repository's projects (then to version 36). |
| ce41cd8e5 | Gives the scoped argument phase its own number. It and #705's `EntityComponentPhase` were both 15, so a write to another entity's component resumed as a scoped argument and stage one ran out of its step budget. |
| 8dfb4102c | The signature reader and named binding cases read `^Row` and `(value: ^int)` in place of main's `ref Row`. |
| 51c464a80 | Merges #695 at 4972ddeed (main 80d533cf0 with #693). #693 owns project 35 and #695's ref-call step 35 makes 36, so the pointer step becomes step 36 (`project_migrate36.c`) and the project version 37. The frozen grammar with `ref` becomes `project_parser36.c`, which versions 33 to 36 parse with. Conflicting lines keep the pointer spellings with #693's type names. |
| c46fd34b2 | Migrates the repository's projects to version 37 with `build/bin/chibi_ctl --project-root <dir> --migrate`. |
| 550c0c98b | Merges #695 at 9c283e3ad (main 959d22d4b, prepared format 23). No conflicts. This branch changes no prepared format or writer field. |
| 961589eca | Regenerated bootstrap, recorded as `550c0c98b-dirty`. |
| ac110bc89 | Records the clean revision 961589eca in `bootstrap.json`. The closure is unchanged. |
| 509acba10 | Merges #695's receipts commit 6bbc25692. Evidence files only. |

The regeneration follows #695's recipe: a temporary `if(FALSE AND TARGET
chibi-build-host` edit in `sdk/CMakeLists.txt` for the first run, reverted
before its commit, then a second run on the clean tree that changes only
`revision`. `chibi-compile --project-root tools/compiler --format --check`
passed before the first run.

## Requested bounded replay

| Check | Where | Result |
| --- | --- | --- |
| The two earlier failures, literal state 5 | `program_typed_and_parenthesized_forwarding_keep_pointer_scoped` in `language-program`: `Bump(value: ^double = p)`, `Bump((p))` and `Bump((p^))` on `BEHAVIOR_BYTECODE` and the compiled backend; `a`, `b` and `c` must be 5 | ok |
| Stage one `(p)` and `(p^)` with a stage zero control | `scoped pointer relays` (`Wrapped`, `Unwrapped`, `Parenthesized` gives 3) in `language-package-compiler-chibi-collections`, which compares every module's source points and watches with stage zero's and runs each call on both backends and on stage zero; `-collections-compiled` runs the compiled closure | ok |
| Returned `n^` keeps its span | `scoped pointer spans` (`Read`/`Reading`) in the same two selectors, with the same point and watch comparison | ok |
| Nested forwarding | `program_nested_forwarding_keeps_pointer_scoped` (stage zero only; stage one has no nested callables) | ok |
| Migration | `program_version_36_ref_becomes_pointers` and `program_migration_retry_resumes_after_finished_steps` | ok |

Not included: backend counters beyond the selectors' own checks, the
reviewer's separate order-12 fixture (no such case is in this repository), and
imported migration compatibility. Imported forwarding runs in the `imported
references` sample (`[10]` on both compilers), but nothing asserts that the
forwarding parameter is inferred scoped across the import, and the migration
of imported `ref` calls has no two-file probe. Stage one still rejects typed
arguments, with main's `unsupported syntax` message.

## CI chain (`chain/`, at 509acba10)

| Log | Command | Result |
| --- | --- | --- |
| `test-build.log` | `make test-build JOBS=4` | ok |
| `release.log` | `make build-release JOBS=4 BUILD_TARGET='chibi_ctl chibi chibi-compiler-host'` | ok |
| `test-native.log` | `make test-native` | ok |
| `test-materials.log` | `make test-materials` | ok |
| `test-projects.log` | `make test-projects` | ok |
| `test-language-all.log` | `make test-language-all NATIVE_LLVM_BIN=/usr/lib/llvm-18/bin` | ok |
| `test-tools.log` | `make test-tools` | ok |
| `check-format.log` | `make check-format` | ok |
| `test-recovery.log` | `make test-recovery` | ok |
| `sdk.log` | `make package-sdk SDK_PREFIX=<temporary prefix>` | ok |

## Focused selectors (`focused/`, at 509acba10, 0 modified files)

`make test-build JOBS=4` came first (`focused/test-build.log`, incremental).
Each selector then ran as `python3 tools/test_policy.py tool <selector>` with
`CHIBI_LLVM_BIN=/usr/lib/llvm-18/bin`. The first batch reached its two-hour
session limit while `language-package-compiler-chibi` was running, so that
selector's partial log was discarded. A second batch on the same tree
(`focused/test-build-2.log`, a no-op build) ran it and the selectors after it.

| Selector | Result |
| --- | --- |
| `language-program` | 185 of 185, 12,731 asserts |
| `compiler-artifact` | 17 of 17, 15,966 asserts |
| `compiler-bootstrap` | 18 of 18, 202 asserts |
| `language-asset-compiler-host` | 11 of 11, 99 asserts |
| `language-operations` | 11 of 11, 264 asserts |
| `language-record-prepared` | 14 of 14, 3,633 asserts |
| `language-migration34` | 12 of 12, 271 asserts |
| `language-migration34-generic-repro` | 4 of 4, 124 asserts |
| `language-named-binding-chibi` | 3 of 3, 54,495 asserts |
| `language-signature-reader-chibi` | 4 of 4, 202,668 asserts |
| `native-catalog` | 8 of 8, 386 asserts |
| `language-scalar-native` | 4 of 4, 111,389 asserts |
| `language-package-compiler-chibi` | 3 of 3, 23,242 asserts |
| `language-package-compiler-chibi-compiled` | 1 of 1, 17,467 asserts |
| `language-package-compiler-chibi-types` | 1 of 1, 5,616 asserts |
| `language-package-compiler-chibi-types-compiled` | 1 of 1, 5,144 asserts |
| `language-package-compiler-chibi-collections` | 1 of 1, 6,174 asserts |
| `language-package-compiler-chibi-collections-compiled` | 1 of 1, 5,718 asserts |
| `language-package-compiler-chibi-literals` | 1 of 1, 4,090 asserts |
| `language-package-compiler-chibi-behaviors` | 1 of 1, 2,859 asserts |
| `language-package-compiler-chibi-actions` | 10 of 10, 4,628 asserts |
| `language-package-compiler-chibi-behaviors-rest` | 2 of 2, 5,742 asserts |
| `language-package-compiler-chibi-libraries` | 13 of 13, 17,185 asserts |
| `language-package-compiler-chibi-sharing` | 1 of 1, 139 asserts |
| `language-package-compiler-chibi-debug-free` | 1 of 1, 2,458 asserts |
| `language-package-compiler-chibi-native-arguments` | 1 of 1, 12,094 asserts |
| `language-package-compiler-chibi-native-arguments-oracle` | 2 of 2, 200 asserts |
| `language-package-compiler-chibi-scalar` | 4 of 4, 38,137 asserts |
| `language-package-compiler-chibi-imports` | 2 of 2, 13,323 asserts |
| `language-package-compiler-chibi-imports-compiled` | 2 of 2, 13,098 asserts |

Every selector exited 0. That covers the whole `compiler` group except
`language-package-compiler-chibi-bootstrap`, the fixed-point check, which was
not run; no fixed-point result is claimed.

`SHA256SUMS` covers every log here. Wrapper logs keep each step's status and
duration. Their per-step `.chibi/logs` files are not kept, because
`tools/quiet.py` keeps only the 20 most recent.

## Not covered

- The `compiler-recovery` workflow and the hosted "Compiler self-build" jobs.
  They run the host `tools/compiler/runtime.json` pins (main's prepared 22,
  compiler-runtime-prepared22-7b151ebc5), which can't read #695's format 23.
  They wait on a prepared 23 runtime built from #695's frozen source.
  `make test-recovery` above recovers with the host built from the tree.
- The fixed-point selector and imported migration compatibility, as above.
- GPU, Studio, Windows and macOS.

## Artifacts

`tools/compiler/bootstrap/compiler.chibip` at 509acba10 is 10,051,437 bytes,
SHA-256 `a14c180f5631b716d558a1f1b71db48adeb4fd7a578e70f7f71845b81fca01d7`.
The other binaries are not checked in:

| File | Built at | SHA-256 |
| --- | --- | --- |
| Sanitizer `build/sdk/generated/assets/asset_compiler.chibip` (11,401,153 bytes) | 509acba10 | `5104ab8bad64f27c801d694cfbd4613bbb7139527e647be28d21e6105280ac0a` |
| Sanitizer `build/sdk/chibi-compiler-host` | 509acba10 | `2636db605da7812f018b574b8e585f410ff0bb9b71bc429c04c0ab1d64c97246` |
| Sanitizer `build/test/language/test_language` | 509acba10 | `05406416d0fdb86dbf5d1c823996843435ec2dd2055045a3321677d1094b4c9d` |
| Sanitizer `build/test/tools/test_tools` | 509acba10 | `daa92402907f08c3a22a41497ff82165d356259659d8cd5dc41aef48709d6b79` |
| `build-release/sdk/chibi-compiler-host` | 509acba10 | `f5698f5c6f2354d16e0f265c8e7706b0556c5774025230475267a1db3a47b177` |
| `build-release/bin/chibi_ctl` | 509acba10 | `8242d8aed7bd80a7302b2b7942846b95cbad274c217dda918a9ec39b771be263` |
