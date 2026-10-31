# Automatically scheduled emitter contracts

These are authored `@test` regressions of `llvm_native.Emit` and `EmitLibrary`, not
native execution or ABI tests. `chibi test tools/compiler` (the `chibi-test-compiler`
selector) discovers every file here. Add a test or file without adding a C selector.

Coverage equivalence to the retired C cases of `test/language/package_compiler_chibi.c`:

| C semantic helper | Authored replacement |
| --- | --- |
| `native_calls_emit_definitions` | `CallsKeepDefinitionsAndArgumentOrder`: all five exact IR fragments |
| `native_rejects_malformed_calls` | Eleven named rejection tests: three argument-table variants, seven instruction edits and late argument definition; exact messages retained |
| `native_floats_emit` | `FloatsKeepBitsAndSaturatingConversions`: all nine IR fragments and two unique declaration counts, including the exact NaN payload |
| `native_rejects_floats` | Five named rejection tests: 33-bit float literal, negation, remainder, int-to-float and bool-to-double; exact messages retained |

Each test owns its module tables and result. No shared writable fixture, process,
file, timing loop or benchmark is involved. Purity is inferred through the real
host closure, not opted in by the tests. Other emitter contracts, native O0/O2
execution, detached-row lifetimes, library ABI binding and the C harness's
expected-result oracle remain in their existing C routes.
