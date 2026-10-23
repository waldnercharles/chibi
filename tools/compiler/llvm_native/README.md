# Runtime-free scalar LLVM backend (opt-in WIP)

`llvm_native.Emit` validates a bounded scalar instruction plan and writes LLVM
IR. `native_abi` owns the exact Linux x86-64 target and internal scalar mapping;
the selected LLVM backend supplies its actual data layout. This clean integration
carries those reviewed pure packages from [3571908d](https://github.com/waldnercharles/chibi/commit/3571908d0cc4f3ad395b6a4fceb66641445c3d07)
onto the current-base [authored LLVM driver](https://github.com/waldnercharles/chibi/commit/ae4828697dfc976a657c33014513f00e047ec422).

It deliberately does **not** carry `package_compiler/Native.chibi`,
`native_compiler`, or the diagnostic Front/Back packet adapters. There is no
source-to-LLVM frontend connection in this checkpoint. A new leaf consumer must
wait for an approved, independently owned semantic producer result. In
particular, occurrence/type/constant tables alone do not prove a function's
complete body/result relation. Private frontend Code/Slot IDs are not an API.

The compiler still runs in the existing bootstrap runtime. Emitted scalar
objects have no first-party C runtime, pin-value header, context offset or helper
calls. No C source is produced. This is not a C-free compiler, a foreign-call
interface, or the complete allocation/memory/output/free native island.

## Emitter contract

- Plain `int`/`bool` parameters and one scalar result. The symbol is
  `chibi_scalar`, or `main` for a zero-argument `int` entry. Foreign bool ABI is
  not promised. Storage ordinals are backend-internal, not public semantic IDs.
- Scalar locals, branches, comparisons, bitwise operations and integer
  arithmetic. Limits are eight parameters, 128 slots, 64 locals and 256 body
  instructions. These are plan bounds, not a source-language acceptance claim.
- Integer add/subtract/multiply/negate wrap: no `nsw`/`nuw` promises. Division
  and remainder check zero and signed minimum divided by minus one. Shifts
  check counts against 32, including negative counts.
- An explicit positive CHECK budget is required. Each invocation initializes a
  private counter; every CHECK tests remaining budget before decrementing.
  Budget N admits exactly N CHECKs, not N LLVM/machine instructions. The future
  producer/consumer boundary must agree which checked operations are present;
  this backend does not infer them from source text or facts tables.
- Arithmetic failure and exhausted budgets branch to `llvm.trap`, even for
  unused results. This provisional fail-stop contract does not supply VM
  diagnostics, recovery or asynchronous cancellation. The caller still
  supervises process timeout.

The emitter rejects unsupported instructions/types and validates table bounds,
operand types, canonical bool constants, unique scalar definitions and control
flow dominance before writing IR. There are no call, reference, managed-memory
or conversion operations. The caller must reject nominal/container/reference
metadata before reducing types to the backend's scalar ordinals. Checked,
wrapping and clamping conversions must not silently become LLVM casts.

## Consumers and checks

- `test/native_aot/emitter/Tests.chibi`: pure `Tests() -> int`, zero on success
  and 1-5 for a failed group. It checks scalar emission, malformed types/control
  flow, definition dominance, failure labels and explicit budgets.
- `test/fixtures/native-abi/Checks.chibi`: pure target/type checks; the adjacent
  `Tests.chibi` provides normal authored test wrappers. Neither belongs in a
  production package's dependency closure.
- `tools/native_aot/Driver.chibi`: existing explicit `Object` and `Build` entries
  consume emitted LLVM text through trusted LLVM CLI tools. See
  `test/native_aot/driver/README.md` for invocation, tool identities, exclusive
  publication rules and current validation limits.

The historical scalar proof includes independently verified O0/O2 arithmetic,
traps and source comparisons. It is evidence for the retained backend, not
validation of a new producer or lifetime boundary. Its Div/Mod formatter-limit
regression remains in these tests: each formatting call stays within the
bootstrap text formatter's twelve-placeholder bound.

This carry-forward does not change defaults, bootstrap inventories/artifacts,
runtime ABI or installed SDK routes. Focused validation of the clean combination
is recorded separately from broad repository test dispatcher/platform CI; those
broader checks and complete native memory/FFI coverage remain open.

### Clean carry-forward validation

The current-base compiler seed and checksum-verified published Linux compiler
host compiled these exact pure packages on both bytecode and SLJIT. ABI checks
and emitter checks returned zero, with zero fallback/overflow. The unchanged
SDK build host then ran both authored driver suites successfully, including
real LLVM19 O0/O2 executable and parameterized-object builds. Canonical SDK
formatting passed for the carried Chibi files. Independent exact-source review and replay passed: canonicalizing the historical
sources reproduced the exact candidate bytes, and both pure suites again returned
zero on both VMs with identical compiled artifacts and no fallback/overflow. The ordinary repository test dispatcher and
platform CI have not been run for this clean combination.
