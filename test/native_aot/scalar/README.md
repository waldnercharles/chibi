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

- takes at most eight plain number or `bool` parameters (`i8`, `u8`,
  `i16`, `u16`, `int`, `u32`, `i64`, `u64`, `number` or `f64`; no
  `ref`, defaults or managed, nominal or container types) and returns one
  plain number or `bool`;
- has a body of locals (`x := EXPR;` or `x: T = EXPR;`), assignments to its
  parameters and locals (`x = EXPR;`, or compound, `x += EXPR;` and the other
  arithmetic and shift operators), if statements with any `else` or
  `else if`, switches on an integer value, `while` loops, `for` loops over an `int` range (`a..b`, `a..<b`,
  either counted down with `for <`; bounds of a narrower integer type widen),
  `break`, `continue` and returns, at most 32 statements and 16 locals,
  counting each loop's index. It ends in a terminating statement, as stage
  zero requires: a return, an if statement with an `else` whose branches both
  end in one, a switch with a default arm whose arms all end in one, or a
  `while (true)` that no `break` leaves; otherwise it is
  rejected with stage zero's "Missing return". Statements after a return are
  kept, though they never run; any other statement is rejected, reachable or
  not;
- has `bool` conditions and returns, assigns and declares expressions of the
  type expected, or of a type that widens to it, as a return widens: a
  narrower integer, or `number` to `f64`. Each is a number literal within
  its type, possibly signed (in an integer context a whole decimal such as
  `1.0` or `1e2` is the integer it spells), `true` or `false`, a parameter or local in scope,
  integer arithmetic `a + b` (or
  `-`, `*`, `/`, `%`, `&`, `|`, `^`), floating arithmetic `a + b` (or `-`,
  `*`, `/`), a shift `a << n` or `a >> n`, an integer negation `-a` or
  complement `~a`, a comparison `a < b` (or `<=`, `>`, `>=`, `==`, `!=`) of
  two numbers, `a == b` or `a != b` of two bools, `a && b`, `a || b` or `!a`
  (or `and`, `or` and `not`), a conversion `T(a)` or `T(wrap: a)` to an
  integer type,
  `number(a)` or `f64(a)`, or a direct call
  `F(a, b, ...)` of a function of the same source with exactly its arity and
  parameter types, whose operands are again such expressions: at most 64
  operands per function, counted over all its statements.

Any expression may be parenthesized. Each operand has the type the ordinary
compiler gives it, so mixed integer types and a signed shift count are its
errors. Comparisons, division, remainders and
`>>` are unsigned for an unsigned type. Arithmetic follows the language's
integer contract and never fails: it wraps, `x / 0` is 0, `x % 0` is `x`,
`MIN / -1` is `MIN` and `MIN % -1` is 0, and a shift count at or past the
width gives 0, or the sign fill for a signed `>>`. A shift's count converts to
the shifted type first, as `ShiftStep` converts it: a constant count that does
not fit becomes the type's largest value, and a typed one clamps. `T(a)`
clamps to T's range and `T(wrap: a)` keeps the low bits. The emitter guards
the divisor and count with `select`, so no path traps.

`number` and `f64` are IEEE at their width with no fast-math: arithmetic
rounds to nearest, overflow gives an infinity, subnormals are kept, and NaN
makes every comparison false except `!=`. An integer beside a floating
value, or a `number` beside an `f64`, converts to the floating type first, as
`OperatorStep` converts it; an integer reaches `number` through `f64`, so
2^60 + 2^36 + 1 rounds twice. A `number` or `f64` converted to an integer type
truncates toward zero, saturates at the type's bounds and gives 0 for NaN, in
either mode. A floating literal is the binary64 nearest it, and a `number` one
the binary32 nearest that, as stage zero reads it.

A consequence runs only when its condition holds and its alternative only
when it does not, and `&&` and `||` evaluate their right side only when the
left does not decide them. A local is visible from the end of its
declaration to the end of its block, so one may hide an outer name, and a
compound assignment combines its variable with its value as `x = x op v`
would, at the variable's type.

