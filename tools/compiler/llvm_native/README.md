# Runtime-free scalar LLVM backend (opt-in WIP)

`llvm_native.Emit` validates a scalar instruction plan and writes LLVM
IR. `native_abi` owns the exact targets (Linux x86-64, macOS arm64 and Windows
x64) and internal scalar mapping; the selected LLVM backend supplies its actual
data layout. This clean integration carries those reviewed pure packages from [3571908d](https://github.com/waldnercharles/chibi/commit/3571908d0cc4f3ad395b6a4fceb66641445c3d07)
onto the current-base [authored LLVM driver](https://github.com/waldnercharles/chibi/commit/ae4828697dfc976a657c33014513f00e047ec422).

It deliberately does **not** carry `package_compiler/Native.chibi`,
`native_compiler`, or the diagnostic Front/Back packet adapters. Its one source
connection is the `scalar_facts` handoff in
`test/native_aot/scalar/README.md`: the compiler's `CollectScalar` produces
owned rows and `scalar_llvm` lowers them to this plan. Private frontend
Code/Slot IDs are not an API.

The compiler still runs in the existing bootstrap runtime. Emitted scalar
objects have no first-party C runtime, pin-value header, context offset or helper
calls, and they install no fault handler: reporting a stack overflow by name is
the host's job (see `Call` below). No C source is produced. This is not a C-free compiler, a foreign-call
interface, or the complete allocation/memory/output/free native island.

## Emitter contract

- A module of one or more functions, each a consecutive range of the parameter,
  slot, local, code and argument tables. Function 0 is the exported entry,
  `chibi_scalar`, or `main` for a zero-argument `int` entry; the others are
  `internal` `chibi_scalar.N`. Each takes plain integer, `number`, `f64`
  and `bool` parameters (`i8` through `u64` as `iN` with no sign, and
  `number` and `f64` as LLVM `float` and `double`) and returns one scalar result. Foreign bool ABI is not promised. Storage
  ordinals are backend-internal, not public semantic IDs.
- `EmitLibrary` emits the same tables as the native library `name`, which
  `execution_natives_bind` (`src/execution/native.h`) binds. Every function
  is `internal`, and the object exports one symbol, `chibi_natives_<name>`,
  a `{ i32 abi, i32 count, ptr exports }` manifest at `native_abi.ExportAbi`.
  Each export is `{ ptr name, i64 id, ptr types, i32 parameters, i32 results,
  ptr entry }`, named and identified by the caller's `names` and `ids`, and
  its entry `void (ptr in, ptr out)` reads its arguments from and writes its
  result to 64-bit words: integers sign- or zero-extended, `bool` as 0 or 1,
  `number` and `f64` as their bits. A function with a string or record
  parameter or result has no wrapper and stays callable only inside the
  library. Libraries are Linux x86-64 only: a macOS or Windows target is
  rejected.
- A module declares records, each a range of one or more scalar
  field types in the field table, emitted in that order as
  `%recordN = type { ... }`. Record type `RecordType + N` is a value: internal
  functions take and return it, slots and locals hold it, and its only
  `Constant` is zero, `zeroinitializer`. `Insert` gives the `left` record
  with field `literal` replaced by the `right` slot's value (`insertvalue`),
  and `Extract` reads field `literal` of the `left` record, whose type is
  `right` (`extractvalue`); the field's type must be the value's. The
  exported function takes and returns scalars only, so no aggregate crosses
  the C ABI.
- A string is `native_abi.TextType()`, `{ ptr, i32, ptr }`, the layout of
  the runtime's `pin_string`: its bytes, its length and the owner of shared
  runtime bytes, null for a constant. Native code only borrows strings. Its
  `Constant`'s
  `literal` indexes the module's text table, and each
  text is a `private unnamed_addr` global of its bytes and a NUL. `Eq` and
  `Ne` call the module's internal `@chibi_scalar.text_equal`, emitted only
  when some instruction compares strings: equal lengths, then equal bytes,
  reading no byte of an empty string. `TextLength` is a string's byte count,
  an `int`; `TextByte` its byte at the `right` index, zero-extended, or -1
  outside it (a negative index is outside); `TextDrop` and `TextTake` the
  string past its first `right` bytes and its first `right` bytes, with the
  count clamped to 0 through the length, so no byte read or pointer passes
  the string. No allocation, copy or drop is needed, since every string is
  a view of a constant. The exported function takes and returns no string.
- `Call` defines a slot from another function's result. Its arguments are
  slots of the caller's own argument range, each of the callee's parameter
  type and defined on every path; they load in order, so evaluation order stays
  visible at O0. Calls may recurse, directly or mutually, with no depth
  counter, as Rust's and Odin's do. Every function carries
  `native_abi.StackProbe`: `"probe-stack"="inline-asm"`, or on Windows
  `"probe-stack"="__chkstk"`, a call to the probe MSVC's and rustc's code
  calls, which the program defines. A frame larger than a page touches each
  page in turn and running out of stack always faults in the guard page below
  it rather than past it. That makes an overflow a fault, never a silent write
  past the stack. What the fault prints is up to the process. A host that has
  called `stack_overflow_init` (`src/host/stack_overflow.c`) on the thread
  running native code writes `stack overflow` and aborts. So far only the
  native test host does that, on Linux. A program that `tools/native_aot`
  links on its own installs no handler, so an overflow there ends with the
  system's default fault (SIGSEGV). Installing the report in production hosts
  is open (D4).
- Scalar locals, branches, comparisons, bitwise operations, integer and
  floating arithmetic, and conversions. Comparisons, division, remainders and
  `>>` are unsigned for an unsigned type. `bool` has `==`, `!=` and `BitNot`,
  its `!`. As in Rust and Odin, nothing bounds a module's functions, records
  or texts, or a function's parameters, slots, locals, instructions or call
  arguments.
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
- `number` and `f64` follow IEEE at their width, with no fast-math flags:
  `+`, `-`, `*` and `/` round to nearest. There is no floating negation or
  remainder: stage one rejects both before they reach this lowering, although
  the runtime computes them (`BF_NEG`, `BF_MOD_F32` and `BF_MOD_F64`).
  `<`, `<=`, `>`, `>=` and `==` are ordered, so NaN makes them false, and
  `!=` is unordered, so NaN makes it true. A constant's `literal` is its encoding's bits, a
  `number`'s zero-extended, emitted through `bitcast` so NaN payloads and
  signed zeros stay exact.
- A `number` or `f64` converts to an integer type through
  `llvm.fpto{s,u}i.sat`, in either `literal` mode: it truncates toward zero,
  saturates at the type's bounds and gives 0 for NaN, as the runtime's casts
  do. An integer or a `number` converts to `f64`, and an `f64` to `number`,
  rounding to nearest. An integer reaches `number` only through `f64`, as
  the runtime converts it, so a direct integer-to-`number` conversion is
  rejected. Each intrinsic is declared once.
- Control may cycle without a CHECK: native loops run unchecked, as Odin's
  do, and no `mustprogress` attribute lets LLVM remove an endless one.
- An explicit positive CHECK budget is required. Each invocation of each
  function initializes a private counter; every CHECK tests remaining budget before decrementing.
  Budget N admits exactly N CHECKs, not N LLVM/machine instructions. The future
  producer/consumer boundary must agree which checked operations are present;
  this backend does not infer them from source text or facts tables.
- An exhausted budget branches to `llvm.trap`. This provisional fail-stop
  contract does not supply VM diagnostics, recovery or asynchronous
  cancellation. The caller still supervises process timeout.
- Line tables are opt-in, as rustc's `-C debuginfo=line-tables-only`: a
  `Source` with a file, a name for each function and a line and column for
  each code instruction add a DWARF compile unit, one subprogram per function
  (and an artificial one for `text_equal`), and a `!dbg` location on every
  instruction. `native_abi.DebugFormat` gives the target's DWARF version
  flag. Line tables are Linux x86-64 only: a macOS or Windows module with a
  source file is rejected. An empty file emits the module unchanged.

The emitter rejects unsupported instructions/types and validates table bounds,
operand types, constants within their type, unique scalar definitions and
control flow dominance before writing IR. There are no indirect call, foreign
call, reference or managed-memory operations. The caller must reject
nominal/container/reference metadata before reducing types to the backend's
scalar ordinals.

## Consumers and checks

- `test/native_aot/emitter/Tests.chibi`: pure `Tests() -> int`, zero on success
  and 1-6 for a failed group. It checks scalar emission, malformed types/control
  flow, definition dominance, total arithmetic, explicit budgets and
  recursion with stack probes.
- `tool language-package-compiler-chibi-scalar` (`test/language`): call
  definitions and argument order, malformed calls and misplaced call
  graphs, library wrappers and manifests, floating encodings, comparisons,
  conversions and intrinsic
  declarations, and floating operations and conversions outside the native subset,
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
