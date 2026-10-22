# Runtime-free scalar LLVM checkpoint

`Emit.chibi` emits textual LLVM IR from a bounded, typed scalar instruction
body. `native_abi` owns the target identity and internal scalar type mapping.
The destination is explicitly Linux x86-64; the selected LLVM backend supplies
its data layout. This is not a platform-general ABI or a prepared-program ABI.

The initial subset has native `int`/`bool` parameters, one scalar result,
scalar locals, branches, comparisons, bitwise operations and integer arithmetic.
The sole exported symbol is `chibi_scalar`, or `main` for a zero-argument `int`
entry. There are no generated C files, pin-value headers, context offsets,
managed pointers, helper calls, allocations or foreign calls. Unsupported
instructions, types, targets, malformed control flow and non-dominating scalar
operands reject before any IR is returned. Table limits are 256 instructions,
128 scalar slots, 64 locals and eight arguments.

Integer add/subtract/multiply/negate wrap; no `nsw` or `nuw` promises are emitted.
Division and remainder check zero and signed minimum divided by minus one.
Shifts check the unsigned count against 32, rejecting negative counts too.
Failure branches call `llvm.trap`, including when the arithmetic result is unused.
This standalone fail-stop contract does not yet carry normal VM diagnostic or
catchable-error metadata. Conversions are deliberately unsupported rather than
silently replacing checked, wrap or clamp semantics with LLVM poison.

`Tests.chibi` contains four finite authored groups and exposes `Tests() -> int`:
zero means all passed; 1–4 identify the failed group. This entry needs only the
existing private compiler host's text/array catalog, not a C assertion harness.

## Checkpoint evidence and limits

The exact emitter and test sources compiled with the checked-in compiler seed
at `1bc97b7305ee91102d3ee035e4852aa5341df31c` and the checksum-verified published
Linux compiler host. SLJIT execution returned `[0]`, with 50,649 actual entries,
zero bytecode/Wasm/fallback/overflow entries. A generated integer-add IR module
passed LLVM 19.1.7 verification and object emission; its object had no undefined
symbols and linked/executed using only platform startup/runtime libraries.
LLVM supplied the layout; no physical layout was guessed in Chibi.

This is emitter-level evidence, not yet a Chibi-source-to-native proof, native
compiler self-reproduction, complete language coverage or full native-island
acceptance. The compiler still runs in the existing bootstrap runtime. Ordinary
Chibi-source adaptation, exact-result differential tests, optimized/unoptimized
error tests, production tool invocation, native memory/foreign calls and other
targets remain separate work. Broad native/materials/projects/language/tools
suites and installed SDK proofs have not been run for this checkpoint.
