# Owned scalar modules → LLVM objects

This is a bounded, opt-in path from Chibi source to a native object. It is not
the default compiler, a general semantic IR or a public ABI. The authored
compiler's `CollectScalar` entry produces an owned `scalar_facts.Row[]`; the
separate `scalar_llvm` consumer validates those rows and lowers them to the
`llvm_native` emitter; the unchanged authored `Driver.Object` turns the IR into
an object with trusted LLVM tools.

## What is supported

One source document holding one package, no imports, and only ordinary
function declarations without attributes: at most 32 functions. The first
function is the module's entry and must be `() -> int`. Every function:

- takes at most eight plain `int` or `bool` parameters (no `ref`, defaults or
  managed, nominal or container types) and returns one plain `int` or `bool`;
- has a body of exactly one `return EXPR;`;
- returns an expression that is an integer literal within `int`, `true` or
  `false`, one of its parameters, or a direct call `F(a, b, ...)` of a function
  of the same source with exactly its arity and parameter types, whose
  arguments are again such expressions: at most 64 operands per function.

Calls form a bounded acyclic set, so recursion (direct or mutual) is rejected.
Calls may repeat and nest. Arguments evaluate left to right, each operand
after the operands it consumes, and the emitted IR keeps that order.

```chibi
package scalar
Main :: () -> int { return Third(1, Second(2, 3), Third(4, 5, 6)); }
Second :: (a: int, b: int) -> int { return b; }
Third :: (a: int, b: int, c: int) -> int { return c; }
```

`Main` lowers to `chibi_scalar() -> i32`, and `Second` and `Third` to internal
`chibi_scalar.1`/`chibi_scalar.2`. The O0 object keeps the three calls; at O2
LLVM inlines them to `ret i32 6`. Both return 6 natively.

Everything else is rejected with a located diagnostic and no partial result:
arithmetic, negation, parentheses, comparisons, control flow, locals and other
statements, conversions such as `int(x)`, function values, qualified or
imported calls, native or foreign calls (`print`), named arguments, strings and
other managed values, attributes, structs and constants. Sources are at most
16,384 bytes. `test/scalar_facts/cases/Cases.chibi` holds the 79-source corpus
with each accepted value and operand trace and each rejection's exact message
and source excerpt, including both sides of every bound.

## Shared decisions

`CollectScalar` adds no second frontend. It runs the ordinary snapshot,
package graph and `Declare`, so duplicate declarations, unknown types and
signature errors are the compiler's own. Parameters bind through `Prologue` and
`Lookup`, literals through `ReadIntegerLiteral`, and callees through
`CalledFunction`, the resolution the ordinary call step makes. Each body must
pass `EmitBody` first, which types every name, argument and result, with no
effects, host calls or roots, and no cells but its calls' argument and result
cells. The checks in `Scalar.chibi` only reject forms the subset cannot
express; the backend never reparses source or reads VM instructions, and no
private `Code`/`Slot` identity crosses the boundary.

## The owned handoff

The compiler host passes arrays of flat records, so a module is one array of
rows: its source, then either one rejection or its document key and each
function's signature, parameters, return and operands in evaluation order
(`tools/compiler/scalar_facts/Facts.chibi` defines the layout and bounds). The
rows own their strings and, once snapshotted, their record shape.

- Across processes, `Run.ps1` passes the producer's exact stdout file as the
  consumer's input.
- In one process, `tool language-package-compiler-chibi-scalar` produces the
  rows on both VMs, frees the producer's packages and inputs, reads the
  retained rows, then prepares the consumer and emits them as IR with the call.

`scalar_llvm.Validate` checks the representation: bounds before counts, row
layout, canonical fields, positions, typed operand stacks, callee signatures
and acyclicity, through the same `scalar_facts.Cycle` the producer diagnoses
with. A rejection keeps its whole source; an accepted source must be within
the bound. It does not prove that fabricated rows match their source;
the caller binds the trusted producer artifact, source bytes and envelope.
`Emit` returns `Rejected` with the producer's rows, `Invalid` or `Emitted`, and
`EmitBytes` returns raw IR or empty bytes. Each function starts with one CHECK
under budget 1; neither the export name nor the budget comes from source.

## Native objects

Feed `EmitBytes` output, written with the compiler host's `--output-bytes`, to
the unchanged driver:

```text
chibi-build-host --root REPO --source tools/native_aot/Driver.chibi \
  --core REPO/packages/core --compiler REPO/tools/compiler \
  --compiler-core REPO/packages/core --entry Object --timeout-ms 120000 -- \
  ABS_INPUT_LL ABS_FRESH_OUTPUT_DIRECTORY ABS_OPT ABS_LLC 0|2
```

See the [driver contract](../driver/README.md) for its trusted-tool,
target/layout and publication rules. The entry's int32 result is the only
native observation: `bool` parameters and results are internal, and any `bool`
probe is a validation-only adapter, not an ABI.

On Linux x86-64 with Ubuntu LLVM 18.1.3 `opt`/`llc` (whose canonical data
layout is the one the driver admits), all 21 accepted corpus sources built O0
and O2 objects. No object has an undefined symbol. Each O0 module keeps every
recorded call (up to 63 in `most_operands` and 31 in `most_functions`), O2
inlines them all, and a validation-only harness that links each object as a
shared library got every case's int32 value from `chibi_scalar()`. A
validation-only IR adapter appended to copies of `bool_literals` and
`bool_passthrough` observed `true`, `false` and both `Flip` results at O0 and
O2. This harness is not part of the authored path.

## Portable recovery regression

`Run.ps1` runs after the two-stage self-build in `compiler-recovery.yml`. It
binds the pinned runtime, recovered stage2, compiler and regression source
inventories, compiles `Regression.chibi` on both VMs with identical bytes, and
runs the authored `Tests` (28 malformed-row fixtures plus emission checks).
Every corpus source then crosses all four producer/consumer VM combinations:
`Regression.Check` asserts the case and the complete rejection or emitted IR,
which must define every function and call each recorded callee. The runner
only transports, checks identity and requires selected-backend counters with
no fallback. Rejected sources must publish no raw IR and preserve a prior
output. Its 180-second budget covers at most 96 sources.
