# Runtime-free scalar LLVM backend (opt-in WIP)

`llvm_native.Emit` validates a bounded scalar instruction plan and writes LLVM
IR. `native_abi` owns the exact Linux x86-64 target and internal scalar mapping;
the selected LLVM backend supplies its actual data layout. This clean integration
carries those reviewed pure packages from [3571908d](https://github.com/waldnercharles/chibi/commit/3571908d0cc4f3ad395b6a4fceb66641445c3d07)
onto the current-base [authored LLVM driver](https://github.com/waldnercharles/chibi/commit/ae4828697dfc976a657c33014513f00e047ec422).

It deliberately does **not** carry `package_compiler/Native.chibi`,
`native_compiler`, or the diagnostic Front/Back packet adapters. Its one source
connection is the bounded `scalar_facts` handoff in
`test/native_aot/scalar/README.md`: the compiler's `CollectScalar` produces
owned rows and `scalar_llvm` lowers them to this plan. Private frontend
Code/Slot IDs are not an API.

The compiler still runs in the existing bootstrap runtime. Emitted scalar
objects have no first-party C runtime, pin-value header, context offset or helper
calls. No C source is produced. This is not a C-free compiler, a foreign-call
interface, or the complete allocation/memory/output/free native island.

## Emitter contract

- A module of at most 32 functions, each a consecutive range of the parameter,
  slot, local, code and argument tables. Function 0 is the exported entry,
  `chibi_scalar`, or `main` for a zero-argument `int` entry; the others are
  `internal` `chibi_scalar.N`. Each takes plain integer, `float`, `double`
  and `bool` parameters (`int8` through `uint64` as `iN` with no sign, and
  `float` and `double` as themselves) and returns one scalar result. Foreign bool ABI is not promised. Storage
  ordinals are backend-internal, not public semantic IDs.
- `Call` defines a slot from another function's result. Its arguments are
  slots of the caller's own argument range, each of the callee's parameter
  type and defined on every path; they load in order, so evaluation order stays
  visible at O0. The call graph must be acyclic: recursion is rejected.
- Scalar locals, branches, comparisons, bitwise operations, integer and
  floating arithmetic, and conversions. Comparisons, division, remainders and
  `>>` are unsigned for an unsigned type. `bool` has `==`, `!=` and `BitNot`,
  its `!`. Limits per function are eight parameters, 256 slots, 128 locals,
  1,024 body instructions and 256 call arguments. These are plan bounds, not a
  source-language acceptance claim.
- Integer operations never fail, as the language's integer contract says.
  Add, subtract, multiply and negate wrap, with no `nsw`/`nuw` promises.
  `x / 0` is 0 and `x % 0` is `x`; `MIN / -1` is `MIN` and `MIN % -1` is 0.
  A shift count read unsigned at or past the width gives 0, or the sign fill
  for a signed `>>`. The divisor and count are made safe with `select` before
  LLVM sees them, so no result is poison.
- `Convert` defines a slot of its integer `type` from the `left` slot, whose
  integer type is `right`. `literal` 0 clamps the value to the type's range
  and 1 keeps its low bits. The value is first extended to `i65`, which holds
  every value of every integer type, so a clamp never becomes a plain
  truncation.
- `float` and `double` follow IEEE at their width, with no fast-math flags:
  `+`, `-`, `*` and `/` round to nearest, and there is no floating negation
  or remainder, which the runtime does not compute either. `<`, `<=`, `>`,
  `>=` and `==` are ordered, so NaN makes them false, and `!=` is unordered,
  so NaN makes it true. A constant's `literal` is its encoding's bits, a
  `float`'s zero-extended, emitted through `bitcast` so NaN payloads and
  signed zeros stay exact.
- A `float` or `double` converts to an integer type through
  `llvm.fpto{s,u}i.sat`, in either `literal` mode: it truncates toward zero,
  saturates at the type's bounds and gives 0 for NaN, as the runtime's casts
  do. An integer or a `float` converts to `double`, and a `double` to `float`,
  rounding to nearest. An integer reaches `float` only through `double`, as
  the runtime converts it, so a direct integer-to-`float` conversion is
  rejected. Each intrinsic is declared once.
- Every control cycle must pass a CHECK: without its CHECKs a function's
  control is acyclic, as the runtime's verifier requires of a loop.
- An explicit positive CHECK budget is required. Each invocation of each
  function initializes a private counter; every CHECK tests remaining budget before decrementing.
  Budget N admits exactly N CHECKs, not N LLVM/machine instructions. The future
  producer/consumer boundary must agree which checked operations are present;
  this backend does not infer them from source text or facts tables.
- An exhausted budget branches to `llvm.trap`. This provisional fail-stop
  contract does not supply VM diagnostics, recovery or asynchronous
  cancellation. The caller still supervises process timeout.

The emitter rejects unsupported instructions/types and validates table bounds,
operand types, constants within their type, unique scalar definitions and
control flow dominance before writing IR. There are no indirect call, foreign
call, reference or managed-memory operations. The caller must reject
nominal/container/reference metadata before reducing types to the backend's
scalar ordinals.

## Consumers and checks

- `test/native_aot/emitter/Tests.chibi`: pure `Tests() -> int`, zero on success
  and 1-5 for a failed group. It checks scalar emission, malformed types/control
  flow, definition dominance, total arithmetic and explicit budgets.
- `tool language-package-compiler-chibi-scalar` (`test/language`): call
  definitions and argument order, malformed calls and cyclic or misplaced call
  graphs, floating encodings, comparisons, conversions and intrinsic
  declarations, and floating operations and conversions the runtime lacks,
  each with its exact rejection, on both VMs.
- `tools/compiler/scalar_llvm`: lowers producer-owned `scalar_facts` rows; see
  `test/native_aot/scalar/README.md`.
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

The emitter changes no defaults, runtime ABI or installed SDK routes, and it is
not part of the compiler's bootstrap closure. The language test fixture embeds
it only for the same-process scalar handoff check. Complete native memory/FFI
coverage remains open.

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
