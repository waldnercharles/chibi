# Managed pointers: raw receipts at b43d01b13

These are the unedited logs behind #695's source candidate, b43d01b13.
That revision composes the previous candidate 9c283e3ad (receipts in this
branch's history at 6bbc25692) with main at ffe15dacd, then at abbe2e29c.
Main there adds #724 to #752. Among them is b0ba08cb0, which lets running
programs call natively compiled pure library functions, and #740 and #751,
which change the scalar compiler and regenerate main's bootstrap. The commit
that adds this directory also removes the 150a81fac receipts, which were
pushed at 1ccfac548, and changes nothing else.

Host: Linux x64, 4 CPUs, clang 18. `build` uses ASan/UBSan and `build-release`
is Release. Every command ran with `CC=clang CXX=clang++
TOOLS_C_COMPILER=clang LIBCLANG_ROOT=/usr/lib/llvm-18
BUILD_HOST=/nonexistent/chibi-build-host`, so no external build host was used.
Each focused log starts with its command, the revision and the number of
modified tracked files, and ends with its exit status.

## Revisions

| Commit | Change |
| --- | --- |
| 7f43b41ba | Merges main at ffe15dacd. The only conflict is `call.c`'s includes, which keep #695's `intrinsics.h` and main's `native.h`. |
| b43d01b13 | `native_calls_reject_mismatched_exports` gains a reference parameter (`Bump`) and a managed pointer parameter (`Read`). Each passes an address, so an export claiming an int signature must not bind. Both already fail main's pure-scalar signature check, so the case pins that contract rather than fixing a failure. |
| cced956f0 | Merges main at abbe2e29c. Only the bootstrap conflicts; this branch's scalar, facts and LLVM sources equal main's after the merge, apart from #695's existing `Prologue` argument in `Scalar.chibi`. |
| ee8e53e9c | Regenerated bootstrap, recorded as `cced956f0-dirty`. |
| b43d01b13 | Records the clean revision ee8e53e9c in `bootstrap.json`. The closure is unchanged. |
| 1ad2b489c | Merges main at 959d22d4b. #719 took prepared format 22 for interned metadata strings, so managed pointers move to 23. See the table below. |
| 49475a74f | Regenerated bootstrap, recorded as `1ad2b489c-dirty`. |
| 9c283e3ad | Records the clean revision 49475a74f in `bootstrap.json`. The closure is unchanged. |

**Native calls and pointers.** Main's native entry runs a synchronous call in
place of a VM frame and passes scalar words. It binds only to an effect-free
function whose parameters and results are exact scalars, so no managed pointer
or reference reaches it, and it allocates nothing, so no collection runs
during it. The frame state #695 adds (`managed_heap` and pointer roots) is
needed only by frames the VM creates. `language-native-calls` below covers
both backends, inspection and mismatched exports.

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
closure, and only `revision` changes. At b43d01b13 the manifest records
`ee8e53e9cc410853991ed502e8531ed6e347bddc`. No compiler source changed after
that, and `compiler-bootstrap` checks that the recorded sources match the tree.

## CI chain (`chain/`, at b43d01b13)

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

## Focused selectors (`focused/`, at b43d01b13, 0 modified files)

`make test-build JOBS=4` came first (`focused/test-build.log`, incremental).
Each selector then ran as `python3 tools/test_policy.py tool <selector>` with
`CHIBI_LLVM_BIN=/usr/lib/llvm-18/bin`. `test-language-all` runs only the
`language` group, so this list covers the `compiler` group, plus the
selectors this composition touches that run in no group.

| Selector | Result |
| --- | --- |
| `language-program` | 183 of 183, 12,713 asserts |
| `language-native-calls` | 4 of 4, 241 asserts |
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
| `language-scalar-native` | 4 of 4, 112,241 asserts |
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
| `language-package-compiler-chibi-scalar` | 4 of 4, 48,541 asserts |
| `language-package-compiler-chibi-imports` | 2 of 2, 13,323 asserts |
| `language-package-compiler-chibi-imports-compiled` | 2 of 2, 13,098 asserts |

Every selector exited 0. That covers the whole `compiler` group except
`language-package-compiler-chibi-bootstrap`, the fixed-point check. The
fixed-point check was not run, and no fixed-point result is claimed.

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

`tools/compiler/bootstrap/compiler.chibip` at b43d01b13 is 9,953,470 bytes,
SHA-256 `82f3f9176423c5c45ea59da1d40a39d56cd155a3c4dd54d2a5968a8ccc41044f`.
That is within the 16 MiB output bound. The other binaries are not checked in:

| File | Built at | SHA-256 |
| --- | --- | --- |
| Sanitizer `build/sdk/generated/assets/asset_compiler.chibip` (11,303,189 bytes) | b43d01b13 | `b13dc2fc880fd8f48b98f26473453d548692776186b815f3b43a34c961ac755c` |
| Sanitizer `build/sdk/chibi-compiler-host` | b43d01b13 | `b498ad9facfd35fcd07a709c4ae1148c98cf93d581db1e95fb81fd6caed35103` |
| Sanitizer `build/test/language/test_language` | b43d01b13 | `fde8464ac1f634cdc9829c9c80ccf424d3f522ad8ee8130d679c1d8491f82647` |
| Sanitizer `build/test/tools/test_tools` | b43d01b13 | `1fef8cec142f43b9867b0941a8e331f9261a5ee2e70b94847dcc7d0ec005c5ba` |
| `build-release/sdk/chibi-compiler-host` | b43d01b13 | `69f97fcb6151515820b425c9436e264746625bc4ca9b541976cd042e2089550b` |
| `build-release/bin/chibi_ctl` | b43d01b13 | `2b1f61a8df62b3e9be41dcb4f1ae0fb0a56ecad1d3c367d7e5f028fc6b686717` |
