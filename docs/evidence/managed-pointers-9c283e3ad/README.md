# Managed pointers: raw receipts at 9c283e3ad

These are the unedited logs behind #695's source candidate, 9c283e3ad.
That revision composes the previous candidate 4972ddeed (receipts in this
branch's history at 39b1725e9) with main at 959d22d4b. Main there adds #719
(interned metadata strings, prepared format 22), #721, #722 (the seed workflow
that publishes and pins runtimes) and #723 (sanitized CI builds planned with
the Release build host). The commit that adds this directory also removes the
4972ddeed receipts and changes nothing else.

Host: Linux x64, 4 CPUs, clang 18. `build` uses ASan/UBSan and `build-release`
is Release. Every command ran with `CC=clang CXX=clang++
TOOLS_C_COMPILER=clang LIBCLANG_ROOT=/usr/lib/llvm-18
BUILD_HOST=/nonexistent/chibi-build-host`, so no external build host was used.
Each focused log starts with its command, the revision and the number of
modified tracked files, and ends with its exit status.

## Revisions

| Commit | Change |
| --- | --- |
| 1ad2b489c | Merges main at 959d22d4b. #719 took prepared format 22 for interned metadata strings, so managed pointers move to 23. See the table below. |
| 49475a74f | Regenerated bootstrap, recorded as `1ad2b489c-dirty`. |
| 9c283e3ad | Records the clean revision 49475a74f in `bootstrap.json`. The closure is unchanged. |

1ad2b489c does the following:

| Area | Change |
| --- | --- |
| Prepared format | 23 in `prepared.h`, the `scalar_lowering_plan.c` assert, `Writer.chibi`'s `Version`, the `abi.prepared` checks in `bounded_compiler/Compile.chibi` and `scalar_lowering/Lower.chibi`, and `docs/managed-pointers.md`. |
| String table | Main's `prepared.c`, `value_wire.h`, `test/prepared_fixture.h` and the writer's `Gather` pass are taken unchanged. #695 adds no string field to `Write`, so `Gather` needs no new entry. The remaining `value_wire.c` delta to main is #695's pointer tag (`PIN_POINTER` fields and values, and "Managed pointers cannot be persisted"). |
| Lookup | `prepared_record.c` keeps main's charged binary search and equality-result reuse. The delta to main is #695's pointer type and hash-work changes, as at 4972ddeed. |
| Versions | Project versions are unchanged: main's 35 for numeric names, 36 here for ref-call markers. No project changes. |

**Manifest revisions.** `make bootstrap-artifact` records `git describe
--always --dirty`. To let stage zero rebuild the closure, its first run
temporarily changes `sdk/CMakeLists.txt` to `if(FALSE AND TARGET
chibi-build-host`, which disables the asset compiler host. That run records
`<commit>-dirty`, and the edit is reverted before the commit. A second run on
the clean tree recovers from the checked-in bootstrap and reproduces the same
closure, and only `revision` changes. At 9c283e3ad the manifest records
`49475a74f1eafb3aedd50cc63d1adadc30a4fd08`. No compiler source changed after
that, and `compiler-bootstrap` checks that the recorded sources match the tree.

## CI chain (`chain/`, at 9c283e3ad)

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

## Focused selectors (`focused/`, at 9c283e3ad, 0 modified files)

`make test-build JOBS=4` came first (`focused/test-build.log`, incremental).
Each selector then ran as `python3 tools/test_policy.py tool <selector>` with
`CHIBI_LLVM_BIN=/usr/lib/llvm-18/bin`. `test-language-all` runs only the
`language` group, so this list covers the `compiler` group, plus the
selectors this composition touches that run in no group.

| Selector | Result |
| --- | --- |
| `language-program` | 182 of 182, 12,699 asserts |
| `compiler-artifact` | 17 of 17, 15,966 asserts |
| `compiler-bootstrap` | 18 of 18, 202 asserts |
| `language-asset-compiler-host` | 11 of 11, 99 asserts |
| `language-operations` | 11 of 11, 264 asserts |
| `language-record-prepared` | 14 of 14, 3,633 asserts |
| `language-migration34` | 12 of 12, 271 asserts |
| `language-migration34-generic-repro` | 4 of 4, 124 asserts |
| `language-named-binding-chibi` | 3 of 3, 54,246 asserts |
| `language-signature-reader-chibi` | 4 of 4, 201,417 asserts |
| `native-catalog` | 8 of 8, 386 asserts |
| `language-scalar-native` | 4 of 4, 111,389 asserts |
| `language-package-compiler-chibi` | 3 of 3, 23,242 asserts |
| `language-package-compiler-chibi-compiled` | 1 of 1, 17,467 asserts |
| `language-package-compiler-chibi-types` | 1 of 1, 5,616 asserts |
| `language-package-compiler-chibi-types-compiled` | 1 of 1, 5,144 asserts |
| `language-package-compiler-chibi-collections` | 1 of 1, 6,412 asserts |
| `language-package-compiler-chibi-collections-compiled` | 1 of 1, 5,998 asserts |
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
`language-package-compiler-chibi-bootstrap`, the fixed-point check. The
fixed-point check was not run, and no fixed-point result is claimed.
`compiler-artifact` counts 15,966 asserts here against 29,522 at 4972ddeed;
its 17 cases are the same, and the closure they check shrank from
18,764,380 to 9,935,000 bytes with #719's interning.

`SHA256SUMS` covers every log here. Wrapper logs keep each step's status and
duration. Their per-step `.chibi/logs` files are not kept, because
`tools/quiet.py` keeps only the 20 most recent.

## Not covered

- The `compiler-recovery` workflow and the hosted "Compiler self-build" jobs.
  They run the host that `tools/compiler/runtime.json` pins, which is main's
  prepared 22 release (compiler-runtime-prepared22-7b151ebc5), unchanged here.
  That host reads #719's format 22, not this branch's 23, so these jobs fail
  on every platform until a prepared 23 runtime built from this source is
  released. `make test-recovery` above recovers with the host built from the
  tree.
- The fixed-point selector, as above.
- GPU, Studio, Windows and macOS.

## Artifacts

`tools/compiler/bootstrap/compiler.chibip` at 9c283e3ad is 9,935,000 bytes,
SHA-256 `478b37aad488abc1d375e7285cee1e2e92b0b203fb7333dcdb09c2ff7f828a59`.
That is within the 16 MiB output bound. The other binaries are not checked in:

| File | Built at | SHA-256 |
| --- | --- | --- |
| Sanitizer `build/sdk/generated/assets/asset_compiler.chibip` (11,284,718 bytes) | 9c283e3ad | `d17085106acdbd768c7ddc5c421fddebc0eb4cdecdabb78e1de46065a4663804` |
| Sanitizer `build/sdk/chibi-compiler-host` | 9c283e3ad | `57fbef7eae71ad6805ffa08d214ce5d99d292756cd9a3f0557dd462416c1ce96` |
| Sanitizer `build/test/language/test_language` | 9c283e3ad | `1d01360d49fc536f49663c77450452b191d25ddd920bc777ceb13370143d7cb9` |
| Sanitizer `build/test/tools/test_tools` | 9c283e3ad | `e81162fc1699f434ff890a3ba9a1a0085c6322a2a817e1ad589318b227df1043` |
| `build-release/sdk/chibi-compiler-host` | 9c283e3ad | `d4385fcd28d8be45288282bb3112270e2af7757adb8dc92c50c3c361990140c2` |
| `build-release/bin/chibi_ctl` | 9c283e3ad | `cdf40486458ac3fba660ce93970fc0caaa2932cc78ebb55fb60c5cd47e056775` |