Calls may repeat, nest and recurse, directly or mutually. Natively recursion
is unchecked, as Rust's and Odin's is: it runs as deep as the stack allows,
every function probes its stack, and an endless recursion faults in the guard
page. The native test host reports that as a stack overflow and aborts; a
standalone program installs no report. Stage zero's
VM limits a script to 64 nested calls (`SCRIPT_CALL_DEPTH_LIMIT`); the VMs
here run stage one's output, which has no limit, and are only asked to agree
with native code on recursion that ends. Arguments evaluate left to right, each
operand after the operands it consumes, and the emitted IR keeps that order.

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
floating negation and remainder, ordering `bool`s, statements outside the
subset, function values, qualified or imported calls, native or foreign
calls (`print`), named arguments, strings and other managed values,
attributes, structs and constants. A call may omit trailing arguments whose
parameters have defaults; each omitted one is a literal row of its default.
Sources are at most 16,384 bytes. `test/scalar_facts/cases/Cases.chibi` holds
the 96-source corpus (51 accepted) with each accepted value and statement
trace and each rejection's exact message and source excerpt, including both
sides of every bound.

A constant expression, such as `(2147483647 + 1) / 2` or `int(42)`, is one
literal row holding the value the ordinary compiler folds it to exactly, so
native code never computes it with wrapping rows.

Two untyped literals compared, as in `if (7 < 3)`, take their common
spelled type, as stage zero types them; the ordinary compiler's
`OperatorStep` decides that, not this subset.

## Shared decisions

`CollectScalar` adds no second frontend. It runs the ordinary snapshot,
package graph and `Declare`, so duplicate declarations, unknown types and
signature errors are the compiler's own. Parameters bind through `Prologue`,
and every name through `Lookup` over the scopes the ordinary compiler keeps,
with each local bound after its value as `StatementStep` binds it; literals through `ReadIntegerLiteral`, and callees through
`CalledFunction`, the resolution the ordinary call step makes. Each body must
pass `EmitBody` first, which types every name, argument and result, with no
effects, host calls or roots, and no cells but its calls' argument and result
cells. The checks in `Scalar.chibi` only reject forms the subset cannot
express; the backend never reparses source or reads VM instructions, and no
private `Code`/`Slot` identity crosses the boundary.

## The owned handoff

The compiler host passes arrays of flat records, so a module is one array of
rows: its source, then either one rejection or its document key and each
function's signature, parameters and statements in source order, each
followed by its operands in evaluation order; an if statement's rows bracket
its consequence and any alternative with Else and End rows, a switch's arms
with a Case row each and an End row, and a loop's body with an End row (`tools/compiler/scalar_facts/Facts.chibi` defines the layout and bounds). The
rows own their strings and, once snapshotted, their record shape.

- Across processes, `Run.ps1` passes the producer's exact stdout file as the
  consumer's input.
- In one process, `tool language-package-compiler-chibi-scalar` produces the
  rows on both VMs, frees the producer's packages and inputs, reads the
  retained rows, then prepares the consumer and emits them as IR with the call.
  It also validates a guarded module's rows and 19 malformed variants of them:
  if statement layout, comparison operator, arity, result and operand types,
  the condition's stack, statement and end ranges, a read past the variables,
  a body that can end without returning, and a recursive call misnamed.
  An arithmetic module's rows get 9 more: binary and unary operators, arity,
  result type and bool operands. A module with locals, an else and a logical
  gets 18: logical counts, operators and right sides, a `!` of an int, reads
  of a local in its own initializer and out of its block, local and
  assignment rows, else and end layout and ranges, and return paths. A
  module of a `for` loop that breaks and an endless `while` loop that
  continues gets 17: break and continue outside a loop or with fields, an
  endless loop that breaks or tests false, condition and bound types, index
  rows, an assignment to the index, reads of it in its bound and after its
  loop, and loop end and body ranges. A switch whose default arm declares a
  local gets 15: statements before the first arm, arms outside a switch, a
  second default, switch and arm types, arm fields and ranges, a read of the
  default arm's local in the next arm, and an arm that does not return.

`scalar_llvm.Validate` checks the representation: bounds before counts, row
layout, canonical fields, positions, typed operand stacks and callee
signatures. A rejection keeps its whole source; an accepted source must be within
the bound. It does not prove that fabricated rows match their source;
the caller binds the trusted producer artifact, source bytes and envelope.
`Emit` returns `Rejected` with the producer's rows, `Invalid` or `Emitted`, and
`EmitBytes` returns raw IR or empty bytes. Each function starts with one CHECK
under budget 1; neither the export name nor the budget comes from source.
Loops and recursion run unchecked, as Odin's and Rust's do (decision D4). Each
variable is a native local, the parameters moved into theirs first. An if
statement branches into its consequence or jumps past it, and a logical saves
its left operand in a local that its right operand replaces only on the path
that evaluates it, as stage one emits it. Loops lower as stage one emits
them: a range's bounds are evaluated once into locals, a reversed exclusive
range starts at `last - 1` only after `first < last` shows that cannot wrap,
and an inclusive or reversed range leaves when its index reaches the bound,
before stepping, so no index wraps. `continue` jumps to a `while` loop's head
or a `for` loop's step. A switch saves its value in a local; each value arm
compares it with the arm's value, evaluated only when the arms before it
missed, and the default arm, jumped over where it stands, runs when the last
test fails, as stage zero and stage one lower it.

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
through a validation-only IR adapter that passes i64 arguments, truncated
to each parameter's type, and extends the result to i64, so a `bool` is 0 or
1 and a `u64` is its bits. The adapter is not an ABI. The O0 IR must keep `Min`'s
parameter-dependent `icmp` and `br`; `lazy_arm` must call the consequence's
callee only after the branch's true target and the fallback's only past the
jump it takes otherwise; and `logical_and` must compare `b < c` only after
branching on `a < b`.

