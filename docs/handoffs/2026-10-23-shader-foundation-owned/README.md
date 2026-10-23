# Native shader foundation: owned validation checkpoint

The native source is unchanged from source commit
`1c1599fa1ca2405e0ee1d6a57dc7a9146db3b252`, tree
`e4efa14d334f7dc5b6d465bf1906f77120992084`. Its preserved parents include
the original PR663 source and main `d0de4a84ecf50badcab661411470bdf6f3da5fa2`.
This retained development checkpoint adds validation records only. It does not
land the source, clear the draft, or authorize a merge or release.

## New execution evidence

All checks below ran with the source checkout clean. The full native build used
the exact source and its checksum-pinned dependencies, not the preexisting SDK.
Linux x86_64, clang 19.1.7, CMake 3.31.6; Debug with sanitizers **off** unless
specified. Projects used freshly built Release command-line tools.

| CPU gate family | Passed cases | Assertions |
| --- | ---: | ---: |
| Native | 647 | 237,362 |
| Materials | 110 | 270,494 |
| Language | 583 | 1,021,839 |
| Projects | 106 | 11,933 |
| Standalone `make test-tools` | 163 | 28,910 |
| **Total** | **1,609** | **1,570,538** |

The real full tooling driver separately passes `--shader-source-debug` (13
cases, 165 assertions) and `--shader-bake` (8 cases, 320 assertions). The same
unchanged C suites pass under **UBSan only** through one bounded scratch
dispatcher. Those repeated focused runs are not added to the table totals.
The full `make check-format` passes using clang-format 22.1.5. Independent source
review found no concrete defect; see `independent-source-review.md`.

The native implementation supplies owned CPU snapshots of supplied existing
shader IR, principal-result aliases, provenance/call lookup, exact ordered
document guards, nonfinite labels and transactional capture failure. The
sampled-SDF arithmetic diagnostic regression passes. These tests do not compile
authored shaders or demonstrate a rendered authored material. The caller must
still pair the map with the exact IR generation.

## Failed attempts and remaining coverage

The normal combined ASan/UBSan build reaches the sanitized `reflect` generator,
then fails because LeakSanitizer cannot operate under this environment's ptrace
sandbox. The combined-sanitizer bounded dispatcher has the same limitation.
An outside-tracing execution attempt failed before running tests. No leak
settings or assertions were disabled. **Combined sanitizer and leak coverage
remain blocked**, not passed. UBSan-only success is not a substitute.

Initial missing tool, libclang-development, OpenGL-discovery and Ninja setup
failures were recovered locally without source changes. In particular the first
standalone tooling run had six failures due to missing Ninja; the unchanged
complete `make test-tools` rerun passed after installing it. The final logs are in `logs/`; only trailing line whitespace in the tooling
log is normalized, with its original digest retained in `validation.json`.
Earlier setup logs remain in task scratch. Build time is separate
from test execution time in `validation.json` and the logs.

No other-platform, hardware-GPU, Studio or installed/relocated SDK acceptance is
claimed. GPU discovery found no accessible Vulkan device. Existing explicit
technical, ABI, default-selection, deployment, merge and release holds remain.

## Handoff identity and authored boundary

Fresh remote reads matched foundation handoff `1f82a73a7e0f76002e4fe9c9021176da24c090e4`
and authored handoff `6dc3219759f5aac491b1bc94231de63d75845e0c`.
All 17 native active paths and 7 authored active paths matched their Git blobs,
SHA256 digests and byte counts. All 36 native and 7 authored handoff-manifest
entries matched. Both source trees and both complete handoff trees were
reconstructed and matched the remote commit trees exactly. The authored overlay
was verified separately and is not applied to this branch.

Facts/SourceMap remain **uncompiled, unexecuted and unwired**. The compiler
agreement covers only the initial flat Float3 declaration/consumer contract,
not a producer or hook permission. Analyze production, ordinary nested semantics,
prepared inspection service, facts-driven lowering, typed finalization, a real
authored material and same-generation CPU/GPU inspection remain absent here.
No compiler producer, schema, pin, bootstrap or authored-resource edits were made.

## Reproduction

Use the repository's normal toolchain setup and exact third-party pins. The
additional resource-directory/OpenGL paths needed by this container are local
environment configuration, not product changes. Relevant commands are:

```sh
make test-build LOCAL_CMAKE_FLAGS='-DCMAKE_BUILD_TYPE=Debug -DCHIBI_ENABLE_SANITIZERS=OFF -DCHIBI_ENABLE_WARNINGS=ON'
CHIBI_TOOLS_BUILD_DIR=build/test/tools CHIBI_LANGUAGE_BUILD_DIR=build/test/language python3 tools/test_policy.py group native
CHIBI_TOOLS_BUILD_DIR=build/test/tools python3 tools/test_policy.py group materials
CHIBI_LANGUAGE_BUILD_DIR=build/test/language python3 tools/test_policy.py group language
make build-release BUILD_TARGET='chibi_ctl chibi'
CHIBI_TOOLS_BUILD_DIR=build/test/tools CHIBI_CTL=build-release/bin/chibi_ctl CHIBI_CLI=build-release/sdk/chibi CHIBI_COMPILE=build-release/sdk/chibi-compile python3 tools/test_policy.py group projects
make test-tools
make check-format
```

Use the default sanitizer configuration in an environment where LeakSanitizer
works before calling the sanitizer gate passed. Re-review and bind all affected
checks to the eventual integration head; this checkpoint is not merge approval.
