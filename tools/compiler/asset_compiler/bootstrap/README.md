# Asset compiler bootstrap resources

`native64` and `wasm32` contain prepared closures produced by the Chibi package
compiler, their exact target facts, the authored resource writer's C output, and
source/artifact fingerprints. The SDK embeds only its matching target. Prepared
loading checks the actual runtime ABI and capability signatures.

Regenerate both resources after changing any listed source. First build the
standalone compiler host and build host. Then run the authored generator:

```
build/sdk/chibi-build-host --root "$PWD" --source tools/graph/AssetCompiler.chibi --core packages/core --entry Build --timeout-ms 180000 -- "$PWD/build/sdk/chibi-compiler-host" "$PWD/tools/compiler/asset_compiler/bootstrap/native64"
cmake -S sdk -B build-web-sdk -DCHIBI_BUILD_COMPILER_HOST_COMMAND=ON
cmake --build build-web-sdk --target chibi-compiler-host
node build-web-sdk/chibi-compiler-host.js --target-json > .chibi/asset-wasm-target.json
cmake -S sdk -B build-web-sdk -DCHIBI_BUILD_COMPILER_HOST_COMMAND=OFF
build/sdk/chibi-build-host --root "$PWD" --source tools/graph/AssetCompiler.chibi --core packages/core --entry Build --timeout-ms 180000 -- "$PWD/build/sdk/chibi-compiler-host" "$PWD/tools/compiler/asset_compiler/bootstrap/wasm32" "$PWD/.chibi/asset-wasm-target.json"
```

The generator recovers the current compiler from the compiler bootstrap closure,
compiles the asset facade for the requested target, and invokes its authored
resource writer. `CompilerSources.txt` lists the verified compiler bootstrap
closure; current compiler, asset service and collection sources are added by the
generator. Each manifest records every compilation and resource source, including
the generator, plus the artifact and generated resource checksums.

The graph verifies `sources.sha256` before staging a shipped resource and
regenerates a changed snapshot in the build directory. Recovery CMake only copies
and embeds these checked-in products. Internal JSON is the owned-value transport;
asset source is compiled Chibi.