`most_operands` and `too_many_operands` nest 64 calls deep, past the
stage-zero test fixture's call depth limit, so that selector skips them;
`Run.ps1` runs them on the recovered compiler. With Ubuntu LLVM 18.1.3 the
other 47 accepted sources build, run and agree at O0 and O2.

Generated width modules stay out of the corpus, which `Run.ps1` bounds. For
each integer type, one module declares every operator on that type and
another its clamping and wrapping conversions from every integer type. Each
operator runs on every pair of the type's boundary values, its least and
greatest and those next to them, 0, 1 and -1, or the top bit unsigned. Each
shift runs on every count from 0 through the width and past it to 255 and
`UINT32_MAX`, and with a constant count past every type. Each conversion runs
on every value at the edges of each type that its source type holds. Every
call must give the integer contract's value, computed in C, on both VMs and
natively at O0 and O2.

Generated floating modules sit beside them. For `number` and `f64`, one
module runs each operator and comparison on every pair of both zeros, 1,
-0.1, the largest finite value, the least subnormal, both infinities and NaN,
adds an `int` operand, and returns a signed, a `+`-prefixed and a rounding
literal; the `number` module also widens to `f64`. Another converts the
type to and from every integer type, at each integer type's edges with
fractions on either side, past every type, at the infinities, NaN and the
least subnormals, and between `number` and `f64`. Each call must give the
value C's IEEE arithmetic gives on both VMs and natively at O0 and O2. The
adapter passes a `number` or `f64` as its encoding's bits, zero-extended, and
returns any NaN as the canonical quiet NaN, since IEEE leaves a NaN's sign
and payload open.

A generated control module runs else-if chains that end and that fall
through, nested consequences with locals of their own and a local that hides
another, compound assignments to `int` parameters and a `u8` local, shifts
assigned to `u8` locals by a `u32` count and by a constant past the
width, `&&`,
`||`, `!` and `bool` equality on every combination of their operands, and a
condition mixing them. Each call must give the value C gives on both VMs and
natively at O0 and O2.

A generated switch module runs every arm of switches on `int`, `u8` (every
value) and `i64` values, a default arm written first, arm values that are
calls and expressions, a repeated value whose second arm cannot run, `break`
and `continue` through a switch to its loop, and a switch nested in an arm.
Each call must give the value C gives on both VMs and natively at O0 and O2.

A generated loop module runs each range form on every pair of ends near zero
and both ends of `int` at most 16 apart, every empty range among them, folding
the indices into a hash so their order and count show; a range over `u8`
bounds; a `while` loop that may not run; an endless loop that only a return
leaves; and nested loops whose `break` and `continue` leave or restart the
innermost. Each call must give the value C gives on both VMs and natively at
O0 and O2. A function that never returns fails both VMs at their instruction
budget, and natively runs at O0 and O2 until its one-second deadline stops
it.

## Portable recovery regression

`Run.ps1` runs after the two-stage self-build in `compiler-recovery.yml`. It
binds the pinned runtime, recovered stage2, compiler and regression source
inventories, compiles `Regression.chibi` on both VMs with identical bytes, and
runs the authored `Tests` (28 malformed-row fixtures plus emission checks).
Statement-row validation is asserted in C, by the scalar selector above.
Every corpus source then crosses all four producer/consumer VM combinations:
`Regression.Check` asserts the case and the complete rejection or emitted IR,
which must define every function and call each recorded callee. The runner
only transports, checks identity and requires selected-backend counters with
no fallback. Rejected sources must publish no raw IR and preserve a prior
output. Its finite 240-second total budget covers at most 96 sources (970 child
invocations), retaining a 120-second per-command limit clamped to the remaining
time. Deadline failures report completed invocations/crossings and the next
invocation, including when the corpus has not yet been enumerated.
