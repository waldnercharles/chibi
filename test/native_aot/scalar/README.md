# Owned scalar modules → LLVM objects

This is a bounded, opt-in path from Chibi source to a native object. It is not
the default compiler, a general semantic IR or a public ABI. The authored
compiler's `CollectScalar` entry produces an owned `scalar_facts.Row[]`; the
separate `scalar_llvm` consumer validates those rows and lowers them to the
`llvm_native` emitter; the unchanged authored `Driver.Object` turns the IR into
an object with trusted LLVM tools. The [native LLVM ledger](../../../docs/native-llvm-ledger.md)
tracks which language features reach native code and what blocks the rest.

## What is supported

One source document holding one package, no imports, and only ordinary
function declarations without attributes: at most 32 functions. The first
function is the module's entry and must be `() -> int`. Every function:

- takes at most eight plain `int` or `bool` parameters (no `ref`, defaults or
  managed, nominal or container types) and returns one plain `int` or `bool`;
- has a body of at most one guard `if (CONDITION) { return EXPR; }`, then
  exactly one final `return EXPR;`, and no other statement, reachable or not;
- has a `bool` condition and returns expressions of its result type. Each is
  an integer literal within `int`, possibly signed, `true` or `false`, one of
  its parameters, `int` arithmetic `a + b` (or `-`, `*`, `/`, `%`, `&`, `|`,
  `^`), a negation `-a` or complement `~a`, a comparison `a < b` (or `<=`,
  `>`, `>=`, `==`, `!=`) of two `int` expressions, or a direct call
  `F(a, b, ...)` of a function of the same source with exactly its arity and
  parameter types, whose operands are again such expressions: at most 64
  operands per function, counted over its condition and returns.

Any expression may be parenthesized. Arithmetic and comparisons are signed
32-bit and admit only `int` operands, so `bool` equality is rejected.
Arithmetic follows the language's integer contract and never fails: it
wraps, `x / 0` is 0, `x % 0` is `x`, `MIN / -1` is `MIN` and `MIN % -1` is 0.
The emitter guards the divisor with `select`, so no path traps. The arm's
expression is evaluated only when the condition holds, and the final
return's only when it does not.

Calls form a bounded acyclic set, so recursion (direct or mutual) is rejected.
Calls may repeat and nest. Arguments evaluate left to right, each operand
after the operands it consumes, and the emitted IR keeps that order.

```chibi
package scalar
Main :: () -> int { return Clamp(12, 0, 10); }
Clamp :: (x: int, low: int, high: int) -> int { return Min(Max(x, low), high); }
Min :: (a: int, b: int) -> int {
    if (a < b) { return a; }
    return b;
}
Max :: (a: int, b: int) -> int {
    if (a > b) { return a; }
    return b;
}
```

At O0 `Min` stores its parameters, compares them with `icmp slt` and branches
on the result; it returns 10 natively and on both VMs.

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
shifts, which need the unsigned counts the subset does not have yet, `&&`,
`||` and `!`, `bool` comparisons, `else` branches, a second guard, a guard that does not return,
loops and other control flow, locals and other statements, conversions such as
`int(x)`, function values, qualified or imported calls, native or foreign calls
(`print`), named arguments, strings and other managed values, attributes,
structs and constants. Sources are at most 16,384 bytes.
`test/scalar_facts/cases/Cases.chibi` holds the 96-source corpus (35
accepted) with each accepted value and operand trace and each rejection's
exact message and source excerpt, including both sides of every bound.

A constant expression, such as `(2147483647 + 1) / 2` or `int(42)`, is one
literal row holding the value the ordinary compiler folds it to exactly, so
native code never computes it with wrapping rows.

Two untyped literals compared, as in `if (7 < 3)`, take their common
spelled type, as stage zero types them; the ordinary compiler's
`OperatorStep` decides that, not this subset.

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
function's signature, parameters, guard with its condition operands and its
arm's return, and final return, each followed by its operands in evaluation
order
(`tools/compiler/scalar_facts/Facts.chibi` defines the layout and bounds). The
rows own their strings and, once snapshotted, their record shape.

- Across processes, `Run.ps1` passes the producer's exact stdout file as the
  consumer's input.
- In one process, `tool language-package-compiler-chibi-scalar` produces the
  rows on both VMs, frees the producer's packages and inputs, reads the
  retained rows, then prepares the consumer and emits them as IR with the call.
  It also validates a guarded module's rows and 13 malformed variants of them:
  guard layout, comparison operator, arity, result and operand types, the
  condition's stack, guard, arm and final-return ranges, and an arm call that
  closes a cycle. An arithmetic module's rows get 9 more: binary and unary
  operators, arity, result type and bool operands.

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

`tool language-scalar-native` (`test/language`, Linux x86-64 only) runs the
corpus natively. `CHIBI_LLVM_BIN` names the directory of `opt`, `llc`,
`ld.lld` and `llvm-nm`; nothing is found through `PATH`, and `make
test-language-all` passes `NATIVE_LLVM_BIN` as it. Each case goes through
`CollectScalar` on one VM, alternating by case, and must be what
`Cases.Check` expects. Each accepted module's IR becomes O0 and O2 objects
through the driver's fixed `opt` and `llc` argument vectors, with no undefined
symbol, and each object, linked as a validation-only shared library, runs in a
child process. Its `chibi_scalar()` must return what the authored compiler's
closure of the same source returns on both VMs. Probes call internal functions
such as `Min`, `Max`, `Clamp` and each comparison with `INT32_MIN`,
`INT32_MAX`, equal and adjacent values, and both outcomes of each guard,
through a validation-only IR adapter that passes int32 arguments and widens a
`bool` result. The adapter is not an ABI. The O0 IR must keep `Min`'s
parameter-dependent `icmp` and `br`, and `lazy_arm` must call the arm's
callee only after the branch's true target and the fallback's only on the
false path.

`most_operands` and `too_many_operands` nest 64 calls deep, past the
stage-zero test fixture's call depth limit, so that selector skips them;
`Run.ps1` runs them on the recovered compiler. With Ubuntu LLVM 18.1.3 the
other 34 accepted sources build, run and agree at O0 and O2.

## Portable recovery regression

`Run.ps1` runs after the two-stage self-build in `compiler-recovery.yml`. It
binds the pinned runtime, recovered stage2, compiler and regression source
inventories, compiles `Regression.chibi` on both VMs with identical bytes, and
runs the authored `Tests` (28 malformed-row fixtures plus emission checks).
Guarded-row validation is asserted in C, by the scalar selector above.
Every corpus source then crosses all four producer/consumer VM combinations:
`Regression.Check` asserts the case and the complete rejection or emitted IR,
which must define every function and call each recorded callee. The runner
only transports, checks identity and requires selected-backend counters with
no fallback. Rejected sources must publish no raw IR and preserve a prior
output. Its finite 240-second total budget covers at most 96 sources (970 child
invocations), retaining a 120-second per-command limit clamped to the remaining
time. Deadline failures report completed invocations/crossings and the next
invocation, including when the corpus has not yet been enumerated.
