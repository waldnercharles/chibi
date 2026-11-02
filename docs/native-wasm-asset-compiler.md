# Browser asset compiler

The optional `chibi-asset-compiler-wasm` graph target builds the authored package
compiler and its public C facade as LLVM Wasm32 code. It produces plain and
pthread archives. Desktop builds do not select this target or require
Emscripten.

From the engine checkout, pass the verified native bootstrap, native LLVM tools,
Emscripten's `emcc`, and the LLVM tools belonging to that Emscripten installation:

```sh
chibi build --target chibi-asset-compiler-wasm \
  --native-driver /path/to/verified/chibi --llvm /path/to/native/llvm/bin \
  --emcc /path/to/emcc --wasm-llvm /path/to/emscripten/llvm/bin
```

`CHIBI_EMCC` and `CHIBI_WASM_LLVM_BIN` supply the two Emscripten options when
flags are absent. The matching `em-config` must be beside `emcc`; its selected
LLVM root must match `--wasm-llvm`. Implicit `EMCC_CFLAGS`, compiler wrappers,
LLVM version suffixes and Clang include-path overrides are rejected. The graph
binds the effective config, Emscripten implementation, Binaryen and LLVM tools
to its build receipt. Compiler source and tool contents must also match before
and after a cold build.

The builder accepts Linux and macOS hosts. Windows archive production and
consumer validation remain open gates.

To include the component in a normal SDK installation:

```sh
chibi build install --prefix /path/to/sdk --with-wasm \
  --native-driver /path/to/verified/chibi --llvm /path/to/native/llvm/bin \
  --emcc /path/to/emcc --wasm-llvm /path/to/emscripten/llvm/bin
```

The component installs:

- `include/chibi/authoring/asset_compiler_host.h`
- `lib/chibi-wasm32/libchibi_asset_compiler.a` and its `.json` record
- `lib/chibi-wasm32/libchibi_asset_compiler_threads.a` and its `.json` record
- `lib/cmake/ChibiAssetCompilerWasm/ChibiAssetCompilerWasmConfig.cmake`

For an Emscripten CMake consumer, add the SDK prefix to `CMAKE_FIND_ROOT_PATH`
and use `find_package(ChibiAssetCompilerWasm CONFIG REQUIRED)`. Link
`Chibi::asset_compiler_wasm` or `Chibi::asset_compiler_wasm_threads`. The imported
targets provide the include directory, Wasm longjmp and stack-check settings;
the threaded target also supplies `-pthread`. Memory and worker-pool sizes
remain the consumer's choices. Package discovery verifies archive and header
hashes before compiling the consumer.

Both public compile functions retain their caller-owned output and diagnostic
contract. The embedded compiler's symbols and quota hooks are private, so it
can share a Wasm module with an independently compiled native game. One compile
runs synchronously per thread. Cancellation and quota exhaustion release the
call's temporary allocations, and a later compile can reuse the same instance.
The call wrapper and immutable payload identity are LLVM implementations. A
build-time SDK-header probe supplies only the opaque jump-buffer size and
alignment; its C metadata is not linked into the product. Persistent caching
still uses the shared C cache facade.

The default budget is four billion shared native CHECK boundaries at function
entries and loop backedges. This replaces the historical VM instruction count.

The builder holds an exclusive `.prepare` reservation while staging and
publishing. A failed replacement restores the previous products. If rollback
also fails, the diagnostic names the retained recovery directory; inspect it
before removing the reservation. The SDK installer rejects partial or stale
archive/header sets before copying them.

`test/native_aot/asset_compiler/wasm.sh` exercises the real public plain and
pthread archives, typed source diagnostics, exact catalog output, quota,
cancellation, repeated recovery, cache checks and coexistence with native game
symbols. `wasm_sdk.sh` builds consumers from a relocated installed component and
rejects a changed public header. Node supplies a test filesystem; filesystem
calls are not part of the authored browser compiler.

Generic Wasm source emission rejects task programs and foreign `rawptr`,
`rawptr[]` and function-pointer signatures until their runtime and ABI are
implemented. The asset compiler's numeric, byte-buffer and cstring callbacks
are supported.
