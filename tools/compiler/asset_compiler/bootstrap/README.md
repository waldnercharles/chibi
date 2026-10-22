# Asset compiler bootstrap resources

`native64` and `wasm32` contain prepared closures produced by the Chibi package
compiler, their exact target facts, the authored resource writer's C output, and
source/artifact fingerprints. The SDK embeds only its matching target. Prepared
loading checks the actual runtime ABI and capability signatures.

Regenerate both resources after changing any listed source. First build the
standalone compiler host and build host. If every build host is missing and
sources changed, configure a recovery tree with
`-DCHIBI_ASSET_COMPILER_RECOVERY=ON` and build only `chibi-build-host` and
`chibi-compiler-host`. This uses the checksum-verified previous snapshot to
recover those tools; recovery builds cannot be installed. Regenerate both
resources, then reconfigure with the option off before building products.

Run the authored generator:

```
build/sdk/chibi-build-host --root "$PWD" --source tools/toolchain/AssetCompiler.chibi --core packages/core --collection graph=tools/graph --entry Build --timeout-ms 180000 -- "$PWD/build/sdk/chibi-compiler-host" "$PWD/tools/compiler/asset_compiler/bootstrap/native64"
cmake -S sdk -B build-web-sdk -DCHIBI_BUILD_COMPILER_HOST_COMMAND=ON
cmake --build build-web-sdk --target chibi-compiler-host
node build-web-sdk/chibi-compiler-host.js --target-json > .chibi/asset-wasm-target.json
cmake -S sdk -B build-web-sdk -DCHIBI_BUILD_COMPILER_HOST_COMMAND=OFF
build/sdk/chibi-build-host --root "$PWD" --source tools/toolchain/AssetCompiler.chibi --core packages/core --collection graph=tools/graph --entry Build --timeout-ms 180000 -- "$PWD/build/sdk/chibi-compiler-host" "$PWD/tools/compiler/asset_compiler/bootstrap/wasm32" "$PWD/.chibi/asset-wasm-target.json"
```

The generator recovers the current compiler from the compiler bootstrap closure,
compiles the asset facade for the requested target, and invokes its authored
resource writer. `CompilerSources.txt` lists the verified compiler bootstrap
closure; current compiler, asset service and collection sources are added by the
generator. Each manifest records every compilation and resource source, including
the generator, plus the artifact and generated resource checksums.

The graph verifies `sources.sha256` and the complete directory inventory before
staging a shipped resource. It builds the independent compiler host before
regenerating a changed snapshot in process. Recovery CMake verifies fingerprints
and inventory before building the asset service and before installation copies any files, then copies and embeds these checked-in products. Internal JSON is the owned-value transport;
asset source is compiled Chibi.

## Current integration evidence — 2026-10-22

The first engine-main merge was checkpointed at `d46e436a0`; the merge of newer
main `329d8fb04` is in progress. Resource staging and generation preserve their
boolean return contracts, and the production graph build passed after compiler
link-closure corrections (31.7 seconds). This is integration evidence, not a
claim that the final candidate is clean or that all test groups passed.

Both prepared artifacts remain byte-identical across this resource refresh:

- native64: `1dedea839e7d9d8e84c2ebe7f74f71e9b8a05b6d20ea7deaff02c55be2d0b02f`
- wasm32: `fd14178f100dec8736192ba54425ad7c5561b8234862a6a00332caa2daabfbe9`

All 36 recorded input fingerprints were refreshed and checked for each target.
Installed SDK metadata still needs refreshing, so earlier SDK identities must
not be presented as identities of this candidate. See
[`docs/self-hosting-build.md`](../../../../docs/self-hosting-build.md#compiled-asset-resources)
for the build record and coverage limits.

The earlier stabilization snapshot built and packaged native/Web SDKs and Studio,
and compiled installed and relocated public SDK consumers. Its recovery-install
refusal and added-file inventory invalidation check passed in 2.670 seconds under
that run's former 60-second supervisor. Newer main has retired global timing
admissions and quarantines; this historical measurement imposes no current test
gate. Choose current checks from the affected contracts under
[`docs/testing.md`](../../../../docs/testing.md).
