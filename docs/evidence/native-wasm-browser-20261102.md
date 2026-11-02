# Native Wasm asset compiler browser acceptance

Validated on 2026-11-02 in Google Chrome on macOS. The product change is
`bd05b1eab`, based on combined compiler revision `921c56bd0`. Emscripten was
`6.0.9-git`, with its matching LLVM tools. This is browser execution evidence;
the Node and relocated SDK checks below are separate checks.

The real optional graph target produced these production archives:

| Archive | SHA-256 |
| --- | --- |
| `libchibi_asset_compiler.a` | `4d07ee99d3288ff555e1caa03b5a29d5fa1b81fda9fe28fd7d5711a2eba33af2` |
| `libchibi_asset_compiler_threads.a` | `449a5fd02c326e0352bdf387bb42a44c32a9b6096b00e9055c1f6dbe84407220` |

Both browser pages linked the committed C fixtures against those archives,
with `SUPPORT_LONGJMP=wasm`, `STACK_OVERFLOW_CHECK=2`, an 8 MiB stack, and the
same embedded catalog request used by the differential verifier. The fixtures
were built at `-O0`; the packaged compiler payload was already built at `-O2`.
No compiler-host or VM objects were linked. Browser filesystem operations used
Emscripten's virtual filesystem, without `NODERAWFS` or a Node runtime.

The plain page used `Wasm.c` with `CHIBI_ASSET_COMPILER_WASM_PUBLIC`,
`ENVIRONMENT=web`, and `EXIT_RUNTIME=1`. Its arguments were
`--reuse /request.json /cache`. Chrome's visible page output was:

```text
one Wasm instance recovers after quota and cancellation with balanced allocations
EXIT 0
```

This executes sixteen quota/cancellation/success cycles, compares the returned
329-byte closure to the successful baseline, then exercises cache hits and
cancellation during a cache read. The production archive omits the test
allocation counter, so browser acceptance proves successful recovery and output
bytes. The instrumented Node harness separately checks allocation balance.

The pthread page linked `WasmThreads.c` with `ENVIRONMENT=web,worker`,
`PROXY_TO_PTHREAD=1`, a pool of three workers, 8 MiB worker stacks, 256 MiB
initial memory, and `EXIT_RUNTIME=1`. Its argument was `/request.json`. The local
server supplied `Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`. Chrome's visible page output was:

```text
two Wasm threads recover from simultaneous cancellation with balanced allocations
EXIT 0
```

Two compiler threads rendezvous inside their cancellation callbacks while both
compiler contexts are live, unwind, then compile successfully again. Each
thread repeats this four times and compares its successful output bytes.

The local test server bound only loopback and served only the generated fixture
directory. The browser test used its own tab. That tab was closed, the previous
tab restored, and the owned server stopped after verification.

Other checks on this named revision passed:

- Full public plain/pthread Wasm harness: 160.34 s. This includes source
  diagnostics, exact catalog closure bytes, u64/schema rejection, numeric and
  embedded-NUL formatter ABI, native-game symbol isolation, quota, cancellation,
  allocation balance and repeated recovery.
- Production optional graph build: 85.41 s; unchanged receipt reuse: 0.59 s.
- Fresh native SDK products: 146.22 s; `SDK.Install --with-wasm`: 0.65 s.
- Relocated installed CMake plain/pthread consumers and stale-header rejection:
  95.48 s.
- Desktop C facade regression after the browser cutover: 3.89 s.
- Real publication failure/rollback and SDK stale-input rejection passed.

This evidence does not establish browser-renderer or Studio acceptance, Safari
or Firefox behavior, or Windows archive production/consumer acceptance.
