# Runtime-free scalar LLVM checkpoint

`native_compiler.Compile` takes ordinary Chibi source through the shared
snapshot, declaration and body frontend. The bounded accessor is
`package_compiler/Native.chibi`; no existing frontend implementation is replaced.
`llvm_native.Emit` validates the resulting scalar control flow and writes LLVM
IR. `native_abi` owns the exact Linux x86-64 target and internal scalar mapping;
the selected LLVM backend supplies its actual data layout.

This is a **scalar AoT proof**, not the complete native memory/foreign-call
island or a C-free compiler. The compiler still runs in the existing bootstrap
runtime, including its raw parser. Emitted programs have no first-party C
runtime, pin-value header, context offset or helper calls. No C source is
produced. Platform startup/runtime libraries and LLVM are third-party inputs.

## Admitted contract

- One package/file with one function; ordinary `int`/`bool` parameters and one
  scalar result. The symbol is `chibi_scalar`, or `main` for a zero-argument
  `int` entry. Foreign bool ABI is not promised.
- Scalar locals, branches, comparisons, bitwise operations and integer
  arithmetic. Limits are eight parameters, 128 slots, 64 locals, 256 body
  instructions, 2,048 syntax nodes and 16 KiB source.
- Integer add/subtract/multiply/negate wrap: no `nsw`/`nuw` promises. Division
  and remainder check zero and signed minimum divided by minus one. Shifts
  check counts against 32, including negative counts.
- An explicit **positive CHECK budget** is required by Compile and Emit.
  Each invocation initializes a private counter; every shared-frontend CHECK
  tests remaining budget before decrementing. Budget N admits exactly N CHECKs,
  not N LLVM/machine instructions. There is no implicit unbounded default.
- Arithmetic failure and exhausted budgets branch to `llvm.trap`, even for
  unused results. This provisional standalone fail-stop contract does not
  supply VM diagnostic metadata, recovery or asynchronous cancellation. The
  caller still supervises process timeout.

Calls, imports, managed values, raw memory, aggregates, references, defaults,
float/other integer types and conversions are deliberately rejected. The
frontend accessor rejects runtime-shaped syntax before asking body lowering to
use physical-layout facts. The emitter additionally checks table bounds,
operand types, canonical bool constants, unique scalar definitions and control
flow dominance; malformed or unsupported instructions never escape as IR.
Checked/wrap/clamp conversions are not silently replaced by LLVM casts.

## Authored proof fixtures

- `test/native_aot/emitter/Tests.chibi`, package `llvm_native_tests`:
  `Tests() -> int`, zero on success and 1–5 for a failed group. Production
  `llvm_native` contains no test-only code.
- `test/native_aot/proof/Proof.chibi`, package `native_aot_proof`:
  `Tests() -> int` checks source admission/rejection. `Count`, `Source`, `Budget`,
  `Expected` and `IR` expose one authored corpus to an external verifier/runner.
  Expected `-1` means intentional runtime failure, not an exit-code assertion.
  The same source and CHECK budget run under bytecode, SLJIT and native code.

The sixteen cases include wrapping boundaries, locals/loops, short-circuiting,
negative division/remainder, negative right shift, unused invalid arithmetic,
exact budget boundaries and exhaustion. Compile-negative cases cover managed
parameters/results/locals, references, multiple declarations, recursive calls,
conversions, invalid syntax and an invalid `main` signature.

## Measured evidence and remaining work

The exact source fixture compiled using the checked-in compiler seed at
`1bc97b7305ee91102d3ee035e4852aa5341df31c` and the checksum-verified published
Linux compiler host. Both bytecode and SLJIT returned `[0]`: emitter checks
68,277 actual entries each, source checks 77,769 each; no fallback/overflow.
The generated source corpus passes real LLVM 19.1.7 parsing/verification,
object emission and native execution with both no middle-end optimization and
`default<O2>` passes, matching the VM/SLJIT result or checked failure. The
objects have no undefined helper symbols; executable link maps contain the
emitted object and platform startup/runtime inputs only.

Independent review caught a first-checkpoint Div/Mod formatting defect: the
bootstrap text formatter has a twelve-placeholder cap. The check now uses two
bounded formatting calls; the fixtures test actual indexed labels and the
source corpus verifies both Div and Mod with LLVM, not just text substrings.

The new file in the compiler package must be included when refreshing the
bootstrap source inventory/closure; it uses no new language feature or ABI.
This checkpoint does not itself refresh that artifact or wire a production
LLVM driver. Native compiler self-reproduction, malloc/load/store/output/free,
full language equivalence, other platforms and installed SDK proofs remain.
Broad native/materials/projects/language/tools suites have not been run here.
