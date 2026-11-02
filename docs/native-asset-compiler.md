# Native asset compiler

The desktop `chibi-asset-compiler` is an authored native program built from
`tools/compiler/asset_driver/Main.chibi`. It parses the runtime's existing
catalog request and calls `package_compiler.CompileCatalogResult` once. It
publishes the same prepared closure bytes and first source diagnostic as the
former VM-hosted compiler. Prepared closures still belong to the runtime; this
migration removes the compiler's VM dependency, not the runtime reader.

The SDK installs the service in `libexec/chibi`. Authoring consumers ship the
companion beside their executable; Studio copies and installs it beside
`chibi-dev`. The desktop C facade accepts an explicit `options.program`, then
`CHIBI_ASSET_COMPILER`, then searches beside its executable, in its `sdk` child,
and in `../libexec/chibi`. It uses no shell or PATH lookup.

Each C call owns a temporary directory, a catalog request, an output closure
and a diagnostic file. The request is limited to 64 MiB and the returned closure
to 16 MiB. Process supervision preserves cancellation and a separate 600 second
deadline. The child and its remaining descendants are reaped before the caller
removes its files. Successful persistent cache keys include the native binary's
digest, the request and the collection salt. A binary changed during compilation
cannot publish under its former key.

A supplied compile budget arms a shared native scope immediately before the
package compile. Zero selects four billion steps. Desktop steps count native
CHECK boundaries at function entries and loop backedges. They are not the
historical VM instruction count. Exhaustion exits 125, which the C facade
reports as a step-budget failure; assertion failures retain status 134.

The request schema retains unsigned 64-bit identities, defaults and layout
hashes without passing them through signed JSON integers. The service accepts
`REQUEST OUTPUT DIAGNOSTIC [BUDGET]`. Status 0 atomically publishes OUTPUT;
status 1 leaves OUTPUT alone and writes document, line and column as decimal
lines followed by the diagnostic's message bytes. Status 2 reports a request or
environment error on stderr. The C facade supplies fresh private paths.

The Emscripten branch retains the original embedded compiler until a browser
replacement passes. The existing Wasm JIT imports `execution_machine_step` and
has an inspection bytecode fallback, so it cannot remove the VM. A replacement
needs a real native LLVM wasm32 target, a 32-bit runtime context and allocation
shim, explicit request/result callbacks, and the same closure/diagnostic/quota
regressions. LLVM's wasm backend is available locally; the authored native
compiler currently admits only the three verified desktop 64-bit targets.

`test/native_aot/asset_compiler/run.sh` exercises the relocated native service
with an authored test driver and no runtime toolchain. Its optional reference
arguments compare bytes against the former compiler and execute the resulting
closure. `bridge.sh` builds a real C consumer with no compiler-host or VM
objects, then checks closure bytes, diagnostics, quota, cancellation, cache and
SDK-relative discovery after relocation.
