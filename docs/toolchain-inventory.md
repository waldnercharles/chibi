# Toolchain inventory

The normal toolchain is authored Chibi compiled to native code through LLVM.
The verified native seed builds the current compiler, which builds the CLI,
asset service and compile-only facade archive. No C compiler host, bytecode VM,
reflection generator or generated C registry participates in that route.

| Capability | Owning source | Verification |
| --- | --- | --- |
| Native compiler and CLI | `tools/compiler/native_driver`, shared package compiler/facts/LLVM packages | Compiler tests, native source suites and byte-identical native stages |
| Checkout entry | `tools/build.sh`, `tools/build.ps1`, `tools/bootstrap/native-seed.*` | Hash/manifest failures, cold/warm entry, argv/Unicode/exit/cwd controls |
| Build planning and execution | `tools/toolchain`, `tools/graph` | Native driver/Graph selectors, cache and filesystem receipts |
| Generic foreign C graph | `tools/graph/c`, explicit `chibi_graph_fixture` and `graph_headerless` | Cold/warm edits, headers, flags, failure/retry, archive consumer and headerless executable |
| Native compiler-tools SDK | `tools/toolchain/SDK.chibi`, native product/archive recipes | Fresh/overlay install, complete source and receipt validation, relocation, public C ABI consumer |
| Desktop compile-only C ABI | `src/authoring/asset_compiler_host.h`, authored facade archive | Real C caller, exact closure bytes, diagnostics, quota, cancellation, cache, nested calls and threads |
| Optional browser compiler | Authored Wasm compiler/runtime, public Wasm headers/adapter | Wasm ABI, ownership, archive identity and SDK consumer probes |
| Cross-platform objects | Native LLVM backend, `test/native_aot/targets` | Identical manifest inventory consumed on macOS and Windows |
| Vendor libraries | `packages/core/vendor`, pinned third-party archives | Real native vendor imports and no-C compiler proof |
| Native OS/runtime | `packages/core/sys`, `tools/compiler/native_link`, LLVM runtime emitters | Runtime suites, retained generations, platform process and allocation probes |
| Seed maintenance | `.github/maintenance/native-seed.sh`, `native-seed.ps1` | Exact source/CI artifact identity, SHA verification and restored-source hashes |

The installed `chibi-compile` name is an alias of the native `chibi` executable.
It does not restore the retired C compiler program. The prepared artifact writer
remains a compile-only API; prepared bytecode compiler recovery is removed.

The former Studio runtime SDK/player product was retired before this compiler
migration. Current compiler-tools SDK checks do not validate that historical
Studio product. Studio owns its editor APIs and browser storage tests in its own
repository.

## Retirement candidate and acceptance

The final retirement branch removes the old C compiler/VM/runtime source tree,
C reflection tools and targets, C driver selectors, explicit VM comparison and
prepared-runtime seed/recovery jobs. It preserves both asset ABI headers,
Windows resources, vendor bindings, native/Wasm runtime code, generic foreign
C fixtures and native metadata/source APIs.

This deletion is prepared separately from runtime acceptance. Integration still
requires the coordinated survivor proofs, including actual hosted Windows
runtime and target-consumer results. Compiler reproduction and local build
success alone do not establish that all former runtime behavior is preserved.

The C target-runner reference remains until its macOS and Windows differential
gate passes. Existing historical worktrees and recovered oracle evidence are not
deletion targets. Earlier construction protocols and checkpoints are recorded in
[self-hosting history](self-hosting-build.md); their retired commands are not
current recovery mechanisms.
