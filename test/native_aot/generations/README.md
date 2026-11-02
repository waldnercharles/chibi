# Numeric package generations

On Linux x86-64, run the isolated real-driver regression with existing hosts
and LLVM tools (absolute paths):

```sh
CHIBI_COMPILER_CACHE=/path/to/cache sh test/native_aot/generations/run.sh \
  /path/to/chibi-build-host /path/to/chibi-compiler-host /path/to/llvm/bin
```

The runner invokes `tools/native_aot/driver/Driver.chibi`'s actual `Native`
entry, compiles `apps/host/Host.chibi`, and swaps actual shared objects. The
build and compiler hosts remain C; this is not a no-C bootstrap or a test of
the whole SDK launcher. LLVM's `opt`, `llc` and `ld.lld` are required.

The package swap inserts a function and a retained binding, preserves the old
bindings by their `Function.variable` names rather than indices, converts an
`int` count to `f64`, and clamps a retained `i64` to `int`. Exit status 7 proves
the count survived, the new binding kept its initializer, the conversion
worked, and the edited `Main` did not run. The same fixture in a `library`
block checks the existing path. A first generation with typed `Main` and void
`Frame` also swaps: existing scalar lowering adapts non-entry void results to int zero.
A sibling import checks package collection.

Negative cases require meaningful diagnostics for missing or incompatible
`Main`/`Frame`, runtime-dependent arrays, formatting and assertions, and
duplicate function names across imports. Every rejected edit leaves the
prior shared object byte-identical and publishes no new generation. This
does not disable assertions: a generation needing the unavailable runtime
context is rejected before publication.

The unchanged implementation fails at the first retained package build,
before any host process starts. The script runs only these authored fixtures,
not a broader native-build or language group.
